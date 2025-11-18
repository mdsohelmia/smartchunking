#ifndef SMARTCHUNK_H
#define SMARTCHUNK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // ---------------------------------------------
  // Frame metadata (from packet probing)
  // ---------------------------------------------
  typedef struct
  {
    double pts_time;    // best-effort timestamp (sec)
    int is_keyframe;    // safe cut point
    int64_t pkt_size;   // packet size (bytes) - proxy for complexity
    int pict_type;      // frame type: I=1, P=2, B=3, unknown=0
    double complexity;  // computed complexity score (0.0-1.0)
    int is_scene_cut;   // detected scene change
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
    double avg_complexity;  // average complexity of this chunk
    int keyframe_count;     // number of keyframes in chunk
    int scene_cut_count;    // number of scene changes in chunk
    double quality_score;   // overall quality score for this chunk
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

    // Smart chunking options
    int enable_scene_detection;   // use scene changes for cut points
    int enable_complexity_adapt;  // adapt chunk sizes based on complexity
    int enable_gop_analysis;      // prefer closed GOP boundaries
    int enable_balanced_dist;     // optimize distribution across all chunks
    double scene_threshold;       // scene detection sensitivity (0.0-1.0)
    double complexity_weight;     // how much to weight complexity (0.0-1.0)
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
