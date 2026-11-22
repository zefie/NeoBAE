/*
 * © 2021–2026 zefie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

 /****************************************************************************
 *
 * playbae.c
 *
 * Command-line audio file player using the NeoBAE engine.
 * Mirrors the GUI (zefidi) startup and playback process:
 *   - Same BAEMixer_Open parameters (64 MIDI voices, 8 sound voices)
 *   - X_DetermineFileType for format detection
 *   - BAESong_Preroll before BAESong_Start (like bae_play in gui_bae.c)
 *
 * © Copyright 1999 Beatnik, Inc, All Rights Reserved.
 * © 2021–2026 zefie
 *
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <NeoBAE.h>
#include <NeoBAEConfigPath.h>
#include <X_Assert.h>
#include <X_Formats.h>
#include <BAE_API.h>
#include <GenSnd.h>
#include "bankinfo.h"


#if SUPPORT_BAESCRIPT == TRUE
    #include "baescript.h"
#endif

#if USE_NATIVE_DLS == TRUE
    #include "GenDLS_MobileBAE.h"
#endif

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
    #include "GenSF2_FluidLite.h"
#endif
#if USE_XMF_SUPPORT == TRUE && USE_NATIVE_DLS == TRUE
    #include "GenXMF.h"
#endif

#if USE_NATIVE_DLS == TRUE
    #include "GenDLS_MobileBAE.h"
    static int gDLSCompatibilityMode = 0;
#endif

#ifdef main
#undef main
#endif

#ifdef _WIN32
#define stricmp _stricmp
#define strcasecmp _stricmp
#include <windows.h>
#else
#define stricmp strcasecmp
#endif

/* =========================================================================
 * Global playback options
 * ========================================================================= */

static volatile int  gInterrupt      = 0;
static volatile int  gVerbose        = 0;
static volatile int  gSilent         = 0;
static int           gFadeOut        = 1;
static int           gWriteToFile    = 0;
static BAEFileType   gWriteToFileType = BAE_WAVE_TYPE;
static int           gMP3BitrateKbps = 128;
static int           gVelocityCurve  = 1; /* -1 = engine default */
static int           gVolumePct      = 100; /* raw user volume percent, for overdrive */
static int           gVolume2Pct     = 50;  /* second-bank volume (dual mode); default 50 */
static int           gDualBal1       = 0;   /* -bal1: bank A pan -100..100 (L..R), default mid */
static int           gDualBal2       = 0;   /* -bal2: bank B pan -100..100 (L..R), default mid */
static float         gDualPan1L      = 1.0f;
static float         gDualPan1R      = 1.0f;
static float         gDualPan2L      = 1.0f;
static float         gDualPan2R      = 1.0f;
static int           gDualMode       = 0;   /* -p2: two mixers, same song, sum overlay */
static BAEMixer      gDualMixerB     = NULL;
static struct GM_Mixer *gDualGmMixerB = NULL;
static int16_t      *gDualTempBuf    = NULL;
static int32_t       gDualTempBufBytes = 0;
static int           gEqEnabled      = 0;
static float         gEqGains[5]     = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
static int           gNormalize      = 0; /* -n: MIDI+patch peak normalize (playback + export) */

/* Position display: update every N idle calls (~15 ms each) */
static int           gPosCounter  = 0;
static int           gPosInterval = 10; /* ~150 ms between updates */

static int gHasCustomReverb = 0;
static char gVoiceCaptureDir[1024] = {0};
static char gInputFilePath[1024] = {0};
static char gBankFilePath[1024] = {0};
static char gBank2FilePath[1024] = {0};
static int  gTempOutputFile = 0;
static int  gPassNumber = 0;
static int  gPassTotal  = 0;

#if USE_NEO_EFFECTS == TRUE
static BAEReverbType gDefaultReverbIndex = BAE_REVERB_TYPE_17;
#else
static BAEReverbType gDefaultReverbIndex = BAE_REVERB_TYPE_1;
#endif

static int gCustomReverbType = 1;
static int gCustomReverbCombCount = 4;
static int gCustomReverbDelays[4] = {0};
static int gCustomReverbFeedback[4] = {0};
static int gCustomReverbGain[4] = {127, 127, 127, 127};
static int gCustomReverbLowpass = 64;
static int gCustomReverbMix = 255;

#if SUPPORT_KARAOKE == TRUE
#include <ctype.h>
static int  gKaraokeEnabled = 0;
static char gKaraokeCurrent[256]  = {0};
static char gKaraokePrevious[256] = {0};
static char gKaraokeLastFrag[128] = {0};
static int  gKaraokeHaveMeta      = 0;

static void cli_karaoke_reset(void)
{
    gKaraokeCurrent[0]  = '\0';
    gKaraokePrevious[0] = '\0';
    gKaraokeLastFrag[0] = '\0';
    gKaraokeHaveMeta    = 0;
}

static void cli_karaoke_print(void)
{
    if (!gKaraokeEnabled) return;
    if (gKaraokePrevious[0] && gKaraokeCurrent[0])
        printf("\nKARAOKE:\n%s\n%s\n", gKaraokePrevious, gKaraokeCurrent);
    else if (gKaraokeCurrent[0])
        printf("\nKARAOKE: %s\n", gKaraokeCurrent);
}

static void cli_karaoke_newline(void)
{
    if (gKaraokeCurrent[0]) {
        strncpy(gKaraokePrevious, gKaraokeCurrent, sizeof(gKaraokePrevious)-1);
        gKaraokePrevious[sizeof(gKaraokePrevious)-1] = '\0';
        gKaraokeCurrent[0] = '\0';
    }
    gKaraokeLastFrag[0] = '\0';
}

static void cli_karaoke_add_fragment(const char *frag)
{
    if (!frag || !frag[0]) return;
    size_t fl = strlen(frag), ll = strlen(gKaraokeLastFrag);
    int cumul = (ll > 0 && fl > ll && strncmp(frag, gKaraokeLastFrag, ll) == 0);
    if (cumul) {
        strncpy(gKaraokeCurrent, frag, sizeof(gKaraokeCurrent)-1);
        gKaraokeCurrent[sizeof(gKaraokeCurrent)-1] = '\0';
    } else {
        strncat(gKaraokeCurrent, frag, sizeof(gKaraokeCurrent)-strlen(gKaraokeCurrent)-1);
    }
    strncpy(gKaraokeLastFrag, frag, sizeof(gKaraokeLastFrag)-1);
    gKaraokeLastFrag[sizeof(gKaraokeLastFrag)-1] = '\0';
    cli_karaoke_print();
}

static void cli_karaoke_process(const char *text)
{
    const char *p = text, *seg = p;
    while (1) {
        if (*p == '/' || *p == '\\' || *p == '\0') {
            size_t len = (size_t)(p - seg);
            if (len > 0) {
                char buf[192];
                if (len >= sizeof(buf)) len = sizeof(buf)-1;
                memcpy(buf, seg, len);
                buf[len] = '\0';
                cli_karaoke_add_fragment(buf);
            }
            if (*p == '/' || *p == '\\') {
                cli_karaoke_newline();
                p++; seg = p;
                continue;
            }
            break;
        }
        p++;
    }
}

extern BAEResult BAESong_SetLyricCallback(BAESong song,
    GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);

static void cli_lyric_callback(struct GM_Song *song, const char *lyric,
    uint32_t t_us, void *ref)
{
    (void)song; (void)t_us; (void)ref;
    if (!gKaraokeEnabled || gWriteToFile || !lyric) return;
    if (lyric[0] == '\0') { cli_karaoke_newline(); cli_karaoke_print(); return; }
    cli_karaoke_process(lyric);
}

static void cli_meta_callback(void *ctx, struct GM_Song *song, char type,
    void *text, int32_t len, int16_t track)
{
    (void)ctx; (void)song; (void)len; (void)track;
    if (!gKaraokeEnabled || gWriteToFile || !text) return;
    const char *t = (const char *)text;
    if (type == 0x05) {
        gKaraokeHaveMeta = 1;
    } else if (type == 0x01) {
        if (t[0] == '@') { cli_karaoke_newline(); return; }
        if (gKaraokeHaveMeta) return;
    } else {
        return;
    }
    if (t[0] == '\0') { cli_karaoke_newline(); cli_karaoke_print(); return; }
    cli_karaoke_process(t);
}
#endif /* SUPPORT_KARAOKE */

#if SUPPORT_BAESCRIPT == TRUE
static BAEScript_Context *gScript = NULL;
#endif

/* =========================================================================
 * Logging
 * ========================================================================= */

static void playbae_printf(const char *fmt, ...)
{
    if (gSilent) return;
    va_list ap; va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap);
}

static void playbae_dprintf(const char *fmt, ...)
{
    if (!gVerbose) return;
    va_list ap; va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap);
}

/* =========================================================================
 * Signal handler
 * ========================================================================= */

static void intHandler(int dummy) { (void)dummy; gInterrupt = 1; }

/* =========================================================================
 * CLI helpers
 * ========================================================================= */

static int PV_ParseCommands(int argc, char *argv[], const char *cmd,
    int getResult, char *result)
{
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], cmd) == 0) {
            if (getResult && i+1 < argc)
                strcpy(result, argv[i+1]);
            return 1;
        }
    }
    return 0;
}

static int PV_IsFileExtension(const char *path, const char *ext)
{
    if (!path || !ext) return 0;
    size_t lp = strlen(path), le = strlen(ext);
    if (le > lp) return 0;
    return stricmp(path + lp - le, ext) == 0;
}

/* =========================================================================
 * Error string
 * ========================================================================= */

static const char *BAE_GetErrorString(BAEResult err)
{
    switch (err) {
    case BAE_NO_ERROR:              return "No error";
    case BAE_PARAM_ERR:             return "Parameter error";
    case BAE_MEMORY_ERR:            return "Memory error";
    case BAE_BAD_INSTRUMENT:        return "Bad instrument";
    case BAE_BAD_MIDI_DATA:         return "Bad MIDI data";
    case BAE_ALREADY_PAUSED:        return "Already paused";
    case BAE_ALREADY_RESUMED:       return "Already resumed";
    case BAE_DEVICE_UNAVAILABLE:    return "Device unavailable";
    case BAE_NO_SONG_PLAYING:       return "No song playing";
    case BAE_STILL_PLAYING:         return "Still playing";
    case BAE_TOO_MANY_SONGS_PLAYING: return "Too many songs playing";
    case BAE_NO_VOLUME:             return "No volume";
    case BAE_GENERAL_ERR:           return "General error";
    case BAE_NOT_SETUP:             return "Not setup";
    case BAE_NO_FREE_VOICES:        return "No free voices";
    case BAE_STREAM_STOP_PLAY:      return "Stream stop play";
    case BAE_BAD_FILE_TYPE:         return "Bad file type";
    case BAE_GENERAL_BAD:           return "General bad";
    case BAE_BAD_FILE:              return "Bad file";
    case BAE_NOT_REENTERANT:        return "Not reentrant";
    case BAE_BAD_SAMPLE:            return "Bad sample";
    case BAE_BUFFER_TOO_SMALL:      return "Buffer too small";
    case BAE_BAD_BANK:              return "Bad bank";
    case BAE_BAD_SAMPLE_RATE:       return "Bad sample rate";
    case BAE_TOO_MANY_SAMPLES:      return "Too many samples";
    case BAE_UNSUPPORTED_FORMAT:    return "Unsupported format";
    case BAE_FILE_IO_ERROR:         return "File I/O error";
    case BAE_SAMPLE_TOO_LARGE:      return "Sample too large";
    case BAE_UNSUPPORTED_HARDWARE:  return "Unsupported hardware";
    case BAE_ABORTED:               return "Aborted";
    case BAE_FILE_NOT_FOUND:        return "File not found";
    case BAE_RESOURCE_NOT_FOUND:    return "Resource not found";
    case BAE_NULL_OBJECT:           return "Null object";
    case BAE_ALREADY_EXISTS:        return "Already exists";
    default:                        return "Unknown error";
    }
}

/* =========================================================================
 * Usage strings (built at runtime for conditional format support)
 * ========================================================================= */

static char gPlayFileString[512];

static void init_playFileString(void)
{
    strcpy(gPlayFileString, "Play a file (MIDI, RMF");
#if USE_ZMF_SUPPORT == TRUE
    strcat(gPlayFileString, ", ZMF");
#endif
#if USE_XMF_SUPPORT == TRUE && (_USING_FLUIDLITE == TRUE || USE_NATIVE_DLS == TRUE)
    strcat(gPlayFileString, ", XMF, MXMF");
#endif
#if USE_RETRO_RINGTONE_SUPPORT == TRUE
    strcat(gPlayFileString, ", iMelody, RTX, RNG (v1/v2)");
#endif
    strcat(gPlayFileString, ", WAV, AIFF, AU");
#if USE_MPEG_DECODER == TRUE
    strcat(gPlayFileString, ", MP2, MP3");
#endif
#if USE_FLAC_DECODER == TRUE
    strcat(gPlayFileString, ", FLAC");
#endif
#if USE_VORBIS_DECODER == TRUE
    strcat(gPlayFileString, ", Ogg Vorbis");
#endif
#if USE_OPUS_DECODER == TRUE
    strcat(gPlayFileString, ", Ogg Opus");
#endif
#if USE_ADP_SUPPORT == TRUE
    strcat(gPlayFileString, ", Nokia ADP");
#endif
#if USE_ADX_SUPPORT == TRUE
    strcat(gPlayFileString, ", CRI ADX");
#endif
#if USE_WMA_SUPPORT == TRUE
    strcat(gPlayFileString, ", Windows Media Audio (WMA v1/v2/v7/v8/v9)");
#endif
    strcat(gPlayFileString, ")");
}

