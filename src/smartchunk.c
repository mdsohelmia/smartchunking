#include "smartchunk.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

static const double EPS = 1e-6;

// Scene detection threshold: ratio of size change indicating scene cut
static const double DEFAULT_SCENE_THRESHOLD = 0.35;

// Frame type constants
#define PICT_TYPE_I 1
#define PICT_TYPE_P 2
#define PICT_TYPE_B 3
#define PICT_TYPE_UNKNOWN 0

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
      fm->pkt_size = pkt->size;

      // Decode picture type from packet side data if available
      fm->pict_type = PICT_TYPE_UNKNOWN;
      if (pkt->flags & AV_PKT_FLAG_KEY)
        fm->pict_type = PICT_TYPE_I;

      // Initialize complexity and scene cut flags (computed later)
      fm->complexity = 0.0;
      fm->is_scene_cut = 0;

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
/* Complexity and scene analysis                                      */
/* ------------------------------------------------------------------ */

// Compute normalized complexity scores based on packet sizes
static void compute_complexity(sc_probe_result *m)
{
  if (m->count == 0)
    return;

  // Find min/max packet sizes for normalization
  int64_t min_size = m->frames[0].pkt_size;
  int64_t max_size = m->frames[0].pkt_size;

  for (int i = 1; i < m->count; i++)
  {
    if (m->frames[i].pkt_size < min_size)
      min_size = m->frames[i].pkt_size;
    if (m->frames[i].pkt_size > max_size)
      max_size = m->frames[i].pkt_size;
  }

  double range = (double)(max_size - min_size);
  if (range < 1.0)
    range = 1.0;

  // Normalize complexity scores
  for (int i = 0; i < m->count; i++)
  {
    m->frames[i].complexity = (double)(m->frames[i].pkt_size - min_size) / range;
  }
}

// Detect scene changes based on packet size discontinuities
static void detect_scene_changes(sc_probe_result *m, double threshold)
{
  if (m->count < 2)
    return;

  if (threshold <= 0.0)
    threshold = DEFAULT_SCENE_THRESHOLD;

  // Use a sliding window to detect significant changes in packet sizes
  const int window = 5;

  for (int i = window; i < m->count - window; i++)
  {
    // Only consider keyframes as potential scene cuts
    if (!m->frames[i].is_keyframe)
      continue;

    // Calculate average size before and after this frame
    double avg_before = 0.0;
    double avg_after = 0.0;

    for (int j = i - window; j < i; j++)
      avg_before += m->frames[j].pkt_size;
    avg_before /= window;

    for (int j = i; j < i + window && j < m->count; j++)
      avg_after += m->frames[j].pkt_size;
    avg_after /= window;

    // Check for significant change
    double ratio = 0.0;
    if (avg_before > 0.0)
      ratio = fabs(avg_after - avg_before) / avg_before;

    if (ratio > threshold)
      m->frames[i].is_scene_cut = 1;
  }
}

/* ------------------------------------------------------------------ */
/* Chunk planning helpers                                             */
/* ------------------------------------------------------------------ */
// Collect potential cut points (keyframes or scene cuts)
typedef struct
{
  double time;
  int is_keyframe;
  int is_scene_cut;
  double complexity;
  int quality_score;  // higher is better for cutting here
} cut_point;

static int collect_cut_points(const sc_probe_result *m,
                               cut_point **cuts_out,
                               int *count_out,
                               int use_scene_cuts)
{
  cut_point *cuts = malloc(sizeof(cut_point) * m->count);
  if (!cuts)
    return SC_ERR_NOMEM;

  int count = 0;
  for (int i = 0; i < m->count; i++)
  {
    // Always include keyframes
    if (m->frames[i].is_keyframe)
    {
      cuts[count].time = m->frames[i].pts_time;
      cuts[count].is_keyframe = 1;
      cuts[count].is_scene_cut = m->frames[i].is_scene_cut;
      cuts[count].complexity = m->frames[i].complexity;

      // Score: prefer scene cuts at keyframes
      cuts[count].quality_score = 100;
      if (m->frames[i].is_scene_cut && use_scene_cuts)
        cuts[count].quality_score += 50;

      count++;
    }
  }

  if (count == 0)
  {
    free(cuts);
    *cuts_out = NULL;
    *count_out = 0;
    return SC_OK;
  }

  *cuts_out = cuts;
  *count_out = count;
  return SC_OK;
}

