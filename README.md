[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

# FastYZ

FastYZ is a fast Yaz0 compression library for C/C++, adapted from [FastLZ](https://github.com/ariya/fastlz) by Ariya Hidayat.

## Overview

Yaz0 (also known as SZS) is a compression format commonly used in Nintendo games, including titles for the Nintendo 64, GameCube, Wii, 3DS, Wii U, and Nintendo Switch. FastYZ provides an efficient implementation of Yaz0 compression using the same high-performance LZ77 strategy implemented by FastLZ.

The focus of FastYZ is **very fast compression** while maintaining full compatibility with standard Yaz0 decoders. Like FastLZ, this comes at the cost of compression ratio. If you need the smallest possible output, consider using a slower optimal parser like [libyaz0](https://github.com/aboood40091/libyaz0) or similar tools.

FastYZ is ideal for scenarios where compression speed matters more than achieving the smallest possible file size, such as:

- Build systems that compress many assets
- Real-time applications (a real use-case done by Nintendo in MKW & MK8, for example)
- Batch processing of game files

## Yaz0 Format

Yaz0 is a simple LZ77-based compression format with the following characteristics:

- **16-byte header**: Magic "Yaz0" + decompressed size (big-endian) + reserved bytes
- **Flag-byte encoding**: Each flag byte controls 8 subsequent items
  - Bit = 1: Literal byte follows
  - Bit = 0: Match reference follows (2-3 bytes)
- **Match parameters**:
  - Distance: 1-4096 bytes (12-bit encoding)
  - Length: 3-273 bytes (short form: 3-17, long form: 18-273)

### Header Structure

```
Offset  Size  Description
0x00    4     Magic signature "Yaz0" (0x59617A30)
0x04    4     Decompressed size (big-endian)
0x08    4     Reserved (alignment hint, usually 0)
0x0C    4     Reserved (usually 0)
```

### Match Encoding

**Short form (2 bytes)** for lengths 3-17:
```
Byte 0: [NNNN RRRR]  N = length - 2 (1-15), R = distance high bits
Byte 1: [RRRR RRRR]  R = distance low bits
```

**Long form (3 bytes)** for lengths 18-273:
```
Byte 0: [0000 RRRR]  R = distance high bits (length nibble = 0)
Byte 1: [RRRR RRRR]  R = distance low bits
Byte 2: [NNNN NNNN]  N = length - 18
```

## Algorithm Details

FastYZ uses a hash-based LZ77 compression strategy adapted from FastLZ:

1. **Hash Table Lookup**: A 14-bit (default) hash table (16,384 entries) is used for fast match finding. Each 3-byte sequence is hashed, and the table stores the position of the most recent occurrence. Small inputs automatically use a smaller table (10-bit under 4 KiB, 12-bit under 32 KiB) to cut initialization cost.

2. **Match Extension**: When a potential match is found, it is extended 4/8 bytes at a time on 32/64-bit targets, locating the first differing byte with a count-trailing/leading-zeros intrinsic.

3. **Greedy Parsing**: Unlike optimal parsers that consider all possible match combinations, FastYZ uses a greedy approach where the first sufficiently long match is used immediately. There is no lazy matching and no hash chaining, which is the main source of the ratio difference against an optimal parser.

4. **Literal Runs**: Unmatched bytes are accumulated and emitted as literal runs, with flag bits set to 1.

## Configuration

All options are compile-time defines.

| Define | Default | Effect |
|---|---|---|
| `FASTYZ_HTAB` | `FASTYZ_HTAB_STATIC` | Hash table placement. See below. |
| `FASTYZ_HASH_LOG` | `14` | Hash table size, `2^N` entries of 4 bytes. Larger improves ratio, costs memory. |
| `FASTYZ_LITTLE_ENDIAN` | auto-detected | Override endianness detection. Must be `0` or `1`. |

If endianness cannot be detected for your target, the build fails rather than silently guessing. Define `FASTYZ_LITTLE_ENDIAN` yourself in that case.

### Hash table placement and thread safety

The hash table is 64 KiB at the default `FASTYZ_HASH_LOG` (14). Where it lives is a trade-off between speed and reentrancy, so it is your choice:

**`FASTYZ_HTAB_STATIC` (default)**: the table is a single static array.

- Exports `yaz0_compress()`
- Fastest, no allocation, no per-call setup
- Costs 64 KiB of BSS
- **Not thread-safe,** concurrent compressions share one table, single-threaded use only

**`FASTYZ_HTAB_SCRATCH`**: the table lives in caller-supplied scratch memory.

- Exports `yaz0_compress_scratch()` instead
- Thread-safe, no allocation, no static footprint
- Roughly 3-4% slower on inputs of a few KiB and above
- Caller-supplied scratch memory must be at least `FASTYZ_SCRATCH_SIZE` bytes big

## Usage

FastYZ consists of just two files: `fastyz.h` and `fastyz.c`. Add them to your project to use the library.

### API Reference

```c
#include "fastyz.h"

/* Compress data to Yaz0 format.
   Default build (FASTYZ_HTAB_STATIC); not thread-safe. */
int yaz0_compress(const void* input, int length, void* output);

/* Compress data to Yaz0 format, thread-safe.
   Only present when built with -DFASTYZ_HTAB=FASTYZ_HTAB_SCRATCH.
   'scratch' must be at least FASTYZ_SCRATCH_SIZE bytes and
   aligned to at least alignof(uint32_t). */
int yaz0_compress_scratch(const void* input, int length, void* output, void* scratch);

/* Decompress Yaz0 data. Always thread-safe. */
int yaz0_decompress(const void* input, int length, void* output, int maxout);

/* Get decompressed size from Yaz0 header */
uint32_t yaz0_get_decompressed_size(const void* input);

/* Validate Yaz0 magic signature */
int yaz0_is_valid(const void* input);

/* Calculate worst-case compressed size */
#define FASTYZ_BOUND(length) (YAZ0_HEADER_SIZE + (length) + ((length) / 8) + 1)

/* Scratch buffer size, only defined under FASTYZ_HTAB_SCRATCH */
#define FASTYZ_SCRATCH_SIZE ((size_t)(1u << FASTYZ_HASH_LOG) * sizeof(uint32_t))
```

`yaz0_compress`, `yaz0_compress_scratch` and `yaz0_decompress` all return `0` on failure.
(Note that a successful zero-byte decompression result is indistinguishable from failure.)

### Example: Compression

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fastyz.h"

int main(void) {
    /* Your input data */
    const char* data = "Hello, Yaz0 compression!";
    int data_len = strlen(data) + 1;

    /* Allocate output buffer */
    int max_compressed = FASTYZ_BOUND(data_len);
    uint8_t* compressed = malloc(max_compressed);

    /* Compress */
    int compressed_size = yaz0_compress(data, data_len, compressed);

    printf("Original: %d bytes\n", data_len);
    printf("Compressed: %d bytes\n", compressed_size);

    /* Write to file, etc. */

    free(compressed);
    return 0;
}
```

### Example: Compression from multiple threads

```c
/* Build with -DFASTYZ_HTAB=FASTYZ_HTAB_SCRATCH */
#include <stdlib.h>
#include "fastyz.h"

void* worker(void* arg) {
    /* One scratch buffer per thread; it may be reused across calls
       and does not need to be initialized. */
    void* scratch = malloc(FASTYZ_SCRATCH_SIZE);

    for (...) {
        int n = yaz0_compress_scratch(input, length, output, scratch);
        ...
    }

    free(scratch);
    return NULL;
}
```

### Example: Decompression

```c
#include <stdio.h>
#include <stdlib.h>
#include "fastyz.h"

int main(void) {
    /* Read compressed data from file... */
    uint8_t* compressed = /* ... */;
    int compressed_len = /* ... */;

    /* Validate and get decompressed size. */
    if (compressed_len < YAZ0_HEADER_SIZE || !yaz0_is_valid(compressed)) {
        fprintf(stderr, "Not a valid Yaz0 file\n");
        return 1;
    }

    uint32_t decompressed_size = yaz0_get_decompressed_size(compressed);
    uint8_t* decompressed = malloc(decompressed_size);

    /* Decompress */
    int result = yaz0_decompress(compressed, compressed_len,
                                  decompressed, decompressed_size);

    if (result > 0) {
        printf("Decompressed %d bytes\n", result);
    }

    free(decompressed);
    return 0;
}
```

## Portability

FastYZ is C99 and builds on both little-endian and big-endian targets. Compressed output is **byte-identical regardless of host endianness**, so an asset pipeline produces the same artifact on any build machine.

Verified on x86-64, AArch64, 32-bit big-endian PowerPC and 64-bit big-endian PowerPC.

## Building

The library itself needs no build system; simply drop `fastyz.c` and `fastyz.h` into your project.

The CLI tool uses [premake5](https://premake.github.io/):

1. Invoke Premake on your system as:  
    * GCC: `premake5 gmake`
    * Clang: `premake5 --cc=clang gmake`
    * MSBuild+Clang-cl: `premake5 vs2022`
2. Compile:
    * GCC and Clang: Using `make` with `-C build config={Config}_{Architecture}` (e.g., `make -C build config=Release_x64`).
    * For MSBuild+Clang-cl: Using the command `msbuild FastYZ.sln /m /p:Configuration={Config} /p:Platform={Architecture} /p:PlatformToolset=ClangCl` (e.g., `msbuild FastYZ.sln /m /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=ClangCl`).

The output executable will be in `bin/Release/x64` for the given example commands.

## Command-Line Tool

FastYZ includes a simple CLI tool for compressing and decompressing files.

### Usage

```bash
# Compress a file (auto-generates output name)
fastyz file.bin              # Creates file.bin.yaz0

# Compress with custom output name
fastyz -c file.bin -o output.szs

# Decompress a file
fastyz file.yaz0             # Creates file (removes .yaz0 extension)
fastyz -d file.szs -o raw.bin

# Overwrite an existing output file
fastyz -f file.bin

# Show help
fastyz --help
```

### Options

| Option | Description |
|--------|-------------|
| `-c` | Force compression mode |
| `-d` | Force decompression mode |
| `-o <file>` | Specify output filename |
| `-f, --force` | Overwrite the output file if it already exists |
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |

If no mode is specified, the operation is auto-detected based on file extension (`.yaz0`, `.szs`, `.carc`) or file magic signature.

The tool will not overwrite an existing file unless `-f` is given, and refuses to write its output over its own input. Note that `fastyz file.bin` followed by `fastyz file.bin.yaz0` would otherwise restore over the original. It exits `0` on success and `1` on any failure, and removes a partially written output file if the write fails.

## Changes since v1.0.0

**Compression ratio**

- Fixed an off-by-one inherited from FastLZ that discarded back-references at a distance of exactly 4096, even though the format encodes distances 1-4096.  
  Data with a period of 4096 (such as page-aligned records, or tile strides) previously failed to compress at all.  
  On a 96 KiB file of 4096-byte repeating records, output went from 112.5% (larger than the input) to 5.8%.

**Performance**

- Match comparison now processes 8 bytes per step on 64-bit targets and 4 on 32-bit, instead of byte-at-a-time, using a ctz/clz intrinsic to locate the first difference.
- Literal flag bits are set in one operation instead of a per-bit loop.
- The decompressor gained a bounds-check-free fast path for the bulk of the stream, with a checked path near the buffer ends.
- Hash table size now adapts to the input size.

**Portability**

- Big-endian support. Output is byte-identical across endianness.
- Replaced an unaligned type-punned load (undefined behaviour, and a sanitizer failure on every compression) with a `memcpy` form that compiles to the same single instruction.
- Added `_BitScan*` and `__forceinline` shims so the code no longer depends on GCC/Clang builtins being present.
- Endianness that cannot be auto-detected is now a build error, overridable with `-DFASTYZ_LITTLE_ENDIAN=0|1`.

**API**

- New `FASTYZ_HTAB` policy selecting between the default static hash table and a thread-safe caller-supplied scratch buffer (`yaz0_compress_scratch`).
- `HASH_LOG` renamed to `FASTYZ_HASH_LOG`; the old name still works but warns.
- `yaz0_decompress` now bounds the output by the header's decompressed size rather than the caller's `maxout`, and rejects a negative `maxout`.

**CLI**

- Added `-f`/`--force`. Without it, the tool refuses to overwrite an existing file, which previously made `fastyz x.bin` then `fastyz x.bin.yaz0` silently destroy the original.
- Write failures are now detected at flush and close time rather than only at `fwrite`. A full disk previously reported success while writing nothing.
- A failed write no longer leaves a truncated file behind.
- Exit status is now `0` or `1`.
- Inputs above ~2 GiB are rejected with a clear message instead of silently truncating.
- Fixed a division by zero that would've printed `inf MB/s` for very small files.

**Build**

- premake5: `warnings "Extra"`, static runtime, Release link-time optimization, AddressSanitizer and UndefinedBehaviorSanitizer on Debug x64.
