/****************************************************************************
 *
 * rmfinfo.c
 *
 * A command line RMF file information utility
 *
 * Usage: rmfinfo [-c|-j] <rmffile>
 *   -c    Comma-separated values output
 *   -j    JSON output
 *
 * Based on NeoBAE audio engine
 *
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <NeoBAE.h>
#include <BAE_API.h>

// Copy of rmf_info_label function from playbae.c
const char *rmf_info_label(BAEInfoType t)
{
    switch (t)
    {
    case TITLE_INFO:
        return "Title";
    case PERFORMED_BY_INFO:
        return "Performed By";
    case COMPOSER_INFO:
        return "Composer";
    case COPYRIGHT_INFO:
        return "Copyright";
    case PUBLISHER_CONTACT_INFO:
        return "Publisher";
    case USE_OF_LICENSE_INFO:
        return "Use Of License";
    case LICENSED_TO_URL_INFO:
        return "Licensed URL";
    case LICENSE_TERM_INFO:
        return "License Term";
    case EXPIRATION_DATE_INFO:
        return "Expiration";
    case COMPOSER_NOTES_INFO:
        return "Composer Notes";
    case INDEX_NUMBER_INFO:
        return "Index Number";
    case GENRE_INFO:
        return "Genre";
    case SUB_GENRE_INFO:
        return "Sub-Genre";
    case TEMPO_DESCRIPTION_INFO:
        return "Tempo";
    case ORIGINAL_SOURCE_INFO:
        return "Source";
    default:
        return "Unknown";
    }
}

// Check if file has RMF magic header (IREZ)
int is_rmf_file(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }
    
    unsigned char header[4];
    if (fread(header, 1, 4, file) != 4) {
        fclose(file);
        return 0;
    }
    fclose(file);
    
    // Check for RMF magic bytes "IREZ" (0x49524552A in big-endian)
    return (header[0] == 0x49 && header[1] == 0x52 && 
            header[2] == 0x45 && header[3] == 0x5A);
}

// Escape a string for JSON output
void json_escape_string(const char *input, char *output, size_t output_size)
{
    const char *src = input;
    char *dst = output;
    char *end = output + output_size - 1;
    
    while (*src && dst < end - 1) {
        switch (*src) {
            case '"':
                if (dst < end - 2) {
                    *dst++ = '\\';
                    *dst++ = '"';
                }
                break;
            case '\\':
                if (dst < end - 2) {
                    *dst++ = '\\';
                    *dst++ = '\\';
                }
                break;
            case '\n':
                if (dst < end - 2) {
                    *dst++ = '\\';
                    *dst++ = 'n';
                }
                break;
            case '\r':
                if (dst < end - 2) {
                    *dst++ = '\\';
                    *dst++ = 'r';
                }
                break;
            case '\t':
                if (dst < end - 2) {
                    *dst++ = '\\';
                    *dst++ = 't';
                }
                break;
            default:
                *dst++ = *src;
                break;
        }
        src++;
    }
    *dst = '\0';
}

// Escape a string for CSV output
void csv_escape_string(const char *input, char *output, size_t output_size)
{
    const char *src = input;
    char *dst = output;
    char *end = output + output_size - 1;
    int needs_quotes = 0;
    
    // Check if we need quotes (contains comma, quote, or newline)
    for (const char *p = input; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quotes = 1;
            break;
        }
    }
    
    if (needs_quotes && dst < end) {
        *dst++ = '"';
    }
    
    while (*src && dst < end - (needs_quotes ? 2 : 1)) {
        if (*src == '"' && needs_quotes) {
            *dst++ = '"';  // Escape quote with double quote
        }
        *dst++ = *src++;
    }
    
    if (needs_quotes && dst < end) {
        *dst++ = '"';
    }
    *dst = '\0';
}

typedef enum {
    OUTPUT_NORMAL,
    OUTPUT_CSV,
    OUTPUT_JSON
} OutputFormat;

void print_usage(const char *program_name)
{
    printf("Usage: %s [-c|-j] <rmffile>\n", program_name);
    printf("  -c    Comma-separated values output\n");
    printf("  -j    JSON output\n");
    printf("  -h    Show this help\n");
}

int main(int argc, char *argv[])
{
    char *filename = NULL;
    OutputFormat output_format = OUTPUT_NORMAL;
    int i;
    
    // Parse command line arguments
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            output_format = OUTPUT_CSV;
        } else if (strcmp(argv[i], "-j") == 0) {
            output_format = OUTPUT_JSON;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (filename != NULL) {
                fprintf(stderr, "Multiple files specified. Only one file is supported.\n");
                print_usage(argv[0]);
                return 1;
            }
            filename = argv[i];
        }
    }
    
    if (filename == NULL) {
        fprintf(stderr, "No RMF file specified.\n");
        print_usage(argv[0]);
        return 1;
    }
    
    // Initialize BAE
    BAEResult err = BAE_Setup();
    if (err != BAE_NO_ERROR) {
        fprintf(stderr, "Error: Failed to initialize BAE audio engine (error %d)\n", err);
        return 1;
    }
    
    // Check if file exists and can be read
    FILE *test_file = fopen(filename, "rb");
    if (test_file == NULL) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        BAE_Cleanup();
        return 1;
    }
    fclose(test_file);
    
    // Validate that this is actually an RMF file
    if (!is_rmf_file(filename)) {
        fprintf(stderr, "Error: '%s' is not a valid RMF file (missing RMF magic header)\n", filename);
        BAE_Cleanup();
        return 1;
    }
    
    // Extract RMF information
    char buf[512];
    BAEInfoType it;
    int first_field = 1;
    int has_any_info = 0;
    
    // Output header based on format
    if (output_format == OUTPUT_CSV) {
        printf("Field,Value\n");
    } else if (output_format == OUTPUT_JSON) {
        printf("{\n");
    } else {
        printf("RMF File Information: %s\n", filename);
        printf("===============================================\n");
    }
    
    // Iterate through all info types
    for (it = TITLE_INFO; it <= ORIGINAL_SOURCE_INFO; it = (BAEInfoType)(it + 1)) {
        if (BAEUtil_GetRmfSongInfoFromFile((BAEPathName)filename, 0, it, buf, sizeof(buf) - 1) == BAE_NO_ERROR) {
            has_any_info = 1;
            
            if (output_format == OUTPUT_CSV) {
                char escaped_field[128];
                char escaped_value[1024];
                csv_escape_string(rmf_info_label(it), escaped_field, sizeof(escaped_field));
                csv_escape_string(buf, escaped_value, sizeof(escaped_value));
                printf("%s,%s\n", escaped_field, escaped_value);
            } else if (output_format == OUTPUT_JSON) {
                char escaped_value[1024];
                json_escape_string(buf, escaped_value, sizeof(escaped_value));
                
                if (!first_field) {
                    printf(",\n");
                }
                printf("  \"%s\": \"%s\"", rmf_info_label(it), escaped_value);
                first_field = 0;
            } else {
                printf("%-18s: %s\n", rmf_info_label(it), buf);
            }
        }
    }
    
    // Output footer based on format
    if (output_format == OUTPUT_JSON) {
        if (has_any_info) {
            printf("\n");
        }
        printf("}\n");
    } else if (output_format == OUTPUT_NORMAL && !has_any_info) {
        printf("No RMF metadata found in file.\n");
    }
    
    // Cleanup
    BAE_Cleanup();
    
    return 0;
}