static const char usageMain[] =
    "USAGE:  playbae  -p  {patches.hsb"
#if USE_ZMF_SUPPORT == TRUE
    "/zsb"
#endif
#if USE_SF2_SUPPORT == TRUE
    "/sf2"
#endif
#if SF3_SUPPORT > 0
    "/sf3"
#endif    
#if USE_NATIVE_DLS == TRUE
    "/dls"
#endif
    "}\n"
    "                 -p2 {second bank for dual-bank overlay (same song, sum mix)}\n"
    "                 -f  {%s}\n"
#if USE_NATIVE_DLS == TRUE
    "                 -dlscompat {Enable compatibility mode to broaden support for DLS banks. Do not use if you desire authentic MobileBAE behavior.}\n"
#endif
    "                 -o  {output file (wav"
#if USE_MPEG_ENCODER == TRUE
    "/mp3"
#endif
#if USE_FLAC_ENCODER == TRUE
    "/flac"
#endif
#if USE_VORBIS_ENCODER == TRUE
    "/ogg"
#endif
#if USE_OPUS_ENCODER == TRUE
    "/opus"
#endif
    ")}\n"
#if SUPPORT_KARAOKE == TRUE
    "                 -k  {enable karaoke lyric display}\n"
#endif
    "                 -l  {loop count (default: 0)}\n"
    "                 -v  {master volume %% (default: 100; dual: 50)}\n"
    "                 -v2 {second-bank volume %% with -p2 (default: 50)}\n"
    "                 -bal1 {bank A pan -100..100 (L..R, default: 0) with -p2}\n"
    "                 -bal2 {bank B pan -100..100 (L..R, default: 0) with -p2}\n"
    "                 -n  {normalize playback/export: MIDI estimate / PCM peak}\n"
    "                 -vc {velocity curve 0-5 (default: engine (0), none for SF2/DLS)}\n"
    "                 -t  {max duration in seconds (0 = no limit)}\n"
    "                 -mc {MIDI channels to mute, 1-16, comma-separated}\n"
    "                 -rv {reverb type 0-"
#if USE_NEO_EFFECTS == TRUE
    "18"
#else
    "11"
#endif
    " (default: %d)}\n"
    "                 -nf {disable fade-out on stop}\n"
    "                 -q  {quiet mode}\n"
#if USE_MPEG_ENCODER == TRUE || USE_VORBIS_ENCODER == TRUE || USE_OPUS_ENCODER == TRUE
    "                 -b  {"
#if USE_MPEG_ENCODER == TRUE
    "MP3/"
#endif
#if USE_VORBIS_ENCODER == TRUE
    "Vorbis/"
#endif
#if USE_OPUS_ENCODER == TRUE
    "Opus"
#endif
    " export bitrate kbps (default: 128)}\n"
#endif
    "                 -h  {this help}\n"
    "                 -eq {enable 5-band EQ (flat)}\n"
    "                 -eqg {custom EQ gains g1,g2,g3,g4,g5 (e.g. 1.2,0,0,-2.5,1.0)}\n"
    "                 --eqp {EQ preset name (e.g. \"Bass Boost\" or custom zefidi.ini preset)}\n"
#if SUPPORT_BAESCRIPT == TRUE
    "                 --script {BAEScript file}\n"
#endif
#if BAE_FIX_SPAN_DC
    "                 --panfix=off {disable STEREO_PAN LFO DC fix}\n"
#endif
#if BAE_CLASSIC_CHORUS
    "                 --classicchorus {enable pre-DLS classic chorus}\n"
#endif
#if USE_WEBTV_MODE
    "                 --webtv {WebTV Mode: WebTV velocity curve, LFO shapes, default pans, filter glide}\n"
#endif
    "                 -x  {additional options}\n";

static const char usageExtra[] =
    " Additional flags:\n"
    "                 -mr {sample rate in Hz (default: 44100)}\n"
    "                 -ns {mono output (no stereo)}\n"
    "                 -2p {2-point interpolation instead of linear}\n"
    "                 -sinc {windowed sinc interpolation}\n"
    "                 -8 {8-bit mixer output}\n"
    "                 -mv {max MIDI voices (default: 64)}\n"
    "                 -cl {list velocity curves}\n"
    "                 -rl {list reverb types}\n"
    "                 -sw {stream a WAV file}\n"
    "                 -sa {stream an AIFF file}\n"
    "                 -i  {show RMF file info}\n"
    "                 -d  {verbose/debug mode}\n"
    "                 -oc <dir> {record per-channel WAV files}\n";

static const char *reverbTypeName(BAEReverbType t)
{
    switch (t) {
    case BAE_REVERB_NO_CHANGE: return "Default";
    case BAE_REVERB_NONE:      return "None";
    case BAE_REVERB_TYPE_2:    return "Igor's Closet";
    case BAE_REVERB_TYPE_3:    return "Igor's Garage";
    case BAE_REVERB_TYPE_4:    return "Igor's Acoustic Lab";
    case BAE_REVERB_TYPE_5:    return "Igor's Cavern";
    case BAE_REVERB_TYPE_6:    return "Igor's Dungeon";
    case BAE_REVERB_TYPE_7:    return "Small reflections (WebTV)";
    case BAE_REVERB_TYPE_8:    return "Early reflections (variable)";
    case BAE_REVERB_TYPE_9:    return "Basement (variable)";
    case BAE_REVERB_TYPE_10:   return "Banquet hall (variable)";
    case BAE_REVERB_TYPE_11:   return "Catacombs (variable)";
#if USE_NEO_EFFECTS == TRUE
    case BAE_REVERB_TYPE_12:   return "Neo Room (Neo reverb)";
    case BAE_REVERB_TYPE_13:   return "Neo Hall (Neo reverb)";
    case BAE_REVERB_TYPE_14:   return "Neo Cavern (Neo reverb)";
    case BAE_REVERB_TYPE_15:   return "Neo Dungeon (Neo reverb)";
    case BAE_REVERB_TYPE_16:   return "Neo Nokia (Neo reverb)";
    case BAE_REVERB_TYPE_17:   return "MobileBAE";
    case BAE_REVERB_TYPE_18:   return "Neo Tap Delay (Neo reverb)";
#endif
    case BAE_REVERB_TYPE_19:   return "Custom";
    default:                   return "Unknown";
    }
}

static const char reverbList[] =
    "Valid Reverb Types (-rv):\n"
    "   0   Default\n"
    "   1   None\n"
    "   2   Igor's Closet\n"
    "   3   Igor's Garage\n"
    "   4   Igor's Acoustic Lab\n"
    "   5   Igor's Cavern\n"
    "   6   Igor's Dungeon\n"
    "   7   Small reflections (WebTV)\n"
    "   8   Early reflections (variable)\n"
    "   9   Basement (variable)\n"
    "   10  Banquet hall (variable)\n"
    "   11  Catacombs (variable)\n"
#if USE_NEO_EFFECTS == TRUE
    "   12  Neo Room (Neo reverb)\n"
    "   13  Neo Hall (Neo reverb)\n"
    "   14  Neo Cavern (Neo reverb)\n"
    "   15  Neo Dungeon (Neo reverb)\n"
    "   16  Neo Nokia (Neo reverb)\n"
    "   17  MobileBAE\n"
    "   18  Neo Tap Delay (Neo reverb)\n";
#endif

static const char velocityList[] =
    "Valid Velocity Curves (HSB/ZSB Only) (-vc):\n"
    "   0   Default S Curve\n"
    "   1   Peaky S Curve\n"
    "   2   WebTV Curve\n"
    "   3   2x Exponential\n"
    "   4   2x Linear\n"
    "   5   No Curve\n";

/* =========================================================================
 * RMF metadata display
 * ========================================================================= */

static const char *rmf_info_label(BAEInfoType t)
{
    switch (t) {
    case TITLE_INFO:             return "Title";
    case PERFORMED_BY_INFO:      return "Performed By";
    case COMPOSER_INFO:          return "Composer";
    case COPYRIGHT_INFO:         return "Copyright";
    case PUBLISHER_CONTACT_INFO: return "Publisher";
    case USE_OF_LICENSE_INFO:    return "Use Of License";
    case LICENSED_TO_URL_INFO:   return "Licensed URL";
    case LICENSE_TERM_INFO:      return "License Term";
    case EXPIRATION_DATE_INFO:   return "Expiration";
    case COMPOSER_NOTES_INFO:    return "Composer Notes";
    case INDEX_NUMBER_INFO:      return "Index Number";
    case GENRE_INFO:             return "Genre";
    case SUB_GENRE_INFO:         return "Sub-Genre";
    case TEMPO_DESCRIPTION_INFO: return "Tempo";
    case ORIGINAL_SOURCE_INFO:   return "Source";
    default:                     return "Unknown";
    }
}

static void print_rmf_info(const char *path)
{
    FILE *f;
    unsigned char hdr[12];
    uint32_t mapID, version;
    char buf[256];
    BAEInfoType it;

    playbae_printf("RMF Metadata:\n");
    f = fopen(path, "rb");
    if (f) {
        if (fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr)) {
            mapID   = ((uint32_t)hdr[0]<<24)|((uint32_t)hdr[1]<<16)|
                      ((uint32_t)hdr[2]<<8)|(uint32_t)hdr[3];
            version = ((uint32_t)hdr[4]<<24)|((uint32_t)hdr[5]<<16)|
                      ((uint32_t)hdr[6]<<8)|(uint32_t)hdr[7];
            if (mapID == XFILERESOURCE_ID || mapID == XFILERESOURCE_ZMF_ID)
                playbae_printf("  %s Version: %u\n",
                    version >= 2u ? "ZMF" : "RMF", (unsigned)version);
        }
        fclose(f);
    }
    for (it = TITLE_INFO; it <= ORIGINAL_SOURCE_INFO; it = (BAEInfoType)(it+1)) {
        if (BAEUtil_GetRmfSongInfoFromFile((BAEPathName)path, 0, it,
                buf, sizeof(buf)-1) == BAE_NO_ERROR)
            playbae_printf("  %s: %s\n", rmf_info_label(it), buf);
    }
}

static void print_song_engine_config(BAESong song)
{
    uint32_t flags = 0;
    if (BAESong_GetEngineConfig(song, &flags) != BAE_NO_ERROR || !flags) return;
    playbae_printf("  Engine config: 0x%08X\n", (unsigned)flags);
    if (flags & SONG_CONFIG_HAS_CLASSIC_CHORUS)
        playbae_printf("  Classic Chorus: %s\n",
            (flags & SONG_CONFIG_CLASSIC_CHORUS_ON) ? "On" : "Off");
    if (flags & SONG_CONFIG_HAS_PANFIX)
        playbae_printf("  Pan Fix: %s\n",
            (flags & SONG_CONFIG_PANFIX_ON) ? "On" : "Off");
    if (flags & SONG_CONFIG_OVERRIDE_VOLUME_CURVE)
        playbae_printf("  Override Volume Curve: %s\n",
            (flags & SONG_CONFIG_OVERRIDE_VOLUME_CURVE) ? "On" : "Off");
    if (flags & SONG_CONFIG_HAS_EXTENDED_PITCH_RANGE)
        playbae_printf("  Extended Pitch Range: %s\n",
            (flags & SONG_CONFIG_EXTENDED_PITCH_RANGE_ON) ? "On" : "Off");
}

/* =========================================================================
 * Audio task and idle (mirrors the GUI audio task in gui_bae.c)
 * ========================================================================= */

static void PV_AudioTask(void *ref) { BAEMixer_ServiceStreams((BAEMixer)ref); }

static void PV_Idle(BAEMixer mixer, uint32_t us)
{
    if (gWriteToFile) {
        BAEResult serr = BAEMixer_ServiceAudioOutputToFile(mixer);
        if (serr != BAE_NO_ERROR) {
            playbae_printf("Export error (%d: %s). Aborting.\n",
                serr, BAE_GetErrorString(serr));
            BAEMixer_StopOutputToFile();
            BAEMixer_Delete(mixer);
            exit(1);
        }
    } else {
        uint32_t slices = us / 12000;
        for (uint32_t i = 0; i < slices; i++)
            BAE_WaitMicroseconds(12000);
    }
}

/* =========================================================================
 * Position display
 * ========================================================================= */

static void display_song_position(uint32_t posMs, uint32_t totalMs)
{
    if (++gPosCounter < gPosInterval) return;
    gPosCounter = 0;

    if (gWriteToFile) {
        if (gPassNumber > 0) {
            playbae_printf("Pass %d/%d\r", gPassNumber, gPassTotal);
        }
        return;
    }

    int m  = (int)(posMs / 60000);
    int s  = (int)((posMs - m*60000) / 1000);
    int ms = (int)(posMs - m*60000 - s*1000);
    if (ms > 0 || s > 0 || m > 0) {
        if (totalMs > posMs) {
            int tm  = (int)(totalMs / 60000);
            int ts  = (int)((totalMs - tm*60000) / 1000);
            int tms = (int)(totalMs - tm*60000 - ts*1000);
            playbae_printf("Position: %02d:%02d.%03d  Total: %02d:%02d.%03d\r",
                m, s, ms, tm, ts, tms);
        } else {
            playbae_printf("Position: %02d:%02d.%03d\r", m, s, ms);
        }
        playbae_printf("\n");
    }
}

