/*****************************************************************************/
/*
** "GenRMI.h"
**
** RMI (RIFF MIDI) file format parser with DLS support - Header
**
****************************************************************************/

#ifndef GEN_RMI_H
#define GEN_RMI_H

#include "X_API.h"

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE

/**
 * GM_LoadRMIFromMemory
 * 
 * Load an RMI (RIFF MIDI) file from memory, extracting both the MIDI data
 * and any embedded DLS soundbank.
 * 
 * @param buf                  Pointer to RMI file data in memory
 * @param len                  Length of the RMI file data
 * @param outMidiData          Pointer to receive allocated MIDI data buffer
 *                             (caller must free with XDisposePtr)
 * @param outMidiLen           Pointer to receive MIDI data length
 * @param loadDLS              If TRUE, attempt to load embedded DLS data
 * @return OPErr error code (NO_ERR on success)
 */
OPErr GM_LoadRMIFromMemory(const unsigned char *buf, uint32_t len,
                           unsigned char **outMidiData, uint32_t *outMidiLen,
                           XBOOL loadDLS);

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
XBOOL GM_IsRMIFile(const unsigned char *buf, uint32_t len);

#endif // USE_FULL_RMF_SUPPORT || USE_CREATION_API

#endif // GEN_RMI_H
