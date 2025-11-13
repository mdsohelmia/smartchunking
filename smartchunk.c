#include "smartchunk.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

// -----------------------------
// Helpers
// -----------------------------

static double sc_chunk_dur(const sc_chunk *c)
{
  return c->end - c->start;
}

static int ensure_frame_capacity(sc_probe_result *out, int needed)
{
  if (needed <= out->capacity)
    return SC_OK;

  int new_cap = out->capacity ? out->capacity * 2 : 1024;
  if (new_cap < needed)
    new_cap = needed;

  sc_frame_meta *p = (sc_frame_meta *)realloc(out->frames,
                                              new_cap * sizeof(sc_frame_meta));
  if (!p)
    return SC_ERR_NOMEM;

  out->frames = p;
  out->capacity = new_cap;
  return SC_OK;
}

static int ensure_chunk_capacity(sc_chunk_plan *plan, int needed)
{
  if (needed <= plan->capacity)
    return SC_OK;

  int new_cap = plan->capacity ? plan->capacity * 2 : 128;
  if (new_cap < needed)
    new_cap = needed;

  sc_chunk *p = (sc_chunk *)realloc(plan->chunks,
                                    new_cap * sizeof(sc_chunk));
  if (!p)
    return SC_ERR_NOMEM;

  plan->chunks = p;
  plan->capacity = new_cap;
  return SC_OK;
}

// -------------------------------------------------------------
// FAST PROBE: PTS + KEYFRAME ONLY — No decoding
// -------------------------------------------------------------
int sc_probe_video(const char *filename, sc_probe_result *out)
{
  if (!filename || !out)
    return SC_ERR_INVAL;

  memset(out, 0, sizeof(*out));

  AVFormatContext *fmt = NULL;
  int ret = avformat_open_input(&fmt, filename, NULL, NULL);
  if (ret < 0)
    return SC_ERR_FFMPEG;

  ret = avformat_find_stream_info(fmt, NULL);
  if (ret < 0)
  {
    avformat_close_input(&fmt);
    return SC_ERR_FFMPEG;
  }

  int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (vstream < 0)
  {
    avformat_close_input(&fmt);
    return SC_ERR_NOSTREAM;
  }

  AVStream *st = fmt->streams[vstream];
  AVRational tb = st->time_base;

  if (fmt->duration > 0)
  {
    out->duration = (double)fmt->duration / AV_TIME_BASE;
  }
  else if (st->duration > 0)
  {
    out->duration = st->duration * av_q2d(tb);
  }
  else
  {
    out->duration = 0.0;
  }

  AVPacket pkt;
  double last_pts = 0.0;

  av_seek_frame(fmt, vstream, 0, AVSEEK_FLAG_BACKWARD);

  while ((ret = av_read_frame(fmt, &pkt)) >= 0)
  {
    if (pkt.stream_index == vstream)
    {
      double pts = 0.0;
      if (pkt.pts != AV_NOPTS_VALUE)
        pts = pkt.pts * av_q2d(tb);

      last_pts = pts;

      int next_index = out->count + 1;
      int er = ensure_frame_capacity(out, next_index);
      if (er != SC_OK)
      {
        av_packet_unref(&pkt);
        avformat_close_input(&fmt);
        return er;
      }

      sc_frame_meta *fm = &out->frames[out->count];
      fm->pts_time = pts;
      fm->is_keyframe = (pkt.flags & AV_PKT_FLAG_KEY) ? 1 : 0;
      out->count++;
    }
    av_packet_unref(&pkt);
  }

  avformat_close_input(&fmt);

  if (out->duration <= 0.0 && last_pts > 0.0)
  {
    out->duration = last_pts;
  }

  return SC_OK;
}

void sc_free_probe(sc_probe_result *res)
{
  if (!res)
    return;
  free(res->frames);
  res->frames = NULL;
  res->count = 0;
  res->capacity = 0;
  res->duration = 0.0;
}