static void display_sound_position(uint32_t frames, int sampleRate)
{
    if (++gPosCounter < gPosInterval) return;
    gPosCounter = 0;
    if (sampleRate <= 0) return;
    uint32_t secs = frames / (uint32_t)sampleRate;
    int m = (int)(secs / 60);
    int s = (int)(secs - m*60);
    if (s > 0 || m > 0)
        playbae_printf("Position: %02d:%02d\r", m, s);
}

/* =========================================================================
 * Channel muting
 * ========================================================================= */

static void MuteChannels(BAESong song, char *list)
{
    char *tok = strtok(list, ",");
    while (tok) {
        int ch = atoi(tok);
        if (ch >= 1 && ch <= 16) {
            BAESong_MuteChannel(song, ch-1);
            playbae_printf("Muting MIDI channel %d\n", ch);
        } else {
            playbae_printf("Invalid MIDI channel: %s\n", tok);
        }
        tok = strtok(NULL, ",");
    }
}

/* =========================================================================
 * Volume helper: percent -> BAE fixed-point (same scale as old code)
 * ========================================================================= */

/* Per-song/sound volume: BAE_UNSIGNED_FIXED is 16.16 fixed-point.
 * For MIDI/RMF/DLS-RMF, player -v uses BAEMixer_SetOutputGain (full mix bus).
 * Keep BAESong_SetVolume at unity so HSB is not double-attenuated vs Native DLS. */
static BAE_UNSIGNED_FIXED volume_pct_to_fixed(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100; /* song/sound object path; overdrive via OutputGain */
    return FLOAT_TO_UNSIGNED_FIXED(pct / 100.0);
}

/* Player volume on the full mix bus (HSB+SF2+DLS) before the peak limiter. */
static void apply_output_gain(BAEMixer mixer)
{
    BAEMixer_SetOutputGain(mixer, gVolumePct);
}

static void apply_output_gain_pct(BAEMixer mixer, int pct)
{
    BAEMixer_SetOutputGain(mixer, pct);
}

/* Dual-bank pan: -100 = full L, 0 = mid (unity), 100 = full R. */
static void PV_DualPanGainsFromBal(int bal, float *outL, float *outR)
{
    if (bal > 100) bal = 100;
    if (bal < -100) bal = -100;
    if (bal >= 0) {
        *outL = 1.0f - ((float)bal / 100.0f);
        *outR = 1.0f;
    } else {
        *outL = 1.0f;
        *outR = 1.0f + ((float)bal / 100.0f);
    }
}

static void PV_DualUpdatePanGains(void)
{
    PV_DualPanGainsFromBal(gDualBal1, &gDualPan1L, &gDualPan1R);
    PV_DualPanGainsFromBal(gDualBal2, &gDualPan2L, &gDualPan2R);
}

static int32_t PV_ClampS16(int32_t s)
{
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return s;
}

#if USE_CALLBACKS == TRUE
/* After mixer A's slice is in `samples`, pull-render mixer B, pan both, sum (clamp). */
static void PV_DualMixOutput(void *threadContext, void *samples, int32_t sampleSize,
    int32_t channels, uint32_t lengthInFrames)
{
    int32_t bytes;
    uint32_t f;
    (void)threadContext;

    if (!gDualGmMixerB || !gDualTempBuf || !samples || sampleSize <= 0 || channels <= 0)
        return;

    bytes = (int32_t)lengthInFrames * sampleSize * channels;
    if (bytes <= 0 || bytes > gDualTempBufBytes)
        return;

    memset(gDualTempBuf, 0, (size_t)bytes);
    BAE_BuildMixerSlice(gDualGmMixerB, gDualTempBuf, bytes, (int32_t)lengthInFrames);

    if (sampleSize == 2 && channels >= 2) {
        int16_t *a = (int16_t *)samples;
        int16_t *b = gDualTempBuf;
        for (f = 0; f < lengthInFrames; f++) {
            int32_t aL = (int32_t)((float)a[0] * gDualPan1L);
            int32_t aR = (int32_t)((float)a[1] * gDualPan1R);
            int32_t bL = (int32_t)((float)b[0] * gDualPan2L);
            int32_t bR = (int32_t)((float)b[1] * gDualPan2R);
            a[0] = (int16_t)PV_ClampS16(aL + bL);
            a[1] = (int16_t)PV_ClampS16(aR + bR);
            a += channels;
            b += channels;
        }
    } else if (sampleSize == 2) {
        int16_t *a = (int16_t *)samples;
        int16_t *b = gDualTempBuf;
        float mid1 = (gDualPan1L + gDualPan1R) * 0.5f;
        float mid2 = (gDualPan2L + gDualPan2R) * 0.5f;
        for (f = 0; f < lengthInFrames; f++) {
            a[0] = (int16_t)PV_ClampS16(
                (int32_t)((float)a[0] * mid1) + (int32_t)((float)b[0] * mid2));
            a++;
            b++;
        }
    } else if (sampleSize == 1 && channels >= 2) {
        uint8_t *a = (uint8_t *)samples;
        uint8_t *b = (uint8_t *)gDualTempBuf;
        for (f = 0; f < lengthInFrames; f++) {
            int32_t aL = (int32_t)(((float)a[0] - 128.0f) * gDualPan1L);
            int32_t aR = (int32_t)(((float)a[1] - 128.0f) * gDualPan1R);
            int32_t bL = (int32_t)(((float)b[0] - 128.0f) * gDualPan2L);
            int32_t bR = (int32_t)(((float)b[1] - 128.0f) * gDualPan2R);
            int32_t sL = aL + bL;
            int32_t sR = aR + bR;
            if (sL > 127) sL = 127;
            if (sL < -128) sL = -128;
            if (sR > 127) sR = 127;
            if (sR < -128) sR = -128;
            a[0] = (uint8_t)(sL + 128);
            a[1] = (uint8_t)(sR + 128);
            a += channels;
            b += channels;
        }
    } else if (sampleSize == 1) {
        uint8_t *a = (uint8_t *)samples;
        uint8_t *b = (uint8_t *)gDualTempBuf;
        float mid1 = (gDualPan1L + gDualPan1R) * 0.5f;
        float mid2 = (gDualPan2L + gDualPan2R) * 0.5f;
        for (f = 0; f < lengthInFrames; f++) {
            int32_t s = (int32_t)(((float)a[0] - 128.0f) * mid1)
                      + (int32_t)(((float)b[0] - 128.0f) * mid2);
            if (s > 127) s = 127;
            if (s < -128) s = -128;
            a[0] = (uint8_t)(s + 128);
            a++;
            b++;
        }
    }
}

static void PV_DualMixTeardown(BAEMixer mixerA)
{
    if (mixerA) {
        BAEMixer_MakeCurrent(mixerA);
        GM_SetAudioOutput(NULL);
    }
    free(gDualTempBuf);
    gDualTempBuf = NULL;
    gDualTempBufBytes = 0;
    gDualGmMixerB = NULL;
}

static int PV_DualMixSetup(BAEMixer mixerA, BAEMixer mixerB)
{
    int16_t frames;
    int32_t bytes;

    if (!mixerA || !mixerB)
        return 0;

    if (BAEMixer_MakeCurrent(mixerB) != BAE_NO_ERROR)
        return 0;
    gDualGmMixerB = GM_GetCurrentMixer();
    if (!gDualGmMixerB)
        return 0;

    frames = BAE_GetMaxSamplePerSlice();
    if (frames <= 0)
        frames = 1024;
    /* Cover device slices and file-export block size (GM_GetAudioBufferOutputSize). */
    bytes = GM_GetAudioBufferOutputSize();
    {
        int32_t minBytes = (int32_t)frames * 2 * 2 * 4;
        if (bytes < minBytes)
            bytes = minBytes;
    }
    if (bytes < 65536)
        bytes = 65536;
    gDualTempBuf = (int16_t *)calloc(1, (size_t)bytes);
    if (!gDualTempBuf) {
        gDualGmMixerB = NULL;
        return 0;
    }
    gDualTempBufBytes = bytes;

    if (BAEMixer_MakeCurrent(mixerA) != BAE_NO_ERROR) {
        PV_DualMixTeardown(NULL);
        return 0;
    }
    GM_SetAudioOutput(PV_DualMixOutput);
    return 1;
}
#endif /* USE_CALLBACKS */

/* =========================================================================
 * PV_PlaySong – unified MIDI/RMF/XMF playback
 *
 * Order matches the original playbae / simple.c:
 *   Set velocity curve + callbacks (before Start) → BAESong_Start
 *   → set volume, reverb, loops, mutes (after Start)
 *
 * All song types (MIDI, RMF, XMF, RMI, ringtone) use this single path.
 * ========================================================================= */

static BAEResult PV_PlaySong(BAEMixer mixer, BAESong song, const char *fileName,
    BAE_UNSIGNED_FIXED volume, unsigned int timeLimitSec, unsigned int loopCount,
    BAEReverbType reverbType, char *muteChannels, int soloChannel)
{
    unsigned int effectiveLoopCount = loopCount;
    /* Velocity curve must be set before Start (same as original PlayMidi) */
    if (gVelocityCurve >= 0) {
        BAESong_SetVelocityCurve(song, gVelocityCurve);
        if (gPassNumber <= 0) playbae_printf("Velocity curve: %d\n", gVelocityCurve);
    }
#if USE_SF2_SUPPORT == TRUE
    else if (BAESong_IsSF2Song(song)) {
        BAESong_SetVelocityCurve(song, 1); /* peaky for SF2, same as GUI */
    }
#endif

#if SUPPORT_KARAOKE == TRUE
    cli_karaoke_reset();
    if (!gWriteToFile && gKaraokeEnabled) {
        if (BAESong_SetLyricCallback(song, cli_lyric_callback, NULL) != BAE_NO_ERROR)
            BAESong_SetMetaEventCallback(song, cli_meta_callback, NULL);
    }
#endif

    /* Apply mixer FX before optional normalize so estimate padding matches render. */
    if (gHasCustomReverb) {
        BAEMixer_SetDefaultReverb(mixer, gCustomReverbType);
        SetNeoCustomReverbCombCount(gCustomReverbCombCount);
        for (int i = 0; i < 4; i++) {
            SetNeoCustomReverbCombDelay(i, gCustomReverbDelays[i]);
            SetNeoCustomReverbCombFeedback(i, gCustomReverbFeedback[i]);
            SetNeoCustomReverbCombGain(i, gCustomReverbGain[i]);
        }
        SetNeoCustomReverbLowpass(gCustomReverbLowpass);
        SetNeoReverbMix(gCustomReverbMix);
    } else {
        BAEMixer_SetDefaultReverb(mixer, reverbType);
    }

    /* Mixer-level gain applies to both live playback and -o file export. */
    int32_t normalizeGainPct = 100;
    if (gNormalize) {
        playbae_printf("Normalizing (MIDI+patch estimate)%s...\n",
            gWriteToFile ? " for export" : "");
    }
    {
        BAEResult nerr = BAESong_ApplyNormalizeFromMidiEstimate(
            song, mixer, gNormalize ? TRUE : FALSE,
            BAE_NORMALIZE_DEFAULT_TARGET_PEAK_PCT, &normalizeGainPct);
        if (gNormalize && nerr != BAE_NO_ERROR) {
            playbae_printf("playbae: Normalize failed for '%s' (%d: %s); continuing without\n",
                fileName, nerr, BAE_GetErrorString(nerr));
        } else if (gNormalize && gPassNumber <= 0) {
            playbae_printf("Normalize gain: %d%%\n", (int)normalizeGainPct);
        }
    }

    BAESong_Preroll(song);
    /* Start playback (volume, loops, reverb, mutes applied after, like original) */
    BAEResult err = BAESong_Start(song, 0);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't start '%s' (%d: %s)\n",
            fileName, err, BAE_GetErrorString(err));
        return err;
    }

    if (gVerbose) BAESong_DisplayInfo(song);
    if (gPassNumber <= 0) print_song_engine_config(song);

    /* Apply settings after Start (matches original PlayMidi/PlayRMF order).
     * Song volume stays at unity; player -v is OutputGain on the full mix bus
     * so Native DLS and HSB/SF2 stay balanced in DLS-RMF songs. */
    (void)volume;
    BAESong_SetVolume(song, volume_pct_to_fixed(100));
    if (muteChannels && muteChannels[0])
        MuteChannels(song, muteChannels);

    if (soloChannel >= 0)
        BAESong_SoloChannel(song, (uint16_t)soloChannel);

    // Force reverb to reinit by briefly switching to None and back.
    // This clears all internal reverb delay line buffers that might
    // contain residual tails from prior playback state.
    {
        BAEReverbType savedVerb;
        BAEMixer_GetDefaultReverb(mixer, &savedVerb);
        BAEMixer_SetDefaultReverb(mixer, BAE_REVERB_NONE);
        BAEMixer_SetDefaultReverb(mixer, savedVerb);
    }

    /* Re-assert normalize after Start/reverb churn (export slices use this gain). */
    BAEMixer_SetSongNormalizeGain(mixer, normalizeGainPct);

    /* BAESong_SetLoops(song, N) has a known engine bug: it sets both loopSong=TRUE and
     * songMaxLoopCount=N, but the sequencer re-loops forever when loopSong=TRUE even after
     * songMaxLoopCount is exhausted (the max count is meant for controller 86/87 mute markers).
     * Fix: if loopCount > 0, tell the engine to loop indefinitely (large value keeps loopSong=TRUE
     * so IsDone stays FALSE), and stop externally when we detect N wrap-arounds via position
     * tracking. loopCount == 0 keeps the default one-shot behaviour (loopSong=FALSE).
     * Honor -l for both live playback and -o offline render (needed to capture 2nd+ loops). */
    if (loopCount > 0)
        BAESong_SetLoops(song, 30000); /* effectively infinite; we stop it ourselves */
    else
        BAESong_SetLoops(song, 0); /* one-shot, loopSong=FALSE, IsDone fires when done */

