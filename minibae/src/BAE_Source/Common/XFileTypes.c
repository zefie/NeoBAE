/*****************************************************************************/
/*
**  XFileTypes.c
**
**  Content-based file type detection for audio and MIDI files.
**  
**  Determines file types based on file content (magic bytes/FOURCCs)
**  rather than relying solely on file extensions.
**
**  Supports:
**  - MIDI (.mid) - "MThd" header
**  - RMF (.rmf) - "IREZ" header  
**  - RMI (.rmi) - RIFF container with embedded MIDI
**  - XMF/MXMF (.xmf/.mxmf) - "XMF_" header
**  - WAV (.wav) - RIFF WAVE container
**  - AIFF (.aif/.aiff) - FORM AIFF container
**  - AU (.au) - Sun Audio ".snd" header
**  - FLAC (.flac) - "fLaC" header
**  - MP2/MP3 (.mp2/.mp3) - MPEG frame sync or ID3 tags
**  - OGG (.ogg) - "OggS" header (Vorbis/FLAC only)
**
*/
/*****************************************************************************/

#include "X_API.h"
#include "MiniBAE.h"
#include "BAE_API.h"
#include <string.h>
#include "X_Assert.h"

// Maximum number of bytes to read for file type detection
#define FILETYPE_PROBE_SIZE 64

// Magic byte signatures for different file types
#define BAE_FOURCC_MIDI     0x4D546864  // "MThd" - Standard MIDI File
#define BAE_FOURCC_RMF      0x4952455A  // "IREZ" - Rich Music Format
#define BAE_FOURCC_XMF      0x584D465F  // "XMF_" - eXtensible Music Format
#define BAE_FOURCC_RIFF     0x52494646  // "RIFF" - Resource Interchange File Format
#define BAE_FOURCC_FORM     0x464F524D  // "FORM" - IFF FORM container (AIFF)
#define BAE_FOURCC_AU       0x2E736E64  // ".snd" - Sun Audio
#define BAE_FOURCC_FLAC     0x664C6143  // "fLaC" - Free Lossless Audio Codec
#define BAE_FOURCC_OGGS     0x4F676753  // "OggS" - Ogg container

// RIFF/IFF subtype FOURCCs
#define BAE_FOURCC_WAVE     0x57415645  // "WAVE" - RIFF Wave audio
#define BAE_FOURCC_RMID     0x524D4944  // "RMID" - RIFF MIDI
#define BAE_FOURCC_AIFF     0x41494646  // "AIFF" - Audio Interchange File Format

// OGG codec identifiers
#define OGG_VORBIS_MAGIC    0x01766F72  // "\x01vor" - Vorbis identification header
#define OGG_FLAC_MAGIC      0x7F464C41  // "\x7fFLA" - FLAC mapping header

// Function prototypes for internal helpers
static uint32_t PV_ReadBigEndian32(const unsigned char *data);
static uint32_t PV_ReadLittleEndian32(const unsigned char *data);
static BAEFileType PV_DetectRIFFType(const unsigned char *buffer, int32_t bufferSize);
static BAEFileType PV_DetectOGGType(const unsigned char *buffer, int32_t bufferSize);
static int PV_IsLikelyMPEGHeader(const unsigned char *header);

/**
 * Read a 32-bit big-endian value from a buffer
 */
static uint32_t PV_ReadBigEndian32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           ((uint32_t)data[3]);
}

/**
 * Read a 32-bit little-endian value from a buffer
 */
static uint32_t PV_ReadLittleEndian32(const unsigned char *data)
{
    return ((uint32_t)data[3] << 24) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[0]);
}

/**
 * Detect specific RIFF file subtypes (WAV, RMI)
 */
static BAEFileType PV_DetectRIFFType(const unsigned char *buffer, int32_t bufferSize)
{
    uint32_t subtype;
    
    if (bufferSize < 12)
        return BAE_INVALID_TYPE;
    
    // Read the RIFF subtype at offset 8
    subtype = PV_ReadBigEndian32(&buffer[8]);
    
    switch (subtype)
    {
        case BAE_FOURCC_WAVE:
            return BAE_WAVE_TYPE;
            
        case BAE_FOURCC_RMID:
            // Verify there's actually MIDI data embedded
            // Look for "data" chunk followed by "MThd"
            if (bufferSize >= 24)
            {
                for (int i = 12; i < bufferSize - 8; i += 4)
                {
                    if (PV_ReadBigEndian32(&buffer[i]) == 0x64617461 && // "data"
                        i + 12 < bufferSize &&
                        PV_ReadBigEndian32(&buffer[i + 8]) == BAE_FOURCC_MIDI)
                    {
                        return BAE_RMI; // RMI files are treated as MIDI
                    }
                }
            }
            return BAE_WAVE_TYPE; // Default to WAVE if no MIDI found
            
        default:
            return BAE_WAVE_TYPE; // Unknown RIFF subtype, assume WAVE
    }
}

