#include "smartchunk.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

static const double EPS = 1e-6;

static double packet_time(const AVPacket *pkt, AVRational tb, double fallback)
{
  if (pkt->pts != AV_NOPTS_VALUE)
    return pkt->pts * av_q2d(tb);
  if (pkt->dts != AV_NOPTS_VALUE)
    return pkt->dts * av_q2d(tb);
  return fallback;
}

static double packet_end(const AVPacket *pkt, AVRational tb, double pts)
{
  if (pkt->duration > 0)
    return pts + pkt->duration * av_q2d(tb);
  return pts;
}

static int ensure_frame_capacity(sc_probe_result *out, int needed)
{
  if (needed <= out->capacity)
    return SC_OK;

  int newcap = out->capacity ? out->capacity * 2 : 2048;
  if (newcap < needed)
    newcap = needed;

  sc_frame_meta *frames = realloc(out->frames, newcap * sizeof(*frames));
  if (!frames)
    return SC_ERR_NOMEM;

  out->frames = frames;
  out->capacity = newcap;
  return SC_OK;
}

static int ensure_chunk_capacity(sc_chunk_plan *plan, int needed)
{
  if (needed <= plan->capacity)
    return SC_OK;

  int newcap = plan->capacity ? plan->capacity * 2 : 64;
  if (newcap < needed)
    newcap = needed;

  sc_chunk *chunks = realloc(plan->chunks, newcap * sizeof(*chunks));
  if (!chunks)
    return SC_ERR_NOMEM;

  plan->chunks = chunks;
  plan->capacity = newcap;
  return SC_OK;
}