#if SUPPORT_BAESCRIPT == TRUE
    uint32_t scriptLenMs = 0;
    if (gScript) {
        uint64_t scriptLenUs = 0;
        BAEScript_SetSong(gScript, song);
        BAEScript_SetExporting(gScript, gWriteToFile);
#if BAESCRIPT_EXPORTER_LOOPCOUNT == TRUE
        if (gWriteToFile)
            BAEScript_ResetExporterOptions(gScript);
#endif
        BAESong_GetMicrosecondLength64(song, &scriptLenUs);
        scriptLenMs = (scriptLenUs / 1000ull > UINT32_MAX)
            ? UINT32_MAX
            : (uint32_t)(scriptLenUs / 1000ull);
        BAEScript_Tick(gScript, 0, scriptLenMs);

#if BAESCRIPT_EXPORTER_LOOPCOUNT == TRUE
        if (gWriteToFile) {
            int scriptLoopCount = 0;
            if (BAEScript_GetExporterLoopCount(gScript, &scriptLoopCount)) {
                BAESong_SetLoops(song, (int16_t)scriptLoopCount);
                effectiveLoopCount = (unsigned int)scriptLoopCount;
            }
        }
#endif
    }
#endif

    if (gPassNumber <= 0) {
        playbae_printf("Reverb: %s (%d)\n", reverbTypeName(reverbType), (int)reverbType);
        if (effectiveLoopCount > 0)
            playbae_printf("Will loop %u time(s)\n", effectiveLoopCount);
        if (timeLimitSec > 0)
            playbae_printf("Time limit: %u sec\n", timeLimitSec);
    }

    if (gWriteToFile) {
        BAEResult perr = BAEMixer_PrimeAudioOutputToFile(mixer, song);
        if (perr != BAE_NO_ERROR) {
            playbae_printf("Export priming failed (%d: %s).\n",
                perr, BAE_GetErrorString(perr));
            BAESong_Stop(song, 0);
            return perr;
        }
    }

    /* ---- Main playback loop ---- */
    uint64_t lastPos       = 0;
    uint64_t cumulative    = 0;
    unsigned int loopsDone = 0;
    BAE_BOOL done          = FALSE;
    gPosCounter            = 0;

    while (!done) {
        if (gInterrupt) {
            playbae_printf("\nStop requested...\n");
            gInterrupt = 0;
            BAESong_Stop(song, gFadeOut);
        }

        if (gWriteToFile)
            BAEMixer_ServiceAudioOutputToFile(mixer);

        BAESong_IsDone(song, &done);

        uint64_t posUs = 0;
        BAESong_GetMicrosecondPosition64(song, &posUs);
        uint64_t posMs = posUs / 1000ull;

        /* Detect loop wrap-around via position going backwards by >1 second */
        if (posMs < lastPos && (lastPos - posMs) > 1000ull) {
            cumulative += lastPos;
            if (effectiveLoopCount > 0) {
                loopsDone++;
                if (loopsDone >= effectiveLoopCount)
                    BAESong_Stop(song, gFadeOut);
            }
        }
        lastPos = posMs;

        uint64_t totalMs = cumulative + posMs;
        display_song_position(
            (posMs > UINT32_MAX) ? UINT32_MAX : (uint32_t)posMs,
            (totalMs > UINT32_MAX) ? UINT32_MAX : (uint32_t)totalMs);

#if SUPPORT_BAESCRIPT == TRUE
        if (gScript) {
            uint32_t tickMs = (totalMs > UINT32_MAX) ? UINT32_MAX : (uint32_t)totalMs;
            BAEScript_Tick(gScript, tickMs, scriptLenMs);
        }
#endif

        if (timeLimitSec > 0 && totalMs > (uint64_t)timeLimitSec * 1000ull - 750ull)
            BAESong_Stop(song, gFadeOut);

        if (!done) PV_Idle(mixer, 15000);
    }

    PV_Idle(mixer, 900000);
    if (gPassNumber <= 0) playbae_printf("\n");
    return BAE_NO_ERROR;
}

/* =========================================================================
 * PV_PlaySound – audio file (WAV/AIFF/MP3/FLAC/Vorbis/Opus/ADP) playback
 * ========================================================================= */

static BAEResult PV_PlaySound(BAEMixer mixer, BAESound sound, const char *fileName,
    BAE_UNSIGNED_FIXED volume, unsigned int timeLimitSec, unsigned int loopCount)
{
    /* Mixer song-normalize must not boost PCM sample playback/export. */
    BAEMixer_SetSongNormalizeGain(mixer, 100);

    BAESound_SetVolume(sound, volume);

    if (loopCount > 0) {
        BAESound_SetLoopCount(sound, loopCount);
        playbae_printf("Loop count: %u\n", loopCount);
    }

    BAEResult err = BAESound_Start(sound, 0, BAE_FIXED_1, 0);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't start audio '%s' (%d: %s)\n",
            fileName, err, BAE_GetErrorString(err));
        return err;
    }

    BAESampleInfo info;
    int sampleRate = 44100;
    if (BAESound_GetInfo(sound, &info) == BAE_NO_ERROR)
        sampleRate = info.sampledRate / 65536;

    BAE_BOOL done = FALSE;
    gPosCounter   = 0;

    while (!done) {
        if (gInterrupt) {
            playbae_printf("\nStop requested...\n");
            gInterrupt = 0;
            BAESound_Stop(sound, gFadeOut);
        }

        BAESound_IsDone(sound, &done);

        uint32_t frames = 0;
        BAESound_GetSamplePlaybackPosition(sound, &frames);
        display_sound_position(frames, sampleRate);

        if (timeLimitSec > 0 && sampleRate > 0) {
            uint32_t secs = frames / (uint32_t)sampleRate;
            if (secs >= timeLimitSec)
                BAESound_Stop(sound, gFadeOut);
        }

        if (!done) PV_Idle(mixer, 15000);
    }

    PV_Idle(mixer, 900000);
    playbae_printf("\n");
    return BAE_NO_ERROR;
}

/* =========================================================================
 * PV_PlayStreamed – WAV/AIFF streaming (position not tracked)
 * ========================================================================= */

static BAEResult PV_PlayStreamed(BAEMixer mixer, const char *fileName,
    BAEFileType ftype, BAE_UNSIGNED_FIXED volume)
{
    BAEStream stream = BAEStream_New(mixer);
    if (!stream) return BAE_MEMORY_ERR;

    BAEResult err = BAEStream_SetupFile(stream, (BAEPathName)fileName,
        ftype, BAE_MIN_STREAM_BUFFER_SIZE, FALSE);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't open stream '%s' (%d)\n", fileName, err);
        BAEStream_Delete(stream);
        return err;
    }

    BAEStream_SetVolume(stream, volume);
    err = BAEStream_Start(stream);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't start stream (%d)\n", err);
        BAEStream_Delete(stream);
        return err;
    }

    BAE_BOOL done = FALSE;
    while (!done) {
        if (gInterrupt) { gInterrupt = 0; BAEStream_Stop(stream, gFadeOut); }
        BAEStream_IsDone(stream, &done);
        if (!done) PV_Idle(mixer, 15000);
    }
    PV_Idle(mixer, 900000);
    playbae_printf("\n");
    BAEStream_Delete(stream);
    return BAE_NO_ERROR;
}

/* =========================================================================
 * PV_PlayFile – unified file loader and player
 *
 * Mirrors bae_load_song() in gui_bae.c:
 *   - Uses X_DetermineFileType for format detection
 *   - Loads with appropriate BAESong_Load / BAESound_Load call
 *   - Routes to PV_PlaySong or PV_PlaySound
 * ========================================================================= */

static BAEResult PV_PlayFile(BAEMixer mixer, const char *path,
    BAE_UNSIGNED_FIXED volume, unsigned int timeLimitSec, unsigned int loopCount,
    BAEReverbType reverbType, char *muteChannels, int soloChannel)
{
    BAEFileType ftype = X_DetermineFileType((BAEPathName)path);
    if (ftype == BAE_INVALID_TYPE) {
        playbae_printf("playbae: Unrecognized file type: '%s'\n", path);
        return BAE_BAD_FILE;
    }

    /* ---- Determine if this is an audio (PCM/compressed) file ---- */
    int isAudio = 0;
    switch (ftype) {
    case BAE_WAVE_TYPE:
    case BAE_AIFF_TYPE:
    case BAE_AU_TYPE:
        isAudio = 1; break;
#if USE_MPEG_DECODER == TRUE
    case BAE_MPEG_TYPE:    isAudio = 1; break;
#endif
#if USE_FLAC_DECODER == TRUE
    case BAE_FLAC_TYPE:    isAudio = 1; break;
#endif
#if USE_VORBIS_DECODER == TRUE
    case BAE_VORBIS_TYPE:  isAudio = 1; break;
#endif
#if USE_OPUS_DECODER == TRUE
    case BAE_OPUS_TYPE:    isAudio = 1; break;
#endif
#if USE_ADP_SUPPORT == TRUE
    case BAE_ADP_TYPE:     isAudio = 1; break;
#endif
#if USE_ADX_SUPPORT == TRUE
    case BAE_ADX_TYPE:     isAudio = 1; break;
#endif
#if USE_QOA_SUPPORT == TRUE
    case BAE_QOA_TYPE:     isAudio = 1; break;
#endif
#if USE_WMA_SUPPORT == TRUE
    case BAE_WMA_TYPE:     isAudio = 1; break;
#endif
    default: break;
    }

    if (isAudio) {
        /* Describe detected type */
        const char *typeName = "audio";
        switch (ftype) {
        case BAE_WAVE_TYPE:    typeName = "WAVE";          break;
        case BAE_AIFF_TYPE:    typeName = "AIFF";          break;
        case BAE_AU_TYPE:      typeName = "AU";            break;
#if USE_MPEG_DECODER == TRUE
        case BAE_MPEG_TYPE:    typeName = "MPEG (MP2/MP3)"; break;
#endif
#if USE_FLAC_DECODER == TRUE
        case BAE_FLAC_TYPE:    typeName = "FLAC";          break;
#endif
#if USE_VORBIS_DECODER == TRUE
        case BAE_VORBIS_TYPE:  typeName = "Ogg Vorbis";   break;
#endif
#if USE_OPUS_DECODER == TRUE
        case BAE_OPUS_TYPE:    typeName = "Ogg Opus";     break;
#endif
#if USE_ADP_SUPPORT == TRUE
        case BAE_ADP_TYPE:     typeName = "Nokia ADP";    break;
#endif
#if USE_ADX_SUPPORT == TRUE
        case BAE_ADX_TYPE:     typeName = "CRI ADX";      break;
#endif
#if USE_WMA_SUPPORT == TRUE
        case BAE_WMA_TYPE:     typeName = "WMAv1/WMAv2";   break;
#endif
        default: break;
        }
        if (gPassNumber <= 0) playbae_printf("Playing %s: %s\n", typeName, path);

        BAESound sound = BAESound_New(mixer);
        if (!sound) return BAE_MEMORY_ERR;

        BAEResult err = BAESound_LoadFileSample(sound, (BAEPathName)path, ftype);
        if (err != BAE_NO_ERROR) {
            playbae_printf("playbae: Couldn't load '%s' (%d: %s)\n",
                path, err, BAE_GetErrorString(err));
            BAESound_Delete(sound);
            return err;
        }

        if (gNormalize) {
            int32_t gainPct = 100;
            BAEResult nerr = BAESound_NormalizeFromPeak(sound, 89, &gainPct);
            if (nerr != BAE_NO_ERROR) {
                playbae_printf("playbae: Sound normalize failed for '%s' (%d: %s); continuing without\n",
                    path, nerr, BAE_GetErrorString(nerr));
            } else if (gPassNumber <= 0) {
                playbae_printf("Sound normalize gain: %d%%\n", (int)gainPct);
            }
        }

        err = PV_PlaySound(mixer, sound, path, volume, timeLimitSec, loopCount);
        BAESound_Delete(sound);
        return err;
    }

    /* ---- Song file (MIDI / RMF / XMF / RMI / ringtone) ---- */
    BAESong song = BAESong_New(mixer);
    if (!song) return BAE_MEMORY_ERR;

    BAEResult err = BAE_NO_ERROR;

    if (ftype == BAE_RMF) {
        if (gPassNumber <= 0) playbae_printf("Playing RMF: %s\n", path);
        if (!gWriteToFile) print_rmf_info(path);
        err = BAESong_LoadRmfFromFile(song, (BAEPathName)path, 0, TRUE);
    }
#if USE_XMF_SUPPORT == TRUE && (_USING_FLUIDLITE == TRUE || USE_NATIVE_DLS == TRUE)
    else if (ftype == BAE_XMF) {
        if (gPassNumber <= 0) playbae_printf("Playing XMF: %s\n", path);
        err = BAESong_LoadXmfFromFile(song, (BAEPathName)path, TRUE);
    }
#endif
#if USE_RMI_SUPPORT == TRUE
    else if (ftype == BAE_RMI) {
        if (gPassNumber <= 0) playbae_printf("Playing RMI: %s\n", path);
        err = BAESong_LoadRmiFromFile(song, (BAEPathName)path, TRUE, TRUE);
    }
#endif
#if USE_RETRO_RINGTONE_SUPPORT == TRUE
    else if (ftype == BAE_RINGTONE_IMY || ftype == BAE_RINGTONE_RNG ||
             ftype == BAE_RINGTONE_RTX) {
        if (gPassNumber <= 0) playbae_printf("Playing ringtone: %s\n", path);
        unsigned char *midi = NULL;
        uint32_t midiSize   = 0;
        err = BAERingtone_ConvertToMidiFromFile((BAEPathName)path, ftype,
            &midi, &midiSize);
        if (err == BAE_NO_ERROR)
            err = BAESong_LoadMidiFromMemory(song, midi, midiSize, TRUE);
        BAERingtone_FreeMidiBuffer(midi);
    }
#endif
    else {
        /* Default: standard MIDI */
        if (gPassNumber <= 0) playbae_printf("Playing MIDI: %s\n", path);
        err = BAESong_LoadMidiFromFile(song, (BAEPathName)path, TRUE);
    }

    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't load '%s' (%d: %s)\n",
            path, err, BAE_GetErrorString(err));
        BAESong_Delete(song);
        return err;
    }

    err = PV_PlaySong(mixer, song, path,
        volume, timeLimitSec, loopCount, reverbType, muteChannels, soloChannel);
    BAESong_Delete(song);
    return err;
}

