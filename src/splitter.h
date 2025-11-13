#ifndef SPLITTER_H
#define SPLITTER_H

#include "smartchunk.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    SPLIT_OK = 0,
    SPLIT_ERR_FFMPEG = -10,
    SPLIT_ERR_OPEN = -11,
    SPLIT_ERR_OUTPUT = -12,
    SPLIT_ERR_STREAM = -13,
    SPLIT_ERR_WRITE = -14,
    SPLIT_ERR_SEEK = -15,
    SPLIT_ERR_NOMEM = -16,
    SPLIT_ERR_INVAL = -17
};

typedef struct
{
    int auto_mode;         /* 1 = detect from input filename (default) */
    const char *force_fmt; /* optional muxer short name */
    int output_frag;       /* fragmented MP4 when >0 */
} split_output_mode;

int split_one_chunk(const char *input,
                    const sc_chunk *chunk,
                    const char *output_file,
                    const split_output_mode *mode);

int split_all_chunks(const char *input,
                     const sc_chunk_plan *plan,
                     const char *outdir,
                     const split_output_mode *mode);

#ifdef __cplusplus
}
#endif

#endif /* SPLITTER_H */
