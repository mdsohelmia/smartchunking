#ifndef SMARTCHUNK_H
#define SMARTCHUNK_H

#ifdef __cplusplus
extern "C"
{
#endif

  // -------------------------
  // Types
  // -------------------------

  typedef struct
  {
    double pts_time; // seconds
    int is_keyframe; // 1 = safe cut point
  } sc_frame_meta;

  typedef struct
  {
    sc_frame_meta *frames;
    int count;
    int capacity;    // internal use, you can ignore
    double duration; // seconds
  } sc_probe_result;

  typedef struct
  {
    int index;
    double start;
    double end;
  } sc_chunk;

  typedef struct
  {
    sc_chunk *chunks;
    int count;
    int capacity; // internal use, you can ignore
  } sc_chunk_plan;

  typedef struct
  {
    double target_dur;   // e.g. 8.0
    double min_dur;      // e.g. 4.0
    double max_dur;      // e.g. 15.0
    int avoid_tiny_last; // 1 = merge tiny last chunk with previous

    int min_chunks; // 0 = no minimum
    int max_chunks; // 0 = no maximum

    int ideal_parallel; // if > 0, target_dur ≈ duration / ideal_parallel
  } sc_plan_config;

  // -------------------------
  // Return codes
  // -------------------------

#define SC_OK 0
#define SC_ERR_FFMPEG -1
#define SC_ERR_NOSTREAM -2
#define SC_ERR_NOMEM -3
#define SC_ERR_INVAL -4

  // -------------------------
  // API
  // -------------------------

  // Fast probe using packets only (no decoding).
  // Fills pts_time + is_keyframe, and best-effort duration.
  int sc_probe_video(const char *filename, sc_probe_result *out);
  void sc_free_probe(sc_probe_result *res);

  // Plan smart chunks over keyframes given config.
  // Uses duration, min/max durations, optional min/max chunk counts.
  int sc_plan_chunks(const sc_probe_result *meta,
                     sc_plan_config cfg,
                     sc_chunk_plan *out);
  void sc_free_chunk_plan(sc_chunk_plan *plan);

#ifdef __cplusplus
}
#endif

#endif // SMARTCHUNK_H