/* =========================================================================
 * Dual-bank overlay: same song on two mixers, summed in A's output callback
 * ========================================================================= */

static BAEResult PV_LoadSongOntoMixer(BAEMixer mixer, const char *path, BAESong *outSong)
{
    BAEFileType ftype;
    BAESong song;
    BAEResult err = BAE_NO_ERROR;

    if (!mixer || !path || !outSong)
        return BAE_PARAM_ERR;
    *outSong = NULL;

    ftype = X_DetermineFileType((BAEPathName)path);
    if (ftype == BAE_INVALID_TYPE)
        return BAE_BAD_FILE;

    switch (ftype) {
    case BAE_WAVE_TYPE:
    case BAE_AIFF_TYPE:
    case BAE_AU_TYPE:
#if USE_MPEG_DECODER == TRUE
    case BAE_MPEG_TYPE:
#endif
#if USE_FLAC_DECODER == TRUE
    case BAE_FLAC_TYPE:
#endif
#if USE_VORBIS_DECODER == TRUE
    case BAE_VORBIS_TYPE:
#endif
#if USE_OPUS_DECODER == TRUE
    case BAE_OPUS_TYPE:
#endif
#if USE_ADP_SUPPORT == TRUE
    case BAE_ADP_TYPE:
#endif
#if USE_ADX_SUPPORT == TRUE
    case BAE_ADX_TYPE:
#endif
#if USE_QOA_SUPPORT == TRUE
    case BAE_QOA_TYPE:
#endif
#if USE_WMA_SUPPORT == TRUE
    case BAE_WMA_TYPE:
#endif
        return BAE_UNSUPPORTED_FORMAT;
    default:
        break;
    }

    if (BAEMixer_MakeCurrent(mixer) != BAE_NO_ERROR)
        return BAE_NOT_SETUP;

    song = BAESong_New(mixer);
    if (!song)
        return BAE_MEMORY_ERR;

    if (ftype == BAE_RMF)
        err = BAESong_LoadRmfFromFile(song, (BAEPathName)path, 0, TRUE);
#if USE_XMF_SUPPORT == TRUE && (_USING_FLUIDLITE == TRUE || USE_NATIVE_DLS == TRUE)
    else if (ftype == BAE_XMF)
        err = BAESong_LoadXmfFromFile(song, (BAEPathName)path, TRUE);
#endif
#if USE_RMI_SUPPORT == TRUE
    else if (ftype == BAE_RMI)
        err = BAESong_LoadRmiFromFile(song, (BAEPathName)path, TRUE, TRUE);
#endif
#if USE_RETRO_RINGTONE_SUPPORT == TRUE
    else if (ftype == BAE_RINGTONE_IMY || ftype == BAE_RINGTONE_RNG ||
             ftype == BAE_RINGTONE_RTX) {
        unsigned char *midi = NULL;
        uint32_t midiSize = 0;
        err = BAERingtone_ConvertToMidiFromFile((BAEPathName)path, ftype, &midi, &midiSize);
        if (err == BAE_NO_ERROR)
            err = BAESong_LoadMidiFromMemory(song, midi, midiSize, TRUE);
        BAERingtone_FreeMidiBuffer(midi);
    }
#endif
    else
        err = BAESong_LoadMidiFromFile(song, (BAEPathName)path, TRUE);

    if (err != BAE_NO_ERROR) {
        BAESong_Delete(song);
        return err;
    }
    *outSong = song;
    return BAE_NO_ERROR;
}

static void PV_ApplySongPrep(BAEMixer mixer, BAESong song, BAEReverbType reverbType)
{
    if (gVelocityCurve >= 0)
        BAESong_SetVelocityCurve(song, gVelocityCurve);
#if USE_SF2_SUPPORT == TRUE
    else if (BAESong_IsSF2Song(song))
        BAESong_SetVelocityCurve(song, 1);
#endif

    BAEMixer_MakeCurrent(mixer);
    if (gHasCustomReverb) {
        BAEMixer_SetDefaultReverb(mixer, gCustomReverbType);
        SetNeoCustomReverbCombCount(gCustomReverbCombCount);
        for (int i = 0; i < 4; i++) {
            SetNeoCustomReverbCombDelay(i, gCustomReverbDelays[i]);
            SetNeoCustomReverbCombFeedback(i, gCustomReverbFeedback[i]);
            SetNeoCustomReverbCombGain(i, gCustomReverbGain[i]);
        }
        SetNeoCustomReverbLowpass(gCustomReverbLowpass);
        SetNeoReverbMix(gCustomReverbMix);
    } else {
        BAEMixer_SetDefaultReverb(mixer, reverbType);
    }
}

static BAEResult PV_PlayDualFile(BAEMixer mixerA, BAEMixer mixerB, const char *path,
    unsigned int timeLimitSec, unsigned int loopCount,
    BAEReverbType reverbType, char *muteChannels)
{
#if USE_CALLBACKS != TRUE
    (void)mixerA; (void)mixerB; (void)path;
    (void)timeLimitSec; (void)loopCount; (void)reverbType; (void)muteChannels;
    playbae_printf("playbae: Dual-bank overlay requires USE_CALLBACKS.\n");
    return BAE_UNSUPPORTED_FORMAT;
#else
    BAESong songA = NULL, songB = NULL;
    BAEResult err;
    BAE_UNSIGNED_FIXED songVol = volume_pct_to_fixed(100);
    unsigned int effectiveLoopCount = loopCount;

    playbae_printf("Dual-bank overlay: bank A %d%% (pan %+d) + bank B %d%% (pan %+d)\n",
        gVolumePct, gDualBal1, gVolume2Pct, gDualBal2);
    playbae_printf("Playing (dual): %s\n", path);

    err = PV_LoadSongOntoMixer(mixerA, path, &songA);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't load '%s' on bank A (%d: %s)\n",
            path, err, BAE_GetErrorString(err));
        return err;
    }
    err = PV_LoadSongOntoMixer(mixerB, path, &songB);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't load '%s' on bank B (%d: %s)\n",
            path, err, BAE_GetErrorString(err));
        BAESong_Delete(songA);
        return err;
    }

    PV_ApplySongPrep(mixerA, songA, reverbType);
    PV_ApplySongPrep(mixerB, songB, reverbType);

    {
        int32_t normalizeGainPct = 100;
        BAEMixer_MakeCurrent(mixerA);
        (void)BAESong_ApplyNormalizeFromMidiEstimate(
            songA, mixerA, gNormalize ? TRUE : FALSE,
            BAE_NORMALIZE_DEFAULT_TARGET_PEAK_PCT, &normalizeGainPct);
        BAEMixer_MakeCurrent(mixerB);
        (void)BAESong_ApplyNormalizeFromMidiEstimate(
            songB, mixerB, gNormalize ? TRUE : FALSE,
            BAE_NORMALIZE_DEFAULT_TARGET_PEAK_PCT, &normalizeGainPct);
        BAEMixer_MakeCurrent(mixerA);
    }

    /* Sync start: preroll both, then Start back-to-back on this thread. */
    BAESong_Preroll(songA);
    BAESong_Preroll(songB);
    err = BAESong_Start(songA, 0);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't start bank A (%d: %s)\n",
            err, BAE_GetErrorString(err));
        BAESong_Delete(songA);
        BAESong_Delete(songB);
        return err;
    }
    err = BAESong_Start(songB, 0);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Couldn't start bank B (%d: %s)\n",
            err, BAE_GetErrorString(err));
        BAESong_Stop(songA, 0);
        BAESong_Delete(songA);
        BAESong_Delete(songB);
        return err;
    }

    BAESong_SetVolume(songA, songVol);
    BAESong_SetVolume(songB, songVol);
    if (muteChannels && muteChannels[0]) {
        MuteChannels(songA, muteChannels);
        MuteChannels(songB, muteChannels);
    }

    {
        BAEReverbType savedVerb;
        BAEMixer_MakeCurrent(mixerA);
        BAEMixer_GetDefaultReverb(mixerA, &savedVerb);
        BAEMixer_SetDefaultReverb(mixerA, BAE_REVERB_NONE);
        BAEMixer_SetDefaultReverb(mixerA, savedVerb);
        BAEMixer_MakeCurrent(mixerB);
        BAEMixer_GetDefaultReverb(mixerB, &savedVerb);
        BAEMixer_SetDefaultReverb(mixerB, BAE_REVERB_NONE);
        BAEMixer_SetDefaultReverb(mixerB, savedVerb);
        BAEMixer_MakeCurrent(mixerA);
    }

    /* Honor -l for live and -o offline (same SetLoops quirk as single-bank path). */
    if (loopCount > 0) {
        BAESong_SetLoops(songA, 30000);
        BAESong_SetLoops(songB, 30000);
    } else {
        BAESong_SetLoops(songA, 0);
        BAESong_SetLoops(songB, 0);
    }

    if (gPassNumber <= 0) {
        playbae_printf("Reverb: %s (%d)\n", reverbTypeName(reverbType), (int)reverbType);
        if (effectiveLoopCount > 0)
            playbae_printf("Will loop %u time(s)\n", effectiveLoopCount);
        if (timeLimitSec > 0)
            playbae_printf("Time limit: %u sec\n", timeLimitSec);
        print_song_engine_config(songA);
    }

    if (gWriteToFile) {
        BAEResult perr = BAEMixer_PrimeAudioOutputToFile(mixerA, songA);
        if (perr != BAE_NO_ERROR) {
            playbae_printf("Export priming failed (%d: %s).\n",
                perr, BAE_GetErrorString(perr));
            BAESong_Stop(songA, 0);
            BAESong_Stop(songB, 0);
            BAESong_Delete(songA);
            BAESong_Delete(songB);
            return perr;
        }
    }

    {
        uint64_t lastPos = 0;
        uint64_t cumulative = 0;
        unsigned int loopsDone = 0;
        BAE_BOOL done = FALSE;
        gPosCounter = 0;

        while (!done) {
            if (gInterrupt) {
                playbae_printf("\nStop requested...\n");
                gInterrupt = 0;
                BAESong_Stop(songA, gFadeOut);
                BAESong_Stop(songB, gFadeOut);
            }

            if (gWriteToFile)
                BAEMixer_ServiceAudioOutputToFile(mixerA);

            BAESong_IsDone(songA, &done);

            {
                uint64_t posUs = 0;
                BAESong_GetMicrosecondPosition64(songA, &posUs);
                uint64_t posMs = posUs / 1000ull;

                if (posMs < lastPos && (lastPos - posMs) > 1000ull) {
                    cumulative += lastPos;
                    if (effectiveLoopCount > 0) {
                        loopsDone++;
                        if (loopsDone >= effectiveLoopCount) {
                            BAESong_Stop(songA, gFadeOut);
                            BAESong_Stop(songB, gFadeOut);
                        }
                    }
                }
                lastPos = posMs;

                {
                    uint64_t totalMs = cumulative + posMs;
                    display_song_position(
                        (posMs > UINT32_MAX) ? UINT32_MAX : (uint32_t)posMs,
                        (totalMs > UINT32_MAX) ? UINT32_MAX : (uint32_t)totalMs);

                    if (timeLimitSec > 0 && totalMs > (uint64_t)timeLimitSec * 1000ull - 750ull) {
                        BAESong_Stop(songA, gFadeOut);
                        BAESong_Stop(songB, gFadeOut);
                    }
                }
            }

            if (!done) PV_Idle(mixerA, 15000);
        }
    }

    BAESong_Stop(songB, 0);
    PV_Idle(mixerA, 900000);
    if (gPassNumber <= 0) playbae_printf("\n");

    BAESong_Delete(songA);
    BAESong_Delete(songB);
    return BAE_NO_ERROR;
#endif
}

/* =========================================================================
 * EQ Helpers
 * ========================================================================= */
static void apply_eq_state(BAEMixer mixer)
{
    if (mixer) {
        BAEMixer_SetEQEnabled(mixer, gEqEnabled ? TRUE : FALSE);
        if (gEqEnabled) {
            for (int i = 0; i < 5; i++) {
                BAEMixer_SetEQGain(mixer, i, gEqGains[i]);
            }
        }
    }
}