#if USE_OGG_FORMAT == TRUE
/**
 * Detect OGG container contents (Vorbis or FLAC)
 */
static BAEFileType PV_DetectOGGType(const unsigned char *buffer, int32_t bufferSize)
{
    // OGG pages start with "OggS" followed by version, header type, etc.
    // The actual codec data starts after the OGG page header
    // We need to find the first page with audio data
    
    int offset = 4; // Skip "OggS"
    
    while (offset + 27 < bufferSize) // Minimum OGG page header size
    {
        // Skip OGG page header fields to get to the payload
        unsigned char headerType = buffer[offset + 1];
        int segmentCount = buffer[offset + 22];
        
        if (segmentCount > 255 || offset + 27 + segmentCount >= bufferSize)
            break;
            
        // Skip to segment table
        offset += 27;
        
        // Calculate payload offset by summing segment lengths
        int payloadOffset = offset + segmentCount;
        int payloadSize = 0;
        
        for (int i = 0; i < segmentCount && offset + i < bufferSize; i++)
        {
            payloadSize += buffer[offset + i];
        }
        
        if (payloadOffset + 8 <= bufferSize)
        {
            uint32_t magic = PV_ReadBigEndian32(&buffer[payloadOffset]);
            
#if USE_VORBIS_DECODER == TRUE
            // Check for Vorbis identification header
            if (magic == OGG_VORBIS_MAGIC && payloadOffset + 7 < bufferSize &&
                buffer[payloadOffset + 4] == 'b' &&
                buffer[payloadOffset + 5] == 'i' &&
                buffer[payloadOffset + 6] == 's')
            {
                return BAE_VORBIS_TYPE;
            }
#endif

#if USE_FLAC_DECODER == TRUE
            // Check for FLAC in OGG (rare but possible)
            if (magic == OGG_FLAC_MAGIC && payloadOffset + 4 < bufferSize &&
                buffer[payloadOffset + 4] == 'C')
            {
                return BAE_FLAC_TYPE;
            }
#endif
        }

        // Look for next OGG page
        offset = payloadOffset + payloadSize;
        while (offset + 4 <= bufferSize)
        {
            if (PV_ReadBigEndian32(&buffer[offset]) == BAE_FOURCC_OGGS)
                break;
            offset++;
        }
        
        if (offset + 4 > bufferSize)
            break;
    }
#if USE_VORBIS_DECODER == TRUE    
    // Default to Vorbis if we can't determine specifically
    return BAE_VORBIS_TYPE;
#else
    return BAE_INVALID_TYPE;
#endif

}
#endif

/**
 * Check if header bytes look like an MPEG audio frame
 */
static int PV_IsLikelyMPEGHeader(const unsigned char *header)
{
    BAE_PRINTF("[FileType] Checking MPEG header: %02X %02X %02X %02X\n", 
               header[0], header[1], header[2], header[3]);
    
    // Check for ID3v2 tag
    if (header[0] == 'I' && header[1] == 'D' && header[2] == '3')
    {
        BAE_PRINTF("[FileType] Detected ID3v2 tag\n");
        return 1;
    }
    
    // Check for specific MPEG pattern: FF F3 40 CC
    if (header[0] == 0xFF && header[1] == 0xF3 && header[2] == 0x40 && header[3] == 0xCC)
    {
        BAE_PRINTF("[FileType] Detected specific MPEG pattern FF F3 40 CC\n");
        return 1;
    }
        
    // Check for MPEG frame sync pattern (11 bits of 1s: 0xFFE)
    if (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)
    {
        BAE_PRINTF("[FileType] Found MPEG frame sync pattern\n");
        // Additional validation: check for valid MPEG version and layer
        unsigned char version = (header[1] >> 3) & 0x03;
        unsigned char layer = (header[1] >> 1) & 0x03;
        unsigned char bitrate = (header[2] >> 4) & 0x0F;
        
        BAE_PRINTF("[FileType] MPEG validation - version: %02X, layer: %02X, bitrate: %02X\n", 
                   version, layer, bitrate);
        
        // Version should not be 01 (reserved)
        // Layer should not be 00 (reserved)  
        // Bitrate should not be 0000 (free) or 1111 (reserved)
        if (version != 0x01 && layer != 0x00 && bitrate != 0x00 && bitrate != 0x0F)
        {
            BAE_PRINTF("[FileType] MPEG validation passed\n");
            return 1;
        }
        else
        {
            BAE_PRINTF("[FileType] MPEG validation failed\n");
        }
    }
    
    BAE_PRINTF("[FileType] No MPEG pattern detected\n");
    return 0;
}

