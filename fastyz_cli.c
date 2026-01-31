/*
  FastYZ Command-Line Interface
  
  A simple command-line tool for compressing and decompressing files
  using the Yaz0 compression format.

  Usage:
    fastyz [-c|-d] [-o output] input
    fastyz -c input.bin                  # Compress to input.bin.yaz0
    fastyz -c input.bin -o output.szs    # Compress to output.szs
    fastyz -d input.yaz0                 # Decompress to input (without .yaz0)
    fastyz -d input.yaz0 -o output.bin   # Decompress to output.bin
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "fastyz.h"

/* ========================================================================
 * Constants
 * ======================================================================== */

#define PROGRAM_NAME    "fastyz"
#define PROGRAM_VERSION FASTYZ_VERSION_STRING

typedef enum {
    MODE_AUTO,       /* Auto-detect based on file extension/content */
    MODE_COMPRESS,   /* Force compression */
    MODE_DECOMPRESS  /* Force decompression */
} operation_mode_t;

/* ========================================================================
 * File I/O Utilities
 * ======================================================================== */

/*
 * Read entire file into memory.
 * Returns allocated buffer (caller must free) or NULL on error.
 */
static uint8_t* read_file(const char* filename, long* out_size)
{
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open '%s' for reading\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fprintf(stderr, "Error: File '%s' is empty or invalid\n", filename);
        fclose(fp);
        return NULL;
    }

    uint8_t* buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate %ld bytes\n", size);
        fclose(fp);
        return NULL;
    }

    size_t read = fread(buffer, 1, size, fp);
    fclose(fp);

    if ((long)read != size) {
        fprintf(stderr, "Error: Failed to read '%s' (expected %ld, got %zu)\n",
                filename, size, read);
        free(buffer);
        return NULL;
    }

    *out_size = size;
    return buffer;
}

/*
 * Write buffer to file.
 * Returns 0 on success, -1 on error.
 */
static int write_file(const char* filename, const uint8_t* data, size_t size)
{
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open '%s' for writing\n", filename);
        return -1;
    }

    size_t written = fwrite(data, 1, size, fp);
    fclose(fp);

    if (written != size) {
        fprintf(stderr, "Error: Failed to write '%s' (expected %zu, wrote %zu)\n",
                filename, size, written);
        return -1;
    }

    return 0;
}

/* ========================================================================
 * String Utilities
 * ======================================================================== */

/*
 * Check if string ends with the given suffix (case-insensitive).
 */
static int str_ends_with_i(const char* str, const char* suffix)
{
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len)
        return 0;

    const char* str_suffix = str + str_len - suffix_len;
    
    for (size_t i = 0; i < suffix_len; i++) {
        char c1 = str_suffix[i];
        char c2 = suffix[i];
        
        /* Convert to lowercase */
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
        if (c1 != c2)
            return 0;
    }
    
    return 1;
}

/*
 * Generate output filename based on operation and input filename.
 */
static char* generate_output_filename(const char* input, operation_mode_t mode)
{
    size_t len = strlen(input);
    char* output;

    if (mode == MODE_COMPRESS) {
        /* Append .yaz0 extension */
        output = (char*)malloc(len + 5 + 1);
        if (output) {
            strcpy(output, input);
            strcat(output, ".yaz0");
        }
    } else {
        /* Remove .yaz0, .szs, or .carc extension if present */
        if (str_ends_with_i(input, ".yaz0")) {
            output = (char*)malloc(len - 5 + 1);
            if (output) {
                strncpy(output, input, len - 5);
                output[len - 5] = '\0';
            }
        } else if (str_ends_with_i(input, ".szs")) {
            output = (char*)malloc(len - 4 + 1);
            if (output) {
                strncpy(output, input, len - 4);
                output[len - 4] = '\0';
            }
        } else if (str_ends_with_i(input, ".carc")) {
            /* Replace .carc with .arc */
            output = (char*)malloc(len - 5 + 4 + 1);
            if (output) {
                strncpy(output, input, len - 5);
                output[len - 5] = '\0';
                strcat(output, ".arc");
            }
        } else {
            /* Append .bin for decompressed output */
            output = (char*)malloc(len + 4 + 1);
            if (output) {
                strcpy(output, input);
                strcat(output, ".bin");
            }
        }
    }

    return output;
}

/* ========================================================================
 * Compression/Decompression Operations
 * ======================================================================== */

static int do_compress(const char* input_file, const char* output_file)
{
    long input_size;
    uint8_t* input_data = read_file(input_file, &input_size);
    if (!input_data)
        return 1;

    /* Allocate output buffer with worst-case size */
    size_t max_output = FASTYZ_BOUND(input_size);
    uint8_t* output_data = (uint8_t*)malloc(max_output);
    if (!output_data) {
        fprintf(stderr, "Error: Failed to allocate output buffer\n");
        free(input_data);
        return 1;
    }

    /* Compress */
    clock_t start = clock();
    int output_size = yaz0_compress(input_data, (int)input_size, output_data);
    clock_t end = clock();

    if (output_size <= 0) {
        fprintf(stderr, "Error: Compression failed\n");
        free(input_data);
        free(output_data);
        return 1;
    }

    /* Write output */
    int result = write_file(output_file, output_data, output_size);
    
    if (result == 0) {
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        double ratio = 100.0 * output_size / input_size;
        double speed = (input_size / (1024.0 * 1024.0)) / elapsed;
        
        printf("Compressed: %s -> %s\n", input_file, output_file);
        printf("  Original:   %ld bytes\n", input_size);
        printf("  Compressed: %d bytes (%.1f%%)\n", output_size, ratio);
        printf("  Time:       %.3f sec (%.1f MB/s)\n", elapsed, speed);
    }

    free(input_data);
    free(output_data);
    return result;
}