// -------------------------------------------------------------
// Basic planning pass
// -------------------------------------------------------------
static int plan_basic(const sc_probe_result *meta,
                      double target,
                      double min_dur,
                      double max_dur,
                      int avoid_tiny_last,
                      sc_chunk_plan *out)
{
  out->chunks = NULL;
  out->count = 0;
  out->capacity = 0;

  if (!meta || meta->count <= 0 || meta->duration <= 0.0)
    return SC_ERR_INVAL;

  int first = -1;
  for (int i = 0; i < meta->count; i++)
  {
    if (meta->frames[i].is_keyframe)
    {
      first = i;
      break;
    }
  }

  if (first < 0)
  {
    if (ensure_chunk_capacity(out, 1) != SC_OK)
      return SC_ERR_NOMEM;
    out->chunks[0] = (sc_chunk){.index = 0, .start = 0.0, .end = meta->duration};
    out->count = 1;
    return SC_OK;
  }

  double start = meta->frames[first].pts_time;
  int idx = 0;

  for (int i = first + 1; i < meta->count; i++)
  {
    const sc_frame_meta *f = &meta->frames[i];
    if (!f->is_keyframe)
      continue;

    double d = f->pts_time - start;
    int cut = 0;

    if (d >= target)
      cut = 1;
    if (max_dur > 0 && d >= max_dur)
      cut = 1;

    if (cut)
    {
      int next_index = out->count + 1;
      int r = ensure_chunk_capacity(out, next_index);
      if (r != SC_OK)
        return r;

      out->chunks[out->count++] = (sc_chunk){
          .index = idx++,
          .start = start,
          .end = f->pts_time};
      start = f->pts_time;
    }
  }

  if (start < meta->duration)
  {
    int next_index = out->count + 1;
    int r = ensure_chunk_capacity(out, next_index);
    if (r != SC_OK)
      return r;

    out->chunks[out->count++] = (sc_chunk){
        .index = idx,
        .start = start,
        .end = meta->duration};
  }

  if (avoid_tiny_last && out->count >= 2)
  {
    sc_chunk *last = &out->chunks[out->count - 1];
    sc_chunk *prev = &out->chunks[out->count - 2];

    if (sc_chunk_dur(last) < (min_dur * 0.5))
    {
      prev->end = last->end;
      out->count--;
    }
  }

  for (int i = 0; i < out->count; i++)
    out->chunks[i].index = i;

  return SC_OK;
}

// -------------------------------------------------------------
// Merge chunks to meet max_chunks
// -------------------------------------------------------------
static void merge_chunks_to(sc_chunk_plan *p, int target)
{
  if (!p || target <= 0)
    return;

  while (p->count > target && p->count > 1)
  {
    int best = 0;
    double best_sum = 1e30;

    for (int i = 0; i < p->count - 1; i++)
    {
      double sum = sc_chunk_dur(&p->chunks[i]) +
                   sc_chunk_dur(&p->chunks[i + 1]);
      if (sum < best_sum)
      {
        best_sum = sum;
        best = i;
      }
    }

    p->chunks[best].end = p->chunks[best + 1].end;

    for (int j = best + 1; j < p->count - 1; j++)
      p->chunks[j] = p->chunks[j + 1];

    p->count--;
  }

  for (int i = 0; i < p->count; i++)
    p->chunks[i].index = i;
}

// -------------------------------------------------------------
// Public API: Smart chunk planning
// -------------------------------------------------------------
int sc_plan_chunks(const sc_probe_result *meta,
                   sc_plan_config cfg,
                   sc_chunk_plan *out)
{
  if (!meta || !out)
    return SC_ERR_INVAL;
  if (meta->duration <= 0.0)
    return SC_ERR_INVAL;

  memset(out, 0, sizeof(*out));

  double target = cfg.target_dur;

  if (cfg.ideal_parallel > 0)
  {
    target = meta->duration / (double)cfg.ideal_parallel;
  }

  if (target <= 0.0)
  {
    target = 10.0;
  }

  double min_dur = (cfg.min_dur > 0.0) ? cfg.min_dur : target * 0.5;
  double max_dur = (cfg.max_dur > 0.0) ? cfg.max_dur : target * 2.0;

  int r = plan_basic(meta, target, min_dur, max_dur,
                     cfg.avoid_tiny_last, out);
  if (r != SC_OK)
    return r;

  if (cfg.min_chunks > 0 && out->count < cfg.min_chunks)
  {
    free(out->chunks);
    out->chunks = NULL;
    out->count = 0;
    out->capacity = 0;

    double new_target = meta->duration / (double)cfg.min_chunks;
    r = plan_basic(meta, new_target, min_dur, max_dur,
                   cfg.avoid_tiny_last, out);
    if (r != SC_OK)
      return r;
  }

  if (cfg.max_chunks > 0 && out->count > cfg.max_chunks)
  {
    merge_chunks_to(out, cfg.max_chunks);
  }

  return SC_OK;
}

void sc_free_chunk_plan(sc_chunk_plan *plan)
{
  if (!plan)
    return;
  free(plan->chunks);
  plan->chunks = NULL;
  plan->count = 0;
  plan->capacity = 0;
}