/* Resolve zefidi.ini under the shared NeoBAE config root (migrate from exeDir once). */
static void playbae_get_zefidi_ini_path(char *out, size_t outSize)
{
    static int s_prepared = 0;

    if (!out || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!s_prepared)
    {
        (void)BAE_PrepareConfigFile("zefidi.ini", NULL);
        s_prepared = 1;
    }
    if (BAE_GetConfigFilePath(out, outSize, "zefidi.ini") != 0)
    {
        (void)BAE_GetRuntimeFilePath(out, outSize, "zefidi.ini");
    }
}

static int load_eq_preset(const char *name, float *gains)
{
    /* Standard presets */
    const char *std_names[] = {"Flat", "Bass Boost", "Acoustic", "Rock", "Pop", "Classical", "Vocal"};
    const float std_gains[][5] = {
        {0, 0, 0, 0, 0},        // Flat
        {8, 4, 0, 0, 0},        // Bass Boost
        {4, 2, 0, 2, 4},        // Acoustic
        {6, 2, -2, 2, 6},       // Rock
        {-2, 4, 6, 4, -2},      // Pop
        {6, 4, 0, 2, 4},        // Classical
        {-4, 0, 6, 4, -2}       // Vocal
    };
    
    for (int i = 0; i < 7; i++) {
        if (stricmp(name, std_names[i]) == 0) {
            for (int j = 0; j < 5; j++) gains[j] = std_gains[i][j];
            return 1;
        }
    }
    
    /* Attempt to load from zefidi.ini */
    char iniPath[1024] = {0};
    playbae_get_zefidi_ini_path(iniPath, sizeof(iniPath));
    
    FILE *f = fopen(iniPath, "r");
    if (!f) return 0;
    
    char line[512];
    int found_idx = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "custom_eq_", 10) == 0) {
            char *p = line + 10;
            int idx = atoi(p);
            char *pname = strstr(p, "_name=");
            if (pname) {
                pname += 6;
                char *nl = strchr(pname, '\n'); if (nl) *nl = '\0';
                nl = strchr(pname, '\r'); if (nl) *nl = '\0';
                if (stricmp(name, pname) == 0) {
                    found_idx = idx;
                    break;
                }
            }
        }
    }
    
    if (found_idx >= 0) {
        rewind(f);
        char prefix[64];
        sprintf(prefix, "custom_eq_%d_", found_idx);
        int prefix_len = strlen(prefix);
        
        int found_gains = 0;
        while (fgets(line, sizeof(line), f)) {
            char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
            nl = strchr(line, '\r'); if (nl) *nl = '\0';
            
            if (strncmp(line, prefix, prefix_len) == 0) {
                char *key = line + prefix_len;
                char *val = strchr(key, '=');
                if (val) {
                    *val = '\0';
                    val++;
                    if (strncmp(key, "gain_", 5) == 0) {
                        int band = atoi(key + 5);
                        if (band >= 0 && band < 5) {
                            gains[band] = atof(val);
                            found_gains++;
                        }
                    }
                }
            }
        }
        fclose(f);
        return (found_gains > 0) ? 1 : 0;
    }
    fclose(f);
    return 0;
}

static int load_reverb_preset(const char *name)
{
    /* Attempt to load from zefidi.ini */
    char iniPath[1024] = {0};
    playbae_get_zefidi_ini_path(iniPath, sizeof(iniPath));
    
    FILE *f = fopen(iniPath, "r");
    if (!f) return 0;
    
    char line[512];
    int found_idx = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "custom_reverb_", 14) == 0) {
            char *p = line + 14;
            int idx = atoi(p);
            char *pname = strstr(p, "_name=");
            if (pname) {
                pname += 6;
                char *nl = strchr(pname, '\n'); if (nl) *nl = '\0';
                nl = strchr(pname, '\r'); if (nl) *nl = '\0';
                if (stricmp(name, pname) == 0) {
                    found_idx = idx;
                    break;
                }
            }
        }
    }
    
    if (found_idx >= 0) {
        rewind(f);
        char prefix[64];
        sprintf(prefix, "custom_reverb_%d_", found_idx);
        int prefix_len = strlen(prefix);
        
        while (fgets(line, sizeof(line), f)) {
            char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
            nl = strchr(line, '\r'); if (nl) *nl = '\0';
            if (strncmp(line, prefix, prefix_len) == 0) {
                char *key = line + prefix_len;
                char *val = strchr(key, '=');
                if (val) {
                    *val = '\0';
                    val++;
                    if (strcmp(key, "comb_count") == 0) {
                        gCustomReverbCombCount = atoi(val);
                    } else if (strncmp(key, "delay_", 6) == 0) {
                        int i = atoi(key + 6);
                        if (i >= 0 && i < 4) gCustomReverbDelays[i] = atoi(val);
                    } else if (strncmp(key, "feedback_", 9) == 0) {
                        int i = atoi(key + 9);
                        if (i >= 0 && i < 4) gCustomReverbFeedback[i] = atoi(val);
                    } else if (strncmp(key, "gain_", 5) == 0) {
                        int i = atoi(key + 5);
                        if (i >= 0 && i < 4) gCustomReverbGain[i] = atoi(val);
                    } else if (strcmp(key, "lowpass") == 0) {
                        gCustomReverbLowpass = atoi(val);
                    } else if (strcmp(key, "mix") == 0) {
                        gCustomReverbMix = atoi(val);
                    } else if (strcmp(key, "type") == 0) {
                        gCustomReverbType = (BAEReverbType)atoi(val);
                    }
                }
            }
        }
        gHasCustomReverb = 1;
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/* =========================================================================
 * PV_LoadBank – thin wrapper around shared BAEMixer_LoadBankFromPath
 * ========================================================================= */

static int PV_LoadBank(BAEMixer mixer, const char *path, BAEBankToken *tokenOut)
{
    BAEBankLoadInfo info;
    BAEResult err = BAEMixer_LoadBankFromPath(mixer, (BAEPathName)path, &info);
    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: Bank load failed (%d): %s\n", err, path);
        return 0;
    }
    if (tokenOut) *tokenOut = info.token;
    return 1;
}

/* =========================================================================
 * main()
 * ========================================================================= */

int main(int argc, char *argv[])
{
    init_playFileString();
    signal(SIGINT, intHandler);
#ifndef _VERSION
    #define _VERSION "unknown"
#endif
    gCustomReverbType = gDefaultReverbIndex;
    /* ---- Early flags: quiet / verbose ---- */
    if (PV_ParseCommands(argc, argv, "-q", 0, NULL)) { gSilent = 1; gVerbose = 0; }
    if (PV_ParseCommands(argc, argv, "-d", 0, NULL)) { gSilent = 0; gVerbose = 1; }

    /* ---- Version banner ---- */
    if (!gSilent) {
        const char *ver  = BAE_GetVersion();
        const char *comp = BAE_GetCompileInfo();
        const char *arch = BAE_GetCurrentCPUArchitecture();
        const char *feat = BAE_GetFeatureString();
        playbae_printf("playbae %s built with %s, playbae %s, libNeoBAE %s\nfeatures: %s\n",
            arch, comp, _VERSION, ver, feat);
        playbae_printf("Copyright (C) 2009 Beatnik, Inc and"
            " Copyright (C) 2021-2026 Zefie Networks. All rights reserved.\n");
        if (comp) free((void *)comp);
        if (ver)  free((void *)ver);
    }

    /* ---- Informational flags (no mixer needed) ---- */
    if (PV_ParseCommands(argc, argv, "-rl", 0, NULL)) { playbae_printf(reverbList);   return 0; }
    if (PV_ParseCommands(argc, argv, "-cl", 0, NULL)) { playbae_printf(velocityList); return 0; }
    if (PV_ParseCommands(argc, argv, "-h",  0, NULL)) { playbae_printf(usageMain, gPlayFileString, gDefaultReverbIndex); return 0; }
    if (PV_ParseCommands(argc, argv, "-x",  0, NULL)) { playbae_printf(usageExtra);   return 0; }

    /* ---- Parse --script ---- */
#if SUPPORT_BAESCRIPT == TRUE
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i+1 < argc) {
            gScript = BAEScript_LoadFile(argv[i+1]);
            if (!gScript) {
                playbae_printf("Failed to load script: %s\n", argv[i+1]);
                return 1;
            }
            playbae_printf("Loaded BAEScript: %s\n", argv[i+1]);
            break;
        }
    }
#endif

    /* ---- Parse -b (bitrate) early ---- */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-b", 2) == 0) {
            const char *val = (argv[i][2] != '\0') ? &argv[i][2]
                : ((i+1 < argc) ? argv[i+1] : NULL);
            if (val) {
                int kb = atoi(val);
                if (kb >= 16 && kb <= 640) gMP3BitrateKbps = kb;
            }
        }
    }

    /* ---- Parse EQ flags ---- */
    if (PV_ParseCommands(argc, argv, "-eq", 0, NULL)) {
        gEqEnabled = 1;
    }
    char eqTmpBuf[1024] = {0};
    if (PV_ParseCommands(argc, argv, "-eqg", 1, eqTmpBuf)) {
        float g1, g2, g3, g4, g5;
        if (sscanf(eqTmpBuf, "%f,%f,%f,%f,%f", &g1, &g2, &g3, &g4, &g5) == 5) {
            gEqGains[0] = g1; gEqGains[1] = g2; gEqGains[2] = g3; gEqGains[3] = g4; gEqGains[4] = g5;
            gEqEnabled = 1;
        } else {
            playbae_printf("playbae: Invalid format for -eqg. Use g1,g2,g3,g4,g5\n");
            return 1;
        }
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--eqp") == 0 && i+1 < argc) {
            if (load_eq_preset(argv[i+1], gEqGains)) {
                gEqEnabled = 1;
                playbae_printf("Loaded EQ preset: %s\n", argv[i+1]);
            } else {
                playbae_printf("playbae: EQ preset not found: %s\n", argv[i+1]);
                return 1;
            }
            break;
        }
    }

    /* ---- Parse -vc (velocity curve) ---- */
    char tmpBuf[1024] = {0};
    if (PV_ParseCommands(argc, argv, "-vc", 1, tmpBuf)) {
        gVelocityCurve = atoi(tmpBuf);
        if (gVelocityCurve < 0 || gVelocityCurve > 5) {
            playbae_printf("Invalid velocity curve %d (0-5), using 1.\n", gVelocityCurve);
            gVelocityCurve = 1;
        }
        BAE_SetDefaultVelocityCurve(gVelocityCurve);
    }

#if SUPPORT_KARAOKE == TRUE
    if (PV_ParseCommands(argc, argv, "-k", 0, NULL)) gKaraokeEnabled = 1;
#endif

    /* ---- Mixer parameters ---- */
    BAERate     sampleRate = BAE_RATE_44K; /* 44100 Hz */
    BAETerpMode interpol   = BAE_LINEAR_INTERPOLATION;
    BAEAudioModifiers mods = BAE_USE_16 | BAE_USE_STEREO;
    int         maxVoices  = 64;

    if (PV_ParseCommands(argc, argv, "-mr", 1, tmpBuf)) {
        int hz = atoi(tmpBuf);
        if (hz > 0) sampleRate = (BAERate)hz;
        else playbae_printf("Invalid sample rate '%s', using 44100\n", tmpBuf);
    }
    if (PV_ParseCommands(argc, argv, "-ns", 0, NULL))
        mods &= ~BAE_USE_STEREO; /* mono */
    if (PV_ParseCommands(argc, argv, "-2p", 0, NULL))
        interpol = BAE_2_POINT_INTERPOLATION;
    if (PV_ParseCommands(argc, argv, "-sinc", 0, NULL))
        interpol = BAE_SINC_INTERPOLATION;
    if (PV_ParseCommands(argc, argv, "-8", 0, NULL))
        mods &= ~BAE_USE_16;
    if (PV_ParseCommands(argc, argv, "-mv", 1, tmpBuf)) {
        maxVoices = atoi(tmpBuf);
        if (maxVoices < BAE_MIN_VOICES) maxVoices = BAE_MIN_VOICES;
        if (maxVoices > BAE_MAX_VOICES) maxVoices = BAE_MAX_VOICES;
    }

    /* ---- Playback parameters ---- */
    unsigned int       loopCount    = 0;
    unsigned int       timeLimitSec = 0;
    BAE_UNSIGNED_FIXED volume       = volume_pct_to_fixed(100);
    BAEReverbType      reverbType   = BAE_REVERB_TYPE_7; /* small reflections */
    char               muteChannels[512] = {0};
    char               parmFile[1024]    = {0};
    char               bank2Path[1024]   = {0};
    int                volumeSpecified   = 0;
    int                volume2Specified  = 0;

    if (PV_ParseCommands(argc, argv, "-p2", 1, bank2Path)) {
        gDualMode = 1;
        strncpy(gBank2FilePath, bank2Path, sizeof(gBank2FilePath) - 1);
        gBank2FilePath[sizeof(gBank2FilePath) - 1] = '\0';
    }

    if (PV_ParseCommands(argc, argv, "-l",  1, tmpBuf)) loopCount    = (unsigned)atoi(tmpBuf);
    if (PV_ParseCommands(argc, argv, "-t",  1, tmpBuf)) timeLimitSec = (unsigned)atoi(tmpBuf);
    if (PV_ParseCommands(argc, argv, "-mc", 1, tmpBuf))
        strncpy(muteChannels, tmpBuf, sizeof(muteChannels)-1);