/**
 * Determine file type by analyzing file path/extension
 * 
 * @param filePath - Path to file to analyze
 * @return BAEFileType - Detected file type based on extension or BAE_INVALID_TYPE if unknown
 */
BAEFileType X_DetermineFileTypeByPath(const char *filePath)
{
    if (filePath == NULL)
        return BAE_INVALID_TYPE;
    
    // Find the last dot for extension
    const char *ext = strrchr(filePath, '.');
    if (!ext)
    {
        BAE_PRINTF("[FileType] No extension found in path\n");
        return BAE_INVALID_TYPE;
    }
    
    BAE_PRINTF("[FileType] Found extension: %s\n", ext);
    
    // Convert extension to lowercase for comparison
    char extLower[16] = {0};
    int i = 0;
    for (const char *p = ext; *p && i < sizeof(extLower) - 1; ++p, ++i)
    {
        if (*p >= 'A' && *p <= 'Z')
            extLower[i] = *p + ('a' - 'A');
        else
            extLower[i] = *p;
    }
    
    // Check for audio file extensions
    if (strcmp(extLower, ".wav") == 0)
        return BAE_WAVE_TYPE;
    else if (strcmp(extLower, ".aif") == 0 || strcmp(extLower, ".aiff") == 0)
        return BAE_AIFF_TYPE;
    else if (strcmp(extLower, ".au") == 0)
        return BAE_AU_TYPE;
    else if (strcmp(extLower, ".mp2") == 0 || strcmp(extLower, ".mp3") == 0)
        return BAE_MPEG_TYPE;
#if USE_FLAC_DECODER == TRUE
    else if (strcmp(extLower, ".flac") == 0)
        return BAE_FLAC_TYPE;
#endif
#if USE_VORBIS_DECODER == TRUE        
    else if (strcmp(extLower, ".ogg") == 0 || strcmp(extLower, ".oga") == 0)
        return BAE_VORBIS_TYPE;
#endif
    // Check for MIDI/music file extensions
    else if (strcmp(extLower, ".mid") == 0 || strcmp(extLower, ".midi") == 0)
        return BAE_MIDI_TYPE;
    else if (strcmp(extLower, ".rmf") == 0)
        return BAE_RMF;
    else if (strcmp(extLower, ".rmi") == 0)
        return BAE_RMI;        
    else if (strcmp(extLower, ".xmf") == 0 || strcmp(extLower, ".mxmf") == 0)
        return BAE_MIDI_TYPE; // XMF files contain MIDI data
    
    return BAE_INVALID_TYPE;
}

/**
 * Determine file type by trying extension-based detection first, 
 * then falling back to content-based detection if needed
 * 
 * @param filePath - Path to file to analyze
 * @return BAEFileType - Detected file type or BAE_INVALID_TYPE if unknown/error
 */
