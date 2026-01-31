/*
  FastYZ - Fast Yaz0 compression library
  Based on FastLZ by Ariya Hidayat

  This file contains both the compressor (adapted from FastLZ's LZ77 strategy)
  and a standard Yaz0 decompressor.

  The compression algorithm uses a hash-based approach to find matching
  sequences, similar to FastLZ, but outputs in the Yaz0 format which uses
  a flag-byte scheme instead of FastLZ's opcode-based encoding.

  This software is released under the MIT License.
  See LICENSE file for details.
*/

#include "fastyz.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

/*
 * Branch prediction hints for the compiler.
 * These help optimize the hot paths in compression/decompression loops.
 */
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 2))
#define YAZ0_LIKELY(c)   (__builtin_expect(!!(c), 1))
#define YAZ0_UNLIKELY(c) (__builtin_expect(!!(c), 0))
#else
#define YAZ0_LIKELY(c)   (c)
#define YAZ0_UNLIKELY(c) (c)
#endif

/*
 * Enable 64-bit optimizations on supported architectures.
 * This allows reading/comparing 4 bytes at a time using native instructions.
 */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
#define YAZ0_ARCH64
#endif

/*
 * Workaround for DJGPP (DOS GCC) to find fixed-width integer types.
 */
#if defined(__MSDOS__) && defined(__GNUC__)
#include <stdint-gcc.h>
#endif

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

/* Maximum literals that can be emitted in one flag byte */
#define MAX_COPY YAZ0_FLAG_BYTE_NUM_BITS

/* Short-form minimum match length */
#define SHORT_FORM_MIN YAZ0_MIN_MATCH_LENGTH

/* Long-form minimum match length */
#define LONG_FORM_MIN YAZ0_MIN_LONG_MATCH_LENGTH

/* Maximum match length: 273 bytes */
#define MAX_LEN YAZ0_MAX_MATCH_LENGTH

/* Maximum back-reference distance */
#define MAX_MATCH_DISTANCE YAZ0_MAX_MATCH_DISTANCE

/*
 * Hash table size configuration.
 *
 * HASH_LOG determines the hash table size (2^HASH_LOG entries, 4 bytes each).
 * Larger values improve compression ratio at the cost of memory.
 *
 * Can be overridden at compile time with -DHASH_LOG=XX
 */
#ifndef HASH_LOG
#define HASH_LOG 14
#endif

#define HASH_SIZE (1 << HASH_LOG)
#define HASH_MASK (HASH_SIZE - 1)

/* ========================================================================
 * Memory Access Utilities
 * ======================================================================== */

/*
 * Read an unaligned 32-bit value from memory.
 * On 64-bit platforms, we can use direct memory access.
 * On 32-bit platforms, we read byte-by-byte for portability.
 */