#if USE_NATIVE_DLS == TRUE
    if (PV_ParseCommands(argc, argv, "-dlscompat", 0, NULL)) gDLSCompatibilityMode = 1;
    GM_DLS_SetMobileBAEQuirks(gDLSCompatibilityMode ? false : true); // invert
#endif
    if (PV_ParseCommands(argc, argv, "-nf", 0, NULL))   gFadeOut = 0;
    if (PV_ParseCommands(argc, argv, "-n",  0, NULL))   gNormalize = 1;
    if (PV_ParseCommands(argc, argv, "-v",  1, tmpBuf)) {
        volumeSpecified = 1;
        gVolumePct = atoi(tmpBuf);
        if (gVolumePct < 0)   gVolumePct = 0;
        if (gVolumePct > 400) gVolumePct = 400;
        /* PCM/stream object volume (clamped 0-100). MIDI/RMF uses OutputGain
         * (gVolumePct) with song volume held at unity in PV_PlaySong. */
        volume = volume_pct_to_fixed(gVolumePct);
    }
    if (PV_ParseCommands(argc, argv, "-v2", 1, tmpBuf)) {
        volume2Specified = 1;
        gVolume2Pct = atoi(tmpBuf);
        if (gVolume2Pct < 0)   gVolume2Pct = 0;
        if (gVolume2Pct > 400) gVolume2Pct = 400;
    }
    if (PV_ParseCommands(argc, argv, "-bal1", 1, tmpBuf)) {
        gDualBal1 = atoi(tmpBuf);
        if (gDualBal1 < -100) gDualBal1 = -100;
        if (gDualBal1 > 100)  gDualBal1 = 100;
    }
    if (PV_ParseCommands(argc, argv, "-bal2", 1, tmpBuf)) {
        gDualBal2 = atoi(tmpBuf);
        if (gDualBal2 < -100) gDualBal2 = -100;
        if (gDualBal2 > 100)  gDualBal2 = 100;
    }
    /* Dual defaults: 50/50 unless user set -v / -v2 */
    if (gDualMode) {
        if (!volumeSpecified) {
            gVolumePct = 50;
            volume = volume_pct_to_fixed(100); /* song full; mixer gain does -v */
        }
        if (!volume2Specified)
            gVolume2Pct = 50;
        PV_DualUpdatePanGains();
    }
    if (PV_ParseCommands(argc, argv, "-rv", 1, tmpBuf)) {
        int rv = atoi(tmpBuf);
        if (rv < 0 || rv > 18) { playbae_printf("Invalid reverb %d (0-18). Using %d.\n", rv, gDefaultReverbIndex); rv = gDefaultReverbIndex; }
        reverbType = (BAEReverbType)rv;
    }
    if (PV_ParseCommands(argc, argv, "--rvp", 1, tmpBuf)) {
        if (!load_reverb_preset(tmpBuf)) {
            playbae_printf("playbae: Custom reverb preset '%s' not found in zefidi.ini.\n", tmpBuf);
        }
    }

    /* ---- Create mixer (mirrors bae_init in gui_bae.c) ---- */
    BAEMixer mixer = BAEMixer_New();
    if (!mixer) {
        playbae_printf("playbae: BAEMixer_New failed\n");
        return 1;
    }
    BAEMixer mixerB = NULL;

    playbae_dprintf("Opening mixer: %d Hz, %d MIDI voices, 8 sound voices, 64 mix level\n",
        (int)sampleRate, maxVoices);

    /*
     * Open with the same parameters as the GUI's bae_init():
     *   maxMidiVoices  = 64 (or -mv override)
     *   maxSoundVoices = 8   (same as GUI)
     *   mixLevel       = 64  (same as GUI)
     */
    BAEResult err = BAEMixer_Open(mixer,
        sampleRate,
        interpol,
        mods,
        (int16_t)maxVoices, /* MIDI voices */
        8,                  /* sound voices (GUI default) */
        64,                 /* mix level    (GUI default) */
        TRUE);

    if (err != BAE_NO_ERROR) {
        playbae_printf("playbae: BAEMixer_Open failed (%d: %s)\n",
            err, BAE_GetErrorString(err));
        BAEMixer_Delete(mixer);
        return 1;
    }

    BAEMixer_SetAudioTask(mixer, PV_AudioTask, (void *)mixer);
    BAEMixer_MakeCurrent(mixer);
    apply_eq_state(mixer);

    /* ---- Output gain (applies for all volume, not just overdrive) ---- */
    if (!gDualMode)
        apply_output_gain(mixer);
    else
        apply_output_gain_pct(mixer, gVolumePct);

    /* ---- Engine tweaks ---- */
#if BAE_FIX_SPAN_DC
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--panfix=off") == 0) {
            BAE_SetSpanDCFix(FALSE);
            playbae_dprintf("STEREO_PAN LFO DC fix disabled\n");
            break;
        }
    }
#endif
#if BAE_CLASSIC_CHORUS
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--classicchorus") == 0 ||
            strcmp(argv[i], "--classicchorus=on") == 0) {
            BAE_SetClassicChorus(TRUE);
            playbae_dprintf("Classic chorus enabled\n");
            break;
        }
    }
#endif
#if USE_WEBTV_MODE
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--webtv") == 0 ||
            strcmp(argv[i], "--webtv=on") == 0) {
            BAE_SetWebTVMode(TRUE);
            playbae_dprintf("WebTV Mode enabled\n");
            break;
        }
    }
#endif

    /* ---- Parse -oc ---- */
    char tmpBuf2[1024] = {0};
    if (PV_ParseCommands(argc, argv, "-oc", 1, tmpBuf2)) {
        strncpy(gVoiceCaptureDir, tmpBuf2, sizeof(gVoiceCaptureDir)-1);
        gVoiceCaptureDir[sizeof(gVoiceCaptureDir)-1] = '\0';
    }

    /* ---- Load patch bank ---- */
    BAEBankToken bankToken = 0;

    if (PV_ParseCommands(argc, argv, "-p", 1, parmFile)) {
        if (!PV_LoadBank(mixer, parmFile, &bankToken)) {
            BAEMixer_Delete(mixer);
            return 1;
        }
        strncpy(gBankFilePath, parmFile, sizeof(gBankFilePath)-1);
        gBankFilePath[sizeof(gBankFilePath)-1] = '\0';
        char friendly[128] = {0};
        if (bankToken &&
            BAE_GetBankFriendlyName(mixer, bankToken, friendly, sizeof(friendly)) == BAE_NO_ERROR
            && friendly[0]) {
            playbae_printf("Bank: %s (%s)\n", parmFile, friendly);
        } else {
            playbae_printf("Bank: %s\n", parmFile);
        }
    } else {
        if (gDualMode) {
            playbae_printf("playbae: Dual-bank mode requires -p (bank A) and -p2 (bank B).\n");
            BAEMixer_Delete(mixer);
            return 1;
        }
#if _BUILT_IN_PATCHES == TRUE
        err = BAEMixer_LoadBuiltinBank(mixer, &bankToken);
        if (err == BAE_NO_ERROR) {
            char friendly[128] = {0};
            if (BAE_GetBankFriendlyName(mixer, bankToken, friendly, sizeof(friendly)) == BAE_NO_ERROR
                && friendly[0])
                playbae_printf("Bank: built-in (%s)\n", friendly);
            else
                playbae_printf("Bank: built-in\n");
        } else {
            playbae_printf("playbae: No -p bank and built-in bank failed (%d).\n", err);
            playbae_printf(usageMain, gPlayFileString, gDefaultReverbIndex);
            BAEMixer_Delete(mixer);
            return 1;
        }
#else
        playbae_printf("playbae: -p is required (no built-in patches compiled in).\n");
        playbae_printf(usageMain, gPlayFileString, gDefaultReverbIndex);
        BAEMixer_Delete(mixer);
        return 1;
#endif
    }

    /* ---- Dual mixer B (not engaged; pulled from A's output callback) ---- */
    if (gDualMode) {
#if USE_CALLBACKS != TRUE
        playbae_printf("playbae: Dual-bank overlay requires USE_CALLBACKS.\n");
        BAEMixer_Delete(mixer);
        return 1;
#else
        mixerB = BAEMixer_New();
        if (!mixerB) {
            playbae_printf("playbae: BAEMixer_New (bank B) failed\n");
            BAEMixer_Delete(mixer);
            return 1;
        }
        err = BAEMixer_Open(mixerB, sampleRate, interpol, mods,
            (int16_t)maxVoices, 8, 64, FALSE);
        if (err != BAE_NO_ERROR && err != BAE_UNSUPPORTED_HARDWARE) {
            playbae_printf("playbae: BAEMixer_Open (bank B) failed (%d: %s)\n",
                err, BAE_GetErrorString(err));
            BAEMixer_Delete(mixerB);
            BAEMixer_Delete(mixer);
            return 1;
        }
        gDualMixerB = mixerB;
        BAEMixer_SetAudioTask(mixerB, PV_AudioTask, (void *)mixerB);
        apply_eq_state(mixerB);
        apply_output_gain_pct(mixerB, gVolume2Pct);

        {
            BAEBankToken bankTokenB = 0;
            if (!PV_LoadBank(mixerB, gBank2FilePath, &bankTokenB)) {
                BAEMixer_Delete(mixerB);
                gDualMixerB = NULL;
                BAEMixer_Delete(mixer);
                return 1;
            }
            {
                char friendly[128] = {0};
                if (bankTokenB &&
                    BAE_GetBankFriendlyName(mixerB, bankTokenB, friendly, sizeof(friendly)) == BAE_NO_ERROR
                    && friendly[0]) {
                    playbae_printf("Bank B: %s (%s)\n", gBank2FilePath, friendly);
                } else {
                    playbae_printf("Bank B: %s\n", gBank2FilePath);
                }
            }
        }

        if (!PV_DualMixSetup(mixer, mixerB)) {
            playbae_printf("playbae: Dual mix setup failed\n");
            BAEMixer_Delete(mixerB);
            gDualMixerB = NULL;
            BAEMixer_Delete(mixer);
            return 1;
        }
#endif
    }

    /* ---- Set up file export (-o) ---- */
    if (PV_ParseCommands(argc, argv, "-o", 1, parmFile)) {
        gPosInterval = 100; /* less frequent display during fast export */

        if (PV_IsFileExtension(parmFile, ".mp3") ||
            PV_IsFileExtension(parmFile, ".mp2") ||
            PV_IsFileExtension(parmFile, ".mpg")) {
#if defined(USE_MPEG_ENCODER) && (USE_MPEG_ENCODER != 0)
            int totalReq = gMP3BitrateKbps;
            if (totalReq < 32) {
                playbae_printf("MP3 minimum bitrate is 32 kbps (got %d). Aborting.\n", totalReq);
                BAEMixer_Delete(mixer); return 1;
            }
            if (totalReq > 320) totalReq = 320;
            static const struct { int rate; BAECompressionType ct; } mp3Map[] = {
                {32,BAE_COMPRESSION_MPEG_32},{40,BAE_COMPRESSION_MPEG_40},
                {48,BAE_COMPRESSION_MPEG_48},{56,BAE_COMPRESSION_MPEG_56},
                {64,BAE_COMPRESSION_MPEG_64},{80,BAE_COMPRESSION_MPEG_80},
                {96,BAE_COMPRESSION_MPEG_96},{112,BAE_COMPRESSION_MPEG_112},
                {128,BAE_COMPRESSION_MPEG_128},{160,BAE_COMPRESSION_MPEG_160},
                {192,BAE_COMPRESSION_MPEG_192},{224,BAE_COMPRESSION_MPEG_224},
                {256,BAE_COMPRESSION_MPEG_256},{320,BAE_COMPRESSION_MPEG_320}};
            BAECompressionType compType = BAE_COMPRESSION_MPEG_128;
            int best = 100000;
            for (size_t i = 0; i < sizeof(mp3Map)/sizeof(mp3Map[0]); i++) {
                int d = abs(mp3Map[i].rate - totalReq);
                if (d < best) { best = d; compType = mp3Map[i].ct; }
            }
            BAEAudioModifiers modsTmp; BAEMixer_GetModifiers(mixer, &modsTmp);
            int ch = (modsTmp & BAE_USE_STEREO) ? 2 : 1;
            err = BAEMixer_StartOutputToFile(mixer, (BAEPathName)parmFile,
                BAE_MPEG_TYPE, compType);
            apply_eq_state(mixer);
            if (err) { playbae_printf("Error %d starting MP3 export: %s\n", err, parmFile); BAEMixer_Delete(mixer); return 1; }
            gWriteToFile = 1; gWriteToFileType = BAE_MPEG_TYPE;
#if SUPPORT_KARAOKE == TRUE
            gKaraokeEnabled = 0;
#endif
            playbae_printf("Writing MP3 (CBR %d kbps, %s) to %s\n",
                totalReq, ch > 1 ? "joint stereo" : "mono", parmFile);
#else
            playbae_printf("MP3 encoder not built. Rebuild with MP3_ENC=1.\n");
            BAEMixer_Delete(mixer); return 1;
#endif
        } else if (PV_IsFileExtension(parmFile, ".flac")) {
#if defined(USE_FLAC_ENCODER) && (USE_FLAC_ENCODER != 0)
            err = BAEMixer_StartOutputToFile(mixer, (BAEPathName)parmFile,
                BAE_FLAC_TYPE, BAE_COMPRESSION_LOSSLESS);
            apply_eq_state(mixer);
            if (err) { playbae_printf("Error %d starting FLAC export: %s\n", err, parmFile); BAEMixer_Delete(mixer); return 1; }
            gWriteToFile = 1; gWriteToFileType = BAE_FLAC_TYPE;
            playbae_printf("Writing FLAC to %s\n", parmFile);
#else
            playbae_printf("FLAC encoder not built. Rebuild with FLAC_ENC=1.\n");
            BAEMixer_Delete(mixer); return 1;
#endif
        } else if (PV_IsFileExtension(parmFile, ".ogg")) {
#if defined(USE_VORBIS_ENCODER) && (USE_VORBIS_ENCODER != 0)
            err = BAEMixer_StartOutputToFile(mixer, (BAEPathName)parmFile,
                BAE_VORBIS_TYPE, BAE_COMPRESSION_VORBIS_256);
            apply_eq_state(mixer);
            if (err) { playbae_printf("Error %d starting Ogg Vorbis export: %s\n", err, parmFile); BAEMixer_Delete(mixer); return 1; }
            gWriteToFile = 1; gWriteToFileType = BAE_VORBIS_TYPE;
            playbae_printf("Writing Ogg Vorbis to %s\n", parmFile);
#else
            playbae_printf("Ogg Vorbis encoder not built. Rebuild with VORBIS_ENC=1.\n");
            BAEMixer_Delete(mixer); return 1;
#endif
        } else if (PV_IsFileExtension(parmFile, ".opus")) {
#if defined(USE_OPUS_ENCODER) && (USE_OPUS_ENCODER != 0)
            int totalReq = gMP3BitrateKbps;
            if (totalReq < 16) totalReq = 16;
            if (totalReq > 320) totalReq = 320;
            static const struct { int rate; BAECompressionType ct; } opusMap[] = {
                {16,BAE_COMPRESSION_OPUS_16},{32,BAE_COMPRESSION_OPUS_32},
                {64,BAE_COMPRESSION_OPUS_64},{96,BAE_COMPRESSION_OPUS_96},
                {128,BAE_COMPRESSION_OPUS_128},{256,BAE_COMPRESSION_OPUS_256}};
            BAECompressionType compType = BAE_COMPRESSION_OPUS_128;
            int best = 100000;
            for (size_t i = 0; i < sizeof(opusMap)/sizeof(opusMap[0]); i++) {
                int d = abs(opusMap[i].rate - totalReq);
                if (d < best) { best = d; compType = opusMap[i].ct; }
            }
            err = BAEMixer_StartOutputToFile(mixer, (BAEPathName)parmFile,
                BAE_OPUS_TYPE, compType);
            apply_eq_state(mixer);
            if (err) { playbae_printf("Error %d starting Opus export: %s\n", err, parmFile); BAEMixer_Delete(mixer); return 1; }
            gWriteToFile = 1; gWriteToFileType = BAE_OPUS_TYPE;
            playbae_printf("Writing Ogg Opus (%d kbps) to %s\n", totalReq, parmFile);
#else
            playbae_printf("Opus encoder not built. Rebuild with OPUS_ENC=1.\n");
            BAEMixer_Delete(mixer); return 1;
#endif
        } else {
            /* Default: WAV */
            err = BAEMixer_StartOutputToFile(mixer, (BAEPathName)parmFile,
                BAE_WAVE_TYPE, BAE_COMPRESSION_NONE);
            apply_eq_state(mixer);
            if (err) { playbae_printf("Error %d writing WAV: %s\n", err, parmFile); BAEMixer_Delete(mixer); return 1; }
            gWriteToFile = 1; gWriteToFileType = BAE_WAVE_TYPE;
#if SUPPORT_KARAOKE == TRUE
            gKaraokeEnabled = 0;
#endif
            playbae_printf("Writing WAV to %s\n", parmFile);
        }
    }

    if (gVoiceCaptureDir[0]) {
        BAEResult capErr = BAE_EnableChannelCapture(mixer, gVoiceCaptureDir);
        if (capErr == BAE_NO_ERROR) {
            playbae_printf("Per-channel recording enabled: %s/\n", gVoiceCaptureDir);
        } else {
            playbae_printf("WARNING: Failed to enable per-channel capture (%d): %s\n",
                capErr, BAE_GetErrorString(capErr));
        }
        if (!gWriteToFile) {
            char tmpPath[1280];
            snprintf(tmpPath, sizeof(tmpPath), "%s/full.wav", gVoiceCaptureDir);
            err = BAEMixer_StartOutputToFile(mixer, (BAEPathName)tmpPath,
                BAE_WAVE_TYPE, BAE_COMPRESSION_NONE);
            if (err == BAE_NO_ERROR) {
                gWriteToFile = 1;
                gWriteToFileType = BAE_WAVE_TYPE;
                gTempOutputFile = 1;
            }
        }
    }

    /* ---- Play file ---- */
    int played = 0;

    /* Bare first argument (no leading dash) */
    if (!played && argc > 1 && argv[1][0] != '-') {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, argv[1], timeLimitSec, loopCount,
                reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, argv[1], volume, timeLimitSec, loopCount,
                reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, argv[1], sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }

    /* -f: universal auto-detect */
    if (!played && PV_ParseCommands(argc, argv, "-f", 1, parmFile)) {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, parmFile, timeLimitSec, loopCount,
                reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, parmFile, volume, timeLimitSec, loopCount,
                reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, parmFile, sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }

    /* Type-specific flags kept for backward compatibility */
    if (!played && PV_ParseCommands(argc, argv, "-m", 1, parmFile)) {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, parmFile, timeLimitSec, loopCount, reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, parmFile, volume, timeLimitSec, loopCount, reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, parmFile, sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }
    if (!played && PV_ParseCommands(argc, argv, "-r", 1, parmFile)) {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, parmFile, timeLimitSec, loopCount, reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, parmFile, volume, timeLimitSec, loopCount, reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, parmFile, sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }
    if (!played && PV_ParseCommands(argc, argv, "-a", 1, parmFile)) {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, parmFile, timeLimitSec, loopCount, reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, parmFile, volume, timeLimitSec, loopCount, reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, parmFile, sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }
    if (!played && PV_ParseCommands(argc, argv, "-w", 1, parmFile)) {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, parmFile, timeLimitSec, loopCount, reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, parmFile, volume, timeLimitSec, loopCount, reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, parmFile, sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }
