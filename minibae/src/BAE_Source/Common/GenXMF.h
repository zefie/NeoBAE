/*
 * GenXMF.h
 *
 * XMF/MXMF loader entry points.
 */

#pragma once

#include "MiniBAE.h"

#if USE_XMF_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE

// Loads the indicated BAESong from an XMF/MXMF container. Extracts the
// embedded Standard MIDI File and, if present, an embedded SF2/DLS bank to
// drive playback via the FluidSynth backend.
BAEResult BAESong_LoadXmfFromMemory(BAESong song,
                                  const void *data,
                                  uint32_t ulen,
                                  BAE_BOOL ignoreBadInstruments);

// A friendly wrapper to load from file path
BAEResult BAESong_LoadXmfFromFile(BAESong song,
                                  BAEPathName filePath,
                                  BAE_BOOL ignoreBadInstruments);

#endif // USE_XMF_SUPPORT && _USING_FLUIDSYNTH