#if defined(YAZ0_ARCH64)
static uint32_t read_u32(const void* ptr)
{
    return *(const uint32_t*)ptr;
}
#else
static uint32_t read_u32(const void* ptr)
{
    const uint8_t* p = (const uint8_t*)ptr;
    return (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}
#endif

/*
 * Compute a hash value for match finding.
 * Uses a multiplicative hash with a prime constant for good distribution.
 */
static uint16_t compute_hash(uint32_t v)
{
    uint32_t h = (v * 2654435769LL) >> (32 - HASH_LOG);
    return h & HASH_MASK;
}

/*
 * Compare two memory regions and return the length of the matching prefix.
 * The comparison stops at the boundary 'limit' to prevent buffer overruns.
 */
#if defined(YAZ0_ARCH64)
static uint32_t compare_match(const uint8_t* p, const uint8_t* q, const uint8_t* limit)
{
    const uint8_t* start = p;

    /* Compare 4 bytes at a time when possible */
    if (read_u32(p) == read_u32(q))
    {
        p += 4;
        q += 4;
    }
    
    /* Byte-by-byte comparison for remaining bytes */
    while (q < limit && *p == *q)
    {
        ++p;
        ++q;
    }
    
    return (uint32_t)(p - start);
}
#else
static uint32_t compare_match(const uint8_t* p, const uint8_t* q, const uint8_t* limit)
{
    const uint8_t* start = p;
    
    while (q < limit && *p == *q)
    {
        ++p;
        ++q;
    }
    
    return (uint32_t)(p - start);
}
#endif

/* ========================================================================
 * Small Memory Copy Utilities
 * ======================================================================== */

/*
 * Copy a small number of bytes (up to MAX_COPY = 8).
 * Optimized for the common case in literal runs.
 */
static void small_copy(uint8_t* dest, const uint8_t* src, uint32_t count)
{
#if defined(YAZ0_ARCH64)
    if (count >= 4)
    {
        const uint32_t* p = (const uint32_t*)src;
        uint32_t* q = (uint32_t*)dest;
        while (count > 4)
        {
            *q++ = *p++;
            count -= 4;
            dest += 4;
            src += 4;
        }
    }
#endif
    memcpy(dest, src, count);
}

/*
 * Copy exactly MAX_COPY (8) bytes.
 * Optimized for full literal groups in the flag-byte scheme.
 */
static void max_copy(void* dest, const void* src)
{
#if defined(YAZ0_ARCH64)
    const uint32_t* p = (const uint32_t*)src;
    uint32_t* q = (uint32_t*)dest;
    *q++ = *p++;
    *q++ = *p++;
#else
    memcpy(dest, src, MAX_COPY);
#endif
}

/* ========================================================================
 * Yaz0 Writer State
 * ======================================================================== */

/*
 * Writer state for Yaz0 encoding.
 * 
 * Yaz0 uses a flag-byte scheme where each bit indicates:
 *   1 = literal byte follows
 *   0 = match reference follows
 *
 * The flag byte is written first, then up to 8 data items follow
 * (either literal bytes or 2-3 byte match encodings).
 */
typedef struct
{
    uint8_t* op;      /* Current write position in output buffer */
    uint8_t* flagp;   /* Pointer to the current flag byte */
    uint8_t  mask;    /* Current bit mask (starts at 0x80, shifts right) */
} yaz0_writer_t;

/*
 * Start a new flag group.
 * Reserves a byte for the flags and resets the bit mask.
 */
static inline void writer_new_group(yaz0_writer_t* w)
{
    w->flagp = w->op;
    *w->op++ = 0;
    w->mask = 0x80;
}

/*
 * Emit N literal bytes to the output.
 * Each literal byte sets the corresponding flag bit to 1.
 */
static inline void writer_emit_literals(yaz0_writer_t* w, uint32_t count, const uint8_t* src)
{
    if (YAZ0_UNLIKELY(count == 0))
        return;

    /* Fill the current flag group if there's remaining space */
    if (w->mask != 0x80)
    {
        uint32_t room = (uint32_t)__builtin_ctz(w->mask) + 1;
        const bool fits_in_room = count < room;
        uint32_t n = fits_in_room ? count : room;

        /* Set flag bits for literals (1 = literal) */
        for (uint32_t i = 0; i < n; ++i)
        {
            *w->flagp |= w->mask;
            w->mask >>= 1;
        }

        small_copy(w->op, src, n);
        w->op += n;
        src += n;
        count -= n;

        if (fits_in_room)
            return;
        else
            writer_new_group(w);
    }

    /* Emit full groups of 8 literals */
    while (count >= MAX_COPY)
    {
        *w->flagp = 0xFF;  /* All 8 bits set = all literals */
        max_copy(w->op, src);
        w->op += MAX_COPY;
        src += MAX_COPY;
        count -= MAX_COPY;
        writer_new_group(w);
    }

    /* Emit remaining literals */
    if (count)
    {
        *w->flagp |= 0xFFu << (8 - count);
        w->mask = 0x80u >> count;

        small_copy(w->op, src, count);
        w->op += count;
    }
}

/*
 * Emit an LZ match reference.
 * 
 * Yaz0 Match Encoding:
 *   Flag bit = 0 (match reference follows)
 *
 *   Short form (2 bytes, len 3-17):
 *     Byte 0: [NNNN RRRR] - N = (length - 2), R = high 4 bits of distance-1
 *     Byte 1: [RRRR RRRR] - R = low 8 bits of distance-1
 *
 *   Long form (3 bytes, len 18-273):
 *     Byte 0: [0000 RRRR] - R = high 4 bits of distance-1
 *     Byte 1: [RRRR RRRR] - R = low 8 bits of distance-1
 *     Byte 2: [NNNN NNNN] - N = length - 18
 *
 * Distance is stored as (distance - 1), allowing distances 1-4096.
 */
static inline void writer_emit_match(yaz0_writer_t* w, uint32_t len, uint32_t distance)
{
    distance--;
    
    /* Handle matches longer than MAX_LEN by splitting into chunks */
    if (YAZ0_UNLIKELY(len > MAX_LEN))
    {
        while (len > MAX_LEN)
        {
            /*
             * If a full 273-byte chunk would leave a tail of only 1-2 bytes,
             * emit 271 bytes instead so the remaining tail will be >= 3 bytes
             * (the minimum match length).
             */
            uint32_t chunk = YAZ0_UNLIKELY(len - MAX_LEN < SHORT_FORM_MIN) ? (MAX_LEN - (SHORT_FORM_MIN - 1)) : MAX_LEN;

            /* Emit long form (3 bytes) for this chunk */
            uint8_t dist_high = (distance >> 8) & 0x0F;
            *w->op++ = dist_high;
            *w->op++ = distance & 0xFF;
            *w->op++ = (uint8_t)(chunk - LONG_FORM_MIN);

            /* Consume one flag bit (0 = match) */
            w->mask >>= 1;
            if (YAZ0_UNLIKELY(w->mask == 0))
                writer_new_group(w);

            len -= chunk;
        }
    }

    /* Emit the final (or only) chunk */
    if (len < LONG_FORM_MIN)
    {
        /* Short form: 2 bytes for lengths 3-17 */
        uint16_t code = ((len - (SHORT_FORM_MIN - 1)) << 12) | distance;
        *w->op++ = code >> 8;
        *w->op++ = code & 0xFF;
    }
    else
    {
        /* Long form: 3 bytes for lengths 18-273 */
        uint8_t dist_high = (distance >> 8) & 0x0F;
        *w->op++ = dist_high;
        *w->op++ = distance & 0xFF;
        *w->op++ = (uint8_t)(len - LONG_FORM_MIN);
    }

    /* Consume the flag bit for this match (0 = match) */
    w->mask >>= 1;
    if (YAZ0_UNLIKELY(w->mask == 0))
        writer_new_group(w);
}

/* ========================================================================
 * Public API: Compression
 * ======================================================================== */

int yaz0_compress(const void* input, int length, void* output)
{
    const uint8_t* ip = (const uint8_t*)input;
    const uint8_t* ip_start = ip;
    const uint8_t* ip_bound = ip + length - 4;  /* Leave room for read_u32 */
    const uint8_t* ip_limit = ip + length - 12 - 1;
    uint8_t* op = (uint8_t*)output;

    /* Write the Yaz0 header */
    op[0] = 'Y';
    op[1] = 'a';
    op[2] = 'z';
    op[3] = '0';
    op[4] = (length >> 24) & 0xFF;
    op[5] = (length >> 16) & 0xFF;
    op[6] = (length >>  8) & 0xFF;
    op[7] = (length      ) & 0xFF;
    
    /* Reserved fields (alignment hint and padding) */
    for (int i = 8; i < YAZ0_HEADER_SIZE; ++i)
        op[i] = 0;

    /* Initialize writer state after the 16-byte header */
    yaz0_writer_t w;
    w.op = op + YAZ0_HEADER_SIZE;
    writer_new_group(&w);

    /* Initialize hash table for match finding */
    uint32_t htab[HASH_SIZE] = { 0 };

    /* Start with literal copy (first 2 bytes can't have back-references) */
    const uint8_t* anchor = ip;
    ip += (SHORT_FORM_MIN - 1);

    /* Main compression loop */
    while (YAZ0_LIKELY(ip < ip_limit))
    {
        const uint8_t* ref;
        uint32_t distance, cmp, seq, hash;

        /* Find a potential match using the hash table */
        do
        {
            seq = read_u32(ip) & 0xffffff;  /* Use 3 bytes for hashing, the minimum match length */
            hash = compute_hash(seq);
            ref = ip_start + htab[hash];
            htab[hash] = ip - ip_start;
            distance = ip - ref;
            
            /* Check if the match is valid (within distance and matching) */
            cmp = YAZ0_LIKELY(distance < MAX_MATCH_DISTANCE) 
                  ? read_u32(ref) & 0xffffff 
                  : 0x1000000;
            
            if (YAZ0_UNLIKELY(ip >= ip_limit))
                break;
            ++ip;
        } while (seq != cmp);

        if (YAZ0_UNLIKELY(ip >= ip_limit))
            break;
        --ip;

        /* Emit any pending literals before this match */
        if (YAZ0_LIKELY(anchor < ip))
            writer_emit_literals(&w, (uint32_t)(ip - anchor), anchor);

        /* Extend the match as far as possible */
        uint32_t len = compare_match(ref + SHORT_FORM_MIN, ip + SHORT_FORM_MIN, ip_bound) + SHORT_FORM_MIN;
        writer_emit_match(&w, len, distance);

        /* Advance past the matched region */
        ip += len;
        anchor = ip;

        /* Update hash table at the match boundary for future matches */
        seq = read_u32(ip);
        hash = compute_hash(seq & 0xFFFFFF);
        htab[hash] = ip++ - ip_start;
        seq >>= 8;
        hash = compute_hash(seq);
        htab[hash] = ip++ - ip_start;
    }

    /* Emit any remaining literals at the end of input */
    uint32_t remaining = ip_start + length - anchor;
    writer_emit_literals(&w, remaining, anchor);

    return (int)(w.op - (uint8_t*)output);
}

/* ========================================================================
 * Public API: Decompression
 * ======================================================================== */

int yaz0_decompress(const void* input, int length, void* output, int maxout)
{
    /* Validate header magic */
    if (length < YAZ0_HEADER_SIZE)
        return 0;

    /* Read decompressed size */
    uint32_t decompressed_size = yaz0_get_decompressed_size(input);
    if (decompressed_size == 0)
        return 0;

    /* Check output buffer is large enough */
    if ((int)decompressed_size > maxout)
        return 0;

    const uint8_t* src = (const uint8_t*)input;
    const uint8_t* src_end = src + length;
    uint8_t* dst = (uint8_t*)output;
    uint8_t* dst_end = dst + maxout;

    /* Skip header */
    src += YAZ0_HEADER_SIZE;

    /* Decompression loop */
    uint8_t flag = 0;
    int bits_remaining = 0;

    while (dst < (uint8_t*)output + decompressed_size)
    {
        /* Read new flag byte when all bits are consumed */
        if (bits_remaining == 0)
        {
            if (src >= src_end)
                return 0;
            flag = *src++;
            bits_remaining = 8;
        }

        if (flag & 0x80)
        {
            /* Flag bit = 1: literal byte */
            if (src >= src_end || dst >= dst_end)
                return 0;
            *dst++ = *src++;
        }
        else
        {
            /* Flag bit = 0: match reference */
            if (src + 2 > src_end)
                return 0;

            uint8_t byte1 = *src++;
            uint8_t byte2 = *src++;

            /* Extract distance (always present in first 2 bytes) */
            uint32_t distance = ((byte1 & 0x0F) << 8) | byte2;
            distance++;  /* Stored as distance-1 */

            uint32_t len = byte1 >> 4;
            if (len == 0)
            {
                /* Long form: length in third byte */
                if (src >= src_end)
                    return 0;
                len = *src++;
                len += LONG_FORM_MIN;
            }
            else
            {
                /* Short form: length in high nibble */
                len += (SHORT_FORM_MIN - 1);
            }

            /* Validate back-reference */
            if (dst - distance < (uint8_t*)output)
                return 0;
            if (dst + len > dst_end)
                return 0;

            /* Copy from back-reference (byte-by-byte for overlapping copies) */
            const uint8_t* ref = dst - distance;
            for (uint32_t i = 0; i < len; ++i)
                *dst++ = *ref++;
        }

        flag <<= 1;
        bits_remaining--;
    }

    return (int)(dst - (uint8_t*)output);
}

/* ========================================================================
 * Public API: Utility Functions
 * ======================================================================== */

uint32_t yaz0_get_decompressed_size(const void* input)
{
    /* Validate magic */
    if (!yaz0_is_valid(input))
        return 0;

    /* Read big-endian size */
    const uint8_t* src = (const uint8_t*)input;
    return ((uint32_t)src[4] << 24) |
           ((uint32_t)src[5] << 16) |
           ((uint32_t)src[6] << 8) |
           ((uint32_t)src[7]);
}

int yaz0_is_valid(const void* input)
{
    const uint8_t* src = (const uint8_t*)input;
    return (src[0] == 'Y' && src[1] == 'a' && src[2] == 'z' && src[3] == '0');
}

#pragma GCC diagnostic pop