/* ------------------------------------------------------------------ */
/* Packet-level probe (no decoding)                                   */
/* ------------------------------------------------------------------ */
int sc_probe_video(const char *filename, sc_probe_result *out)
{
  if (!filename || !out)
    return SC_ERR_INVAL;

  memset(out, 0, sizeof(*out));

  AVFormatContext *fmt = NULL;
  if (avformat_open_input(&fmt, filename, NULL, NULL) < 0)
    return SC_ERR_FFMPEG;
  if (avformat_find_stream_info(fmt, NULL) < 0)
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

  AVPacket *pkt = av_packet_alloc();
  if (!pkt)
  {
    avformat_close_input(&fmt);
    return SC_ERR_NOMEM;
  }

  double best_end = 0.0;
  while (av_read_frame(fmt, pkt) >= 0)
  {
    if (pkt->stream_index == vstream)
    {
      double pts = packet_time(pkt, tb, best_end);
      double end = packet_end(pkt, tb, pts);

      if (ensure_frame_capacity(out, out->count + 1) != SC_OK)
      {
        av_packet_free(&pkt);
        avformat_close_input(&fmt);
        return SC_ERR_NOMEM;
      }

      sc_frame_meta *fm = &out->frames[out->count++];
      fm->pts_time = pts;
      fm->is_keyframe = (pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0;

      if (end > best_end)
        best_end = end;
    }
    av_packet_unref(pkt);
  }

  av_packet_free(&pkt);
  avformat_close_input(&fmt);

  if (best_end <= 0.0 && st->duration > 0)
    best_end = st->duration * av_q2d(tb);
  if (best_end <= 0.0 && fmt->duration > 0)
    best_end = fmt->duration / (double)AV_TIME_BASE;

  out->duration = best_end;
  return SC_OK;
}

void sc_free_probe(sc_probe_result *res)
{
  if (!res)
    return;
  free(res->frames);
  memset(res, 0, sizeof(*res));
}

/* ------------------------------------------------------------------ */
/* Chunk planning helpers                                             */
/* ------------------------------------------------------------------ */
static int collect_keyframes(const sc_probe_result *m,
                             double **times_out,
                             int *count_out)
{
  double *times = malloc(sizeof(double) * m->count);
  if (!times)
    return SC_ERR_NOMEM;

  int count = 0;
  for (int i = 0; i < m->count; i++)
  {
    if (m->frames[i].is_keyframe)
      times[count++] = m->frames[i].pts_time;
  }

  if (count == 0)
  {
    free(times);
    *times_out = NULL;
    *count_out = 0;
    return SC_OK;
  }

  *times_out = times;
  *count_out = count;
  return SC_OK;
}

static int append_chunk(sc_chunk_plan *plan,
                        int index,
                        double start,
                        double end)
{
  if (end < start + EPS)
    return SC_OK; /* ignore zero-length pieces */

  if (ensure_chunk_capacity(plan, plan->count + 1) != SC_OK)
    return SC_ERR_NOMEM;

  plan->chunks[plan->count++] = (sc_chunk){
      .index = index,
      .start = start,
      .end = end};
  return SC_OK;
}

static double choose_cut(double start,
                         double duration,
                         double target,
                         double min_dur,
                         double max_dur,
                         const double *key_times,
                         int key_count,
                         int *cursor)
{
  double best_cut = -1.0;
  double best_score = DBL_MAX;
  double fallback = -1.0;

  int idx = *cursor;
  while (idx < key_count && key_times[idx] <= start + EPS)
    idx++;

  for (; idx < key_count; idx++)
  {
    double t = key_times[idx];
    if (t >= duration - EPS)
    {
      best_cut = duration;
      break;
    }

    double span = t - start;
    if (span < min_dur - EPS)
      continue;

    if (span > max_dur + EPS)
    {
      fallback = t;
      break;
    }

    double score = fabs(span - target);
    if (score < best_score)
    {
      best_score = score;
      best_cut = t;
    }
  }

  if (best_cut < 0.0)
  {
    if (fallback > 0.0)
      best_cut = fallback;
    else
      best_cut = duration;
  }

  if (best_cut > duration)
    best_cut = duration;
  if (best_cut < start + min_dur)
    best_cut = fmin(start + min_dur, duration);

  while (*cursor < key_count && key_times[*cursor] <= best_cut + EPS)
    (*cursor)++;

  return best_cut;
}

static void merge_tiny_tail(sc_chunk_plan *plan, double min_dur, double duration)
{
  if (plan->count < 2)
    return;

  sc_chunk *last = &plan->chunks[plan->count - 1];
  sc_chunk *prev = &plan->chunks[plan->count - 2];

  if ((last->end - last->start) < (min_dur * 0.5))
  {
    prev->end = duration;
    plan->count--;
  }
}

static void renumber_chunks(sc_chunk_plan *plan)
{
  for (int i = 0; i < plan->count; i++)
    plan->chunks[i].index = i;
}

/* ------------------------------------------------------------------ */
/* Public chunk planner                                               */
/* ------------------------------------------------------------------ */
int sc_plan_chunks(const sc_probe_result *m,
                   sc_plan_config cfg,
                   sc_chunk_plan *out)
{
  if (!m || !out || m->count == 0 || m->duration <= 0.0)
    return SC_ERR_INVAL;

  memset(out, 0, sizeof(*out));

  double target = cfg.target_dur;
  if (cfg.ideal_parallel > 0)
    target = m->duration / cfg.ideal_parallel;
  if (target <= 0.0)
    target = 10.0;

  double min_dur = cfg.min_dur > 0.0 ? cfg.min_dur : target * 0.5;
  double max_dur = cfg.max_dur > 0.0 ? cfg.max_dur : target * 2.0;
  if (max_dur < min_dur)
    max_dur = min_dur;

  double *key_times = NULL;
  int key_count = 0;
  int r = collect_keyframes(m, &key_times, &key_count);
  if (r != SC_OK)
    return r;

  if (key_count == 0)
  {
    free(key_times);
    return append_chunk(out, 0, 0.0, m->duration);
  }

  double start = 0.0;
  int cursor = 0;
  int chunk_index = 0;

  while (start < m->duration - EPS)
  {
    double cut = choose_cut(start, m->duration,
                            target, min_dur, max_dur,
                            key_times, key_count, &cursor);
    if (cut <= start + EPS)
      cut = fmin(start + max_dur, m->duration);

    r = append_chunk(out, chunk_index++, start, cut);
    if (r != SC_OK)
    {
      free(key_times);
      sc_free_chunk_plan(out);
      return r;
    }
    start = cut;
  }

  free(key_times);

  if (out->count == 0)
  {
    sc_free_chunk_plan(out);
    return SC_ERR_INVAL;
  }

  out->chunks[out->count - 1].end = m->duration;
  merge_tiny_tail(out, min_dur, m->duration);

  double total = 0.0;
  for (int i = 0; i < out->count; i++)
  {
    sc_chunk *c = &out->chunks[i];

    if (i > 0)
      c->start = out->chunks[i - 1].end;

    if (c->end < c->start)
      c->end = c->start;

    total += c->end - c->start;
  }

  double diff = fabs(total - m->duration);
  if (diff > 0.001)
    out->chunks[out->count - 1].end += (m->duration - total);

  renumber_chunks(out);
  return SC_OK;
}

void sc_free_chunk_plan(sc_chunk_plan *plan)
{
  if (!plan)
    return;
  free(plan->chunks);
  memset(plan, 0, sizeof(*plan));
}
