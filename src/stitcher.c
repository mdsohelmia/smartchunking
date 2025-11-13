#include "stitcher.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

typedef struct
{
  int out_index;
  int64_t offset;   /* accumulated PTS offset in input time_base units */
  int64_t last_pts; /* last rebased pts in input time_base */
  int64_t last_dts; /* last rebased dts in input time_base */
  AVRational time_base;
  enum AVMediaType type;
} stitch_stream_state;

static const char *detect_fmt(const char *path)
{
  const char *ext = strrchr(path, '.');
  if (!ext)
    return "mp4";
  ext++;
  if (!strcasecmp(ext, "mp4"))
    return "mp4";
  if (!strcasecmp(ext, "mov"))
    return "mov";
  if (!strcasecmp(ext, "mkv"))
    return "matroska";
  if (!strcasecmp(ext, "webm"))
    return "webm";
  return "mp4";
}

static int chunk_abs_path(char *dst, size_t sz,
                          const char *dir,
                          int index)
{
  char tmp[PATH_MAX];
  if (snprintf(tmp, sizeof(tmp), "%s/chunk_%04d.mp4", dir, index) >= (int)sizeof(tmp))
    return -1;
  if (!realpath(tmp, dst))
    return -1;
  if (strlen(dst) >= sz)
    return -1;
  return 0;
}

static int64_t resolve_first_ts(const AVPacket *pkt)
{
  if (pkt->pts != AV_NOPTS_VALUE)
    return pkt->pts;
  if (pkt->dts != AV_NOPTS_VALUE)
    return pkt->dts;
  return 0;
}