static int do_decompress(const char* input_file, const char* output_file)
{
    long input_size;
    uint8_t* input_data = read_file(input_file, &input_size);
    if (!input_data)
        return 1;

    /* Validate Yaz0 header */
    if (input_size < YAZ0_HEADER_SIZE || !yaz0_is_valid(input_data)) {
        fprintf(stderr, "Error: '%s' is not a valid Yaz0 file\n", input_file);
        free(input_data);
        return 1;
    }

    /* Get decompressed size from header */
    uint32_t output_size = yaz0_get_decompressed_size(input_data);
    if (output_size == 0) {
        fprintf(stderr, "Error: Invalid Yaz0 header in '%s'\n", input_file);
        free(input_data);
        return 1;
    }

    /* Allocate output buffer */
    uint8_t* output_data = (uint8_t*)malloc(output_size);
    if (!output_data) {
        fprintf(stderr, "Error: Failed to allocate %u bytes for output\n", output_size);
        free(input_data);
        return 1;
    }

    /* Decompress */
    clock_t start = clock();
    int decompressed = yaz0_decompress(input_data, (int)input_size, output_data, output_size);
    clock_t end = clock();

    if (decompressed <= 0) {
        fprintf(stderr, "Error: Decompression failed\n");
        free(input_data);
        free(output_data);
        return 1;
    }

    /* Write output */
    int result = write_file(output_file, output_data, decompressed);

    if (result == 0) {
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        double speed = (decompressed / (1024.0 * 1024.0)) / elapsed;
        
        printf("Decompressed: %s -> %s\n", input_file, output_file);
        printf("  Compressed:   %ld bytes\n", input_size);
        printf("  Decompressed: %d bytes\n", decompressed);
        printf("  Time:         %.3f sec (%.1f MB/s)\n", elapsed, speed);
    }

    free(input_data);
    free(output_data);
    return result;
}

/* ========================================================================
 * Usage and Main
 * ======================================================================== */

static void print_usage(void)
{
    printf("FastYZ v%s - Fast Yaz0 compression\n", PROGRAM_VERSION);
    printf("\n");
    printf("Usage: %s [options] <input>\n", PROGRAM_NAME);
    printf("\n");
    printf("Options:\n");
    printf("  -c          Force compression mode\n");
    printf("  -d          Force decompression mode\n");
    printf("  -o <file>   Specify output filename\n");
    printf("  -h, --help  Show this help message\n");
    printf("  -v          Show version information\n");
    printf("\n");
    printf("If no mode is specified, the operation is auto-detected:\n");
    printf("  - Files with .yaz0, .szs, or .carc extension are decompressed\n");
    printf("  - Files starting with 'Yaz0' magic are decompressed\n");
    printf("  - All other files are compressed\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s file.bin                 Compress to file.bin.yaz0\n", PROGRAM_NAME);
    printf("  %s -c file.bin -o out.szs   Compress to out.szs\n", PROGRAM_NAME);
    printf("  %s file.yaz0                Decompress to file\n", PROGRAM_NAME);
    printf("  %s -d data.szs -o raw.bin   Decompress to raw.bin\n", PROGRAM_NAME);
}

static void print_version(void)
{
    printf("FastYZ v%s\n", PROGRAM_VERSION);
    printf("Fast Yaz0 compression based on FastLZ\n");
    printf("https://github.com/your-username/fastyz\n");
}

int main(int argc, char* argv[])
{
    operation_mode_t mode = MODE_AUTO;
    const char* input_file = NULL;
    const char* output_file = NULL;
    char* generated_output = NULL;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            mode = MODE_COMPRESS;
        } else if (strcmp(argv[i], "-d") == 0) {
            mode = MODE_DECOMPRESS;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            if (input_file) {
                fprintf(stderr, "Error: Multiple input files specified\n");
                return 1;
            }
            input_file = argv[i];
        }
    }

    /* Validate arguments */
    if (!input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Use '%s --help' for usage information\n", PROGRAM_NAME);
        return 1;
    }

    /* Auto-detect mode if not specified */
    if (mode == MODE_AUTO) {
        /* Check file extension first */
        if (str_ends_with_i(input_file, ".yaz0") || str_ends_with_i(input_file, ".szs") || str_ends_with_i(input_file, ".carc")) {
            mode = MODE_DECOMPRESS;
        } else {
            /* Check file magic */
            FILE* fp = fopen(input_file, "rb");
            if (fp) {
                uint8_t magic[4];
                if (fread(magic, 1, 4, fp) == 4 && yaz0_is_valid(magic)) {
                    mode = MODE_DECOMPRESS;
                } else {
                    mode = MODE_COMPRESS;
                }
                fclose(fp);
            } else {
                fprintf(stderr, "Error: Cannot open '%s'\n", input_file);
                return 1;
            }
        }
    }

    /* Generate output filename if not specified */
    if (!output_file) {
        generated_output = generate_output_filename(input_file, mode);
        if (!generated_output) {
            fprintf(stderr, "Error: Failed to generate output filename\n");
            return 1;
        }
        output_file = generated_output;
    }

    /* Perform operation */
    int result;
    if (mode == MODE_COMPRESS) {
        result = do_compress(input_file, output_file);
    } else {
        result = do_decompress(input_file, output_file);
    }

    free(generated_output);
    return result;
}
