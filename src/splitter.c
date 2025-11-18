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
#include <libavutil/timestamp.h>

// ---------------------------------------------------------
// Create directory
// ---------------------------------------------------------
static int mkdir_if_needed(const char *path)
{
#ifdef _WIN32
  return _mkdir(path) == 0 || errno == EEXIST;
#else
  return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

// ---------------------------------------------------------
// Detect output format based on input file extension
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// Split a single chunk
// ---------------------------------------------------------
int split_one_chunk(const char *input,
                    const sc_chunk *chunk,
                    const char *output_file,
                    const split_output_mode *mode)
{
  int rc = SPLIT_OK;

  AVFormatContext *in_fmt = NULL;
  AVFormatContext *out_fmt = NULL;
  const AVOutputFormat *ofmt = NULL;
  int *stream_map = NULL;
  AVPacket *pkt = NULL;
  AVDictionary *mux_opts = NULL;
  int64_t *pts_base = NULL;
  int64_t *dts_base = NULL;
  int64_t *last_pts = NULL;
  int64_t *last_dts = NULL;
  int *stream_ended = NULL;

  const split_output_mode default_mode = {
      .auto_mode = 1,
      .force_fmt = NULL,
      .output_frag = 0};
  const split_output_mode *cfg = mode ? mode : &default_mode;

  // -----------------------------
  // Open input file
  // -----------------------------
  if (avformat_open_input(&in_fmt, input, NULL, NULL) < 0)
    return SPLIT_ERR_OPEN;

  if (avformat_find_stream_info(in_fmt, NULL) < 0)
  {
    rc = SPLIT_ERR_FFMPEG;
    goto cleanup;
  }

  // -----------------------------
  // Select output container
  // -----------------------------
  const char *fmt_name = NULL;

  if (cfg->auto_mode)
    fmt_name = detect_output_fmt(input);
  else if (cfg->force_fmt)
    fmt_name = cfg->force_fmt;
  else
    fmt_name = "mp4";

  ofmt = av_guess_format(fmt_name, NULL, NULL);
  if (!ofmt)
  {
    rc = SPLIT_ERR_OUTPUT;
    goto cleanup;
  }

  if (avformat_alloc_output_context2(&out_fmt, ofmt, fmt_name, output_file) < 0)
  {
    rc = SPLIT_ERR_OUTPUT;
    goto cleanup;
  }

  // Enable fMP4 mode if requested
  if (cfg->output_frag && !strcmp(fmt_name, "mp4"))
  {
    av_dict_set(&mux_opts, "movflags", "frag_keyframe+empty_moov+omit_tfhd_offset", 0);
  }

  // -----------------------------
  // Create output streams
  // -----------------------------
  stream_map = calloc(in_fmt->nb_streams, sizeof(int));
  if (!stream_map)
  {
    rc = SPLIT_ERR_NOMEM;
    goto cleanup;
  }

  const unsigned in_stream_count = in_fmt->nb_streams;
  last_pts = malloc(sizeof(int64_t) * in_stream_count);
  last_dts = malloc(sizeof(int64_t) * in_stream_count);
  if (!last_pts || !last_dts)
  {
    rc = SPLIT_ERR_NOMEM;
    goto cleanup;
  }

  for (unsigned i = 0; i < in_stream_count; i++)
  {
    AVStream *ist = in_fmt->streams[i];
    AVCodecParameters *ip = ist->codecpar;

    if (ip->codec_type == AVMEDIA_TYPE_ATTACHMENT)
    {
      stream_map[i] = -1;
      continue;
    }

    AVStream *ost = avformat_new_stream(out_fmt, NULL);
    if (!ost)
    {
      rc = SPLIT_ERR_STREAM;
      goto cleanup;
    }

    if (avcodec_parameters_copy(ost->codecpar, ip) < 0)
    {
      rc = SPLIT_ERR_STREAM;
      goto cleanup;
    }

    ost->codecpar->codec_tag = 0;
    ost->time_base = ist->time_base;

    stream_map[i] = ost->index;
  }

  // -----------------------------
  // Open output file
  // -----------------------------
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
  {
    if (avio_open(&out_fmt->pb, output_file, AVIO_FLAG_WRITE) < 0)
    {
      rc = SPLIT_ERR_OUTPUT;
      goto cleanup;
    }
  }

  if (avformat_write_header(out_fmt, &mux_opts) < 0)
  {
    rc = SPLIT_ERR_WRITE;
    goto cleanup;
  }

  // -----------------------------
  // Seek to beginning of chunk
  // -----------------------------
  double start_pts = chunk->start;
  int64_t seek_ts = start_pts * AV_TIME_BASE;

  if (av_seek_frame(in_fmt, -1, seek_ts, AVSEEK_FLAG_BACKWARD) < 0)
  {
    rc = SPLIT_ERR_SEEK;
    goto cleanup;
  }

  // -----------------------------
  // Copy packets
  // -----------------------------
  pkt = av_packet_alloc();
  if (!pkt)
  {
    rc = SPLIT_ERR_NOMEM;
    goto cleanup;
  }

  double end_pts = chunk->end;
  pts_base = calloc(in_stream_count, sizeof(int64_t));
  dts_base = calloc(in_stream_count, sizeof(int64_t));
  stream_ended = calloc(in_stream_count, sizeof(int));
  if (!pts_base || !dts_base || !stream_ended)
  {
    rc = SPLIT_ERR_NOMEM;
    goto cleanup;
  }

  for (unsigned i = 0; i < in_stream_count; i++)
  {
    pts_base[i] = AV_NOPTS_VALUE;
    dts_base[i] = AV_NOPTS_VALUE;
    last_pts[i] = AV_NOPTS_VALUE;
    last_dts[i] = AV_NOPTS_VALUE;
    stream_ended[i] = 0;
  }

  int first_keyframe_found = 0;
  int video_ended = 0;

  while (1)
  {
    if (av_read_frame(in_fmt, pkt) < 0)
      break;

    AVStream *ist = in_fmt->streams[pkt->stream_index];
    int in_si = pkt->stream_index;
    int out_si = stream_map[in_si];

    if (out_si < 0)
    {
      av_packet_unref(pkt);
      continue;
    }

    double ts = 0.0;
    if (pkt->pts != AV_NOPTS_VALUE)
      ts = pkt->pts * av_q2d(ist->time_base);
    else if (pkt->dts != AV_NOPTS_VALUE)
      ts = pkt->dts * av_q2d(ist->time_base);

    // For the first chunk or at exact boundaries, wait for first keyframe
    // For subsequent packets, include everything in the range
    if (!first_keyframe_found)
    {
      if (ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        if (!(pkt->flags & AV_PKT_FLAG_KEY) || ts < start_pts)
        {
          av_packet_unref(pkt);
          continue;
        }
        first_keyframe_found = 1;
      }
      else if (ts < start_pts)
      {
        av_packet_unref(pkt);
        continue;
      }
    }

    // Stop when video hits a keyframe at/past the boundary
    // Mark video as ended but continue processing other streams
    if (ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ts >= end_pts && (pkt->flags & AV_PKT_FLAG_KEY))
    {
      video_ended = 1;
      stream_ended[in_si] = 1;
      av_packet_unref(pkt);
      continue;
    }

    // Skip video packets after video has ended
    if (ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_ended)
    {
      av_packet_unref(pkt);
      continue;
    }

    // For non-video streams, stop when timestamp exceeds boundary
    // This ensures all audio packets within the range are included
    if (ist->codecpar->codec_type != AVMEDIA_TYPE_VIDEO && ts >= end_pts)
    {
      stream_ended[in_si] = 1;
      av_packet_unref(pkt);
      continue;
    }

    // If all streams have ended, we're done
    int all_ended = 1;
    for (unsigned i = 0; i < in_stream_count; i++)
    {
      if (stream_map[i] >= 0 && !stream_ended[i])
      {
        all_ended = 0;
        break;
      }
    }
    if (all_ended)
    {
      av_packet_unref(pkt);
      break;
    }

    AVStream *ost = out_fmt->streams[out_si];

    // For bit-perfect reconstruction, we DON'T rebase timestamps at all
    // Just copy packets exactly as they are
    // The stitcher will handle timeline continuity

    // Rescale timestamps from input to output timebase
    av_packet_rescale_ts(pkt, ist->time_base, ost->time_base);

    // Track last values in output timebase
    if (pkt->pts != AV_NOPTS_VALUE)
      last_pts[out_si] = pkt->pts;
    if (pkt->dts != AV_NOPTS_VALUE)
      last_dts[out_si] = pkt->dts;

    pkt->pos = -1;
    pkt->stream_index = out_si;

    if (av_interleaved_write_frame(out_fmt, pkt) < 0)
    {
      rc = SPLIT_ERR_WRITE;
      av_packet_unref(pkt);
      break;
    }

    av_packet_unref(pkt);
  }

  av_write_trailer(out_fmt);

cleanup:
  if (pkt)
    av_packet_free(&pkt);

  if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt->pb);

  if (out_fmt)
    avformat_free_context(out_fmt);

  if (in_fmt)
    avformat_close_input(&in_fmt);

  free(stream_map);
  av_dict_free(&mux_opts);
  free(pts_base);
  free(dts_base);
  free(last_pts);
  free(last_dts);
  free(stream_ended);

  return rc;
}

// ---------------------------------------------------------
// Split all chunks (single-threaded version)
// (Threaded version exists in chunkify_cli)
// ---------------------------------------------------------
int split_all_chunks(const char *input,
                     const sc_chunk_plan *plan,
                     const char *outdir,
                     const split_output_mode *mode)
{
  if (!plan || plan->count <= 0)
    return SPLIT_OK;

  if (!mkdir_if_needed(outdir))
  {
    fprintf(stderr, "mkdir failed: %s\n", outdir);
    return SPLIT_ERR_OUTPUT;
  }

  char outpath[512];

  for (int i = 0; i < plan->count; i++)
  {
    const sc_chunk *c = &plan->chunks[i];

    snprintf(outpath, sizeof(outpath),
             "%s/chunk_%04d.mp4", outdir, c->index);

    fprintf(stderr, "[split] %s (%.3f → %.3f)\n",
            outpath, c->start, c->end);

    int r = split_one_chunk(input, c, outpath, mode);
    if (r != SPLIT_OK)
    {
      fprintf(stderr, "split_one_chunk failed: %d\n", r);
      return r;
    }
  }
  return SPLIT_OK;
}
