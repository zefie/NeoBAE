#ifndef GUI_EXPORT_H
#define GUI_EXPORT_H

#include "gui_common.h"
#include "MiniBAE.h"

// Export state globals
extern bool g_exporting;
extern int g_export_progress;
extern uint32_t g_export_last_pos;
extern int g_export_stall_iters;
extern char g_export_path[1024];
extern int g_export_file_type;
extern uint32_t g_export_last_device_samples;
extern int g_export_stable_loops;
extern bool g_export_realtime_mode;
extern bool g_export_using_live_song;

// Export codec settings
extern bool g_exportDropdownOpen;
extern int g_exportCodecIndex;
extern const char *g_exportCodecNames[];
extern const int g_exportCodecCount;

#ifdef SUPPORT_MIDI_HW
extern bool g_midiRecordFormatDropdownOpen;
extern int g_midiRecordFormatIndex;
extern const char *g_midiRecordFormatNames[];
extern const int g_midiRecordFormatCount;

// Helper types and functions for MIDI record format handling
typedef enum {
    MIDI_RECORD_FORMAT_MIDI = 0,
    MIDI_RECORD_FORMAT_WAV = 1,
    MIDI_RECORD_FORMAT_FLAC = 2,
    MIDI_RECORD_FORMAT_MP3 = 3,
    MIDI_RECORD_FORMAT_VORBIS = 4
} MidiRecordFormatType;

typedef struct {
    MidiRecordFormatType type;
    int bitrate;  // for MP3/Vorbis
    const char* extension;
} MidiRecordFormatInfo;

MidiRecordFormatInfo get_midi_record_format_info(int index);
#endif

#if USE_MPEG_ENCODER != FALSE
extern const BAECompressionType g_exportCompressionMap[];
extern const int g_exportCompressionCount;
#endif

// Function declarations
void export_init(void);
void export_cleanup(void);
bool bae_start_wav_export(const char *output_file);
#if USE_MPEG_ENCODER != FALSE
bool bae_start_mpeg_export(const char *output_file, int codec_index);
#endif
void bae_stop_wav_export(void);
void bae_service_wav_export(void);
char *save_export_dialog(int export_type); // 0=WAV, 1=FLAC, 2=MP3, 3=OGG
char *save_midi_dialog(void);

#endif // GUI_EXPORT_H
