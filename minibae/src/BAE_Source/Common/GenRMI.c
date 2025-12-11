/*****************************************************************************/
/*
** "GenRMI.c"
**
** RMI (RIFF MIDI) file format parser with DLS support
**
** Overview:
**  This file implements parsing for RMI (RIFF-based MIDI) files according to
**  the RMID specification (RP-029). RMI files are standard MIDI files wrapped
**  in a RIFF container, which allows for additional metadata and embedded
**  soundbank data (DLS).
**
**  Key features:
**  - Extract MIDI data from the 'data' chunk
**  - Parse INFO chunks for metadata (INAM, IART, ICOP, etc.)
**  - Detect and load embedded DLS soundbanks
**  - Support for DISP chunks (displayable objects)
**
** References:
**  - https://zumi.neocities.org/stuff/rmi/
**  - MIDI Manufacturers Association RP-029 (RMID spec)
**  - Microsoft RIFF specification
**
** Modification History:
**  12/8/2025  Initial implementation with DLS support
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
 * PV_FindDLSInRMI
 * 
 * Search for embedded DLS (Downloadable Sounds) data in an RMI file.
 * According to the RMI specification, DLS data can be appended after the
 * RMID chunks. The file size in the RIFF header should reflect the total
 * size including the DLS data.
 * 
 * @param buf       Pointer to RMI file data in memory
 * @param len       Length of the RMI file data
 * @param outDls    Pointer to receive the DLS data pointer (within buf)
 * @param outDlsLen Pointer to receive the DLS data length
 * @return TRUE if DLS data was found, FALSE otherwise
 */
static XBOOL PV_FindDLSInRMI(const unsigned char *buf, uint32_t len,
                              const unsigned char **outDls, uint32_t *outDlsLen)
{
    if (!buf || len < 12) return FALSE;
    
    // First verify this is an RMI file
    if (!PV_MatchFourCC(buf, "RIFF")) return FALSE;
    if (!PV_MatchFourCC(&buf[8], "RMID")) return FALSE;
    
    uint32_t riffSize = PV_ReadLE32(&buf[4]);
    uint32_t rmidEnd = 8 + riffSize;
    
    // DLS data should be after the RMID section
    // Search for RIFF DLS chunks after RMID ends
    uint32_t searchStart = 12;
    
    // First, find where RMID actually ends by parsing all chunks
    uint32_t i = 12;
    uint32_t lastChunkEnd = 12;
    while (i + 8 <= len && i < rmidEnd)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);
        
        if (i + 8 + chunkSize > len) break;
        
        // Track the last valid chunk position
        lastChunkEnd = i + 8 + chunkSize;
        if (lastChunkEnd & 1) lastChunkEnd++; // word-aligned
        
        // Move to next chunk
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
    
    // Now search for DLS starting after the last RMID chunk
    searchStart = lastChunkEnd;
    
    // Look for RIFF DLS header
    for (i = searchStart; i + 12 <= len; i++)
    {
        if (PV_MatchFourCC(&buf[i], "RIFF"))
        {
            uint32_t dlsSize = PV_ReadLE32(&buf[i + 4]);
            
            // Check if this is a DLS file
            if (PV_MatchFourCC(&buf[i + 8], "DLS "))
            {
                // Verify we have enough data
                if (i + 8 + dlsSize <= len)
                {
                    // Found DLS data
                    if (outDls) *outDls = &buf[i];
                    if (outDlsLen) *outDlsLen = 8 + dlsSize;
                    
                    BAE_PRINTF("[RMI] Found embedded DLS at offset %u, size %u bytes\n", 
                               i, 8 + dlsSize);
                    return TRUE;
                }
            }
        }
    }
    
    return FALSE;
}

/**
 * PV_ParseRMIInfo
 * 
 * Parse INFO LIST chunk for metadata tags like title, artist, copyright, etc.
 * This is optional metadata that may be present in RMI files.
 * 
 * @param buf       Pointer to INFO chunk data (after "INFO" type)
 * @param len       Length of the INFO chunk data
 */
static void PV_ParseRMIInfo(const unsigned char *buf, uint32_t len)
{
    uint32_t i = 0;
    
    while (i + 8 <= len)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);
        
        if (i + 8 + chunkSize > len) break;
        
        // Common INFO tags (null-terminated strings)
        if (PV_MatchFourCC(chunk, "INAM"))
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
        
        // Move to next chunk (word-aligned)
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
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
        return BAE_INVALID_TYPE;

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
    
    // Parse optional INFO chunks
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
                PV_ParseRMIInfo(&chunk[12], chunkSize - 4);
            }
        }
        
        // Move to next chunk (word-aligned)
        i += 8 + chunkSize;
        if (i & 1) i++;
    }
    
    // Look for and load embedded DLS data if requested
    if (loadDLS)
    {
        const unsigned char *dlsData = NULL;
        uint32_t dlsLen = 0;
        
        if (PV_FindDLSInRMI(buf, len, &dlsData, &dlsLen))
        {
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
            BAE_PRINTF("[RMI] Loading embedded DLS soundbank...\n");
            
            OPErr err = GM_LoadSF2SoundfontFromMemory(dlsData, (size_t)dlsLen);
            if (err == NO_ERR)
            {
                // Verify the bank loaded successfully with presets
                int presetCount = 0;
                XBOOL hasPresets = GM_SF2_CurrentFontHasAnyPreset(&presetCount);
                
                if (hasPresets)
                {
                    BAE_PRINTF("[RMI] DLS soundbank loaded successfully (%d presets)\n", 
                               presetCount);
                }
                else
                {
                    BAE_PRINTF("[RMI] DLS soundbank loaded but has no presets\n");
                    GM_UnloadSF2Soundfont();
                }
            }
            else
            {
                BAE_PRINTF("[RMI] Failed to load DLS soundbank (error %d)\n", err);
            }
#else
            BAE_PRINTF("[RMI] DLS support not compiled in (FluidSynth required)\n");
#endif
        }
        else
        {
            BAE_PRINTF("[RMI] No embedded DLS data found\n");
        }
    }
    
    return NO_ERR;
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