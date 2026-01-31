/*
  FastYZ - Fast Yaz0 compression library
  Based on FastLZ by Ariya Hidayat

  FastYZ is an adaptation of the FastLZ compression strategy for Nintendo's
  Yaz0 (SZS) compression format. It provides very fast Yaz0 compression while
  maintaining full compatibility with standard Yaz0 decoders.

  This software is released under the MIT License.
  See LICENSE file for details.
*/

#ifndef FASTYZ_H
#define FASTYZ_H

#define FASTYZ_VERSION 0x010000

#define FASTYZ_VERSION_MAJOR 1
#define FASTYZ_VERSION_MINOR 0
#define FASTYZ_VERSION_REVISION 0

#define FASTYZ_VERSION_STRING "1.0.0"

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Yaz0 Header Structure (16 bytes)
 *
 * Offset  Size  Description
 * 0x00    4     Magic "Yaz0"
 * 0x04    4     Decompressed size (big-endian)
 * 0x08    4     Reserved (alignment hint, usually 0)
 * 0x0C    4     Reserved (usually 0)
 */
#define YAZ0_HEADER_SIZE 16

/**
 * Number of bits in a flag byte for Yaz0 format.
 * Each flag byte contains 8 bits, each indicating whether the corresponding
 * token is a literal (1) or a match reference (0).
 */
#define YAZ0_FLAG_BYTE_NUM_BITS 8

/**
 * Minimum match length for Yaz0 format.
 * Matches shorter than 3 bytes are encoded as literals.
 */
#define YAZ0_MIN_MATCH_LENGTH 3

/**
 * Minimum length for long-form matches in Yaz0 format.
 * Long-form matches use a 3-byte encoding for lengths of 18 bytes (3 + 15)
 * or more, where 15 is the maximum encodable length in the short form.
 */
#define YAZ0_MIN_LONG_MATCH_LENGTH (YAZ0_MIN_MATCH_LENGTH + 15)

/**
 * Maximum match length for Yaz0 format.
 * Matches can be up to 273 bytes long (18 + 255), where 255 is the maximum
 * encodable length in the long form.
 */
#define YAZ0_MAX_MATCH_LENGTH (YAZ0_MIN_LONG_MATCH_LENGTH + 255)

/**
 * Maximum back-reference distance for Yaz0 format.
 * Yaz0 uses 12-bit distance encoding, allowing back-references up to 4096 bytes.
 */
#define YAZ0_MAX_MATCH_DISTANCE (1 << 12)

/**
 * Calculate the maximum possible size of compressed output.
 *
 * In the worst case (incompressible data), Yaz0 output is slightly larger
 * than the input due to the flag bytes overhead. This macro provides a
 * safe upper bound for output buffer allocation.
 *
 * @param length  Size of the input data in bytes
 * @return        Maximum possible size of compressed output
 */
#define FASTYZ_BOUND(length) (YAZ0_HEADER_SIZE + (length) + ((length) / 8) + 1)

/**
 * Compress a block of data using Yaz0 compression.
 *
 * This function compresses the input data and produces a valid Yaz0 stream
 * complete with the standard 16-byte header. The output is fully compatible
 * with any standard Yaz0 decompressor.
 *
 * The compression uses a fast hash-based LZ77 algorithm adapted from FastLZ,
 * optimized for speed over compression ratio. For typical data, expect
 * compression speeds of 150-200 MB/s on modern hardware.
 *
 * @param input   Pointer to the input data to compress
 * @param length  Size of the input data in bytes (minimum 16 bytes)
 * @param output  Pointer to the output buffer for compressed data
 *                Must be at least FASTYZ_BOUND(length) bytes
 *
 * @return        Size of the compressed data in bytes,
 *                or 0 if compression failed
 *
 * @note The input and output buffers must not overlap.
 * @note The output includes the 16-byte Yaz0 header.
 */
int yaz0_compress(const void* input, int length, void* output);

/**
 * Decompress a Yaz0-compressed block of data.
 *
 * This function decompresses data that was compressed using Yaz0 compression.
 * It reads the decompressed size from the Yaz0 header and validates that the
 * output buffer is large enough.
 *
 * @param input   Pointer to the compressed Yaz0 data (including header)
 * @param length  Size of the compressed data in bytes
 * @param output  Pointer to the output buffer for decompressed data
 * @param maxout  Maximum size of the output buffer in bytes
 *
 * @return        Size of the decompressed data in bytes,
 *                or 0 if decompression failed (invalid data or buffer too small)
 *
 * @note The input and output buffers must not overlap.
 */
int yaz0_decompress(const void* input, int length, void* output, int maxout);

/**
 * Read the decompressed size from a Yaz0 header.
 *
 * This utility function extracts the original (decompressed) size from a
 * Yaz0 header without performing any decompression. Useful for allocating
 * an appropriately sized output buffer before decompression.
 *
 * @param input   Pointer to the Yaz0 data (at least 8 bytes)
 *
 * @return        The decompressed size in bytes,
 *                or 0 if the header is invalid (wrong magic)
 */
uint32_t yaz0_get_decompressed_size(const void* input);

/**
 * Validate a Yaz0 header.
 *
 * Checks if the input data starts with a valid Yaz0 magic signature ("Yaz0").
 *
 * @param input   Pointer to the data to validate (at least 4 bytes)
 *
 * @return        Non-zero if the header is valid, 0 otherwise
 */
int yaz0_is_valid(const void* input);

#if defined(__cplusplus)
}
#endif

#endif /* FASTYZ_H */