BAEFileType X_DetermineFileType(const char *filePath)
{
    if (filePath == NULL)
        return BAE_INVALID_TYPE;
    
    BAE_PRINTF("[FileType] Detecting type for: %s\n", filePath);
    
    // Try extension-based detection first (fast)
    BAEFileType result = X_DetermineFileTypeByPath(filePath);
    BAE_PRINTF("[FileType] Extension-based detection result: %s\n", X_GetFileTypeString(result));
    
    // If extension-based detection failed, try content-based detection
    if (result == BAE_INVALID_TYPE)
    {
        XFILENAME fileName;
        XFILE fileRef;
        
        // Convert path to XFILENAME and open file
        XConvertPathToXFILENAME((void *)filePath, &fileName);
        
        fileRef = XFileOpenForRead(&fileName);
        
        if (fileRef != 0)
        {
            unsigned char buffer[FILETYPE_PROBE_SIZE];
            uint32_t originalPosition;
            int32_t bytesRead = 0;
            
            // Save current file position
            originalPosition = XFileGetPosition(fileRef);
            
            // Read from beginning of file
            XFileSetPosition(fileRef, 0L);
            
            // Initialize buffer
            memset(buffer, 0, FILETYPE_PROBE_SIZE);
            
            XERR readResult = XFileRead(fileRef, buffer, FILETYPE_PROBE_SIZE);
            
            if (readResult == 0) // NO_ERR = 0, success
            {
                bytesRead = FILETYPE_PROBE_SIZE;
            }
            else
            {
                // Try a smaller read if the first one failed
                XFileSetPosition(fileRef, 0L);
                readResult = XFileRead(fileRef, buffer, 4);
                if (readResult == 0)
                {
                    bytesRead = 4;
                }
            }
            
            // Restore file position
            XFileSetPosition(fileRef, originalPosition);
            
            if (bytesRead >= 4)
            {
                result = X_DetermineFileTypeByData(buffer, bytesRead);
                BAE_PRINTF("[FileType] Content-based detection result: %s\n", X_GetFileTypeString(result));
            }
            else
            {
                BAE_PRINTF("[FileType] Failed to read enough data for content detection\n");
            }
            
            XFileClose(fileRef);
        }
        else
        {
            BAE_PRINTF("[FileType] Failed to open file for content detection\n");
        }
    }
    
    return result;
}


/**
 * Determine file type by analyzing raw data buffer
 * 
 * @param data - Raw data buffer to analyze
 * @param length - Length of data buffer in bytes
 * @return BAEFileType - Detected file type or BAE_INVALID_TYPE if unknown
 */
BAEFileType X_DetermineFileTypeByData(const unsigned char *data, int32_t length)
{
    uint32_t fourcc;
    uint32_t offset = 0;
    
    if (data == NULL || length < 4)
    {
        BAE_PRINTF("[FileType] Invalid data buffer or insufficient length (%d bytes)\n", length);
        return BAE_INVALID_TYPE;
    }
    
    // Read the first FOURCC
    fourcc = PV_ReadBigEndian32(data);
    // Skip leading null bytes (up to 1024 bytes)
    if (fourcc == 0)
    {
        
        offset += 4;
        while (offset + 4 <= length && offset < 1024)
        {
            fourcc = PV_ReadBigEndian32(&data[offset]);
            if (fourcc != 0) {
                // read next bytes until we get a full FOURCC
                while (data[offset] == 0) {
                    offset++;
                }
                fourcc = PV_ReadBigEndian32(&data[offset]);
                break;
            }                
            offset += 4;
        }
        
        // If we still have no valid FOURCC, give up
        if (fourcc == 0)
        {
            BAE_PRINTF("[FileType] No valid FOURCC found in first 1024 bytes\n");
            return BAE_INVALID_TYPE;
        }
        
        BAE_PRINTF("[FileType] Found non-zero FOURCC at offset %d: 0x%08X\n", offset, fourcc);
    }
        
    // Check primary magic signatures
    switch (fourcc)
    {
        case BAE_FOURCC_MIDI:
            return BAE_MIDI_TYPE;
            
        case BAE_FOURCC_RMF:
            return BAE_RMF;
#if USE_XMF_SUPPORT == TRUE            
        case BAE_FOURCC_XMF:
            // Could be XMF or MXMF, both are handled the same way
            return BAE_XMF;
#endif            
        case BAE_FOURCC_RIFF:
            return PV_DetectRIFFType(data, length);
            
        case BAE_FOURCC_FORM:
            // IFF container, check subtype
            if (length >= 12)
            {
                uint32_t subtype = PV_ReadBigEndian32(&data[8]);
                if (subtype == BAE_FOURCC_AIFF)
                    return BAE_AIFF_TYPE;
            }
            return BAE_AIFF_TYPE; // Assume AIFF if we can't determine subtype
            
        case BAE_FOURCC_AU:
            return BAE_AU_TYPE;
            
#if USE_FLAC_DECODER == TRUE || USE_FLAC_ENCODER == TRUE        
        case BAE_FOURCC_FLAC:
            return BAE_FLAC_TYPE;
#endif
#if USE_OGG_FORMAT == TRUE            
        case BAE_FOURCC_OGGS:
            return PV_DetectOGGType(data, length);
#endif

        default:
 #if USE_MPEG_DECODER == TRUE
            // Check for MPEG audio (MP2/MP3)
            if (PV_IsLikelyMPEGHeader(&data[offset]))
            {
                return BAE_MPEG_TYPE;
            }
            
            // Check for ID3v1 tag at the end (less reliable, but possible)
            if (data[offset] == 'T' && data[offset+1] == 'A' && data[offset+2] == 'G')
            {
                // This could be an MP3 with ID3v1 tag at the beginning
                // (unusual but not impossible)
                return BAE_MPEG_TYPE;
            }
#endif            
            break;
    }
    
    return BAE_INVALID_TYPE;
}

