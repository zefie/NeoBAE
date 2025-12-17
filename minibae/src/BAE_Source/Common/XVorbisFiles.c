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
**  XVorbisFiles.c
**
**  Integration of libvorbis for Ogg Vorbis audio file support in miniBAE
**
**  This file provides encoding and decoding support for Ogg Vorbis audio files
**  using the reference libvorbis implementation.
*/
/*****************************************************************************/

#include "X_API.h"
#include "GenSnd.h"
#include "GenPriv.h"

#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE

#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#if USE_VORBIS_ENCODER == TRUE
#include <vorbis/vorbisenc.h>
#endif

// Structure to hold Vorbis decoder state
typedef struct {
    OggVorbis_File vf;
    vorbis_info *vi;
    INT32 current_section;
    XBOOL is_open;
} XVorbisDecoder;

// Structure to hold Vorbis encoder state
#if USE_VORBIS_ENCODER == TRUE
typedef struct {
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    XBOOL is_initialized;
} XVorbisEncoder;
#endif

#if USE_VORBIS_DECODER == TRUE
// Callback functions for libvorbisfile to read from XFILE
static size_t vorbis_read_func(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    XFILE file = (XFILE)datasource;
    size_t bytes_to_read = size * nmemb;
    XERR err;
    if (bytes_to_read == 0) return 0;

    /* XFileRead returns 0 on success, -1 on failure. It will fail if
       asked to read more bytes than remain in the file, so cap the
       requested size to the remaining bytes to allow partial/EOF reads. */
    {
        int32_t file_len = XFileGetLength(file);
        int32_t file_pos = XFileGetPosition(file);
        if (file_len >= 0 && file_pos >= 0) {
            int32_t remaining = file_len - file_pos;
            if (remaining <= 0) return 0; /* EOF */
            if ((uint64_t)bytes_to_read > (uint64_t)remaining) {
                bytes_to_read = (size_t)remaining;
            }
        }
    }

    err = XFileRead(file, ptr, (int32_t)bytes_to_read);

    if (err != 0) {
        /* Error reading */
        return 0;
    }

    /* Return number of 'items' read as fread would (bytes / size). */
    return bytes_to_read / size;
}

static int vorbis_seek_func(void *datasource, ogg_int64_t offset, int whence)
{
    XFILE file = (XFILE)datasource;
    
    // Handle different seek modes
    switch (whence) {
        case 0: // SEEK_SET - absolute position from start
            {
                int result = XFileSetPosition(file, (long)offset) == NO_ERR ? 0 : -1;
                return result;
            }
        case 1: // SEEK_CUR - relative to current position  
            {
                int result = XFileSetPositionRelative(file, (long)offset) == NO_ERR ? 0 : -1;
                return result;
            }
        case 2: // SEEK_END - relative to end
            {
                int32_t file_length = XFileGetLength(file);
                if (file_length >= 0) {
                    int32_t new_pos = file_length + (int32_t)offset;
                    int result = XFileSetPosition(file, new_pos) == NO_ERR ? 0 : -1;
                    return result;
                } else {
                    return -1;
                }
            }
        default:
            return -1;
    }
}

static int vorbis_close_func(void *datasource)
{
    // We don't close the file here, let the caller handle it
    return 0;
}

static long vorbis_tell_func(void *datasource)
{
    XFILE file = (XFILE)datasource;
    return XFileGetPosition(file);
}

static ov_callbacks vorbis_callbacks = {
    vorbis_read_func,
    vorbis_seek_func,
    vorbis_close_func,
    vorbis_tell_func
};
#endif


#if USE_VORBIS_DECODER == TRUE

// Check if file is an Ogg Vorbis file
XBOOL XIsVorbisFile(XFILE file)
{
    OggVorbis_File vf;
    int result;
    long pos;
    
    if (file == NULL) return FALSE;
    
    // Save current position
    pos = XFileGetPosition(file);
    
    // Try to open as Vorbis file
    result = ov_test_callbacks(file, &vf, NULL, 0, vorbis_callbacks);
    
    // Restore position
    XFileSetPosition(file, pos);
    
    if (result == 0) {
        ov_clear(&vf);
        return TRUE;
    }
    
    return FALSE;
}

