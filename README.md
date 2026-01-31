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

1. **Hash Table Lookup**: A 14-bit (default) hash table (16,384 entries) is used for fast match finding. Each 3-byte sequence is hashed, and the table stores the position of the most recent occurrence.

2. **Match Extension**: When a potential match is found, it is extended byte-by-byte to find the longest match within the distance limit (4096 bytes).

3. **Lazy Evaluation**: Unlike optimal parsers that consider all possible match combinations, FastYZ uses a greedy approach where the first sufficiently long match is used immediately.

4. **Literal Runs**: Unmatched bytes are accumulated and emitted as literal runs, with flag bits set to 1.

## Usage

FastYZ consists of just two files: `fastyz.h` and `fastyz.c`. Add them to your project to use the library.

### API Reference

```c
#include "fastyz.h"

/* Compress data to Yaz0 format */
int yaz0_compress(const void* input, int length, void* output);

/* Decompress Yaz0 data */
int yaz0_decompress(const void* input, int length, void* output, int maxout);

/* Get decompressed size from Yaz0 header */
uint32_t yaz0_get_decompressed_size(const void* input);

/* Validate Yaz0 magic signature */
int yaz0_is_valid(const void* input);

/* Calculate worst-case compressed size */
#define FASTYZ_BOUND(length) (YAZ0_HEADER_SIZE + (length) + ((length) / 8) + 1)
```

### Example: Compression

```c
#include <stdio.h>
#include <stdlib.h>
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

### Example: Decompression

```c
#include <stdio.h>
#include <stdlib.h>
#include "fastyz.h"

int main(void) {
    /* Read compressed data from file... */
    uint8_t* compressed = /* ... */;
    int compressed_len = /* ... */;
    
    /* Validate and get decompressed size */
    if (!yaz0_is_valid(compressed)) {
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

# Show help
fastyz --help
```

### Options

| Option | Description |
|--------|-------------|
| `-c` | Force compression mode |
| `-d` | Force decompression mode |
| `-o <file>` | Specify output filename |
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |

If no mode is specified, the operation is auto-detected based on file extension (`.yaz0`, `.szs`, `.carc`) or file magic signature.
