# C Programming for Video Encoding Engineers

A comprehensive guide to mastering C programming specifically for video encoding and codec development.

---

## Table of Contents

1. [Why C for Video Encoding?](#why-c-for-video-encoding)
2. [Phase 1: C Fundamentals](#phase-1-c-fundamentals)
3. [Phase 2: Memory Management](#phase-2-memory-management)
4. [Phase 3: Advanced C Concepts](#phase-3-advanced-c-concepts)
5. [Phase 4: Performance & Optimization](#phase-4-performance--optimization)
6. [Phase 5: Video Encoding Specific](#phase-5-video-encoding-specific)
7. [Practice Projects](#practice-projects)
8. [Resources](#resources)

---

## Why C for Video Encoding?

- **Performance**: Video encoding is CPU-intensive; C provides bare-metal performance
- **Industry Standard**: x264, x265, libaom, libvpx all written in C/C++
- **Hardware Control**: Direct memory management, SIMD instructions
- **Portability**: Runs on all platforms (desktop, mobile, embedded)

---

## Phase 1: C Fundamentals

### 1.1 Basic Syntax & Data Types

**Topics to Master:**
- Primitive types: `int`, `char`, `float`, `double`
- Type modifiers: `signed`, `unsigned`, `short`, `long`
- Constants and literals
- Type casting and conversion
- `sizeof` operator

**Why Important for Video Encoding:**
- Pixel data uses specific types (uint8_t for 8-bit, uint16_t for 10-bit)
- Understanding size matters for buffer allocation
- Type casting between YUV and RGB data

**Practice:**
```c
// Example: Understanding pixel data types
#include <stdint.h>

typedef struct {
    uint8_t y;  // Luma (0-255)
    uint8_t u;  // Chroma U
    uint8_t v;  // Chroma V
} yuv_pixel_t;

// 10-bit video uses uint16_t
typedef struct {
    uint16_t y;  // Luma (0-1023)
    uint16_t u;
    uint16_t v;
} yuv_pixel_10bit_t;
```

### 1.2 Control Flow

**Topics:**
- if/else statements
- switch/case
- for/while/do-while loops
- break/continue
- goto (used sparingly in codecs for error handling)

**Video Encoding Context:**
- Loop through pixels, macroblocks, frames
- Decision trees for mode selection
- Early termination in motion search

**Practice:**
```c
// Example: Iterating through video frame pixels
void process_frame(uint8_t *frame, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = y * width + x;
            uint8_t pixel = frame[index];
            // Process pixel
        }
    }
}
```

### 1.3 Functions

**Topics:**
- Function declaration and definition
- Parameters: pass by value vs pass by reference
- Return values
- Function prototypes
- Scope: local vs global variables
- Static functions
- Inline functions

**Video Encoding Context:**
- Encoder functions are modular (prediction, transform, quantization)
- Performance-critical functions are inlined
- Static functions for internal module functions

**Practice:**
```c
// Example: SAD (Sum of Absolute Differences) function
static inline int calculate_sad(uint8_t *block1, uint8_t *block2, int size) {
    int sad = 0;
    for (int i = 0; i < size * size; i++) {
        sad += abs(block1[i] - block2[i]);
    }
    return sad;
}
```

---

## Phase 2: Memory Management

### 2.1 Pointers (CRITICAL)

**Topics:**
- Pointer basics: declaration, dereferencing
- Pointer arithmetic
- Pointer to pointer (double pointers)
- Null pointers
- Void pointers
- Function pointers
- Const pointers vs pointer to const

**Why Critical:**
- Video frames are large memory buffers accessed via pointers
- YUV planes stored as separate pointers
- Efficient memory traversal requires pointer arithmetic

**Practice:**
```c
// Example: Accessing YUV planes
typedef struct {
    uint8_t *y_plane;  // Luma plane pointer
    uint8_t *u_plane;  // Chroma U plane pointer
    uint8_t *v_plane;  // Chroma V plane pointer
    int width;
    int height;
    int stride;        // Row stride (may differ from width)
} yuv_frame_t;

// Accessing pixel at (x, y)
uint8_t get_pixel(yuv_frame_t *frame, int x, int y) {
    return *(frame->y_plane + y * frame->stride + x);
}

// Alternative: array notation
uint8_t get_pixel_alt(yuv_frame_t *frame, int x, int y) {
    return frame->y_plane[y * frame->stride + x];
}
```

### 2.2 Arrays

**Topics:**
- Array declaration and initialization
- Multidimensional arrays
- Array and pointer relationship
- Array decay to pointer
- Variable-length arrays (VLA)

**Video Encoding Context:**
- Storing blocks, macroblocks (16x16, 8x8 arrays)
- Coefficient matrices after DCT transform
- Motion vector arrays

**Practice:**
```c
// Example: 8x8 DCT coefficient block
typedef int16_t dct_block_t[8][8];

void quantize_block(dct_block_t coeffs, int qp) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            coeffs[y][x] = coeffs[y][x] / qp;
        }
    }
}
```

### 2.3 Dynamic Memory Allocation

**Topics:**
- `malloc()`, `calloc()`, `realloc()`, `free()`
- Memory leaks detection
- Memory alignment (`aligned_alloc()`, `posix_memalign()`)
- Memory pools
- Reference counting

**Why Important:**
- Video frames require large allocations
- Alignment needed for SIMD operations (16-byte, 32-byte)
- Reference counting for frame buffers (like AVFrame)

**Practice:**
```c
#include <stdlib.h>
#include <string.h>

// Example: Allocating aligned memory for video frame
yuv_frame_t* create_frame(int width, int height) {
    yuv_frame_t *frame = malloc(sizeof(yuv_frame_t));
    if (!frame) return NULL;

    frame->width = width;
    frame->height = height;
    frame->stride = (width + 15) & ~15;  // Align to 16 bytes

    // Allocate aligned memory for Y plane
    frame->y_plane = aligned_alloc(32, frame->stride * height);

    // U and V planes (4:2:0 subsampling)
    int chroma_height = height / 2;
    int chroma_stride = frame->stride / 2;
    frame->u_plane = aligned_alloc(32, chroma_stride * chroma_height);
    frame->v_plane = aligned_alloc(32, chroma_stride * chroma_height);

    return frame;
}

void destroy_frame(yuv_frame_t *frame) {
    if (frame) {
        free(frame->y_plane);
        free(frame->u_plane);
        free(frame->v_plane);
        free(frame);
    }
}
```

### 2.4 Strings

**Topics:**
- Character arrays vs string literals
- String functions: `strlen()`, `strcpy()`, `strncpy()`, `strcmp()`
- Buffer overflows and safety
- String parsing

**Video Encoding Context:**
- Parsing encoder parameters
- Reading codec options
- File path handling

---

## Phase 3: Advanced C Concepts

### 3.1 Structures and Unions

**Topics:**
- Structure declaration and initialization
- Nested structures
- Structure padding and alignment
- `typedef` for structures
- Unions (memory overlay)
- Bit fields
- Anonymous structures/unions

**Why Critical:**
- Codecs heavily use structures (frame, codec context, encoder options)
- Understanding padding is crucial for performance
- Unions used for type punning

**Practice:**
```c
// Example: Video encoder context structure
typedef struct {
    // Input parameters
    int width;
    int height;
    int fps;
    int bitrate;

    // Encoder state
    int frame_count;
    int qp;  // Quantization parameter

    // Buffers
    yuv_frame_t *current_frame;
    yuv_frame_t *reference_frame;

    // Statistics
    struct {
        uint64_t total_bits;
        double avg_psnr;
        int keyframe_count;
    } stats;

} encoder_context_t;

// Union example: Type punning for pixel formats
typedef union {
    uint32_t rgba;
    struct {
        uint8_t r, g, b, a;
    };
} pixel_t;
```

### 3.2 Preprocessor

**Topics:**
- `#include` directives
- `#define` macros
- Macro functions
- Conditional compilation: `#ifdef`, `#ifndef`, `#if`
- `#pragma` directives
- Predefined macros: `__FILE__`, `__LINE__`, `__func__`

**Video Encoding Context:**
- Architecture-specific code (x86, ARM)
- Debug vs release builds
- SIMD instruction availability
- Inline optimization macros

**Practice:**
```c
// Example: Architecture-specific optimization
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLIP3(min, max, val) MIN(MAX(min, val), max)

// Conditional compilation for SIMD
#ifdef HAVE_SSE2
    #include <emmintrin.h>
    void process_block_sse2(uint8_t *src, uint8_t *dst);
#endif

#ifdef HAVE_NEON
    #include <arm_neon.h>
    void process_block_neon(uint8_t *src, uint8_t *dst);
#endif

// Debug logging
#ifdef DEBUG
    #define LOG_DEBUG(fmt, ...) \
        fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) ((void)0)
#endif
```

### 3.3 File I/O

**Topics:**
- File pointers: `FILE*`
- Opening/closing: `fopen()`, `fclose()`
- Reading: `fread()`, `fgets()`, `fscanf()`
- Writing: `fwrite()`, `fprintf()`
- Binary vs text mode
- File seeking: `fseek()`, `ftell()`, `rewind()`
- Error handling: `ferror()`, `feof()`

**Video Encoding Context:**
- Reading raw YUV files
- Writing encoded bitstreams
- Container format I/O

**Practice:**
```c
// Example: Reading YUV 4:2:0 file
int read_yuv_frame(FILE *fp, yuv_frame_t *frame) {
    // Read Y plane
    size_t y_size = frame->width * frame->height;
    if (fread(frame->y_plane, 1, y_size, fp) != y_size) {
        return -1;
    }

    // Read U plane (4:2:0 subsampled)
    size_t uv_size = (frame->width / 2) * (frame->height / 2);
    if (fread(frame->u_plane, 1, uv_size, fp) != uv_size) {
        return -1;
    }

    // Read V plane
    if (fread(frame->v_plane, 1, uv_size, fp) != uv_size) {
        return -1;
    }

    return 0;
}
```

### 3.4 Enumerations

**Topics:**
- Enum declaration
- Enum with explicit values
- Enum vs #define

**Practice:**
```c
// Example: Encoder preset levels
typedef enum {
    PRESET_ULTRAFAST = 0,
    PRESET_SUPERFAST,
    PRESET_VERYFAST,
    PRESET_FASTER,
    PRESET_FAST,
    PRESET_MEDIUM,
    PRESET_SLOW,
    PRESET_SLOWER,
    PRESET_VERYSLOW,
    PRESET_PLACEBO
} encoder_preset_t;

// Frame types
typedef enum {
    FRAME_TYPE_I = 0,  // Intra (keyframe)
    FRAME_TYPE_P,      // Predicted
    FRAME_TYPE_B       // Bi-directional
} frame_type_t;
```

---

## Phase 4: Performance & Optimization

### 4.1 Bit Manipulation

**Topics:**
- Bitwise operators: `&`, `|`, `^`, `~`, `<<`, `>>`
- Bit masking
- Setting/clearing/toggling bits
- Counting set bits
- Bit packing

**Why Critical:**
- Entropy coding (bitstream writing)
- Efficient flag storage
- Chroma subsampling operations
- Fast division/multiplication by powers of 2

**Practice:**
```c
// Example: Bitstream writer
typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t bit_pos;
    uint32_t cache;
    int cache_bits;
} bitstream_t;

void write_bits(bitstream_t *bs, uint32_t value, int num_bits) {
    bs->cache = (bs->cache << num_bits) | (value & ((1 << num_bits) - 1));
    bs->cache_bits += num_bits;

    while (bs->cache_bits >= 8) {
        bs->cache_bits -= 8;
        bs->buffer[bs->bit_pos / 8] = (bs->cache >> bs->cache_bits) & 0xFF;
        bs->bit_pos += 8;
    }
}

// Fast operations using bit manipulation
#define IS_POWER_OF_2(x) (((x) & ((x) - 1)) == 0)
#define ROUND_UP_POW2(x, align) (((x) + (align) - 1) & ~((align) - 1))
```

### 4.2 SIMD Programming

**Topics:**
- Understanding vectorization
- SSE/AVX intrinsics (x86)
- NEON intrinsics (ARM)
- Data alignment requirements
- Loading/storing vectors
- Arithmetic operations on vectors
- Shuffling and permutation

**Why Critical:**
- 4-16x speedup for pixel operations
- Essential for real-time encoding
- All production codecs use SIMD

**Practice:**
```c
#include <emmintrin.h>  // SSE2

// Example: SSE2 SAD (Sum of Absolute Differences)
int sad_16x16_sse2(uint8_t *src, int src_stride,
                   uint8_t *ref, int ref_stride) {
    __m128i sum = _mm_setzero_si128();

    for (int y = 0; y < 16; y++) {
        // Load 16 bytes from source and reference
        __m128i s = _mm_loadu_si128((__m128i*)(src + y * src_stride));
        __m128i r = _mm_loadu_si128((__m128i*)(ref + y * ref_stride));

        // Calculate absolute differences
        __m128i sad = _mm_sad_epu8(s, r);

        // Accumulate
        sum = _mm_add_epi32(sum, sad);
    }

    // Horizontal sum
    sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E));
    return _mm_cvtsi128_si32(sum);
}
```

### 4.3 Multi-threading

**Topics:**
- pthread library
- Thread creation and joining
- Mutexes and locks
- Condition variables
- Thread-local storage
- Atomic operations
- Lock-free data structures

**Video Encoding Context:**
- Frame-level parallelism
- Slice-level parallelism
- Wavefront parallel processing (WPP)
- Lookahead and analysis threads

**Practice:**
```c
#include <pthread.h>

typedef struct {
    encoder_context_t *encoder;
    yuv_frame_t *frame;
    int thread_id;
    int num_threads;
} thread_data_t;

void* encode_slice_thread(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;

    int slice_height = data->frame->height / data->num_threads;
    int start_y = data->thread_id * slice_height;
    int end_y = (data->thread_id == data->num_threads - 1) ?
                data->frame->height : start_y + slice_height;

    // Encode slice from start_y to end_y
    encode_slice(data->encoder, data->frame, start_y, end_y);

    return NULL;
}

void encode_frame_parallel(encoder_context_t *encoder, yuv_frame_t *frame) {
    int num_threads = 4;
    pthread_t threads[num_threads];
    thread_data_t thread_data[num_threads];

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].encoder = encoder;
        thread_data[i].frame = frame;
        thread_data[i].thread_id = i;
        thread_data[i].num_threads = num_threads;

        pthread_create(&threads[i], NULL, encode_slice_thread, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
}
```

### 4.4 Cache Optimization

**Topics:**
- CPU cache hierarchy (L1, L2, L3)
- Cache lines (typically 64 bytes)
- Spatial locality
- Temporal locality
- Cache-friendly data structures
- Structure padding and alignment
- False sharing in multi-threading

**Practice:**
```c
// Example: Cache-friendly block processing
// Process 8x8 blocks instead of row-by-row for better cache utilization
void process_frame_cache_friendly(uint8_t *frame, int width, int height) {
    const int BLOCK_SIZE = 8;

    for (int by = 0; by < height; by += BLOCK_SIZE) {
        for (int bx = 0; bx < width; bx += BLOCK_SIZE) {
            // Process 8x8 block - all data stays in cache
            for (int y = by; y < by + BLOCK_SIZE && y < height; y++) {
                for (int x = bx; x < bx + BLOCK_SIZE && x < width; x++) {
                    int index = y * width + x;
                    // Process pixel
                }
            }
        }
    }
}

// Align structures to cache line size
typedef struct __attribute__((aligned(64))) {
    int data[16];  // 64 bytes total
} cache_aligned_t;
```

### 4.5 Profiling

**Topics:**
- Using `gprof`
- Using `perf` (Linux)
- Valgrind (callgrind, cachegrind)
- Time measurement: `clock()`, `gettimeofday()`
- Compiler optimization flags: `-O2`, `-O3`, `-march=native`
- Profile-guided optimization (PGO)

**Practice:**
```c
#include <time.h>

// Example: Timing function execution
double benchmark_function(void (*func)(void), int iterations) {
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        func();
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    return elapsed / iterations;
}
```

---

## Phase 5: Video Encoding Specific

### 5.1 Fixed-Point Arithmetic

**Topics:**
- Integer representation of fractions
- Q format (Q15.16, Q0.31, etc.)
- Multiplication and division
- Avoiding floating-point

**Why Important:**
- Faster than floating-point on some platforms
- Deterministic behavior across platforms
- Used in DCT/IDCT implementations

**Practice:**
```c
// Example: Q16.16 fixed-point (16 bits integer, 16 bits fraction)
typedef int32_t fixed_t;

#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)

fixed_t float_to_fixed(float f) {
    return (fixed_t)(f * FIXED_ONE);
}

float fixed_to_float(fixed_t f) {
    return (float)f / FIXED_ONE;
}

fixed_t fixed_mul(fixed_t a, fixed_t b) {
    return (fixed_t)(((int64_t)a * b) >> FIXED_SHIFT);
}

fixed_t fixed_div(fixed_t a, fixed_t b) {
    return (fixed_t)(((int64_t)a << FIXED_SHIFT) / b);
}
```

### 5.2 Transform Coding (DCT)

**Topics:**
- 2D DCT/IDCT implementation
- Separable transforms
- Integer approximations
- Fast DCT algorithms

**Practice:**
```c
// Example: Simple 8x8 DCT implementation
void dct_8x8(int16_t input[8][8], int16_t output[8][8]) {
    const double PI = 3.14159265358979323846;

    for (int u = 0; u < 8; u++) {
        for (int v = 0; v < 8; v++) {
            double sum = 0.0;

            for (int x = 0; x < 8; x++) {
                for (int y = 0; y < 8; y++) {
                    double cu = (u == 0) ? 1.0/sqrt(2.0) : 1.0;
                    double cv = (v == 0) ? 1.0/sqrt(2.0) : 1.0;

                    sum += cu * cv * input[x][y] *
                           cos((2*x + 1) * u * PI / 16.0) *
                           cos((2*y + 1) * v * PI / 16.0);
                }
            }

            output[u][v] = (int16_t)(sum / 4.0);
        }
    }
}
```

### 5.3 Motion Estimation

**Topics:**
- Block matching algorithms
- Diamond search, hexagon search
- Fractional pixel search
- Sub-pixel interpolation
- SAD, SATD cost functions

**Practice:**
```c
// Example: Diamond search pattern
typedef struct {
    int x, y;
} motion_vector_t;

motion_vector_t diamond_search(uint8_t *current, uint8_t *reference,
                               int width, int height,
                               int block_x, int block_y, int block_size) {
    const int diamond[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};

    motion_vector_t best_mv = {0, 0};
    int best_cost = INT_MAX;
    int search_range = 16;

    // Initial center point
    int center_x = 0, center_y = 0;

    for (int step = search_range; step >= 1; step /= 2) {
        int improved = 1;

        while (improved) {
            improved = 0;

            for (int i = 0; i < 4; i++) {
                int mx = center_x + diamond[i][0] * step;
                int my = center_y + diamond[i][1] * step;

                // Calculate cost (SAD)
                int cost = calculate_sad_mv(current, reference, width, height,
                                           block_x, block_y, mx, my, block_size);

                if (cost < best_cost) {
                    best_cost = cost;
                    best_mv.x = mx;
                    best_mv.y = my;
                    center_x = mx;
                    center_y = my;
                    improved = 1;
                }
            }
        }
    }

    return best_mv;
}
```

### 5.4 Rate Control

**Topics:**
- Rate-distortion optimization
- QP (Quantization Parameter) selection
- VBV (Video Buffering Verifier)
- CBR, VBR, CRF modes
- Bit allocation per frame

**Practice:**
```c
// Example: Simple rate control
typedef struct {
    int target_bitrate;
    int frame_rate;
    double buffer_size;
    double buffer_fullness;
    int last_qp;
} rate_controller_t;

int select_qp(rate_controller_t *rc, int frame_type, int frame_bits) {
    // Target bits per frame
    double target_bits = (double)rc->target_bitrate / rc->frame_rate;

    // Update buffer
    rc->buffer_fullness += target_bits - frame_bits;

    // Clamp buffer
    if (rc->buffer_fullness > rc->buffer_size) {
        rc->buffer_fullness = rc->buffer_size;
    }
    if (rc->buffer_fullness < 0) {
        rc->buffer_fullness = 0;
    }

    // Adjust QP based on buffer fullness
    int qp = rc->last_qp;

    if (rc->buffer_fullness < rc->buffer_size * 0.3) {
        qp += 2;  // Increase QP (lower quality) to save bits
    } else if (rc->buffer_fullness > rc->buffer_size * 0.7) {
        qp -= 2;  // Decrease QP (higher quality)
    }

    // Clamp QP
    if (qp < 10) qp = 10;
    if (qp > 51) qp = 51;

    // I-frames get lower QP
    if (frame_type == FRAME_TYPE_I) {
        qp -= 3;
    }

    rc->last_qp = qp;
    return qp;
}
```

### 5.5 Entropy Coding

**Topics:**
- Huffman coding
- Arithmetic coding
- CABAC (Context-Adaptive Binary Arithmetic Coding)
- Exponential-Golomb codes
- Variable length coding

**Practice:**
```c
// Example: Exponential-Golomb coding (used in H.264)
void write_ue(bitstream_t *bs, uint32_t value) {
    // Exponential-Golomb code of order 0
    value++;  // Map 0 to 1, 1 to 2, etc.

    int num_bits = 0;
    uint32_t temp = value;

    // Count bits needed
    while (temp > 0) {
        temp >>= 1;
        num_bits++;
    }

    // Write leading zeros
    for (int i = 0; i < num_bits - 1; i++) {
        write_bits(bs, 0, 1);
    }

    // Write the value
    write_bits(bs, value, num_bits);
}

uint32_t read_ue(bitstream_t *bs) {
    int leading_zeros = 0;

    // Count leading zeros
    while (read_bits(bs, 1) == 0) {
        leading_zeros++;
    }

    // Read remaining bits
    uint32_t value = (1 << leading_zeros) - 1;
    if (leading_zeros > 0) {
        value += read_bits(bs, leading_zeros);
    }

    return value;
}
```

---

## Practice Projects

### Beginner Level

#### 1. YUV File Reader/Writer
**Goal**: Read and write raw YUV 4:2:0 files

**Skills:**
- File I/O
- Pointer manipulation
- Memory allocation
- Structure usage

**Tasks:**
- Read YUV file into memory
- Extract Y, U, V planes
- Write back to file
- Support different resolutions

#### 2. Image Format Converter
**Goal**: Convert between RGB and YUV

**Skills:**
- Color space conversion math
- Loops and arrays
- Type casting

**Tasks:**
- RGB to YUV conversion
- YUV to RGB conversion
- Handle different bit depths (8-bit, 10-bit)

#### 3. PSNR Calculator
**Goal**: Calculate Peak Signal-to-Noise Ratio

**Skills:**
- Math operations
- Frame comparison
- Statistical calculations

**Tasks:**
- Compare two YUV files
- Calculate MSE (Mean Squared Error)
- Calculate PSNR in dB
- Output per-frame and average PSNR

### Intermediate Level

#### 4. 8x8 DCT/IDCT Implementation
**Goal**: Implement 2D DCT transform

**Skills:**
- Matrix operations
- Mathematical algorithms
- Fixed-point arithmetic

**Tasks:**
- Forward DCT
- Inverse DCT
- Optimize with integer math
- Verify correctness

#### 5. Motion Estimation Engine
**Goal**: Implement block matching

**Skills:**
- Search algorithms
- Cost functions (SAD, SATD)
- Optimization

**Tasks:**
- Full search
- Diamond search
- Calculate motion vectors
- Benchmark performance

#### 6. Simple RLE Video Codec
**Goal**: Build a run-length encoding video codec

**Skills:**
- Compression algorithms
- Bitstream writing
- Codec structure

**Tasks:**
- Encode frame with RLE
- Decode bitstream
- Add frame types (I, P frames)
- Measure compression ratio

### Advanced Level

#### 7. H.264 I-Frame Decoder
**Goal**: Decode H.264 I-frames only

**Skills:**
- Bitstream parsing
- Entropy decoding
- Intra prediction
- DCT/quantization

**Tasks:**
- Parse NAL units
- Decode SPS/PPS
- Implement intra prediction modes
- IDCT and reconstruction

#### 8. SIMD-Optimized SAD Function
**Goal**: Implement SAD with SSE2/AVX2

**Skills:**
- SIMD intrinsics
- Performance optimization
- Benchmarking

**Tasks:**
- Scalar baseline implementation
- SSE2 version
- AVX2 version
- Compare performance

#### 9. Multi-threaded Video Encoder
**Goal**: Parallelize encoding pipeline

**Skills:**
- Multi-threading
- Synchronization
- Lock-free structures

**Tasks:**
- Frame-level parallelism
- Slice-level parallelism
- Thread pool
- Measure speedup

---

## Resources

### Books

#### Essential:
1. **"The C Programming Language" (K&R)** - Brian Kernighan, Dennis Ritchie
   - The definitive C reference

2. **"C Programming: A Modern Approach"** - K.N. King
   - Comprehensive modern C textbook

3. **"The H.264 Advanced Video Compression Standard"** - Iain Richardson
   - Best book for understanding H.264 and video compression

4. **"Digital Video and HD: Algorithms and Interfaces"** - Charles Poynton
   - Video engineering fundamentals

#### Advanced:
5. **"Computer Systems: A Programmer's Perspective"** - Bryant & O'Hallaron
   - Understanding systems programming

6. **"Optimizing Software in C++"** - Agner Fog (free PDF)
   - Low-level optimization techniques

### Online Courses

1. **CS50 (Harvard)** - Introduction to Computer Science (C programming)
2. **Coursera: "Digital Signal Processing" (EPFL)**
3. **YouTube: Computerphile** - Video encoding concepts

### Documentation

1. **ITU-T H.264 Specification** - Official standard (free)
2. **ITU-T H.265 Specification** - HEVC standard
3. **AV1 Bitstream Specification** - AOMedia spec
4. **FFmpeg Documentation** - libavcodec, libavformat

### Open Source Projects to Study

#### Priority Order:
1. **x264** - https://code.videolan.org/videolan/x264
   - Best-written codec, start here
   - Read: encoder/encoder.c, common/macroblock.c

2. **x265** - https://bitbucket.org/multicoreware/x265_git
   - More complex, HEVC implementation

3. **libaom (AV1)** - https://aomedia.googlesource.com/aom/
   - Modern codec

4. **FFmpeg** - https://github.com/FFmpeg/FFmpeg
   - Complete multimedia framework
   - Study libavcodec for codec implementations

5. **libvpx (VP9)** - https://chromium.googlesource.com/webm/libvpx/
   - Google's VP9 codec

### Tools

1. **Compiler**: GCC or Clang
2. **Debugger**: GDB, LLDB
3. **Profiler**: gprof, Valgrind, perf
4. **Build**: Make, CMake
5. **Video Tools**: FFmpeg, YUView, MediaInfo
6. **Hex Editor**: For analyzing bitstreams

### Online Communities

1. **Doom9 Forums** - Video encoding discussions
2. **VideoHelp Forums** - Practical encoding help
3. **Stack Overflow** - Programming questions
4. **FFmpeg mailing lists** - Development questions

---

## Learning Path Timeline

### Month 1-2: C Fundamentals
- Complete K&R or K.N. King book
- Practice basic syntax, pointers, memory
- Write simple CLI tools
- Practice Projects 1-3

### Month 3-4: Advanced C
- Structures, unions, file I/O
- Bit manipulation
- Preprocessor mastery
- Practice Projects 4-5

### Month 5-6: Performance
- SIMD programming (SSE2 basics)
- Multi-threading with pthreads
- Profiling and optimization
- Practice Project 6

### Month 7-9: Video Encoding Concepts
- Read H.264 book (Iain Richardson)
- Study x264 source code
- Understand DCT, quantization, motion estimation
- Practice Projects 7-8

### Month 10-12: Production Codecs
- Deep dive into x264
- Contribute to open source
- Build complete encoder
- Practice Project 9

---

## Daily Practice Routine

### Week 1-4: Fundamentals
- **Morning (1 hour)**: Read textbook chapter
- **Evening (2 hours)**: Code exercises, small projects
- **Weekend (4 hours)**: Larger project work

### Week 5-8: Intermediate
- **Morning (1 hour)**: Study x264 source code
- **Evening (2 hours)**: Implement video algorithms
- **Weekend (4 hours)**: Optimize implementations

### Week 9-12: Advanced
- **Morning (1 hour)**: Read codec specifications
- **Evening (2 hours)**: Contribute to open source / Build encoder
- **Weekend (4 hours)**: Performance optimization, SIMD

---

## Key Mindsets

1. **Read Production Code**: Study x264, don't just write from scratch
2. **Benchmark Everything**: Measure before and after optimization
3. **Start Simple**: Get it working, then optimize
4. **Understand Hardware**: CPU cache, SIMD, memory bandwidth matter
5. **Test Thoroughly**: Video bugs are hard to debug visually
6. **Profile First**: Don't guess what's slow, measure it

---

## Next Steps

1. **Start Today**: Pick Practice Project #1 (YUV File Reader)
2. **Set Up Environment**: Install GCC, Make, FFmpeg
3. **Join Community**: Sign up for Doom9, Stack Overflow
4. **Daily Coding**: Commit to 1-2 hours per day
5. **Build Portfolio**: Push projects to GitHub
6. **Study x264**: Once comfortable with C, dive into x264 source

---

## Success Metrics

After 3 months:
- ✓ Write memory-safe C code
- ✓ Understand pointers and memory management
- ✓ Read YUV files and manipulate frames
- ✓ Implement basic algorithms (DCT, SAD)

After 6 months:
- ✓ Read and understand x264 code
- ✓ Implement SIMD optimizations
- ✓ Build simple video encoder
- ✓ Use profiling tools effectively

After 12 months:
- ✓ Contribute to x264/x265
- ✓ Understand H.264/HEVC internals
- ✓ Optimize encoder performance
- ✓ Ready for video encoding engineer role

---

**Remember**: Video encoding is a marathon, not a sprint. Focus on fundamentals, practice daily, and study production codecs. The investment in learning C deeply will pay off throughout your career.

Good luck on your journey to becoming a video encoding engineer!