// Open Vorbis file for decoding
void* XOpenVorbisFile(XFILE file)
{
    XVorbisDecoder *decoder;
    int result;
    
    if (file == NULL) return NULL;
    
    decoder = (XVorbisDecoder*)XNewPtr(sizeof(XVorbisDecoder));
    if (decoder == NULL) return NULL;
    
    decoder->is_open = FALSE;
    decoder->current_section = 0;
    
    result = ov_open_callbacks(file, &decoder->vf, NULL, 0, vorbis_callbacks);
    if (result != 0) {
        XDisposePtr(decoder);
        return NULL;
    }
    
    decoder->vi = ov_info(&decoder->vf, -1);
    if (decoder->vi == NULL) {
        ov_clear(&decoder->vf);
        XDisposePtr(decoder);
        return NULL;
    }
    
    decoder->is_open = TRUE;
    return decoder;
}

// Get Vorbis file information
OPErr XGetVorbisFileInfo(void *decoder_handle, UINT32 *samples, UINT32 *sample_rate, 
                        UINT32 *channels, UINT32 *bit_depth)
{
    XVorbisDecoder *decoder = (XVorbisDecoder*)decoder_handle;
    
    if (decoder == NULL || !decoder->is_open || decoder->vi == NULL) {
        return PARAM_ERR;
    }
    
    if (samples) *samples = (UINT32)ov_pcm_total(&decoder->vf, -1);
    if (sample_rate) *sample_rate = decoder->vi->rate;
    if (channels) *channels = decoder->vi->channels;
    if (bit_depth) *bit_depth = 16; // Vorbis outputs 16-bit PCM
    
    return NO_ERR;
}

// Decode Vorbis data to PCM
long XDecodeVorbisFile(void *decoder_handle, void *buffer, long buffer_size)
{
    XVorbisDecoder *decoder = (XVorbisDecoder*)decoder_handle;
    long bytes_read = 0;
    long total_read = 0;
    int bitstream;
    
    if (decoder == NULL || !decoder->is_open || buffer == NULL) {
        return -1;
    }
    
    while (total_read < buffer_size) {
        bytes_read = ov_read(&decoder->vf, 
                           (char*)buffer + total_read, 
                           buffer_size - total_read,
                           0, // little endian
                           2, // 16-bit samples
                           1, // signed
                           &bitstream);
        
        if (bytes_read <= 0) {
            break; // EOF or error
        }
        
        total_read += bytes_read;
    }
    
    return total_read;
}

// Close Vorbis decoder
void XCloseVorbisFile(void *decoder_handle)
{
    XVorbisDecoder *decoder = (XVorbisDecoder*)decoder_handle;
    
    if (decoder != NULL) {
        if (decoder->is_open) {
            ov_clear(&decoder->vf);
        }
        XDisposePtr(decoder);
    }
}

#endif // USE_VORBIS_DECODER

#if USE_VORBIS_ENCODER == TRUE

// Initialize Vorbis encoder
void* XInitVorbisEncoder(UINT32 sample_rate, UINT32 channels, float quality)
{
    XVorbisEncoder *encoder;
    int result;
    
    encoder = (XVorbisEncoder*)XNewPtr(sizeof(XVorbisEncoder));
    if (encoder == NULL) return NULL;
    
    encoder->is_initialized = FALSE;
    
    // Initialize vorbis info
    vorbis_info_init(&encoder->vi);
    
    // Set encoding parameters (VBR mode with quality setting)
    result = vorbis_encode_init_vbr(&encoder->vi, channels, sample_rate, quality);
    if (result != 0) {
        vorbis_info_clear(&encoder->vi);
        XDisposePtr(encoder);
        return NULL;
    }
    
    // Initialize comment
    vorbis_comment_init(&encoder->vc);
    vorbis_comment_add_tag(&encoder->vc, "ENCODER", "miniBAE");
    
    // Initialize analysis state and auxiliary encoding storage
    vorbis_analysis_init(&encoder->vd, &encoder->vi);
    vorbis_block_init(&encoder->vd, &encoder->vb);
    
    // Initialize stream
    ogg_stream_init(&encoder->os, rand());
    
    encoder->is_initialized = TRUE;
    return encoder;
}

