#ifndef SMARTCHUNK_H
#define SMARTCHUNK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // ---------------------------------------------
  // Frame metadata (from packet probing)
  // ---------------------------------------------
  typedef struct
  {
    double pts_time; // best-effort timestamp (sec)
    int is_keyframe; // safe cut point
  } sc_frame_meta;

  // ---------------------------------------------
  // Probe result
  // ---------------------------------------------
  typedef struct
  {
    sc_frame_meta *frames;
    int count;
    int capacity;
    double duration; // seconds
  } sc_probe_result;

  // ---------------------------------------------
  // Chunk definition
  // ---------------------------------------------
  typedef struct
  {
    int index;
    double start;
    double end;
  } sc_chunk;

  // ---------------------------------------------
  // Chunk plan
  // ---------------------------------------------
  typedef struct
  {
    sc_chunk *chunks;
    int count;
    int capacity;
  } sc_chunk_plan;

  // ---------------------------------------------
  // Chunk planning options
  // ---------------------------------------------
  typedef struct
  {
    double target_dur;
    double min_dur;
    double max_dur;
    int avoid_tiny_last;

    int min_chunks;
    int max_chunks;

    int ideal_parallel; // override target_dur
  } sc_plan_config;

// ---------------------------------------------
// Return codes
// ---------------------------------------------
#define SC_OK 0
#define SC_ERR_FFMPEG -1
#define SC_ERR_NOSTREAM -2
#define SC_ERR_NOMEM -3
#define SC_ERR_INVAL -4

  // ---------------------------------------------
  // API
  // ---------------------------------------------

  // Fast probe using packet metadata (no decoding)
  int sc_probe_video(const char *filename, sc_probe_result *out);
  void sc_free_probe(sc_probe_result *res);

  // Smart chunk planning
  int sc_plan_chunks(const sc_probe_result *meta,
                     sc_plan_config cfg,
                     sc_chunk_plan *out);

  void sc_free_chunk_plan(sc_chunk_plan *plan);

#ifdef __cplusplus
}
#endif

#endif // SMARTCHUNK_H
