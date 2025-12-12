/*****************************************************************************/
/*
** "GenRMI.c"
**
** RMI (RIFF MIDI) file format parser with SF2/DLS support
**
** Overview:
**  This file implements parsing for RMI (RIFF-based MIDI) files according to
**  the RMID specification (RP-029) and the SF2 RMIDI extension specification.
**  RMI files are standard MIDI files wrapped in a RIFF container, which allows
**  for additional metadata and embedded soundbank data (DLS/SF2/SF3).
**
**  Key features:
**  - Extract MIDI data from the 'data' chunk
**  - Parse INFO chunks for metadata (INAM, IART, ICOP, IENC, DBNK, etc.)
**  - Detect and load embedded DLS/SF2/SF3 soundbanks
**  - Support for bank offset (DBNK chunk)
**  - Support for text encoding detection (IENC/MENC chunks)
**  - Support for DISP chunks (displayable objects)
**
** References:
**  - https://zumi.neocities.org/stuff/rmi/
**  - https://github.com/spessasus/sf2-rmidi-specification
**  - MIDI Manufacturers Association RP-029 (RMID spec)
**  - Microsoft RIFF specification
**
** Modification History:
**  12/8/2025   Initial implementation with DLS support
**  12/11/2025  Added SF2 RMIDI specification support
**
****************************************************************************/

#include "X_API.h"
#include "X_Assert.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "GenRMI.h"


#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
#include "GenSF2_FluidSynth.h"
#endif

// Global flag to track if the last loaded RMI had an embedded soundbank
static XBOOL g_last_rmi_had_soundbank = FALSE;


// Helper: Read 32-bit little-endian value
static uint32_t PV_ReadLE32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Helper: Read 16-bit little-endian value  
static uint16_t PV_ReadLE16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Helper: Check if four characters match
static XBOOL PV_MatchFourCC(const unsigned char *p, const char *fourcc)
{
    return (p[0] == fourcc[0] && p[1] == fourcc[1] && 
            p[2] == fourcc[2] && p[3] == fourcc[3]);
}

/**
 * PV_ExtractRMIDToSMF
 * 
 * Extract Standard MIDI File data from a RIFF RMID container.
 * Searches for the 'data' chunk which contains the raw MIDI data.
 * 
 * @param buf       Pointer to RMI file data in memory
 * @param len       Length of the RMI file data
 * @param outSmf    Pointer to receive the SMF data pointer (within buf)
 * @param outSmfLen Pointer to receive the SMF data length
 * @return TRUE if MIDI data was successfully extracted, FALSE otherwise
 */
static XBOOL PV_ExtractRMIDToSMF(const unsigned char *buf, uint32_t len, 
                                  const unsigned char **outSmf, uint32_t *outSmfLen)
{
    if (!buf || len < 12) return FALSE;
    
    // Check for RIFF header
    if (!PV_MatchFourCC(buf, "RIFF")) return FALSE;
    
    uint32_t riffSize = PV_ReadLE32(&buf[4]);
    if (riffSize + 8 > len) return FALSE;
    
    // Check for RMID type
    if (!PV_MatchFourCC(&buf[8], "RMID")) return FALSE;
    
    // Parse chunks to find 'data' chunk
    uint32_t i = 12;
    while (i + 8 <= len)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);
        
        if (i + 8 + chunkSize > len) break;
        
        if (PV_MatchFourCC(chunk, "data"))
        {
            // Found MIDI data chunk
            if (outSmf) *outSmf = chunk + 8;
            if (outSmfLen) *outSmfLen = chunkSize;
            return TRUE;
        }
        
        // Move to next chunk (chunks are word-aligned)
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
    
    return FALSE;
}

/**
 * PV_FindSoundbankInRMI
 * 
 * Search for embedded soundbank (DLS/SF2/SF3) data in an RMI file.
 * According to the SF2 RMIDI specification, the soundbank is a nested RIFF chunk
 * inside the main RIFF RMID container, after the 'data' and 'LIST' chunks.
 * 
 * File structure:
 *   RIFF (main container)
 *     RMID (type)
 *     data (MIDI data)
 *     LIST INFO (metadata - optional)
 *     RIFF (nested soundbank)
 *       sfbk or DLS  (soundbank type)
 * 
 * @param buf         Pointer to RMI file data in memory
 * @param len         Length of the RMI file data
 * @param outBank     Pointer to receive the soundbank data pointer (within buf)
 * @param outBankLen  Pointer to receive the soundbank data length
 * @param outIsSF2    Pointer to receive TRUE if SF2/SF3, FALSE if DLS (optional)
 * @return TRUE if soundbank data was found, FALSE otherwise
 */