// Legacy function for compatibility
static int collect_keyframes(const sc_probe_result *m,
                             double **times_out,
                             int *count_out)
{
  cut_point *cuts = NULL;
  int cut_count = 0;

  int r = collect_cut_points(m, &cuts, &cut_count, 0);
  if (r != SC_OK)
    return r;

  if (cut_count == 0)
  {
    *times_out = NULL;
    *count_out = 0;
    return SC_OK;
  }

  double *times = malloc(sizeof(double) * cut_count);
  if (!times)
  {
    free(cuts);
    return SC_ERR_NOMEM;
  }

  for (int i = 0; i < cut_count; i++)
    times[i] = cuts[i].time;

  free(cuts);
  *times_out = times;
  *count_out = cut_count;
  return SC_OK;
}

// Calculate chunk statistics from probe data
static void compute_chunk_stats(sc_chunk *chunk,
                                 const sc_probe_result *m,
                                 double start,
                                 double end)
{
  chunk->avg_complexity = 0.0;
  chunk->keyframe_count = 0;
  chunk->scene_cut_count = 0;
  chunk->quality_score = 0.0;

  int frame_count = 0;
  double total_complexity = 0.0;

  for (int i = 0; i < m->count; i++)
  {
    double t = m->frames[i].pts_time;
    if (t >= start - EPS && t < end + EPS)
    {
      frame_count++;
      total_complexity += m->frames[i].complexity;

      if (m->frames[i].is_keyframe)
        chunk->keyframe_count++;
      if (m->frames[i].is_scene_cut)
        chunk->scene_cut_count++;
    }
  }

  if (frame_count > 0)
    chunk->avg_complexity = total_complexity / frame_count;

  // Quality score: prefer chunks with balanced complexity and good GOP structure
  chunk->quality_score = 1.0 - fabs(chunk->avg_complexity - 0.5);
  if (chunk->keyframe_count > 0)
    chunk->quality_score += 0.1;
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
      .end = end,
      .avg_complexity = 0.0,
      .keyframe_count = 0,
      .scene_cut_count = 0,
      .quality_score = 0.0};
  return SC_OK;
}

