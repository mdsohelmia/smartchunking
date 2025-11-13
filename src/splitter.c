#include "splitter.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

static int mkdir_if_needed(const char *path)
{
#ifdef _WIN32
  return _mkdir(path) == 0 || errno == EEXIST;
#else
  return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

static const char *detect_output_fmt(const char *input)
{
  const char *ext = strrchr(input, '.');
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

static double packet_seconds(const AVPacket *pkt,
                             const AVStream *st,
                             double fallback)
{
  if (pkt->pts != AV_NOPTS_VALUE)
    return pkt->pts * av_q2d(st->time_base);
  if (pkt->dts != AV_NOPTS_VALUE)
    return pkt->dts * av_q2d(st->time_base);
  return fallback;
}

int split_one_chunk(const char *input,
                    const sc_chunk *chunk,
                    const char *output_file,
                    const split_output_mode *mode)
{
  if (!input || !chunk || !output_file)
    return SPLIT_ERR_INVAL;
  if (chunk->end <= chunk->start)
    return SPLIT_ERR_INVAL;

  split_output_mode fallback = {
      .auto_mode = 1,
      .force_fmt = NULL,
      .output_frag = 0};
  const split_output_mode *cfg = mode ? mode : &fallback;

  const char *fmt_name = cfg->auto_mode
                             ? detect_output_fmt(input)
                             : (cfg->force_fmt ? cfg->force_fmt : "mp4");

  AVFormatContext *in_ctx = NULL;
  AVFormatContext *out_ctx = NULL;
  AVPacket *pkt = NULL;
  int *stream_map = NULL;
  int64_t *first_pts = NULL;
  int64_t *first_dts = NULL;
  AVDictionary *mux_opts = NULL;
  int rc = SPLIT_OK;

  if (avformat_open_input(&in_ctx, input, NULL, NULL) < 0)
    return SPLIT_ERR_OPEN;
  if (avformat_find_stream_info(in_ctx, NULL) < 0)
  {
    rc = SPLIT_ERR_FFMPEG;
    goto cleanup;
  }

  const AVOutputFormat *ofmt = av_guess_format(fmt_name, NULL, NULL);
  if (!ofmt)
  {
    rc = SPLIT_ERR_OUTPUT;
    goto cleanup;
  }
  if (avformat_alloc_output_context2(&out_ctx, ofmt, fmt_name, output_file) < 0)
  {
    rc = SPLIT_ERR_OUTPUT;
    goto cleanup;
  }

  if (cfg->output_frag && !strcmp(fmt_name, "mp4"))
    av_dict_set(&mux_opts, "movflags", "frag_keyframe+empty_moov+omit_tfhd_offset", 0);

  const unsigned nb_streams = in_ctx->nb_streams;
  stream_map = calloc(nb_streams, sizeof(int));
  first_pts = calloc(nb_streams, sizeof(int64_t));
  first_dts = calloc(nb_streams, sizeof(int64_t));
  if (!stream_map || !first_pts || !first_dts)
  {
    rc = SPLIT_ERR_NOMEM;
    goto cleanup;
  }
  for (unsigned i = 0; i < nb_streams; i++)
  {
    stream_map[i] = -1;
    first_pts[i] = AV_NOPTS_VALUE;
    first_dts[i] = AV_NOPTS_VALUE;
  }

  for (unsigned i = 0; i < nb_streams; i++)
  {
    AVStream *ist = in_ctx->streams[i];
    AVStream *ost = avformat_new_stream(out_ctx, NULL);
    if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0)
    {
      rc = SPLIT_ERR_STREAM;
      goto cleanup;
    }
    ost->codecpar->codec_tag = 0;
    ost->time_base = ist->time_base;
    stream_map[i] = ost->index;
  }

  if (!(out_ctx->oformat->flags & AVFMT_NOFILE))
  {
    if (avio_open(&out_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0)
    {
      rc = SPLIT_ERR_OUTPUT;
      goto cleanup;
    }
  }

  if (avformat_write_header(out_ctx, &mux_opts) < 0)
  {
    rc = SPLIT_ERR_WRITE;
    goto cleanup;
  }

  const double start_sec = chunk->start;
  const double end_sec = chunk->end;
  const int64_t seek_ts = (int64_t)(start_sec * AV_TIME_BASE);

    if (avformat_seek_file(in_ctx, -1, INT64_MIN, seek_ts, seek_ts, AVSEEK_FLAG_BACKWARD) < 0)
    {
        rc = SPLIT_ERR_SEEK;
        goto cleanup;
    }

  pkt = av_packet_alloc();
  if (!pkt)
  {
    rc = SPLIT_ERR_NOMEM;
    goto cleanup;
  }

  double last_ts = start_sec;
  const double tol = 1e-6;
  int done = 0;

  while (!done && av_read_frame(in_ctx, pkt) >= 0)
  {
    AVStream *ist = in_ctx->streams[pkt->stream_index];
    AVStream *ost = out_ctx->streams[stream_map[pkt->stream_index]];
    double pkt_ts = packet_seconds(pkt, ist, last_ts);

    if (pkt_ts + tol < start_sec)
    {
      av_packet_unref(pkt);
      continue;
    }
    if (pkt_ts - tol > end_sec)
    {
      av_packet_unref(pkt);
      break;
    }

    last_ts = pkt_ts;

    if (pkt->pts != AV_NOPTS_VALUE)
    {
      if (first_pts[pkt->stream_index] == AV_NOPTS_VALUE)
        first_pts[pkt->stream_index] = pkt->pts;
      pkt->pts -= first_pts[pkt->stream_index];
      if (pkt->pts < 0)
        pkt->pts = 0;
    }
    if (pkt->dts != AV_NOPTS_VALUE)
    {
      if (first_dts[pkt->stream_index] == AV_NOPTS_VALUE)
        first_dts[pkt->stream_index] = pkt->dts;
      pkt->dts -= first_dts[pkt->stream_index];
      if (pkt->dts < 0)
        pkt->dts = 0;
    }
    if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
      pkt->pts = pkt->dts;
    if (pkt->dts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
      pkt->dts = pkt->pts;

    av_packet_rescale_ts(pkt, ist->time_base, ost->time_base);
    pkt->stream_index = ost->index;
    pkt->pos = -1;

    if (av_interleaved_write_frame(out_ctx, pkt) < 0)
    {
      rc = SPLIT_ERR_WRITE;
      av_packet_unref(pkt);
      break;
    }
    av_packet_unref(pkt);
  }

  if (rc == SPLIT_OK && av_write_trailer(out_ctx) < 0)
    rc = SPLIT_ERR_WRITE;

cleanup:
  if (pkt)
    av_packet_free(&pkt);
  if (out_ctx && !(out_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_ctx->pb);
  if (out_ctx)
    avformat_free_context(out_ctx);
  if (in_ctx)
    avformat_close_input(&in_ctx);

  free(stream_map);
  free(first_pts);
  free(first_dts);
  av_dict_free(&mux_opts);

  return rc;
}

int split_all_chunks(const char *input,
                     const sc_chunk_plan *plan,
                     const char *outdir,
                     const split_output_mode *mode)
{
  if (!input || !plan || plan->count == 0 || !outdir)
    return SPLIT_ERR_INVAL;

  if (!mkdir_if_needed(outdir))
    return SPLIT_ERR_OUTPUT;

  char path[512];
  for (int i = 0; i < plan->count; i++)
  {
    snprintf(path, sizeof(path), "%s/chunk_%04d.mp4", outdir, plan->chunks[i].index);
    fprintf(stderr, "[split] %s (%.3f → %.3f)\n",
            path, plan->chunks[i].start, plan->chunks[i].end);

    int ret = split_one_chunk(input, &plan->chunks[i], path, mode);
    if (ret != SPLIT_OK)
    {
      fprintf(stderr, "split_one_chunk failed (%d)\n", ret);
      return ret;
    }
  }
  return SPLIT_OK;
}