#if USE_MPEG_DECODER == TRUE
    if (!played && PV_ParseCommands(argc, argv, "-mp", 1, parmFile)) {
        if (gDualMode)
            err = PV_PlayDualFile(mixer, mixerB, parmFile, timeLimitSec, loopCount, reverbType, muteChannels);
        else
            err = PV_PlayFile(mixer, parmFile, volume, timeLimitSec, loopCount, reverbType, muteChannels, -1);
        if (err == BAE_NO_ERROR) {
            strncpy(gInputFilePath, parmFile, sizeof(gInputFilePath)-1);
            gInputFilePath[sizeof(gInputFilePath)-1] = '\0';
        }
        played = 1;
    }
#endif

    /* Streaming flags */
    if (!played && PV_ParseCommands(argc, argv, "-sw", 1, parmFile)) {
        playbae_printf("Streaming WAV: %s\n", parmFile);
        err = PV_PlayStreamed(mixer, parmFile, BAE_WAVE_TYPE, volume);
        played = 1;
    }
    if (!played && PV_ParseCommands(argc, argv, "-sa", 1, parmFile)) {
        playbae_printf("Streaming AIFF: %s\n", parmFile);
        err = PV_PlayStreamed(mixer, parmFile, BAE_AIFF_TYPE, volume);
        played = 1;
    }

    /* RMF info only */
    if (!played && PV_ParseCommands(argc, argv, "-i", 1, parmFile)) {
        print_rmf_info(parmFile);
        played = 1;
    }

    if (!played)
        playbae_printf(usageMain, gPlayFileString, gDefaultReverbIndex);

    /* ---- Finalize export ---- */
    if (gWriteToFile) {
#if defined(USE_MPEG_ENCODER) && (USE_MPEG_ENCODER != 0)
        if (gWriteToFileType == BAE_MPEG_TYPE) {
            uint32_t lastSamples = 0, stableCount = 0;
            while (stableCount < 8) {
                BAEMixer_ServiceAudioOutputToFile(mixer);
                BAE_WaitMicroseconds(11000);
                uint32_t cur = BAE_GetDeviceSamplesPlayedPosition();
                if (cur == lastSamples) stableCount++;
                else { stableCount = 0; lastSamples = cur; }
            }
        }
#endif
        BAEMixer_StopOutputToFile();
    }

    if (gTempOutputFile) {
        gWriteToFile = 0;
        gTempOutputFile = 0;
    }

    if (gVoiceCaptureDir[0]) {
        bool channelActive[16] = {false};
        BAEResult capErr = BAE_GetChannelCaptureActive(mixer, channelActive);
        capErr = BAE_DisableChannelCapture(mixer);
        if (capErr == BAE_NO_ERROR) {
            playbae_printf("Per-channel recording complete.\n");
        }

        if (gInputFilePath[0]) {
            int activeCount = 0;
            for (int ch = 0; ch < 16; ch++)
                if (channelActive[ch]) activeCount++;

            int soloCount = 0;
            for (int ch = 0; ch < 16; ch++) {
                if (!channelActive[ch]) continue;

                gPassNumber = soloCount + 1;
                gPassTotal  = activeCount;

                char wetPath[1280];
                snprintf(wetPath, sizeof(wetPath), "%s/channel_%02d.wav",
                         gVoiceCaptureDir, ch);

                BAEMixer soloMixer = BAEMixer_New();
                if (!soloMixer) continue;

                capErr = BAEMixer_Open(soloMixer, sampleRate, interpol, mods,
                    (int16_t)maxVoices, 8, 64, TRUE);
                if (capErr != BAE_NO_ERROR) {
                    BAEMixer_Delete(soloMixer);
                    continue;
                }

                BAEMixer_SetAudioTask(soloMixer, PV_AudioTask, (void *)soloMixer);
                apply_eq_state(soloMixer);
                apply_output_gain(soloMixer);

                if (gHasCustomReverb) {
                    BAEMixer_SetDefaultReverb(soloMixer, gCustomReverbType);
                    SetNeoCustomReverbCombCount(gCustomReverbCombCount);
                    for (int j = 0; j < 4; j++) {
                        SetNeoCustomReverbCombDelay(j, gCustomReverbDelays[j]);
                        SetNeoCustomReverbCombFeedback(j, gCustomReverbFeedback[j]);
                        SetNeoCustomReverbCombGain(j, gCustomReverbGain[j]);
                    }
                    SetNeoCustomReverbLowpass(gCustomReverbLowpass);
                    SetNeoReverbMix(gCustomReverbMix);
                } else {
                    BAEMixer_SetDefaultReverb(soloMixer, reverbType);
                }

                BAEBankToken soloBankToken = 0;
                if (gBankFilePath[0]) {
                    if (!PV_LoadBank(soloMixer, gBankFilePath, &soloBankToken)) {
                        BAEMixer_Delete(soloMixer);
                        continue;
                    }
                } else {
#if _BUILT_IN_PATCHES == TRUE
                    capErr = BAEMixer_LoadBuiltinBank(soloMixer, &soloBankToken);
                    if (capErr != BAE_NO_ERROR) {
                        BAEMixer_Delete(soloMixer);
                        continue;
                    }
#else
                    BAEMixer_Delete(soloMixer);
                    continue;
#endif
                }

                capErr = BAEMixer_StartOutputToFile(soloMixer, (BAEPathName)wetPath,
                    BAE_WAVE_TYPE, BAE_COMPRESSION_NONE);
                if (capErr != BAE_NO_ERROR) {
                    BAEMixer_Delete(soloMixer);
                    continue;
                }

                playbae_printf("Recording channel %d: %s\n", ch, wetPath);

                int wasWriteToFile = gWriteToFile;
                BAEFileType wasWriteType = gWriteToFileType;
                gWriteToFile = 1;
                gWriteToFileType = BAE_WAVE_TYPE;

                PV_PlayFile(soloMixer, gInputFilePath, volume, 0, 0,
                    reverbType, muteChannels, ch);

                gWriteToFile = wasWriteToFile;
                gWriteToFileType = wasWriteType;

                BAEMixer_StopOutputToFile();
                BAEMixer_Close(soloMixer);
                BAEMixer_Delete(soloMixer);
                soloCount++;
            }
            if (soloCount > 0)
                playbae_printf("Wrote %d solo channel WAV(s) to %s/\n", soloCount, gVoiceCaptureDir);
        }
    }

    /* ---- Cleanup ---- */
    BAE_WaitMicroseconds(160000);
#if USE_CALLBACKS == TRUE
    if (gDualMode)
        PV_DualMixTeardown(mixer);
#endif
    if (mixerB) {
        BAEMixer_Close(mixerB);
        BAEMixer_Delete(mixerB);
        gDualMixerB = NULL;
    }
    BAEMixer_Close(mixer);
    BAEMixer_Delete(mixer);

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
    GM_CleanupSF2();
#endif

#if SUPPORT_BAESCRIPT == TRUE
    if (gScript) { BAEScript_Free(gScript); gScript = NULL; }
#endif

    if (played && err > BAE_NO_ERROR) {
        playbae_printf("playbae: Error %d: %s\n", err, BAE_GetErrorString(err));
        return 1;
    }
    return 0;
}

/* EOF */