// Write Vorbis header to output
long XWriteVorbisHeader(void *encoder_handle, XFILE output_file)
{
    XVorbisEncoder *encoder = (XVorbisEncoder*)encoder_handle;
    ogg_packet header, header_comm, header_code;
    long bytes_written = 0;
    
    if (encoder == NULL || !encoder->is_initialized || output_file == NULL) {
        return -1;
    }
    
    // Build headers
    vorbis_analysis_headerout(&encoder->vd, &encoder->vc, 
                             &header, &header_comm, &header_code);
    
    // Stream headers
    ogg_stream_packetin(&encoder->os, &header);
    ogg_stream_packetin(&encoder->os, &header_comm);  
    ogg_stream_packetin(&encoder->os, &header_code);
    
    // Write header pages
    while (ogg_stream_flush(&encoder->os, &encoder->og) != 0) {
        bytes_written += XFileWrite(output_file, encoder->og.header, encoder->og.header_len);
        bytes_written += XFileWrite(output_file, encoder->og.body, encoder->og.body_len);
    }
    
    return bytes_written;
}

// Encode PCM data to Vorbis
long XEncodeVorbisData(void *encoder_handle, float **pcm_data, long samples, XFILE output_file)
{
    XVorbisEncoder *encoder = (XVorbisEncoder*)encoder_handle;
    float **buffer;
    long bytes_written = 0;
    int eos = 0;
    
    if (encoder == NULL || !encoder->is_initialized) {
        return -1;
    }
    
    // Get analysis buffer
    buffer = vorbis_analysis_buffer(&encoder->vd, samples);
    
    // Copy PCM data to analysis buffer
    if (pcm_data != NULL && samples > 0) {
        int channels = encoder->vi.channels;
        for (int ch = 0; ch < channels; ch++) {
            for (long i = 0; i < samples; i++) {
                buffer[ch][i] = pcm_data[ch][i];
            }
        }
        
        vorbis_analysis_wrote(&encoder->vd, samples);
    } else {
        // Signal end of stream
        vorbis_analysis_wrote(&encoder->vd, 0);
        eos = 1;
    }
    
    // Process blocks
    while (vorbis_analysis_blockout(&encoder->vd, &encoder->vb) == 1) {
        vorbis_analysis(&encoder->vb, NULL);
        vorbis_bitrate_addblock(&encoder->vb);
        
        while (vorbis_bitrate_flushpacket(&encoder->vd, &encoder->op)) {
            ogg_stream_packetin(&encoder->os, &encoder->op);
            
            while (!eos) {
                int result = ogg_stream_pageout(&encoder->os, &encoder->og);
                if (result == 0) break;
                
                if (output_file) {
                    bytes_written += XFileWrite(output_file, encoder->og.header, encoder->og.header_len);
                    bytes_written += XFileWrite(output_file, encoder->og.body, encoder->og.body_len);
                }
                
                if (ogg_page_eos(&encoder->og)) eos = 1;
            }
        }
    }
    
    return bytes_written;
}

// Close Vorbis encoder
void XCloseVorbisEncoder(void *encoder_handle)
{
    XVorbisEncoder *encoder = (XVorbisEncoder*)encoder_handle;
    
    if (encoder != NULL) {
        if (encoder->is_initialized) {
            ogg_stream_clear(&encoder->os);
            vorbis_block_clear(&encoder->vb);
            vorbis_dsp_clear(&encoder->vd);
            vorbis_comment_clear(&encoder->vc);
            vorbis_info_clear(&encoder->vi);
        }
        XDisposePtr(encoder);
    }
}

#endif // USE_VORBIS_ENCODER

#endif // USE_VORBIS_DECODER || USE_VORBIS_ENCODER
