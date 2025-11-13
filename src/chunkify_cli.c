#include "smartchunk.h"
#include "splitter.h"
#include "stitcher.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>

typedef struct
{
  const char *input;
  const char *chunks_dir;
  const char *final_out;
  const char *plan_json;
  double target;
  double min_dur;
  double max_dur;
  int ideal_parallel;
  int min_chunks;
  int max_chunks;
  int avoid_tiny_last;
  int frag_output;
  const char *force_format;
  int skip_split;
  int skip_stitch;
} cli_config;

static void cli_defaults(cli_config *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->target = 60.0;
  cfg->avoid_tiny_last = 1;
}

static void print_usage(const char *prog)
{
  fprintf(stderr,
          "Usage: %s [options] <input> <chunks_dir> [final_output]\n"
          "\n"
          "Options:\n"
          "  --target <sec>         Target chunk duration (default 60)\n"
          "  --min <sec>            Minimum chunk duration\n"
          "  --max <sec>            Maximum chunk duration\n"
          "  --ideal-par <n>        Ideal parallel workers (overrides target)\n"
          "  --min-chunks <n>       Minimum number of chunks\n"
          "  --max-chunks <n>       Maximum number of chunks\n"
          "  --allow-tiny-last      Keep very small tail chunks\n"
          "  --no-split             Skip chunk extraction (stitch only)\n"
          "  --no-stitch            Skip stitching\n"
          "  --frag                 Enable fragmented MP4 outputs\n"
          "  --force-format <fmt>   Force muxer (mp4/mov/matroska/...)\n"
          "  --plan-json <path>     Write plan as JSON array\n",
          prog);
}

static int parse_args(int argc, char **argv, cli_config *cfg)
{
  cli_defaults(cfg);

  for (int i = 1; i < argc; i++)
  {
    const char *arg = argv[i];
    if (!strcmp(arg, "--target") && i + 1 < argc)
    {
      cfg->target = atof(argv[++i]);
    }
    else if (!strcmp(arg, "--min") && i + 1 < argc)
    {
      cfg->min_dur = atof(argv[++i]);
    }
    else if (!strcmp(arg, "--max") && i + 1 < argc)
    {
      cfg->max_dur = atof(argv[++i]);
    }
    else if (!strcmp(arg, "--ideal-par") && i + 1 < argc)
    {
      cfg->ideal_parallel = atoi(argv[++i]);
    }
    else if (!strcmp(arg, "--min-chunks") && i + 1 < argc)
    {
      cfg->min_chunks = atoi(argv[++i]);
    }
    else if (!strcmp(arg, "--max-chunks") && i + 1 < argc)
    {
      cfg->max_chunks = atoi(argv[++i]);
    }
    else if (!strcmp(arg, "--allow-tiny-last"))
    {
      cfg->avoid_tiny_last = 0;
    }
    else if (!strcmp(arg, "--frag"))
    {
      cfg->frag_output = 1;
    }
    else if (!strcmp(arg, "--force-format") && i + 1 < argc)
    {
      cfg->force_format = argv[++i];
    }
    else if (!strcmp(arg, "--plan-json") && i + 1 < argc)
    {
      cfg->plan_json = argv[++i];
    }
    else if (!strcmp(arg, "--no-split"))
    {
      cfg->skip_split = 1;
    }
    else if (!strcmp(arg, "--no-stitch"))
    {
      cfg->skip_stitch = 1;
    }
    else if (arg[0] == '-')
    {
      fprintf(stderr, "Unknown option: %s\n", arg);
      return -1;
    }
    else if (!cfg->input)
    {
      cfg->input = arg;
    }
    else if (!cfg->chunks_dir)
    {
      cfg->chunks_dir = arg;
    }
    else if (!cfg->final_out)
    {
      cfg->final_out = arg;
    }
    else
    {
      fprintf(stderr, "Unexpected argument: %s\n", arg);
      return -1;
    }
  }

  if (!cfg->input || !cfg->chunks_dir)
  {
    fprintf(stderr, "Input file and chunks directory are required.\n");
    return -1;
  }

  if (!cfg->final_out)
    cfg->skip_stitch = 1;

  return 0;
}

static void dump_plan(const sc_chunk_plan *plan)
{
  fprintf(stdout, "Chunk plan (%d chunks):\n", plan->count);
  for (int i = 0; i < plan->count; i++)
  {
    const sc_chunk *c = &plan->chunks[i];
    fprintf(stdout, "  #%03d  %.3f -> %.3f  (%.3f s)\n",
            c->index, c->start, c->end, c->end - c->start);
  }
}

static int write_plan_json(const char *path, const sc_chunk_plan *plan)
{
  FILE *f = fopen(path, "w");
  if (!f)
  {
    fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
    return -1;
  }

  fprintf(f, "[\n");
  for (int i = 0; i < plan->count; i++)
  {
    const sc_chunk *c = &plan->chunks[i];
    fprintf(f,
            "  {\"index\": %d, \"start\": %.3f, \"end\": %.3f}%s\n",
            c->index, c->start, c->end,
            (i + 1 == plan->count) ? "" : ",");
  }
  fprintf(f, "]\n");

  fclose(f);
  return 0;
}

int main(int argc, char **argv)
{
  cli_config cfg;
  if (parse_args(argc, argv, &cfg) < 0)
  {
    print_usage(argv[0]);
    return 1;
  }

  av_log_set_level(AV_LOG_INFO);

  sc_probe_result probe;
  sc_chunk_plan plan;
  memset(&probe, 0, sizeof(probe));
  memset(&plan, 0, sizeof(plan));

  if (sc_probe_video(cfg.input, &probe) != SC_OK)
  {
    fprintf(stderr, "sc_probe_video failed for %s\n", cfg.input);
    return 2;
  }

  sc_plan_config pcfg = {
      .target_dur = cfg.target,
      .min_dur = cfg.min_dur,
      .max_dur = cfg.max_dur,
      .avoid_tiny_last = cfg.avoid_tiny_last,
      .min_chunks = cfg.min_chunks,
      .max_chunks = cfg.max_chunks,
      .ideal_parallel = cfg.ideal_parallel};

  if (sc_plan_chunks(&probe, pcfg, &plan) != SC_OK)
  {
    fprintf(stderr, "sc_plan_chunks failed.\n");
    sc_free_probe(&probe);
    return 3;
  }

  dump_plan(&plan);

  if (cfg.plan_json)
    write_plan_json(cfg.plan_json, &plan);

  int exit_code = 0;

  if (!cfg.skip_split)
  {
    split_output_mode smode = {
        .auto_mode = cfg.force_format ? 0 : 1,
        .force_fmt = cfg.force_format,
        .output_frag = cfg.frag_output};
    int sr = split_all_chunks(cfg.input, &plan, cfg.chunks_dir, &smode);
    if (sr != SPLIT_OK)
    {
      fprintf(stderr, "split_all_chunks failed: %d\n", sr);
      exit_code = 4;
      goto done;
    }
  }

  if (!cfg.skip_stitch && cfg.final_out)
  {
    stitch_output_mode stmode = {
        .auto_mode = cfg.force_format ? 0 : 1,
        .force_fmt = cfg.force_format,
        .output_frag = cfg.frag_output};
    int tr = stitch_chunks(cfg.final_out, &plan, cfg.chunks_dir, &stmode);
    if (tr != STITCH_OK)
    {
      fprintf(stderr, "stitch_chunks failed: %d\n", tr);
      exit_code = 5;
      goto done;
    }
  }

done:
  sc_free_chunk_plan(&plan);
  sc_free_probe(&probe);
  return exit_code;
}
