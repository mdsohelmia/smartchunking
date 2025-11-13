#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <libavformat/avformat.h>
#include "smartchunk.h"

typedef struct
{
  const char *input;
  const char *outdir;
  const sc_chunk *chunks;
  int total;
  int *next_index;
  pthread_mutex_t *lock;
} split_worker_ctx;

// ------------------------------------------
// Video info (human readable)
// ------------------------------------------
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

// ------------------------------------------
// Write JSON only
// ------------------------------------------
static int write_json(const char *outjson, const sc_chunk_plan *plan)
{
  FILE *fp = fopen(outjson, "w");
  if (!fp)
  {
    fprintf(stderr, "Could not open output json file %s\n", outjson);
    return -1;
  }

  fprintf(fp, "[\n");
  for (int i = 0; i < plan->count; i++)
  {
    sc_chunk c = plan->chunks[i];
    fprintf(fp,
            "  {\"index\":%d,\"start\":%.3f,\"end\":%.3f}%s\n",
            c.index, c.start, c.end,
            (i + 1 == plan->count ? "" : ","));
  }
  fprintf(fp, "]\n");
  fclose(fp);

  fprintf(stderr, "JSON written to %s\n", outjson);
  return 0;
}

// ------------------------------------------
// Worker: parallel ffmpeg -c copy splitting
// ------------------------------------------
static void *split_worker(void *arg)
{
  split_worker_ctx *ctx = (split_worker_ctx *)arg;

  while (1)
  {
    int idx;

    pthread_mutex_lock(ctx->lock);
    idx = *ctx->next_index;
    if (idx >= ctx->total)
    {
      pthread_mutex_unlock(ctx->lock);
      break;
    }
    (*ctx->next_index)++;
    pthread_mutex_unlock(ctx->lock);

    const sc_chunk *c = &ctx->chunks[idx];

    char outpath[512];
    snprintf(outpath, sizeof(outpath), "%s/chunk_%04d.mp4",
             ctx->outdir, c->index);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -y -ss %.3f -to %.3f -i \"%s\" "
             "-c copy \"%s\" >/dev/null 2>&1",
             c->start, c->end, ctx->input, outpath);

    int ret = system(cmd);
    if (ret != 0)
    {
      fprintf(stderr, "Chunk %d failed (ffmpeg exit %d)\n", c->index, ret);
    }
    else
    {
      fprintf(stderr, "Created %s (%.3f → %.3f)\n",
              outpath, c->start, c->end);
    }
  }

  return NULL;
}

// ------------------------------------------
// Parallel splitting
// ------------------------------------------
static int parallel_split(const char *input,
                          const char *outdir,
                          const sc_chunk_plan *plan,
                          int jobs)
{
  if (plan->count == 0)
  {
    fprintf(stderr, "No chunks to split.\n");
    return 0;
  }

#ifndef _WIN32
  if (mkdir(outdir, 0755) && errno != EEXIST)
  {
    perror("mkdir");
    return -1;
  }
#endif

  if (jobs <= 0)
    jobs = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (jobs < 1)
    jobs = 1;
  if (jobs > plan->count)
    jobs = plan->count;

  fprintf(stderr, "Splitting %d chunks using %d workers...\n",
          plan->count, jobs);

  pthread_t *threads = (pthread_t *)calloc(jobs, sizeof(pthread_t));
  if (!threads)
  {
    fprintf(stderr, "No memory for threads\n");
    return -1;
  }

  int next_index = 0;
  pthread_mutex_t lock;
  pthread_mutex_init(&lock, NULL);

  split_worker_ctx ctx = {
      .input = input,
      .outdir = outdir,
      .chunks = plan->chunks,
      .total = plan->count,
      .next_index = &next_index,
      .lock = &lock,
  };

  for (int i = 0; i < jobs; i++)
  {
    pthread_create(&threads[i], NULL, split_worker, &ctx);
  }

  for (int i = 0; i < jobs; i++)
  {
    pthread_join(threads[i], NULL);
  }

  pthread_mutex_destroy(&lock);
  free(threads);

  fprintf(stderr, "All chunks written to %s\n", outdir);
  return 0;
}

// ------------------------------------------
// NEW: Lossless Stitching
// ------------------------------------------
static int stitch_chunks(const char *json, const char *chunkdir, const char *output)
{
  FILE *f = fopen(json, "r");
  if (!f)
  {
    fprintf(stderr, "Could not open %s\n", json);
    return -1;
  }

  // Count entries in JSON (simple counting of lines with "index")
  int count = 0;
  char line[512];
  while (fgets(line, sizeof(line), f))
  {
    if (strstr(line, "\"index\""))
      count++;
  }
  fclose(f);

  if (count == 0)
  {
    fprintf(stderr, "No chunks in JSON.\n");
    return -1;
  }

  char listfile[512];
  snprintf(listfile, sizeof(listfile), "%s/list.txt", chunkdir);

  FILE *lf = fopen(listfile, "w");
  if (!lf)
  {
    fprintf(stderr, "Could not create %s\n", listfile);
    return -1;
  }

  for (int i = 0; i < count; i++)
  {
    char path[512];
    snprintf(path, sizeof(path), "%s/chunk_%04d.mp4", chunkdir, i);
    fprintf(lf, "file '%s'\n", path);
  }

  fclose(lf);

  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -y -f concat -safe 0 -i \"%s\" -c copy \"%s\"",
           listfile, output);

  fprintf(stderr, "Stitching into %s...\n", output);

  int ret = system(cmd);
  if (ret != 0)
  {
    fprintf(stderr, "Stitch failed.\n");
    return -1;
  }

  fprintf(stderr, "Done: %s\n", output);
  return 0;
}

// ------------------------------------------
// Usage
// ------------------------------------------
static void print_usage(const char *name)
{
  fprintf(stderr,
          "Usage:\n"
          "  %s input.mp4 target_seconds output.json\n"
          "  %s input.mp4 target_seconds output.json --split [--jobs N]\n"
          "  %s --stitch chunks.json chunkdir output.mp4\n",
          name, name, name);
}

// ------------------------------------------
// MAIN
// ------------------------------------------
int main(int argc, char **argv)
{
  av_log_set_level(AV_LOG_ERROR);

  // -------------------------
  // MODE 1: STITCH
  // -------------------------
  if (argc >= 5 && strcmp(argv[1], "--stitch") == 0)
  {
    const char *json = argv[2];
    const char *chunkdir = argv[3];
    const char *out = argv[4];
    return stitch_chunks(json, chunkdir, out);
  }

  // -------------------------
  // MODE 2: JSON or SPLIT
  // -------------------------
  if (argc < 4)
  {
    print_usage(argv[0]);
    return 1;
  }

  const char *input = argv[1];
  double target = atof(argv[2]);
  const char *outjson = argv[3];

  int do_split = 0;
  int jobs = 0;

  for (int i = 4; i < argc; i++)
  {
    if (strcmp(argv[i], "--split") == 0)
      do_split = 1;
    else if (strcmp(argv[i], "--jobs") == 0 && i + 1 < argc)
      jobs = atoi(argv[++i]);
  }

  print_video_info_cli(input);

  sc_probe_result meta;
  int rc = sc_probe_video(input, &meta);
  if (rc != SC_OK)
  {
    fprintf(stderr, "Probe failed: %d\n", rc);
    return 1;
  }

  fprintf(stderr, "  Keyframes  : %d\n\n", meta.count);

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

  write_json(outjson, &plan);

  if (do_split)
    parallel_split(input, "chunks", &plan, jobs);

  sc_free_probe(&meta);
  sc_free_chunk_plan(&plan);
  return 0;
}