int stitch_chunks(const char *output_path,
                  const sc_chunk_plan *plan,
                  const char *chunk_dir,
                  const stitch_output_mode *mode)
{
  if (!output_path || !plan || !chunk_dir || plan->count <= 0)
    return STITCH_ERR_INPUT;

  stitch_output_mode fallback = {
      .auto_mode = 1,
      .force_fmt = NULL,
      .output_frag = 0,
      .enable_faststart = 0};
  const stitch_output_mode *cfg = mode ? mode : &fallback;

  const char *fmt_name =
      cfg->auto_mode ? detect_fmt(output_path)
                     : (cfg->force_fmt ? cfg->force_fmt : "mp4");

  const AVOutputFormat *ofmt = av_guess_format(fmt_name, NULL, NULL);
  if (!ofmt)
    return STITCH_ERR_OUTPUT;

  AVFormatContext *out_ctx = NULL;
  if (avformat_alloc_output_context2(&out_ctx, ofmt, fmt_name, output_path) < 0)
    return STITCH_ERR_OUTPUT;

  AVDictionary *mux_opts = NULL;
  if (cfg->output_frag && !strcmp(fmt_name, "mp4"))
    av_dict_set(&mux_opts, "movflags", "frag_keyframe+empty_moov+omit_tfhd_offset", 0);
  if (cfg->enable_faststart && !cfg->output_frag && !strcmp(fmt_name, "mp4"))
    av_dict_set(&mux_opts, "movflags", "faststart", 0);

  AVPacket *pkt = av_packet_alloc();
  if (!pkt)
  {
    avformat_free_context(out_ctx);
    return STITCH_ERR_NOMEM;
  }

  stitch_stream_state *streams = NULL;
  int stream_count = 0;
  int header_written = 0;
  int rc = STITCH_OK;

  for (int ci = 0; ci < plan->count; ci++)
  {
    char chunk_path[PATH_MAX];
    if (chunk_abs_path(chunk_path, sizeof(chunk_path), chunk_dir, plan->chunks[ci].index) != 0)
    {
      rc = STITCH_ERR_OPEN;
      break;
    }

    AVFormatContext *in_ctx = NULL;
    if (avformat_open_input(&in_ctx, chunk_path, NULL, NULL) < 0)
    {
      rc = STITCH_ERR_OPEN;
      break;
    }
    if (avformat_find_stream_info(in_ctx, NULL) < 0)
    {
      avformat_close_input(&in_ctx);
      rc = STITCH_ERR_FFMPEG;
      break;
    }

    const int chunk_streams = (int)in_ctx->nb_streams;
    int *chunk_map = malloc(sizeof(int) * chunk_streams);
    int64_t *first_pts = malloc(sizeof(int64_t) * chunk_streams);
    if (!chunk_map || !first_pts)
    {
      free(chunk_map);
      free(first_pts);
      avformat_close_input(&in_ctx);
      rc = STITCH_ERR_NOMEM;
      break;
    }
    for (int i = 0; i < chunk_streams; i++)
    {
      chunk_map[i] = -1;
      first_pts[i] = AV_NOPTS_VALUE;
    }

    int media_count = 0;
    for (int i = 0; i < chunk_streams; i++)
    {
      if (in_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT)
        continue;
      chunk_map[i] = media_count++;
    }

    if (stream_count == 0)
    {
      stream_count = media_count;
      streams = calloc(stream_count, sizeof(*streams));
      if (!streams)
      {
        free(chunk_map);
        free(first_pts);
        avformat_close_input(&in_ctx);
        rc = STITCH_ERR_NOMEM;
        break;
      }

      for (int i = 0, st_idx = 0; i < chunk_streams; i++)
      {
        AVStream *ist = in_ctx->streams[i];
        if (chunk_map[i] < 0)
          continue;

        AVStream *ost = avformat_new_stream(out_ctx, NULL);
        if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0)
        {
          rc = STITCH_ERR_STREAM;
          break;
        }
        ost->codecpar->codec_tag = 0;
        ost->time_base = ist->time_base;

        streams[st_idx].out_index = ost->index;
        streams[st_idx].offset = 0;
        streams[st_idx].last_pts = AV_NOPTS_VALUE;
        streams[st_idx].last_dts = AV_NOPTS_VALUE;
        streams[st_idx].time_base = ist->time_base;
        streams[st_idx].type = ist->codecpar->codec_type;
        st_idx++;
      }

      if (rc != STITCH_OK)
      {
        free(chunk_map);
        free(first_pts);
        avformat_close_input(&in_ctx);
        break;
      }

      if (!(out_ctx->oformat->flags & AVFMT_NOFILE))
      {
        if (avio_open(&out_ctx->pb, output_path, AVIO_FLAG_WRITE) < 0)
        {
          rc = STITCH_ERR_OUTPUT;
          free(chunk_map);
          free(first_pts);
          avformat_close_input(&in_ctx);
          break;
        }
      }

      if (avformat_write_header(out_ctx, &mux_opts) < 0)
      {
        rc = STITCH_ERR_WRITE;
        free(chunk_map);
        free(first_pts);
        avformat_close_input(&in_ctx);
        break;
      }

      header_written = 1;
    }
    else
    {
      if (media_count != stream_count)
      {
        rc = STITCH_ERR_LAYOUT;
        free(chunk_map);
        free(first_pts);
        avformat_close_input(&in_ctx);
        break;
      }
    }

    while (av_read_frame(in_ctx, pkt) >= 0)
    {
      int state_idx = chunk_map[pkt->stream_index];
      if (state_idx < 0)
      {
        av_packet_unref(pkt);
        continue;
      }

      stitch_stream_state *state = &streams[state_idx];
      AVStream *ist = in_ctx->streams[pkt->stream_index];
      AVStream *ost = out_ctx->streams[state->out_index];

      if (av_cmp_q(ist->time_base, state->time_base) != 0)
      {
        rc = STITCH_ERR_LAYOUT;
        av_packet_unref(pkt);
        break;
      }

      int64_t base = first_pts[pkt->stream_index];
      if (base == AV_NOPTS_VALUE)
      {
        base = resolve_first_ts(pkt);
        first_pts[pkt->stream_index] = base;
      }

      int64_t rebased_pts = AV_NOPTS_VALUE;
      int64_t rebased_dts = AV_NOPTS_VALUE;

      if (pkt->pts != AV_NOPTS_VALUE)
      {
        rebased_pts = pkt->pts - base + state->offset;
        pkt->pts = rebased_pts;
      }
      if (pkt->dts != AV_NOPTS_VALUE)
      {
        rebased_dts = pkt->dts - base + state->offset;
        pkt->dts = rebased_dts;
      }

      if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
      {
        pkt->pts = pkt->dts;
        rebased_pts = rebased_dts;
      }
      if (pkt->dts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
      {
        pkt->dts = pkt->pts;
        rebased_dts = rebased_pts;
      }
      if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE &&
          pkt->dts > pkt->pts)
      {
        pkt->pts = pkt->dts;
        rebased_pts = rebased_dts;
      }

      av_packet_rescale_ts(pkt, ist->time_base, ost->time_base);
      pkt->duration = av_rescale_q(pkt->duration, ist->time_base, ost->time_base);
      pkt->stream_index = state->out_index;
      pkt->pos = -1;

      if (av_interleaved_write_frame(out_ctx, pkt) < 0)
      {
        rc = STITCH_ERR_WRITE;
        av_packet_unref(pkt);
        break;
      }

      if (rebased_pts != AV_NOPTS_VALUE)
        state->last_pts = rebased_pts;
      if (rebased_dts != AV_NOPTS_VALUE)
        state->last_dts = rebased_dts;

      av_packet_unref(pkt);
    }

    for (int s = 0; s < stream_count; s++)
    {
      int64_t tail = streams[s].last_pts;
      if (tail == AV_NOPTS_VALUE)
        tail = streams[s].last_dts;
      if (tail != AV_NOPTS_VALUE)
        streams[s].offset = tail + 1;
    }

    free(chunk_map);
    free(first_pts);
    avformat_close_input(&in_ctx);

    if (rc != STITCH_OK)
      break;
  }

  if (rc == STITCH_OK)
  {
    if (!header_written)
      rc = STITCH_ERR_STREAM;
    else if (av_write_trailer(out_ctx) < 0)
      rc = STITCH_ERR_WRITE;
  }

  if (pkt)
    av_packet_free(&pkt);
  if (out_ctx && !(out_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_ctx->pb);
  if (out_ctx)
    avformat_free_context(out_ctx);
  av_dict_free(&mux_opts);
  free(streams);

  if (rc != STITCH_OK && !header_written && out_ctx && out_ctx->pb)
    avio_closep(&out_ctx->pb);

  return rc;
}