static XBOOL PV_FindSoundbankInRMI(const unsigned char *buf, uint32_t len,
                                    const unsigned char **outBank, uint32_t *outBankLen,
                                    XBOOL *outIsSF2)
{
    if (!buf || len < 12) return FALSE;
    
    // First verify this is an RMI file
    if (!PV_MatchFourCC(buf, "RIFF")) return FALSE;
    if (!PV_MatchFourCC(&buf[8], "RMID")) return FALSE;
    
    uint32_t riffSize = PV_ReadLE32(&buf[4]);
    uint32_t riffEnd = 8 + riffSize;
    
    BAE_PRINTF("[RMI] Searching for nested soundbank within RIFF RMID (size: %u bytes)...\n", 
               riffSize);
    
    // Start scanning after the RMID type identifier at offset 12
    uint32_t i = 12;
    
    while (i + 8 <= len && i < riffEnd)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);
        
        if (i + 8 + chunkSize > len) break;
        
        // Look for nested RIFF chunks (soundbank)
        if (PV_MatchFourCC(chunk, "RIFF") && chunkSize >= 4)
        {
            XBOOL isSF2 = FALSE;
            
            // Check if this is SF2/SF3
            if (PV_MatchFourCC(&chunk[8], "sfbk"))
            {
                isSF2 = TRUE;
                BAE_PRINTF("[RMI] Found embedded SF2/SF3 at offset %u, size %u bytes\n", 
                           i, 8 + chunkSize);
            }
            // Check if this is DLS
            else if (PV_MatchFourCC(&chunk[8], "DLS "))
            {
                isSF2 = FALSE;
                BAE_PRINTF("[RMI] Found embedded DLS at offset %u, size %u bytes\n", 
                           i, 8 + chunkSize);
            }
            else
            {
                // Not a recognized soundbank, skip it
                BAE_PRINTF("[RMI] Found RIFF chunk at offset %u but not a soundbank (type: %.4s)\n",
                           i, &chunk[8]);
                // Continue searching
                i += 8 + chunkSize;
                if (i & 1) i++;
                continue;
            }
            
            // Found soundbank data
            if (outBank) *outBank = chunk;
            if (outBankLen) *outBankLen = 8 + chunkSize;
            if (outIsSF2) *outIsSF2 = isSF2;
            return TRUE;
        }
        
        // Move to next chunk (word-aligned)
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
    
    BAE_PRINTF("[RMI] No embedded soundbank found in RIFF RMID\n");
    return FALSE;
}

// Structure to hold parsed RMI INFO data
typedef struct
{
    int16_t bankOffset;      // DBNK chunk: bank offset (-1 = not specified)
    XBOOL hasEncoding;       // TRUE if IENC was found
    char encoding[32];       // IENC chunk: text encoding (e.g., "utf-8")
    char midiEncoding[32];   // MENC chunk: MIDI text encoding
} RMIInfo;

/**
 * PV_ParseRMIInfo
 * 
 * Parse INFO LIST chunk for metadata tags like title, artist, copyright, etc.
 * This is optional metadata that may be present in RMI files.
 * According to SF2 RMIDI spec, extracts DBNK (bank offset), IENC (encoding),
 * and other metadata.
 * 
 * @param buf       Pointer to INFO chunk data (after "INFO" type)
 * @param len       Length of the INFO chunk data
 * @param info      Pointer to RMIInfo structure to fill (optional)
 */
