/*
    Copyright (c) 2025 miniBAE Project
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    
    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    
    Neither the name of the miniBAE Project nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.
    
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
    TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*****************************************************************************/
/*
**  XVorbisFiles.h
**
**  Header for Ogg Vorbis audio file support in miniBAE
*/
/*****************************************************************************/

#ifndef __X_VORBIS_FILES__
#define __X_VORBIS_FILES__

#include "X_API.h"

#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE

#ifdef __cplusplus
extern "C" {
#endif

#if USE_VORBIS_DECODER == TRUE

// Vorbis decoder functions
XBOOL XIsVorbisFile(XFILE file);
void* XOpenVorbisFile(XFILE file);
OPErr XGetVorbisFileInfo(void *decoder_handle, UINT32 *samples, UINT32 *sample_rate, 
                        UINT32 *channels, UINT32 *bit_depth);
long XDecodeVorbisFile(void *decoder_handle, void *buffer, long buffer_size);
void XCloseVorbisFile(void *decoder_handle);

#endif // USE_VORBIS_DECODER

#if USE_VORBIS_ENCODER == TRUE

// Vorbis encoder functions
void* XInitVorbisEncoder(UINT32 sample_rate, UINT32 channels, float quality);
long XWriteVorbisHeader(void *encoder_handle, XFILE output_file);
long XEncodeVorbisData(void *encoder_handle, float **pcm_data, long samples, XFILE output_file);
void XCloseVorbisEncoder(void *encoder_handle);

#endif // USE_VORBIS_ENCODER

#ifdef __cplusplus
}
#endif

#endif // USE_VORBIS_DECODER || USE_VORBIS_ENCODER

#endif // __X_VORBIS_FILES__
