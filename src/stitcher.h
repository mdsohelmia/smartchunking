#ifndef STITCHER_H
#define STITCHER_H

#include "smartchunk.h"

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    STITCH_OK = 0,
    STITCH_ERR_INPUT = -30,
    STITCH_ERR_FFMPEG = -31,
    STITCH_ERR_OPEN = -32,
    STITCH_ERR_OUTPUT = -33,
    STITCH_ERR_STREAM = -34,
    STITCH_ERR_WRITE = -35,
    STITCH_ERR_NOMEM = -36,
    STITCH_ERR_LAYOUT = -37
};

  // -----------------------------------------------------------
  // Output format selector
  // -----------------------------------------------------------
typedef struct
{
    int auto_mode;
    const char *force_fmt;
    int output_frag;
    int enable_faststart;
} stitch_output_mode;

  // ---------------------------------------------------------
  // Concatenate sequential chunks
  // ---------------------------------------------------------
int stitch_chunks(const char *output_path,
                  const sc_chunk_plan *plan,
                  const char *chunk_dir,
                  const stitch_output_mode *mode);

#ifdef __cplusplus
}
#endif

#endif // STITCHER_H