/**
 * Determine file type by analyzing file contents
 * 
 * @param filePath - Path to file to analyze
 * @return BAEFileType - Detected file type or BAE_INVALID_TYPE if unknown/error
 */
BAEFileType X_DetermineFileTypeFromPath(const char *filePath)
{
    XFILENAME fileName;
    XFILE fileRef;
    BAEFileType result = BAE_INVALID_TYPE;
    
    if (filePath == NULL)
        return BAE_INVALID_TYPE;
    
    // Convert path to XFILENAME
    XConvertPathToXFILENAME((void *)filePath, &fileName);
    
    // Open file for reading
    fileRef = XFileOpenForRead(&fileName);
    if (fileRef == 0)
        return BAE_INVALID_TYPE;
    
    // Read file data for analysis
    unsigned char buffer[FILETYPE_PROBE_SIZE];
    int32_t bytesRead = 0;
    
    // Initialize buffer
    memset(buffer, 0, FILETYPE_PROBE_SIZE);
    
    XERR readResult = XFileRead(fileRef, buffer, FILETYPE_PROBE_SIZE);
    
    if (readResult == 0) // NO_ERR = 0, success
    {
        bytesRead = FILETYPE_PROBE_SIZE;
    }
    else
    {
        // Try a smaller read if the first one failed
        XFileSetPosition(fileRef, 0L);
        readResult = XFileRead(fileRef, buffer, 4);
        if (readResult == 0)
        {
            bytesRead = 4;
        }
    }
    
    if (bytesRead >= 4)
    {
        // Determine type from file contents
        result = X_DetermineFileTypeByData(buffer, bytesRead);
    }
    
    // Close file
    XFileClose(fileRef);
    
    return result;
}

/**
 * Get a string representation of a file type
 * 
 * @param fileType - BAEFileType to convert to string
 * @return const char* - String representation of file type
 */
const char *X_GetFileTypeString(BAEFileType fileType)
{
    switch (fileType)
    {
        case BAE_MIDI_TYPE:     return "MIDI";
        case BAE_RMF:           return "RMF";
        case BAE_RMI:           return "RMI";
#if USE_XMF_SUPPORT == TRUE
        case BAE_XMF:           return "XMF";
#endif        
        case BAE_AIFF_TYPE:     return "AIFF";
        case BAE_WAVE_TYPE:     return "WAVE";
        case BAE_MPEG_TYPE:     return "MPEG";
        case BAE_AU_TYPE:       return "AU";
#if USE_FLAC_DECODER == TRUE || USE_FLAC_ENCODER == TRUE        
        case BAE_FLAC_TYPE:     return "FLAC";
#endif
#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE        
        case BAE_VORBIS_TYPE:   return "Vorbis";
#endif
        case BAE_GROOVOID:      return "Groovoid";
        case BAE_RAW_PCM:       return "Raw PCM";
        case BAE_INVALID_TYPE:  return "Unknown";
        default:                return "Invalid";
    }
}

/**
 * Convert a file type string constant to BAEFileType enum
 * This is useful for legacy compatibility with X_FILETYPE_* constants
 * 
 * @param typeString - String constant like X_FILETYPE_MIDI ("MThd")
 * @return BAEFileType - Corresponding file type enum
 */
BAEFileType X_ConvertFileTypeString(const char *typeString)
{
    if (typeString == NULL)
        return BAE_INVALID_TYPE;
    
    if (strcmp(typeString, X_FILETYPE_MIDI) == 0)
        return BAE_MIDI_TYPE;
    else if (strcmp(typeString, X_FILETYPE_RMF) == 0)
        return BAE_RMF;
#if USE_XMF_SUPPORT == TRUE
    else if (strcmp(typeString, X_FILETYPE_XMF) == 0)
        return BAE_XMF;
#endif        
    else if (strcmp(typeString, X_FILETYPE_AIFF) == 0)
        return BAE_AIFF_TYPE;
    else if (strcmp(typeString, X_FILETYPE_WAVE) == 0)
        return BAE_WAVE_TYPE;
    
    return BAE_INVALID_TYPE;
}