// Smart cut selection considering quality, scenes, and complexity
static double choose_smart_cut(double start,
                                double duration,
                                double target,
                                double min_dur,
                                double max_dur,
                                const cut_point *cuts,
                                int cut_count,
                                int *cursor,
                                double complexity_weight)
{
  double best_cut = -1.0;
  double best_score = DBL_MAX;
  double fallback = -1.0;

  int idx = *cursor;
  while (idx < cut_count && cuts[idx].time <= start + EPS)
    idx++;

  for (; idx < cut_count; idx++)
  {
    double t = cuts[idx].time;
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

    // Multi-factor scoring:
    // 1. Distance from target duration
    double duration_score = fabs(span - target) / target;

    // 2. Scene cut bonus (lower is better)
    double scene_bonus = cuts[idx].is_scene_cut ? -0.3 : 0.0;

    // 3. Quality score bonus
    double quality_bonus = -((double)cuts[idx].quality_score / 200.0);

    // Combined score with complexity weighting
    double score = duration_score * (1.0 - complexity_weight) +
                   scene_bonus + quality_bonus;

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

  while (*cursor < cut_count && cuts[*cursor].time <= best_cut + EPS)
    (*cursor)++;

  return best_cut;
}

// Legacy function for backward compatibility
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
int sc_plan_chunks(const sc_probe_result *meta,
                   sc_plan_config cfg,
                   sc_chunk_plan *out)
{
  if (!meta || !out || meta->count == 0 || meta->duration <= 0.0)
    return SC_ERR_INVAL;

  memset(out, 0, sizeof(*out));

  // Make a mutable copy of probe data for analysis
  sc_probe_result m = *meta;

  // Run smart analysis if enabled
  if (cfg.enable_complexity_adapt || cfg.enable_scene_detection)
  {
    compute_complexity(&m);
  }

  if (cfg.enable_scene_detection)
  {
    detect_scene_changes(&m, cfg.scene_threshold);
  }

  double target = cfg.target_dur;
  if (cfg.ideal_parallel > 0)
    target = m.duration / cfg.ideal_parallel;
  if (target <= 0.0)
    target = 10.0;

  double min_dur = cfg.min_dur > 0.0 ? cfg.min_dur : target * 0.5;
  double max_dur = cfg.max_dur > 0.0 ? cfg.max_dur : target * 2.0;
  if (max_dur < min_dur)
    max_dur = min_dur;

  // Use smart cut points if enabled, otherwise fall back to simple keyframes
  int use_smart = cfg.enable_scene_detection || cfg.enable_complexity_adapt;

  if (use_smart)
  {
    // Smart chunking path
    cut_point *cuts = NULL;
    int cut_count = 0;
    int r = collect_cut_points(&m, &cuts, &cut_count, cfg.enable_scene_detection);
    if (r != SC_OK)
      return r;

    if (cut_count == 0)
    {
      free(cuts);
      r = append_chunk(out, 0, 0.0, m.duration);
      if (r == SC_OK)
        compute_chunk_stats(&out->chunks[0], &m, 0.0, m.duration);
      return r;
    }

    double start = 0.0;
    int cursor = 0;
    int chunk_index = 0;
    double complexity_weight = cfg.complexity_weight > 0.0 ? cfg.complexity_weight : 0.3;

    while (start < m.duration - EPS)
    {
      double cut = choose_smart_cut(start, m.duration,
                                     target, min_dur, max_dur,
                                     cuts, cut_count, &cursor,
                                     complexity_weight);
      if (cut <= start + EPS)
        cut = fmin(start + max_dur, m.duration);

      r = append_chunk(out, chunk_index++, start, cut);
      if (r != SC_OK)
      {
        free(cuts);
        sc_free_chunk_plan(out);
        return r;
      }

      // Compute statistics for this chunk
      compute_chunk_stats(&out->chunks[out->count - 1], &m, start, cut);

      start = cut;
    }

    free(cuts);
  }
  else
  {
    // Legacy simple chunking path
    double *key_times = NULL;
    int key_count = 0;
    int r = collect_keyframes(&m, &key_times, &key_count);
    if (r != SC_OK)
      return r;

    if (key_count == 0)
    {
      free(key_times);
      return append_chunk(out, 0, 0.0, m.duration);
    }

    double start = 0.0;
    int cursor = 0;
    int chunk_index = 0;

    while (start < m.duration - EPS)
    {
      double cut = choose_cut(start, m.duration,
                              target, min_dur, max_dur,
                              key_times, key_count, &cursor);
      if (cut <= start + EPS)
        cut = fmin(start + max_dur, m.duration);

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
  }

  if (out->count == 0)
  {
    sc_free_chunk_plan(out);
    return SC_ERR_INVAL;
  }

  out->chunks[out->count - 1].end = m.duration;

  if (cfg.avoid_tiny_last)
    merge_tiny_tail(out, min_dur, m.duration);

  // Normalize chunk boundaries
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

  double diff = fabs(total - m.duration);
  if (diff > 0.001)
    out->chunks[out->count - 1].end += (m.duration - total);

  renumber_chunks(out);

  // Recompute stats after normalization if using smart chunking
  if (use_smart)
  {
    for (int i = 0; i < out->count; i++)
    {
      compute_chunk_stats(&out->chunks[i], &m,
                          out->chunks[i].start,
                          out->chunks[i].end);
    }
  }

  return SC_OK;
}

void sc_free_chunk_plan(sc_chunk_plan *plan)
{
  if (!plan)
    return;
  free(plan->chunks);
  memset(plan, 0, sizeof(*plan));
}
