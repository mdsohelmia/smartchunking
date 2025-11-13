#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include "smartchunk.h"

static void print_video_info_cli(const char *input)
{
  AVFormatContext *fmt = NULL;

  if (avformat_open_input(&fmt, input, NULL, NULL) != 0)
  {
    fprintf(stderr, "Could not open input\n");
    return;
  }

  if (avformat_find_stream_info(fmt, NULL) < 0)
  {
    fprintf(stderr, "Could not read stream info\n");
    avformat_close_input(&fmt);
    return;
  }

  int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (v < 0)
  {
    fprintf(stderr, "No video stream found\n");
    avformat_close_input(&fmt);
    return;
  }

  AVStream *st = fmt->streams[v];
  AVCodecParameters *cp = st->codecpar;

  double fps = 0;
  if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0)
    fps = av_q2d(st->avg_frame_rate);

  double duration = (fmt->duration > 0)
                        ? (double)fmt->duration / AV_TIME_BASE
                        : (st->duration > 0 ? st->duration * av_q2d(st->time_base) : 0);

  fprintf(stderr, "Source Video Info:\n");
  fprintf(stderr, "  Resolution : %dx%d\n", cp->width, cp->height);
  fprintf(stderr, "  FPS        : %.3f\n", fps);
  fprintf(stderr, "  Codec      : %s\n", avcodec_get_name(cp->codec_id));
  fprintf(stderr, "  Bitrate    : %lld\n", (long long)cp->bit_rate);
  fprintf(stderr, "  Duration   : %.3f sec\n", duration);

  avformat_close_input(&fmt);
}

// --------------------------------------------------------------
// MAIN
// --------------------------------------------------------------
int main(int argc, char **argv)
{
  // Disable FFmpeg noise
  av_log_set_level(AV_LOG_ERROR);

  if (argc < 4)
  {
    fprintf(stderr, "usage: %s input.mp4 target_seconds output.json\n", argv[0]);
    return 1;
  }

  const char *input = argv[1];
  double target = atof(argv[2]);
  const char *outjson = argv[3];

  // Print human info to stderr
  print_video_info_cli(input);

  // Probe keyframes
  sc_probe_result meta;
  int rc = sc_probe_video(input, &meta);
  if (rc != SC_OK)
  {
    fprintf(stderr, "Probe failed: %d\n", rc);
    return 1;
  }
  fprintf(stderr, "  Keyframes  : %d\n\n", meta.count);

  // Plan chunks
  sc_plan_config cfg = {
      .target_dur = target,
      .min_dur = target * 0.5,
      .max_dur = target * 2.0,
      .avoid_tiny_last = 1,
      .min_chunks = 0,
      .max_chunks = 0,
      .ideal_parallel = 0};

  sc_chunk_plan plan;
  rc = sc_plan_chunks(&meta, cfg, &plan);
  if (rc != SC_OK)
  {
    fprintf(stderr, "Chunk planning failed: %d\n", rc);
    sc_free_probe(&meta);
    return 1;
  }

  // ----------------------------
  // Write JSON file
  // ----------------------------
  FILE *fp = fopen(outjson, "w");
  if (!fp)
  {
    fprintf(stderr, "Could not open output json file\n");
    sc_free_probe(&meta);
    sc_free_chunk_plan(&plan);
    return 1;
  }

  fprintf(fp, "[\n");
  for (int i = 0; i < plan.count; i++)
  {
    sc_chunk c = plan.chunks[i];
    fprintf(fp,
            "  {\"index\":%d,\"start\":%.3f,\"end\":%.3f}%s\n",
            c.index, c.start, c.end,
            (i + 1 == plan.count ? "" : ","));
  }
  fprintf(fp, "]\n");
  fclose(fp);

  fprintf(stderr, "JSON written to %s\n", outjson);

  sc_free_probe(&meta);
  sc_free_chunk_plan(&plan);
  return 0;
}