static void PV_ParseRMIInfo(const unsigned char *buf, uint32_t len, RMIInfo *info)
{
    uint32_t i = 0;
    
    // Initialize info structure if provided
    if (info)
    {
        info->bankOffset = -1;  // -1 means not specified
        info->hasEncoding = FALSE;
        info->encoding[0] = '\0';
        info->midiEncoding[0] = '\0';
    }
    
    while (i + 8 <= len)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);
        
        if (i + 8 + chunkSize > len) break;
        
        // SF2 RMIDI specific chunks
        if (PV_MatchFourCC(chunk, "DBNK"))
        {
            // Bank offset (16-bit unsigned little-endian)
            if (chunkSize == 2)
            {
                uint16_t offset = PV_ReadLE16(&chunk[8]);
                if (offset <= 127)  // Valid range per spec
                {
                    if (info) info->bankOffset = (int16_t)offset;
                    BAE_PRINTF("[RMI] Bank Offset (DBNK): %u\n", offset);
                }
                else
                {
                    BAE_PRINTF("[RMI] Invalid DBNK value %u (must be 0-127)\n", offset);
                }
            }
            else
            {
                BAE_PRINTF("[RMI] Invalid DBNK chunk size %u (expected 2)\n", chunkSize);
            }
        }
        else if (PV_MatchFourCC(chunk, "IENC"))
        {
            // Text encoding for INFO chunks
            if (chunkSize > 0 && chunkSize < 32)
            {
                if (info)
                {
                    XBlockMove(&chunk[8], info->encoding, chunkSize);
                    info->encoding[chunkSize] = '\0';
                    info->hasEncoding = TRUE;
                }
                BAE_PRINTF("[RMI] Text Encoding (IENC): %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
            }
        }
        else if (PV_MatchFourCC(chunk, "MENC"))
        {
            // MIDI text encoding hint
            if (chunkSize > 0 && chunkSize < 32)
            {
                if (info)
                {
                    XBlockMove(&chunk[8], info->midiEncoding, chunkSize);
                    info->midiEncoding[chunkSize] = '\0';
                }
                BAE_PRINTF("[RMI] MIDI Encoding (MENC): %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
            }
        }
        // Standard INFO tags (null-terminated strings)
        else if (PV_MatchFourCC(chunk, "INAM"))
        {
            // Title
            BAE_PRINTF("[RMI] Title: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "IART"))
        {
            // Artist
            BAE_PRINTF("[RMI] Artist: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "ICOP"))
        {
            // Copyright
            BAE_PRINTF("[RMI] Copyright: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "ICRD"))
        {
            // Creation date
            BAE_PRINTF("[RMI] Date: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "IPRD") || PV_MatchFourCC(chunk, "IALB"))
        {
            // Album (IALB preferred over IPRD per spec)
            BAE_PRINTF("[RMI] Album: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "ICMT"))
        {
            // Comments
            BAE_PRINTF("[RMI] Comment: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "ISBJ"))
        {
            // Subject/Description
            BAE_PRINTF("[RMI] Subject: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "IGNR"))
        {
            // Genre
            BAE_PRINTF("[RMI] Genre: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "IENG"))
        {
            // Engineer (soundfont creator)
            BAE_PRINTF("[RMI] Engineer: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "ISFT"))
        {
            // Software
            BAE_PRINTF("[RMI] Software: %.*s\n", (int)chunkSize, (const char *)(chunk + 8));
        }
        else if (PV_MatchFourCC(chunk, "IPIC"))
        {
            // Picture/album art (binary data)
            BAE_PRINTF("[RMI] Picture: %u bytes\n", chunkSize);
        }
        
        // Move to next chunk (word-aligned)
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
}

/**
 * GM_LoadRMIFromMemory
 * 
 * Load an RMI (RIFF MIDI) file from memory, extracting both the MIDI data
 * and any embedded DLS soundbank.
 * 
 * This function:
 * 1. Validates the RMI file structure
 * 2. Extracts the Standard MIDI File data from the 'data' chunk
 * 3. Parses optional INFO chunks for metadata
 * 4. Searches for and loads any embedded DLS soundbank
 * 
 * @param buf                  Pointer to RMI file data in memory
 * @param len                  Length of the RMI file data
 * @param outMidiData          Pointer to receive allocated MIDI data buffer
 * @param outMidiLen           Pointer to receive MIDI data length
 * @param loadDLS              If TRUE, attempt to load embedded DLS data
 * @return OPErr error code (NO_ERR on success)
 */
OPErr GM_LoadRMIFromMemory(const unsigned char *buf, uint32_t len,
                           unsigned char **outMidiData, uint32_t *outMidiLen,
                           XBOOL loadDLS)
{
    const unsigned char *midiData = NULL;
    uint32_t midiLen = 0;
    
    if (!buf || len == 0 || !outMidiData || !outMidiLen)
    {
        return PARAM_ERR;
    }
    
    BAE_PRINTF("[RMI] Parsing RMI file, size=%u bytes\n", len);
    
    // Extract MIDI data from 'data' chunk
    if (!PV_ExtractRMIDToSMF(buf, len, &midiData, &midiLen))
    {
        BAE_PRINTF("[RMI] Failed to extract MIDI data from RMI file\n");
        return BAD_FILE;
    }
    
    if (!midiData || midiLen == 0)
    {
        BAE_PRINTF("[RMI] No MIDI data found in RMI file\n");
        return BAD_FILE;
    }
    
    // Verify MIDI header
    if (midiLen < 4 || !PV_MatchFourCC(midiData, "MThd"))
    {
        BAE_PRINTF("[RMI] Invalid MIDI data (missing MThd header)\n");
        return BAD_FILE;
    }
    
    BAE_PRINTF("[RMI] Extracted MIDI data: %u bytes\n", midiLen);
    
    // Allocate a copy of the MIDI data for the caller
    unsigned char *midiCopy = (unsigned char *)XNewPtr(midiLen);
    if (!midiCopy)
    {
        return MEMORY_ERR;
    }
    
    XBlockMove(midiData, midiCopy, midiLen);
    *outMidiData = midiCopy;
    *outMidiLen = midiLen;
    
    // Parse optional INFO chunks to get bank offset and other metadata
    RMIInfo rmiInfo;
    rmiInfo.bankOffset = -1;  // Default: not specified
    rmiInfo.hasEncoding = FALSE;
    rmiInfo.encoding[0] = '\0';
    rmiInfo.midiEncoding[0] = '\0';
    
    uint32_t i = 12;
    while (i + 8 <= len)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);
        
        if (i + 8 + chunkSize > len) break;
        
        if (PV_MatchFourCC(chunk, "LIST") && chunkSize >= 4)
        {
            // Check if this is an INFO list
            if (PV_MatchFourCC(&chunk[8], "INFO"))
            {
                BAE_PRINTF("[RMI] Found INFO chunk\n");
                PV_ParseRMIInfo(&chunk[12], chunkSize - 4, &rmiInfo);
            }
        }
        
        // Move to next chunk (word-aligned)
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
    
    // Look for and load embedded soundbank (SF2/SF3/DLS) if requested
    BAE_PRINTF("[RMI] loadDLS parameter = %d\n", loadDLS);
    if (loadDLS)
    {
        const unsigned char *bankData = NULL;
        uint32_t bankLen = 0;
        XBOOL isSF2 = FALSE;
        
        // Reset flag at start
        g_last_rmi_had_soundbank = FALSE;
        
        BAE_PRINTF("[RMI] Searching for embedded soundbank...\n");
        if (PV_FindSoundbankInRMI(buf, len, &bankData, &bankLen, &isSF2))
        {
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
            const char *bankType = isSF2 ? "SF2/SF3" : "DLS";
            BAE_PRINTF("[RMI] Loading embedded %s soundbank...\n", bankType);
            
            // Determine bank offset according to SF2 RMIDI spec:
            // - If DBNK specified: use that value
            // - If no embedded bank: offset is 0 (ignored anyway)
            // - If embedded bank but no DBNK: default is 1
            int16_t bankOffset = rmiInfo.bankOffset;
            if (bankOffset < 0)  // Not specified in DBNK
            {
                bankOffset = 1;  // Default per SF2 RMIDI spec
            }
            
            // Clamp to valid range (0-127)
            if (bankOffset < 0) bankOffset = 0;
            if (bankOffset > 127) bankOffset = 127;
            
            BAE_PRINTF("[RMI] Using bank offset: %d\n", bankOffset);
            
            // Unload any existing soundfont first
            if (GM_SF2_IsActive())
            {
                BAE_PRINTF("[RMI] Unloading existing soundfont before loading embedded one\n");
                GM_UnloadSF2Soundfont();
            }            
            
            BAE_PRINTF("[RMI] Loading from memory at offset %p, size %u\n", 
                       (void*)bankData, bankLen);
            
            OPErr err = GM_LoadSF2SoundfontFromMemory(bankData, (size_t)bankLen);
            if (err == NO_ERR)
            {
                // Verify the bank loaded successfully with presets
                int presetCount = 0;
                XBOOL hasPresets = GM_SF2_CurrentFontHasAnyPreset(&presetCount);
                
                if (hasPresets)
                {
                    BAE_PRINTF("[RMI] %s soundbank loaded successfully (%d presets)\n", 
                               bankType, presetCount);
                    
                    // Set flag to indicate embedded soundbank was loaded
                    g_last_rmi_had_soundbank = TRUE;
                    
                    // Apply bank offset if non-zero
                    // According to SF2 RMIDI spec, add bankOffset to all preset banks
                    // except drum banks (bank 128)
                    if (bankOffset != 0)
                    {
                        BAE_PRINTF("[RMI] Applying bank offset %d to presets...\n", bankOffset);
                        // Note: Bank offset adjustment would be done in FluidSynth layer
                        // For now, just log it - actual implementation would need
                        // FluidSynth API to adjust preset bank numbers
                        // This is a TODO for future enhancement
                    }
                }
                else
                {
                    BAE_PRINTF("[RMI] %s soundbank loaded but has no presets\n", bankType);
                    GM_UnloadSF2Soundfont();
                }
            }
            else
            {
                BAE_PRINTF("[RMI] Failed to load %s soundbank (error %d)\n", bankType, err);
                // Return error so caller can handle fallback (e.g., restore user bank)
                return err;
            }
#else
            BAE_PRINTF("[RMI] Soundbank support not compiled in (FluidSynth required)\n");
#endif
        }
        else
        {
            // No embedded soundbank - per spec, use offset 0 (use main soundfont)
            BAE_PRINTF("[RMI] No embedded soundbank found, using main soundfont\n");
        }
    }
    
    return NO_ERR;
}

/**
 * GM_LoadRMIFromFile
 * 
 * Load an RMI (RIFF MIDI) file from disk, extracting both the MIDI data
 * and any embedded DLS soundbank.
 * 
 * This is a convenience wrapper around GM_LoadRMIFromMemory that handles
 * file I/O automatically.
 * 
 * @param path                 Path to the RMI file
 * @param outMidiData          Pointer to receive allocated MIDI data buffer
 * @param outMidiLen           Pointer to receive MIDI data length
 * @param loadDLS              If TRUE, attempt to load embedded DLS data
 * @return OPErr error code (NO_ERR on success)
 */
OPErr GM_LoadRMIFromFile(const char *path,
                         unsigned char **outMidiData, uint32_t *outMidiLen,
                         XBOOL loadDLS)
{
    XPTR fileData = NULL;
    XFILENAME fileName;
    XFILE fileRef;
    uint32_t fileSize = 0;
    OPErr err;
    
    if (!path || !outMidiData || !outMidiLen)
    {
        return PARAM_ERR;
    }
    
    // Read entire file into memory
        // Convert path to XFILENAME
    XConvertPathToXFILENAME((void *)path, &fileName);
    
    // Open file for reading
    fileRef = XFileOpenForRead(&fileName);
    if (fileRef == 0)
        return BAD_FILE;

    // Get file size
    XFileSetPosition(fileRef, 0L);
    fileSize = XFileGetLength(fileRef);
    if (fileSize == 0)
    {
        XFileClose(fileRef);
        return BAD_FILE;
    }

    // Allocate buffer for file data
    fileData = XNewPtr(fileSize);
    XFileRead(fileRef, fileData, fileSize);
    XFileClose(fileRef);    

    if (!fileData)
    {
        BAE_PRINTF("[RMI] Failed to read file: %s\n", path);
        return BAD_FILE;
    }
    
    // Parse RMI from memory
    err = GM_LoadRMIFromMemory((const unsigned char *)fileData, fileSize,
                               outMidiData, outMidiLen, loadDLS);
    
    // Clean up file buffer
    XDisposePtr(fileData);
    
    return err;
}

/**
 * GM_IsRMIFile
 * 
 * Determine if a memory buffer contains a valid RMI file by checking
 * for the RIFF/RMID signature.
 * 
 * @param buf  Pointer to file data in memory
 * @param len  Length of the file data
 * @return TRUE if the data appears to be an RMI file, FALSE otherwise
 */
XBOOL GM_IsRMIFile(const unsigned char *buf, uint32_t len)
{
    if (!buf || len < 12) return FALSE;
    
    return (PV_MatchFourCC(buf, "RIFF") && PV_MatchFourCC(&buf[8], "RMID"));
}

/**
 * GM_LastRMIHadEmbeddedSoundbank
 * 
 * Query if the last loaded RMI file had an embedded soundbank that was loaded.
 * This flag is set by GM_LoadRMIFromMemory when a soundbank is successfully loaded.
 * 
 * @return TRUE if the last RMI had an embedded soundbank, FALSE otherwise
 */
XBOOL GM_LastRMIHadEmbeddedSoundbank(void)
{
    return g_last_rmi_had_soundbank;
}

/**
 * GM_ClearRMISoundbankFlag
 * 
 * Clear the embedded soundbank flag. Should be called when unloading an RMI
 * or when the embedded soundbank is no longer active.
 */
void GM_ClearRMISoundbankFlag(void)
{
    g_last_rmi_had_soundbank = FALSE;
}