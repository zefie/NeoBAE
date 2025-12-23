/****************************************************************************
 *
 * rmf2mid.c
 *
 * RMF to MIDI converter using NeoBAE parsers
 *
 * Usage: rmf2mid <input.rmf> <output.mid>
 *
 * This program extracts the MIDI data from an RMF (Rich Music Format) file
 * and saves it as a standard MIDI file.
 *
 * Based on NeoBAE audio engine
 *
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <NeoBAE.h>
#include <BAE_API.h>
#include <X_API.h>
#include <X_Formats.h>

// Function to write a 32-bit value in big-endian format
static void write_be32(FILE *f, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;
    fwrite(bytes, 1, 4, f);
}

// Function to write a 16-bit value in big-endian format
static void write_be16(FILE *f, uint16_t value)
{
    uint8_t bytes[2];
    bytes[0] = (value >> 8) & 0xFF;
    bytes[1] = value & 0xFF;
    fwrite(bytes, 1, 2, f);
}

// Extract MIDI data from RMF file
static int extract_midi_from_rmf(const char *rmf_path, const char *mid_path)
{
    XFILE rmf_file = NULL;
    XFILENAME xfilename;
    XLongResourceID resource_id;
    XPTR midi_data = NULL;
    int32_t midi_size = 0;
    XResourceType resource_type;
    FILE *output_file = NULL;
    int result = -1;

    // Convert path to XFILENAME
    XConvertPathToXFILENAME((void *)rmf_path, &xfilename);
    
    // Open RMF file as resource file
    rmf_file = XFileOpenResource(&xfilename, TRUE);
    if (!rmf_file)
    {
        fprintf(stderr, "Error: Cannot open RMF file '%s'\n", rmf_path);
        return -1;
    }

    // Try to extract MIDI data from the RMF file
    // RMF files contain MIDI data in resources
    // First try to get indexed SONG resource (ID_SONG)
    SongResource *song_res = (SongResource *)XGetIndexedFileResource(rmf_file, ID_SONG, &resource_id, 0, NULL, &midi_size);
    
    if (song_res)
    {
        // Got a SONG resource - now we need to extract the MIDI data from it
        // The SONG resource contains a reference to the MIDI resource
        SongResource_RMF *rmf_song = (SongResource_RMF *)song_res;
        XShortResourceID midi_id = 0;
        
        // Check song type and get MIDI resource ID
        if (rmf_song->songType == SONG_TYPE_RMF)
        {
            midi_id = XGetShort(&rmf_song->rmfResourceID);
        }
        else if (rmf_song->songType == SONG_TYPE_SMS)
        {
            SongResource_SMS *sms_song = (SongResource_SMS *)song_res;
            midi_id = XGetShort(&sms_song->midiResourceID);
        }
        
        XDisposePtr((XPTR)song_res);
        
        if (midi_id != 0)
        {
            // Now get the actual MIDI data
            midi_data = XGetMidiData((XLongResourceID)midi_id, &midi_size, &resource_type);
        }
    }
    
    // If we didn't get MIDI from SONG resource, try to get it directly
    if (!midi_data)
    {
        // Try to get first MIDI resource directly
        midi_data = XGetIndexedFileResource(rmf_file, ID_MIDI, &resource_id, 0, NULL, &midi_size);
        
        // If still no luck, try old MIDI type
        if (!midi_data)
        {
            midi_data = XGetIndexedFileResource(rmf_file, ID_MIDI_OLD, &resource_id, 0, NULL, &midi_size);
        }
    }
    
    if (!midi_data || midi_size <= 0)
    {
        fprintf(stderr, "Error: No MIDI data found in RMF file\n");
        XFileClose(rmf_file);
        return -1;
    }

    printf("Extracted MIDI data: %d bytes\n", (int)midi_size);

    // Verify this is valid MIDI data (should start with "MThd")
    if (midi_size < 14 || memcmp(midi_data, "MThd", 4) != 0)
    {
        fprintf(stderr, "Error: Extracted data is not valid MIDI format\n");
        XDisposePtr(midi_data);
        XFileClose(rmf_file);
        return -1;
    }

    // Write MIDI data to output file
    output_file = fopen(mid_path, "wb");
    if (!output_file)
    {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", mid_path);
        XDisposePtr(midi_data);
        XFileClose(rmf_file);
        return -1;
    }

    if (fwrite(midi_data, 1, midi_size, output_file) != (size_t)midi_size)
    {
        fprintf(stderr, "Error: Failed to write MIDI data to output file\n");
        fclose(output_file);
        XDisposePtr(midi_data);
        XFileClose(rmf_file);
        return -1;
    }

    printf("Successfully wrote MIDI file: %s\n", mid_path);
    result = 0;

    // Cleanup
    fclose(output_file);
    XDisposePtr(midi_data);
    XFileClose(rmf_file);
    
    return result;
}

static void print_usage(const char *program_name)
{
    printf("RMF to MIDI Converter\n");
    printf("Usage: %s <input.rmf> <output.mid>\n", program_name);
    printf("\n");
    printf("Extracts MIDI data from an RMF (Rich Music Format) file\n");
    printf("and saves it as a standard MIDI file.\n");
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_rmf = argv[1];
    const char *output_mid = argv[2];

    // Initialize BAE
    BAEResult err = BAE_Setup();
    if (err != BAE_NO_ERROR)
    {
        fprintf(stderr, "Error: Failed to initialize BAE audio engine (error %d)\n", err);
        return 1;
    }

    // Check if input file exists
    FILE *test_file = fopen(input_rmf, "rb");
    if (!test_file)
    {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", input_rmf);
        BAE_Cleanup();
        return 1;
    }
    
    // Verify it's an RMF file
    unsigned char header[4];
    if (fread(header, 1, 4, test_file) != 4)
    {
        fprintf(stderr, "Error: Cannot read file header\n");
        fclose(test_file);
        BAE_Cleanup();
        return 1;
    }
    fclose(test_file);
    
    if (!(header[0] == 0x49 && header[1] == 0x52 && 
          header[2] == 0x45 && header[3] == 0x5A))
    {
        fprintf(stderr, "Error: '%s' is not a valid RMF file (missing IREZ header)\n", input_rmf);
        BAE_Cleanup();
        return 1;
    }

    // Extract MIDI data
    int result = extract_midi_from_rmf(input_rmf, output_mid);

    // Cleanup BAE
    BAE_Cleanup();

    return (result == 0) ? 0 : 1;
}
