#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "neobae_theme.inc"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "NeoBAE.h"
#include "NeoBAEConfigPath.h"
#include "nbeditor_crash.h"
#include "X_API.h"
#include "X_Formats.h"
#include "X_EditorTools.h"
#include "GenPriv.h"
#include "mod2rmf_rmfcreat.h"

#ifndef USE_LIB_HYPHEN
#define USE_LIB_HYPHEN 0
#endif
#ifndef SUPPORT_MIDI_HW
#define SUPPORT_MIDI_HW 0
#endif
#ifndef SUPPORT_KARAOKE
#define SUPPORT_KARAOKE 0
#endif

#if USE_LIB_HYPHEN == TRUE
#include "hyphen.h"
#include <atomic>
#include <mutex>
#include <thread>
#endif
#if SUPPORT_MIDI_HW == TRUE
#include "gui_midi_hw_input.h"
#include "rtmidi_c.h"
#endif

#if defined(NBEDITOR_EMBED_FONTS)
#include "nbeditor_embedded_fonts.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <tuple>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if USE_LIB_HYPHEN == TRUE
#include "nbeditor_http.inc"
#endif

namespace
{

#if SUPPORT_MIDI_HW == TRUE
struct MidiHwDevice
{
    char name[128] = {};
    int api_index = -1;
    int port_index = -1;
};
#endif

/* Sample-editor pitched audition voices (OpenMPT-style multi-playhead). */
static constexpr int kSePreviewVoiceCount = 8;
struct SePreviewVoice
{
    BAESound sound = nullptr;
    int midi_note = -1;
    bool active = false;
    uint32_t started_ms = 0;
    double playback_hz = 0.0;
    uint32_t buffer_frames = 0;
    bool looping = false;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    uint32_t playhead_frame = 0;
};

enum class SampleSource
{
    Bank = 0,
    Song = 1,
};

/* SampleRow / editor state: 0xFFFF means "no SND id". Resource id 0 is valid. */
static constexpr uint32_t kNoSndResourceId = 0xFFFFu;

struct SampleRow
{
    uint32_t index = 0;
    uint32_t instrument_index = 0;
    uint32_t split_index = 0;
    uint32_t document_sample_index = 0;
    uint32_t snd_resource_id = kNoSndResourceId;
    uint32_t sample_rate_hz = 44100;
    uint32_t frame_count = 0;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    int bit_depth = 16;
    int channels = 1;
    bool is_custom = false;
    bool unassigned = false; /* bank SND not referenced by any INST */
    SampleSource source = SampleSource::Bank;
    int bank = 0;
    int program = 0;
    int root_key = 60;
    int low_key = 0;
    int high_key = 127;
    std::string name;
    /* Visible-only list suffix e.g. "MP3 128k"; empty for plain PCM. Not part of resource name. */
    std::string codec_label;
};

struct SongRow
{
    std::string name;
    std::string path;
    std::string source_type; // MIDI / RMF / ZMF / Module / BSN
};

struct SessionSongEntry
{
    uint32_t song_resource_id = 0;
    uint32_t midi_resource_id = 0;
    std::string name;
    std::string source_type; /* MIDI / RMF / ZMF / Module / BSN / … */
    std::vector<unsigned char> midi_bytes;
    /* Full RMF/ZMF document bytes when the song carries embeds; preferred over midi_bytes on activate. */
    std::vector<unsigned char> rmf_bytes;
};

struct SessionPcmCacheEntry
{
    std::string name;
    std::vector<unsigned char> pcm;
    uint32_t frame_count = 0;
    uint16_t bit_size = 16;
    uint16_t channels = 1;
    BAE_UNSIGNED_FIXED sample_rate = 0;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    int base_key = 60;
    /* 0 is a valid SND id — unset must be 0xFFFF, never 0. */
    uint32_t snd_resource_id = kNoSndResourceId;
    /* Pending ship-format encode (editor bank stays PCM SND until Export). */
    bool has_export_target = false;
    BAERmfEditorCompressionType export_compression = BAE_EDITOR_COMPRESSION_PCM;
    BAERmfEditorSndStorageType export_storage = BAE_EDITOR_SND_STORAGE_SND;
    BAERmfEditorOpusMode export_opus_mode = BAE_EDITOR_OPUS_MODE_AUDIO;
    bool export_opus_roundtrip = false;
};

enum class ExportInstMode : int
{
    Off = 0,   /* bank program ref only */
    Embed = 1, /* INST + samples in RMF */
    Ghost = 2, /* INST in RMF; samples bank-aliased */
};

struct ExportInstrumentItem
{
    uint32_t inst_id = 0;
    uint32_t requested_inst_id = 0;
    uint32_t instrument_index = 0;
    std::string name;
    bool is_custom = false;
    bool selected = false; /* legacy: true when mode == Embed */
    bool percussion = false;
    ExportInstMode mode = ExportInstMode::Off;
    int custom_index = -1;
    int bank = 0;
    int program = 0;
};

struct BsnIrezResource
{
    uint32_t type = 0;
    uint32_t id = 0;
    std::string name;
    std::vector<unsigned char> body;
};

static uint32_t BsnReadBE32(const unsigned char *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static uint16_t BsnReadBE16(const unsigned char *p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static bool BsnResourceTypeEquals(uint32_t type, char a, char b, char c, char d)
{
    const uint32_t want = (static_cast<uint32_t>(static_cast<unsigned char>(a)) << 24) |
                          (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 16) |
                          (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 8) |
                          static_cast<uint32_t>(static_cast<unsigned char>(d));
    return type == want;
}

/* SMS (0) and structured RMF (1) both point at a Midi object. Linear RMF (2) does not. */
static bool BsnSongTypeHasMidiObject(unsigned char song_type)
{
    return song_type == SONG_TYPE_SMS || song_type == SONG_TYPE_RMF;
}

static bool ParseIrezResources(const unsigned char *data,
                               size_t size,
                               std::vector<BsnIrezResource> &out)
{
    out.clear();
    if (!data || size < 12)
    {
        return false;
    }
    if (!(data[0] == 'I' && data[1] == 'R' && data[2] == 'E' && data[3] == 'Z') &&
        !(data[0] == 'Z' && data[1] == 'R' && data[2] == 'E' && data[3] == 'Z'))
    {
        return false;
    }

    const uint32_t claimed = BsnReadBE32(data + 8);
    size_t offset = 12;
    for (uint32_t idx = 0; idx < claimed; ++idx)
    {
        if (offset + 12 > size)
        {
            break;
        }
        BsnIrezResource res;
        res.type = BsnReadBE32(data + offset + 4);
        res.id = BsnReadBE32(data + offset + 8);
        size_t p = offset + 12;
        if (p >= size)
        {
            break;
        }
        const unsigned char name_len = data[p];
        if (p + 1 + name_len + 4 > size)
        {
            break;
        }
        res.name.assign(reinterpret_cast<const char *>(data + p + 1), name_len);
        p = p + 1 + name_len;
        const uint32_t body_len = BsnReadBE32(data + p);
        p += 4;
        if (p + body_len > size)
        {
            break;
        }
        res.body.assign(data + p, data + p + body_len);
        out.push_back(std::move(res));
        offset = p + body_len;
    }
    return !out.empty();
}

static bool ReadEntireFileBytes(const char *path, std::vector<unsigned char> &out)
{
    out.clear();
    if (!path || !path[0])
    {
        return false;
    }
    FILE *fp = std::fopen(path, "rb");
    if (!fp)
    {
        return false;
    }
    if (std::fseek(fp, 0, SEEK_END) != 0)
    {
        std::fclose(fp);
        return false;
    }
    const long len = std::ftell(fp);
    if (len < 0)
    {
        std::fclose(fp);
        return false;
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0)
    {
        std::fclose(fp);
        return false;
    }
    out.resize(static_cast<size_t>(len));
    if (len > 0 && std::fread(out.data(), 1, out.size(), fp) != out.size())
    {
        std::fclose(fp);
        out.clear();
        return false;
    }
    std::fclose(fp);
    return true;
}

static bool IsBe2SessionDocument(const std::vector<BsnIrezResource> &resources)
{
    bool has_bepf = false;
    bool has_song = false;
    for (const BsnIrezResource &r : resources)
    {
        /* Classic BE2 Session .bsn */
        if (BsnResourceTypeEquals(r.type, 'B', 'e', 'P', 'f') && r.name == "Session Prefs")
        {
            return true;
        }
        /* Beatnik bank/session trailer (DinoPark Tycoon.bsn and similar). */
        if (BsnResourceTypeEquals(r.type, 'B', 'E', 'P', 'F'))
        {
            has_bepf = true;
        }
        /* NeoBAE .zsn (and optional NeoBAE layout on .bsn) */
        if (BsnResourceTypeEquals(r.type, 'n', 'B', 'e', 'T'))
        {
            return true;
        }
        if (BsnResourceTypeEquals(r.type, 'S', 'O', 'N', 'G'))
        {
            has_song = true;
        }
    }
    /* Playback banks with a BEPF trailer and SONG/Midi (no Session Prefs). */
    return has_bepf && has_song;
}

/* BE2 DATe stamps editor-touched resources. Custom Session songs get SONG and
 * object stamps (Midi, or emid/ecmi/cmid when encrypted); bank groovoids
 * typically do not. out_midi_ids holds any stamped song-object id. */
static void CollectDateStampedSongKeys(const unsigned char *body,
                                       size_t size,
                                       std::set<uint32_t> &out_song_ids,
                                       std::set<uint32_t> &out_midi_ids)
{
    if (!body)
    {
        return;
    }
    const uint32_t k_song = (static_cast<uint32_t>('S') << 24) |
                            (static_cast<uint32_t>('O') << 16) |
                            (static_cast<uint32_t>('N') << 8) |
                            static_cast<uint32_t>('G');
    const uint32_t k_midi = (static_cast<uint32_t>('M') << 24) |
                            (static_cast<uint32_t>('i') << 16) |
                            (static_cast<uint32_t>('d') << 8) |
                            static_cast<uint32_t>('i');
    const uint32_t k_emid = (static_cast<uint32_t>('e') << 24) |
                            (static_cast<uint32_t>('m') << 16) |
                            (static_cast<uint32_t>('i') << 8) |
                            static_cast<uint32_t>('d');
    const uint32_t k_ecmi = (static_cast<uint32_t>('e') << 24) |
                            (static_cast<uint32_t>('c') << 16) |
                            (static_cast<uint32_t>('m') << 8) |
                            static_cast<uint32_t>('i');
    const uint32_t k_cmid = (static_cast<uint32_t>('c') << 24) |
                            (static_cast<uint32_t>('m') << 16) |
                            (static_cast<uint32_t>('i') << 8) |
                            static_cast<uint32_t>('d');
    for (size_t off = 0; off + 12 <= size; off += 12)
    {
        const uint32_t type = BsnReadBE32(body + off);
        const uint32_t id = BsnReadBE32(body + off + 4);
        const uint32_t ts = BsnReadBE32(body + off + 8);
        if (type == 0 && id == 0 && ts == 0)
        {
            break;
        }
        if (type == k_song)
        {
            out_song_ids.insert(id);
        }
        else if (type == k_midi || type == k_emid || type == k_ecmi || type == k_cmid)
        {
            out_midi_ids.insert(id);
        }
    }
}

static void CollectDateStampedSongKeysFromResources(const std::vector<BsnIrezResource> &resources,
                                                    std::set<uint32_t> &out_song_ids,
                                                    std::set<uint32_t> &out_midi_ids)
{
    out_song_ids.clear();
    out_midi_ids.clear();
    for (const BsnIrezResource &r : resources)
    {
        if (BsnResourceTypeEquals(r.type, 'D', 'A', 'T', 'e') && !r.body.empty())
        {
            CollectDateStampedSongKeys(r.body.data(), r.body.size(), out_song_ids, out_midi_ids);
        }
    }
}

static void CollectSessionUserSongs(const std::vector<BsnIrezResource> &resources,
                                    std::vector<SessionSongEntry> &out_songs,
                                    int &out_casd_count)
{
    out_songs.clear();
    out_casd_count = 0;

    std::set<uint32_t> dated_song_ids;
    std::set<uint32_t> dated_midi_ids;
    CollectDateStampedSongKeysFromResources(resources, dated_song_ids, dated_midi_ids);
    /* When DATe lists any Midi/SONG, only those are Custom Songs. Remaining
     * SONG→Midi entries are bank groovoids converted from emid (e.g. zpatches). */
    const bool use_date_filter = !dated_song_ids.empty() || !dated_midi_ids.empty();

    std::map<uint32_t, const BsnIrezResource *> midi_by_id;
    for (const BsnIrezResource &r : resources)
    {
        if (BsnResourceTypeEquals(r.type, 'M', 'i', 'd', 'i'))
        {
            midi_by_id[r.id] = &r;
        }
        if (BsnResourceTypeEquals(r.type, 'C', 'a', 'S', 'd'))
        {
            ++out_casd_count;
        }
    }

    for (const BsnIrezResource &r : resources)
    {
        if (!BsnResourceTypeEquals(r.type, 'S', 'O', 'N', 'G') || r.body.size() < 8)
        {
            continue;
        }
        const uint16_t midi_id = BsnReadBE16(r.body.data());
        const unsigned char song_type = r.body[6];
        if (!BsnSongTypeHasMidiObject(song_type))
        {
            continue;
        }
        auto it = midi_by_id.find(midi_id);
        if (it == midi_by_id.end())
        {
            continue;
        }
        const BsnIrezResource *midi = it->second;
        if (midi->body.size() < 4 ||
            !(midi->body[0] == 'M' && midi->body[1] == 'T' &&
              midi->body[2] == 'h' && midi->body[3] == 'd'))
        {
            continue;
        }
        if (use_date_filter &&
            dated_song_ids.count(r.id) == 0 &&
            dated_midi_ids.count(midi_id) == 0)
        {
            continue;
        }

        SessionSongEntry entry;
        entry.song_resource_id = r.id;
        entry.midi_resource_id = midi_id;
        entry.name = r.name.empty() ? midi->name : r.name;
        if (entry.name.empty())
        {
            entry.name = "Untitled Song";
        }
        entry.source_type = "BSN";
        entry.midi_bytes = midi->body;
        out_songs.push_back(std::move(entry));
    }

    std::sort(out_songs.begin(),
              out_songs.end(),
              [](const SessionSongEntry &a, const SessionSongEntry &b) {
                  return a.song_resource_id < b.song_resource_id;
              });
}

/* Enumerate Custom Songs via XFILE so ZREZ ZSNG/ZBNK packed songs are visible. */
static void CollectSessionUserSongsFromBank(XFILE bank,
                                            std::vector<SessionSongEntry> &out_songs,
                                            int &out_casd_count)
{
    out_songs.clear();
    out_casd_count = 0;
    if (!bank)
    {
        return;
    }

    std::set<uint32_t> dated_song_ids;
    std::set<uint32_t> dated_midi_ids;
    {
        const XResourceType date_type = FOUR_CHAR('D', 'A', 'T', 'e');
        const int32_t date_count = XCountFileResourcesOfType(bank, date_type);
        for (int32_t di = 0; di < date_count; ++di)
        {
            XLongResourceID date_id = 0;
            int32_t date_size = 0;
            XPTR date_data =
                XGetIndexedFileResource(bank, date_type, &date_id, di, nullptr, &date_size);
            if (!date_data || date_size <= 0)
            {
                if (date_data)
                {
                    XDisposePtr(date_data);
                }
                continue;
            }
            CollectDateStampedSongKeys(static_cast<const unsigned char *>(date_data),
                                       static_cast<size_t>(date_size),
                                       dated_song_ids,
                                       dated_midi_ids);
            XDisposePtr(date_data);
        }
    }
    const bool use_date_filter = !dated_song_ids.empty() || !dated_midi_ids.empty();

    {
        const int32_t casd = XCountFileResourcesOfType(bank, FOUR_CHAR('C', 'a', 'S', 'd'));
        out_casd_count = (casd > 0) ? casd : 0;
    }

    const int32_t song_count = XCountFileResourcesOfType(bank, ID_SONG);
    for (int32_t i = 0; i < song_count; ++i)
    {
        XLongResourceID song_id = 0;
        int32_t song_size = 0;
        char song_name[256];
        song_name[0] = 0;
        XPTR song_data =
            XGetIndexedFileResource(bank, ID_SONG, &song_id, i, song_name, &song_size);
        if (!song_data || song_size < 8)
        {
            if (song_data)
            {
                XDisposePtr(song_data);
            }
            continue;
        }

        const unsigned char *song_body = static_cast<const unsigned char *>(song_data);
        const uint16_t midi_id = BsnReadBE16(song_body);
        const unsigned char song_type = song_body[6];
        XDisposePtr(song_data);
        if (!BsnSongTypeHasMidiObject(song_type))
        {
            continue;
        }

        int32_t midi_size = 0;
        char midi_name[256];
        midi_name[0] = 0;
        XPTR midi_data = XGetFileResource(bank,
                                          ID_MIDI,
                                          static_cast<XLongResourceID>(midi_id),
                                          midi_name,
                                          &midi_size);
        if (!midi_data || midi_size < 4)
        {
            if (midi_data)
            {
                XDisposePtr(midi_data);
            }
            continue;
        }
        const unsigned char *midi_body = static_cast<const unsigned char *>(midi_data);
        if (!(midi_body[0] == 'M' && midi_body[1] == 'T' &&
              midi_body[2] == 'h' && midi_body[3] == 'd'))
        {
            XDisposePtr(midi_data);
            continue;
        }
        if (use_date_filter &&
            dated_song_ids.count(static_cast<uint32_t>(song_id)) == 0 &&
            dated_midi_ids.count(midi_id) == 0)
        {
            XDisposePtr(midi_data);
            continue;
        }

        SessionSongEntry entry;
        entry.song_resource_id = static_cast<uint32_t>(song_id);
        entry.midi_resource_id = midi_id;
        const unsigned char song_plen = static_cast<unsigned char>(song_name[0]);
        if (song_plen > 0 && song_plen < 255)
        {
            entry.name.assign(song_name + 1, song_name + 1 + song_plen);
        }
        if (entry.name.empty())
        {
            const unsigned char midi_plen = static_cast<unsigned char>(midi_name[0]);
            if (midi_plen > 0 && midi_plen < 255)
            {
                entry.name.assign(midi_name + 1, midi_name + 1 + midi_plen);
            }
        }
        if (entry.name.empty())
        {
            entry.name = "Untitled Song";
        }
        entry.source_type = "BSN";
        entry.midi_bytes.assign(midi_body, midi_body + midi_size);
        XDisposePtr(midi_data);
        out_songs.push_back(std::move(entry));
    }

    std::sort(out_songs.begin(),
              out_songs.end(),
              [](const SessionSongEntry &a, const SessionSongEntry &b) {
                  return a.song_resource_id < b.song_resource_id;
              });
}

static bool ProbeBe2SessionBsn(const char *path,
                               std::vector<SessionSongEntry> *out_songs,
                               int *out_casd_count)
{
    std::vector<unsigned char> bytes;
    if (!ReadEntireFileBytes(path, bytes))
    {
        return false;
    }
    std::vector<BsnIrezResource> resources;
    if (!ParseIrezResources(bytes.data(), bytes.size(), resources))
    {
        return false;
    }
    if (!IsBe2SessionDocument(resources))
    {
        return false;
    }
    if (out_songs || out_casd_count)
    {
        std::vector<SessionSongEntry> songs;
        int casd = 0;
        /* Prefer XFILE so ZREZ sessions expose SONG/Midi inside ZSNG/ZBNK. */
        XFILENAME xf;
        XConvertPathToXFILENAME(const_cast<char *>(path), &xf);
        XFILE file = XFileOpenResource(&xf, TRUE);
        if (file)
        {
            CollectSessionUserSongsFromBank(file, songs, casd);
            XFileClose(file);
        }
        else
        {
            CollectSessionUserSongs(resources, songs, casd);
        }
        if (out_songs)
        {
            *out_songs = std::move(songs);
        }
        if (out_casd_count)
        {
            *out_casd_count = casd;
        }
    }
    return true;
}

#if NBEDITOR_MVP
/* Sample Editor drag targets in the waveform canvas. */
enum class SeDragMode
{
    None = 0,
    Selection,
    LoopStart,
    LoopEnd,
    LoopMid,
    Pan,
    FadeIn,
    FadeOut,
    Gain,
};

/* Full snapshot of an editable sample, used for undo/redo and revert. */
struct SampleEditorUndoState
{
    std::vector<unsigned char> pcm;
    uint32_t frame_count = 0;
    uint16_t bit_size = 0;
    uint16_t channels = 0;
    BAE_UNSIGNED_FIXED sample_rate = 0;
    uint32_t sample_rate_hz = 44100;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    bool loop_enabled = false;
    int root_key = 60;
    int rate_preset = 1;
    std::string name;
};
#endif

struct InstrumentRow
{
    bool is_custom = false;
    bool has_song_override = false;
    bool from_song_document = false;
    bool is_alias = false;
    uint32_t instrument_index = 0;
    uint32_t inst_id = 0;        /* display slot ID (aliasFrom when is_alias) */
    uint32_t target_inst_id = 0; /* concrete INST id (aliasTo when is_alias) */
    int program = 0;
    int split_count = 0;
    int bank = 0;
    bool percussion = false;
    std::string name;
};

struct InstrumentFilterOption
{
    int bank = -1;     /* -1 All, -2 Custom, -3 Used by current song, else bank # */
    int category = -1; // -1 all/special, 0 melodic, 1 percussion
    std::string label;
};

/* BE2 Samples tab Show: — All / Custom / Built-in / Unassigned */
enum class SampleShowFilter : int
{
    All = 0,
    Custom = 1,
    BuiltIn = 2,
    Unassigned = 3,
};

enum class InstrumentListSort : int
{
    ByProgram = 0,
    ByName = 1,
};

enum class SampleListSort : int
{
    ByName = 0,
    BySndId = 1,
};

/* BE2 Songs tab Show: — All / Custom Songs / Groovoids */
enum class SongShowFilter : int
{
    All = 0,
    Custom = 1,
    Groovoids = 2,
};

struct GroovoidEntry
{
    uint32_t song_resource_id = 0;
    uint16_t emid_resource_id = 0; /* object id: emid (classic) or Midi (converted) */
    bool object_is_midi = false;   /* false = emid (decrypt); true = plain Midi */
    std::string name;
};

enum class SessionTab : int
{
    Songs = 0,
    Instruments = 1,
    Samples = 2,
};

/* What a cross-process / in-process clipboard package is for. */
enum class ClipPackageKind : int
{
    None = 0,
    Song = 1,
    Instrument = 2,
    Sample = 3,
};

/* Where an OS file drop should land (updated each frame from hovered windows). */
enum class SessionDropTarget : int
{
    None = 0,
    Songs,        /* Session Songs tab or Player */
    Instruments,
    Samples,
    FileOpen,     /* Status bar — same as File → Open */
};

struct SampleCodecOption
{
    BAERmfEditorCompressionType type;
    const char *label;
};

static std::string MakeBankCategoryLabel(int bank, int category)
{
    const bool perc = (category == 1);
    const char *suffix = nullptr;
    if (bank == 0)
    {
        suffix = "General MIDI";
    }
    else if (bank == 1)
    {
        suffix = "Beatnik Special";
    }
    else if (bank == 2)
    {
        suffix = perc ? "Custom Percussion" : "Custom Melodic";
    }

    char label[128];
    if (suffix && bank == 2)
    {
        std::snprintf(label, sizeof(label), "Bank %d %s - %s", bank, perc ? "Percussion" : "Melodic", suffix);
    }
    else if (suffix)
    {
        std::snprintf(label,
                      sizeof(label),
                      "Bank %d %s - %s",
                      bank,
                      perc ? "Percussion" : "Melodic",
                      suffix);
    }
    else
    {
        std::snprintf(label,
                      sizeof(label),
                      "Bank %d %s",
                      bank,
                      perc ? "Percussion" : "Melodic");
    }
    return std::string(label);
}

static std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

static bool ContainsIgnoreCase(const char *haystack, const char *needle)
{
    if (!needle || !needle[0])
    {
        return true;
    }
    if (!haystack || !haystack[0])
    {
        return false;
    }
    const std::string h = ToLower(haystack);
    const std::string n = ToLower(needle);
    return h.find(n) != std::string::npos;
}

static std::string FileNameFromPath(const std::string &path)
{
    if (path.empty())
    {
        return std::string();
    }
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos || slash + 1 >= path.size())
    {
        return path;
    }
    return path.substr(slash + 1);
}

static bool EndsWith(const std::string &value, const char *suffix)
{
    const std::string lower = ToLower(value);
    const std::string sfx = ToLower(std::string(suffix));
    if (lower.size() < sfx.size())
    {
        return false;
    }
    return lower.compare(lower.size() - sfx.size(), sfx.size(), sfx) == 0;
}

static const int kMixerSampleRates[] = {
    BAE_RATE_8K, BAE_RATE_11K, BAE_RATE_16K, BAE_RATE_22K, BAE_RATE_32K, BAE_RATE_44K, BAE_RATE_48K};

static int ClampMixerSampleRateHz(int hz)
{
    int best = kMixerSampleRates[0];
    int best_diff = std::abs(hz - best);
    for (int r : kMixerSampleRates)
    {
        if (r == hz)
        {
            return r;
        }
        const int d = std::abs(hz - r);
        if (d < best_diff)
        {
            best_diff = d;
            best = r;
        }
    }
    return best;
}

static BAERate RateFromHz(int hz)
{
    switch (ClampMixerSampleRateHz(hz))
    {
    case BAE_RATE_8K:
        return BAE_RATE_8K;
    case BAE_RATE_11K:
        return BAE_RATE_11K;
    case BAE_RATE_16K:
        return BAE_RATE_16K;
    case BAE_RATE_22K:
        return BAE_RATE_22K;
    case BAE_RATE_32K:
        return BAE_RATE_32K;
    case BAE_RATE_48K:
        return BAE_RATE_48K;
    case BAE_RATE_44K:
    default:
        return BAE_RATE_44K;
    }
}

static bool IsOpusCompressionType(BAERmfEditorCompressionType compression_type)
{
    switch (compression_type)
    {
    case BAE_EDITOR_COMPRESSION_OPUS_12K:
    case BAE_EDITOR_COMPRESSION_OPUS_16K:
    case BAE_EDITOR_COMPRESSION_OPUS_24K:
    case BAE_EDITOR_COMPRESSION_OPUS_32K:
    case BAE_EDITOR_COMPRESSION_OPUS_48K:
    case BAE_EDITOR_COMPRESSION_OPUS_64K:
    case BAE_EDITOR_COMPRESSION_OPUS_80K:
    case BAE_EDITOR_COMPRESSION_OPUS_96K:
    case BAE_EDITOR_COMPRESSION_OPUS_128K:
    case BAE_EDITOR_COMPRESSION_OPUS_160K:
    case BAE_EDITOR_COMPRESSION_OPUS_192K:
    case BAE_EDITOR_COMPRESSION_OPUS_256K:
        return true;
    default:
        return false;
    }
}

static BAERmfEditorCompressionType BankCompressionFromCodec(uint32_t compression_type,
                                                            uint32_t compression_sub_type)
{
    switch (compression_type)
    {
    case 0:
    case FOUR_CHAR('n', 'o', 'n', 'e'):
        return BAE_EDITOR_COMPRESSION_PCM;
    case FOUR_CHAR('i', 'm', 'a', '4'):
    case FOUR_CHAR('i', 'm', 'a', 'W'):
        return BAE_EDITOR_COMPRESSION_ADPCM;
#if USE_ZMF_SUPPORT == TRUE
    case FOUR_CHAR('i', 'm', 'a', '2'):
        return BAE_EDITOR_COMPRESSION_ADPCM_2BIT;
#endif
    case FOUR_CHAR('a', 'l', 'a', 'w'):
        return BAE_EDITOR_COMPRESSION_ALAW;
    case FOUR_CHAR('u', 'l', 'a', 'w'):
        return BAE_EDITOR_COMPRESSION_ULAW;
    case FOUR_CHAR('q', 'o', 'a', 'f'):
        return BAE_EDITOR_COMPRESSION_QOA;
    case FOUR_CHAR('f', 'L', 'a', 'C'):
    case FOUR_CHAR('F', 'L', 'A', 'C'):
        return BAE_EDITOR_COMPRESSION_FLAC;
    case FOUR_CHAR('m', 'p', 'g', 'n'):
        return BAE_EDITOR_COMPRESSION_MP3_32K;
    case FOUR_CHAR('m', 'p', 'g', 'b'):
        return BAE_EDITOR_COMPRESSION_MP3_48K;
    case FOUR_CHAR('m', 'p', 'g', 'd'):
        return BAE_EDITOR_COMPRESSION_MP3_64K;
    case FOUR_CHAR('m', 'p', 'g', 'f'):
        return BAE_EDITOR_COMPRESSION_MP3_96K;
    case FOUR_CHAR('m', 'p', 'g', 'h'):
        return BAE_EDITOR_COMPRESSION_MP3_128K;
    case FOUR_CHAR('m', 'p', 'g', 'j'):
        return BAE_EDITOR_COMPRESSION_MP3_192K;
    case FOUR_CHAR('m', 'p', 'g', 'l'):
        return BAE_EDITOR_COMPRESSION_MP3_256K;
    case FOUR_CHAR('m', 'p', 'g', 'm'):
        return BAE_EDITOR_COMPRESSION_MP3_320K;
    case FOUR_CHAR('O', 'g', 'g', 'V'):
    case FOUR_CHAR('V', 'O', 'R', 'B'):
        switch (compression_sub_type)
        {
        case FOUR_CHAR('v', '0', '3', '2'):
            return BAE_EDITOR_COMPRESSION_VORBIS_32K;
        case FOUR_CHAR('v', '0', '4', '8'):
            return BAE_EDITOR_COMPRESSION_VORBIS_48K;
        case FOUR_CHAR('v', '0', '6', '4'):
            return BAE_EDITOR_COMPRESSION_VORBIS_64K;
        case FOUR_CHAR('v', '0', '8', '0'):
            return BAE_EDITOR_COMPRESSION_VORBIS_80K;
        case FOUR_CHAR('v', '0', '9', '6'):
            return BAE_EDITOR_COMPRESSION_VORBIS_96K;
        case FOUR_CHAR('v', '1', '2', '8'):
            return BAE_EDITOR_COMPRESSION_VORBIS_128K;
        case FOUR_CHAR('v', '1', '6', '0'):
            return BAE_EDITOR_COMPRESSION_VORBIS_160K;
        case FOUR_CHAR('v', '1', '9', '2'):
            return BAE_EDITOR_COMPRESSION_VORBIS_192K;
        case FOUR_CHAR('v', '2', '5', '6'):
            return BAE_EDITOR_COMPRESSION_VORBIS_256K;
        default:
            return BAE_EDITOR_COMPRESSION_VORBIS_128K;
        }
    case FOUR_CHAR('O', 'g', 'g', 'O'):
    case FOUR_CHAR('O', 'P', 'U', 'S'):
        switch (compression_sub_type)
        {
        case FOUR_CHAR('o', '0', '1', '2'):
            return BAE_EDITOR_COMPRESSION_OPUS_12K;
        case FOUR_CHAR('o', '0', '1', '6'):
            return BAE_EDITOR_COMPRESSION_OPUS_16K;
        case FOUR_CHAR('o', '0', '2', '4'):
            return BAE_EDITOR_COMPRESSION_OPUS_24K;
        case FOUR_CHAR('o', '0', '3', '2'):
            return BAE_EDITOR_COMPRESSION_OPUS_32K;
        case FOUR_CHAR('o', '0', '4', '8'):
            return BAE_EDITOR_COMPRESSION_OPUS_48K;
        case FOUR_CHAR('o', '0', '6', '4'):
            return BAE_EDITOR_COMPRESSION_OPUS_64K;
        case FOUR_CHAR('o', '0', '8', '0'):
            return BAE_EDITOR_COMPRESSION_OPUS_80K;
        case FOUR_CHAR('o', '0', '9', '6'):
            return BAE_EDITOR_COMPRESSION_OPUS_96K;
        case FOUR_CHAR('o', '1', '2', '8'):
            return BAE_EDITOR_COMPRESSION_OPUS_128K;
        case FOUR_CHAR('o', '1', '6', '0'):
            return BAE_EDITOR_COMPRESSION_OPUS_160K;
        case FOUR_CHAR('o', '1', '9', '2'):
            return BAE_EDITOR_COMPRESSION_OPUS_192K;
        case FOUR_CHAR('o', '2', '5', '6'):
            return BAE_EDITOR_COMPRESSION_OPUS_256K;
        default:
            return BAE_EDITOR_COMPRESSION_OPUS_48K;
        }
    default:
        return BAE_EDITOR_COMPRESSION_PCM;
    }
}

static const SampleCodecOption *GetSampleCodecOptions(size_t *out_count)
{
    static const SampleCodecOption options[] = {
        {BAE_EDITOR_COMPRESSION_DONT_CHANGE, "No Recompression"},
        {BAE_EDITOR_COMPRESSION_PCM, "PCM"},
        {BAE_EDITOR_COMPRESSION_ADPCM, "ADPCM"},
#if USE_ZMF_SUPPORT == TRUE
        {BAE_EDITOR_COMPRESSION_ADPCM_2BIT, "2-bit ADPCM"},
#endif
        {BAE_EDITOR_COMPRESSION_ALAW, "A-law"},
        {BAE_EDITOR_COMPRESSION_ULAW, "u-law"},
#if USE_QOA_SUPPORT == TRUE
        {BAE_EDITOR_COMPRESSION_QOA, "QOA"},
#endif
        {BAE_EDITOR_COMPRESSION_FLAC, "FLAC"},
        {BAE_EDITOR_COMPRESSION_MP3_32K, "MP3 32k"},
        {BAE_EDITOR_COMPRESSION_MP3_48K, "MP3 48k"},
        {BAE_EDITOR_COMPRESSION_MP3_64K, "MP3 64k"},
        {BAE_EDITOR_COMPRESSION_MP3_96K, "MP3 96k"},
        {BAE_EDITOR_COMPRESSION_MP3_128K, "MP3 128k"},
        {BAE_EDITOR_COMPRESSION_MP3_192K, "MP3 192k"},
        {BAE_EDITOR_COMPRESSION_MP3_256K, "MP3 256k"},
        {BAE_EDITOR_COMPRESSION_MP3_320K, "MP3 320k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_32K, "Vorbis 32k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_48K, "Vorbis 48k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_64K, "Vorbis 64k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_80K, "Vorbis 80k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_96K, "Vorbis 96k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_128K, "Vorbis 128k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_160K, "Vorbis 160k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_192K, "Vorbis 192k"},
        {BAE_EDITOR_COMPRESSION_VORBIS_256K, "Vorbis 256k"},
        {BAE_EDITOR_COMPRESSION_OPUS_12K, "Opus 12k"},
        {BAE_EDITOR_COMPRESSION_OPUS_16K, "Opus 16k"},
        {BAE_EDITOR_COMPRESSION_OPUS_24K, "Opus 24k"},
        {BAE_EDITOR_COMPRESSION_OPUS_32K, "Opus 32k"},
        {BAE_EDITOR_COMPRESSION_OPUS_48K, "Opus 48k"},
        {BAE_EDITOR_COMPRESSION_OPUS_64K, "Opus 64k"},
        {BAE_EDITOR_COMPRESSION_OPUS_80K, "Opus 80k"},
        {BAE_EDITOR_COMPRESSION_OPUS_96K, "Opus 96k"},
        {BAE_EDITOR_COMPRESSION_OPUS_128K, "Opus 128k"},
        {BAE_EDITOR_COMPRESSION_OPUS_160K, "Opus 160k"},
        {BAE_EDITOR_COMPRESSION_OPUS_192K, "Opus 192k"},
        {BAE_EDITOR_COMPRESSION_OPUS_256K, "Opus 256k"},
    };
    if (out_count)
    {
        *out_count = SDL_arraysize(options);
    }
    return options;
}

static const char *GetSampleCodecLabel(BAERmfEditorCompressionType type)
{
    size_t option_count = 0;
    const SampleCodecOption *options = GetSampleCodecOptions(&option_count);
    for (size_t i = 0; i < option_count; ++i)
    {
        if (options[i].type == type)
        {
            return options[i].label;
        }
    }
    return "Unknown";
}

/* Visible-only Samples-list codec tag. Empty string = plain PCM (no suffix). */
static std::string FormatVisibleCodecLabel(uint32_t compression_type,
                                           uint32_t compression_sub_type,
                                           bool opus_round_trip = false)
{
    auto sub_bitrate = [](uint32_t sub) -> const char * {
        switch (sub)
        {
        case FOUR_CHAR('v', '0', '3', '2'):
            return " 32k";
        case FOUR_CHAR('v', '0', '4', '8'):
            return " 48k";
        case FOUR_CHAR('v', '0', '6', '4'):
            return " 64k";
        case FOUR_CHAR('v', '0', '8', '0'):
            return " 80k";
        case FOUR_CHAR('v', '0', '9', '6'):
            return " 96k";
        case FOUR_CHAR('v', '1', '2', '8'):
            return " 128k";
        case FOUR_CHAR('v', '1', '6', '0'):
            return " 160k";
        case FOUR_CHAR('v', '1', '9', '2'):
            return " 192k";
        case FOUR_CHAR('v', '2', '5', '6'):
            return " 256k";
        case FOUR_CHAR('o', '0', '1', '2'):
            return " 12k";
        case FOUR_CHAR('o', '0', '1', '6'):
            return " 16k";
        case FOUR_CHAR('o', '0', '2', '4'):
            return " 24k";
        case FOUR_CHAR('o', '0', '3', '2'):
            return " 32k";
        case FOUR_CHAR('o', '0', '4', '8'):
            return " 48k";
        case FOUR_CHAR('o', '0', '6', '4'):
            return " 64k";
        case FOUR_CHAR('o', '0', '8', '0'):
            return " 80k";
        case FOUR_CHAR('o', '0', '9', '6'):
            return " 96k";
        case FOUR_CHAR('o', '1', '2', '8'):
            return " 128k";
        case FOUR_CHAR('o', '1', '6', '0'):
            return " 160k";
        case FOUR_CHAR('o', '1', '9', '2'):
            return " 192k";
        case FOUR_CHAR('o', '2', '5', '6'):
            return " 256k";
        default:
            return nullptr;
        }
    };

    switch (compression_type)
    {
    case 0:
    case FOUR_CHAR('n', 'o', 'n', 'e'):
        return std::string();
    case FOUR_CHAR('i', 'm', 'a', '4'):
        return "IMA4";
    case FOUR_CHAR('i', 'm', 'a', 'W'):
        return "IMA4 (WAV)";
    case FOUR_CHAR('i', 'm', 'a', '2'):
        return "2-bit ADPCM";
    case FOUR_CHAR('m', 'a', 'c', '3'):
        return "MACE3";
    case FOUR_CHAR('m', 'a', 'c', '6'):
        return "MACE6";
    case FOUR_CHAR('u', 'l', 'a', 'w'):
        return "uLaw";
    case FOUR_CHAR('a', 'l', 'a', 'w'):
        return "aLaw";
    case FOUR_CHAR('f', 'L', 'a', 'C'):
    case FOUR_CHAR('F', 'L', 'A', 'C'):
        return "FLAC";
    case FOUR_CHAR('q', 'o', 'a', 'f'):
        return "QOA";
    case FOUR_CHAR('m', 'p', 'g', 'n'):
        return "MP3 32k";
    case FOUR_CHAR('m', 'p', 'g', 'a'):
        return "MP3 40k";
    case FOUR_CHAR('m', 'p', 'g', 'b'):
        return "MP3 48k";
    case FOUR_CHAR('m', 'p', 'g', 'c'):
        return "MP3 56k";
    case FOUR_CHAR('m', 'p', 'g', 'd'):
        return "MP3 64k";
    case FOUR_CHAR('m', 'p', 'g', 'e'):
        return "MP3 80k";
    case FOUR_CHAR('m', 'p', 'g', 'f'):
        return "MP3 96k";
    case FOUR_CHAR('m', 'p', 'g', 'g'):
        return "MP3 112k";
    case FOUR_CHAR('m', 'p', 'g', 'h'):
        return "MP3 128k";
    case FOUR_CHAR('m', 'p', 'g', 'i'):
        return "MP3 160k";
    case FOUR_CHAR('m', 'p', 'g', 'j'):
        return "MP3 192k";
    case FOUR_CHAR('m', 'p', 'g', 'k'):
        return "MP3 224k";
    case FOUR_CHAR('m', 'p', 'g', 'l'):
        return "MP3 256k";
    case FOUR_CHAR('m', 'p', 'g', 'm'):
        return "MP3 320k";
    case FOUR_CHAR('M', 'P', 'E', 'G'):
        return "MPEG";
    case FOUR_CHAR('O', 'g', 'g', 'V'):
    case FOUR_CHAR('V', 'O', 'R', 'B'):
    {
        const char *br = sub_bitrate(compression_sub_type);
        return br ? (std::string("Vorbis") + br) : std::string("Vorbis");
    }
    case FOUR_CHAR('O', 'g', 'g', 'O'):
    case FOUR_CHAR('O', 'P', 'U', 'S'):
    {
        const char *br = sub_bitrate(compression_sub_type);
        const char *base = opus_round_trip ? "Opus RT" : "Opus";
        return br ? (std::string(base) + br) : std::string(base);
    }
    default:
        if (compression_type > 0x20202020u)
        {
            char buf[16];
            std::snprintf(buf,
                          sizeof(buf),
                          "%c%c%c%c",
                          static_cast<char>((compression_type >> 24) & 0xFF),
                          static_cast<char>((compression_type >> 16) & 0xFF),
                          static_cast<char>((compression_type >> 8) & 0xFF),
                          static_cast<char>(compression_type & 0xFF));
            return std::string(buf);
        }
        return std::string();
    }
}

static std::string FormatVisibleCodecLabelFromEditorType(BAERmfEditorCompressionType type)
{
    if (type == BAE_EDITOR_COMPRESSION_PCM || type == BAE_EDITOR_COMPRESSION_DONT_CHANGE)
    {
        return std::string();
    }
    const char *label = GetSampleCodecLabel(type);
    if (!label || !label[0] || std::strcmp(label, "PCM") == 0)
    {
        return std::string();
    }
    return std::string(label);
}

static bool IsModuleExtension(const std::string &ext)
{
    return ext == "mod" || ext == "s3m" || ext == "xm" || ext == "it" ||
           ext == "mtm" || ext == "stm" || ext == "669" || ext == "far" ||
           ext == "ult" || ext == "amf" || ext == "dbm" || ext == "imf" ||
           ext == "liq" || ext == "med" || ext == "mgt" || ext == "okt" ||
           ext == "ptm";
}

static bool IsBankExtension(const std::string &ext)
{
    return ext == "hsb" || ext == "zsb" || ext == "bsn";
}

static std::string FormatBAEError(BAEResult result)
{
    switch (result)
    {
    case BAE_NO_ERROR:
        return "BAE_NO_ERROR";
    case BAE_PARAM_ERR:
        return "BAE_PARAM_ERR";
    case BAE_MEMORY_ERR:
        return "BAE_MEMORY_ERR";
    case BAE_DEVICE_UNAVAILABLE:
        return "BAE_DEVICE_UNAVAILABLE";
    case BAE_NOT_SETUP:
        return "BAE_NOT_SETUP";
    case BAE_BAD_SAMPLE_RATE:
        return "BAE_BAD_SAMPLE_RATE";
    case BAE_TOO_MANY_SAMPLES:
        return "BAE_TOO_MANY_SAMPLES";
    case BAE_FILE_IO_ERROR:
        return "BAE_FILE_IO_ERROR";
    case BAE_BAD_FILE:
        return "BAE_BAD_FILE";
    case BAE_UNSUPPORTED_FORMAT:
        return "BAE_UNSUPPORTED_FORMAT";
    default:
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "BAEResult(%d)", static_cast<int>(result));
        return buf;
    }
    }
}

class NbEditorApp
{
public:
    bool Init()
    {
        if (BAE_Setup() != 0)
        {
            SetStatus("BAE_Setup failed");
            return false;
        }

        m_bae_initialized = true;
        m_dialog_mutex = SDL_CreateMutex();
        LoadEditorPrefsFromDisk();
        if (!CreateMixer(true))
        {
            SetStatus("Audio init failed; running UI without engine");
            return true;
        }

        LoadRecentSessionsFromDisk();
        m_session_undocked_layout = m_pref_default_undocked_layout;
        CleanupStaleClipPackagesOnStartup();
        /* Clean slate by default; optional reopen via Preferences. */
        if (m_pref_reopen_last_session && !m_recent_sessions.empty())
        {
            const std::string &recent = m_recent_sessions.front();
            FILE *probe = std::fopen(recent.c_str(), "rb");
            if (probe)
            {
                std::fclose(probe);
                ImportPathIntoSession(recent, true);
            }
        }
        MidiHwApplyFromPrefs();
        ScheduleStartupTipsIfEnabled();
        SetStatus("NeoBAE Editor ready");
        return true;
    }

    void Shutdown()
    {
        MidiHwShutdownInput();
        CleanupPublishedClipPackages();
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (voice.sound)
            {
                BAESound_Stop(voice.sound, FALSE);
                BAESound_Delete(voice.sound);
                voice.sound = nullptr;
            }
            voice = SePreviewVoice{};
        }
        if (m_midi_click_hi)
        {
            BAESound_Stop(m_midi_click_hi, FALSE);
            BAESound_Delete(m_midi_click_hi);
            m_midi_click_hi = nullptr;
        }
        if (m_midi_click_lo)
        {
            BAESound_Stop(m_midi_click_lo, FALSE);
            BAESound_Delete(m_midi_click_lo);
            m_midi_click_lo = nullptr;
        }
        m_midi_click_hi_pcm.clear();
        m_midi_click_lo_pcm.clear();
        if (m_preview_song)
        {
            BAESong_Stop(m_preview_song, FALSE);
            BAESong_Delete(m_preview_song);
            m_preview_song = nullptr;
        }
        if (m_song)
        {
            BAESong_Stop(m_song, FALSE);
            BAESong_Delete(m_song);
            m_song = nullptr;
        }
        if (m_mixer)
        {
            BAEMixer_Close(m_mixer);
            m_mixer = nullptr;
        }
        if (m_document)
        {
            BAERmfEditorDocument_Delete(m_document);
            m_document = nullptr;
        }
        if (m_dialog_mutex)
        {
            SDL_DestroyMutex(m_dialog_mutex);
            m_dialog_mutex = nullptr;
        }
        if (m_bae_initialized)
        {
            BAE_Cleanup();
            m_bae_initialized = false;
        }
    }

    void SetMainWindow(SDL_Window *window)
    {
        m_main_window = window;
    }

    void SetRenderer(SDL_Renderer *renderer)
    {
        m_renderer = renderer;
    }

    void SetUiFonts(ImFont *regular, ImFont *italic)
    {
        m_font_regular = regular;
        m_font_italic = italic;
    }

    const char *GetStatus() const { return m_status; }

    /* Heavy file ops run between frames so we can pump a progress modal. */
    void PumpPendingLongOp()
    {
        if (m_long_op == LongOp::None)
        {
            return;
        }
        const LongOp op = m_long_op;
        const std::string path = std::move(m_long_op_path);
        m_long_op = LongOp::None;
        m_long_op_path.clear();
        if (op == LongOp::SaveSession)
        {
            if (!m_busy_active)
            {
                BeginBusyProgress("Saving Session…");
            }
            else
            {
                if (m_busy_title.empty())
                {
                    m_busy_title = "Saving Session…";
                }
                PumpBusyProgressFrame(true);
            }
            (void)SaveSessionToPath(path.c_str());
            EndBusyProgress();
        }
        else if (op == LongOp::ExportBank)
        {
            if (!m_busy_active)
            {
                BeginBusyProgress("Exporting Bank…");
            }
            else
            {
                if (m_busy_title.empty())
                {
                    m_busy_title = "Exporting Bank…";
                }
                PumpBusyProgressFrame(true);
            }
            ExportBankCloneToPath(path);
            EndBusyProgress();
        }
        else if (op == LongOp::ApplyInstrument)
        {
            if (!m_busy_active)
            {
                BeginBusyProgress("Applying instrument…");
            }
            else
            {
                if (m_busy_title.empty())
                {
                    m_busy_title = "Applying instrument…";
                }
                PumpBusyProgressFrame(true);
            }
            m_busy_last_pump_ms = 0;
            UpdateBusyProgress(0.25f, "Writing instrument…");
            ApplyInstrumentEditor(true);
            EndBusyProgress();
            if (m_ie_close_after_apply)
            {
                const bool applied_ok = !m_ie_dirty;
                m_ie_close_after_apply = false;
                if (applied_ok)
                {
                    FinishCloseInstrumentEditor(false);
                }
            }
        }
    }

    /* Must run outside NewFrame/EndFrame — mid-frame LoadIni clears docks and undocks windows. */
    void PumpPendingNbetLayout()
    {
        if (!m_pending_apply_nbet)
        {
            return;
        }
        if (m_nbet_apply_delay_frames > 0)
        {
            --m_nbet_apply_delay_frames;
            return;
        }
        /* Load window positions always; docking block is optional (undocked sessions). */
        if (!m_pending_imgui_ini.empty())
        {
            const bool has_docking =
                m_pending_imgui_ini.find("[Docking][Data]") != std::string::npos;
            ImGui::LoadIniSettingsFromMemory(m_pending_imgui_ini.c_str(),
                                             m_pending_imgui_ini.size());
            if (m_session_undocked_layout)
            {
                m_floating_layout_initialized = true;
                m_dock_layout_initialized = true; /* skip default dock builder */
                m_floating_placement_frames = 0;
            }
            else if (has_docking)
            {
                m_dock_layout_initialized = true;
                m_floating_layout_initialized = false;
            }
            m_pending_imgui_ini.clear();
        }
        m_pending_apply_nbet = false;
        /* Reopen IE/SE/Song Info/etc. after session lists are ready. */
        RestoreNbetEditorWindows();
    }

    /* Undocked = no drag-dock previews; docked = normal docking. Call before NewFrame. */
    void SyncDockingConfigForLayoutMode()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (m_session_undocked_layout)
        {
            io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
        }
        else
        {
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        }
    }

    void DrawUI()
    {
        /* Keep last hover sticky across frames: OS file drags often stop updating
         * ImGui hover, so clearing to None each frame broke MIDI→Songs drops. */
        PollHardwareMidiInput();
        MePumpCountInAndMetronome();
        ConsumeDialogResult();
        DrawMainMenuBar();
        DrawDockspace();
        DrawPlayerWindow();
        DrawSessionWindow();
        if (m_floating_placement_frames > 0)
        {
            --m_floating_placement_frames;
        }
        DrawMidiEditorWindow();
        DrawLyricsEditorWindow();
        DrawBankAddDialog();
        DrawSongInfoDialog();
        DrawSongSettingsDialog();
        DrawEditorPreferencesDialog();
        DrawModuleImportSettingsDialog();
        DrawTipsDialog();
        DrawAboutDialog();
        DrawInstrumentEditorDialog();
        DrawExportRmfDialog();
        DrawExportSampleRmfDialog();
        DrawExportBankDialog();
        DrawExportAudioDialog();
#if NBEDITOR_MVP
        DrawSampleEditorDialog();
#endif
        DrawTrashWindow();
        DrawEditConfirmDialogs();
        DrawResourceUsageDialog();
        DrawInstDestDialog();
        DrawDeleteInstrumentAliasConfirmDialog();
        DrawRemoveOrphanAliasesConfirmDialog();
        DrawInstMoveSongRemapConfirmDialog();
        PollInstMoveSongRemapPendingApply();
        DrawMidiNoteRemapperDialog();
        DrawRenameDialog();
        DrawIncludeSamplesPrompt();
        DrawSessionInfoDialog();
        DrawBankMergeConfirmDialog();
        DrawReplaceSessionConfirmDialog();
        DrawRmfOpenChoiceDialog();
        DrawDeleteSongConfirmDialog();
        DrawExportMidiConfirmDialog();
        DrawQuitConfirmDialog();
        DrawBankDestructiveConfirmDialogs();
#if USE_NEO_EFFECTS == TRUE
        DrawCustomReverbDialog();
#endif
        /* After menus/dialogs arm a long-op, paint busy before Present. */
        DrawBusyProgressOverlay();
        /* After windows update focus flags (sample/IE/MIDI). */
        HandleGlobalAppShortcuts();
    }

    void HandleDroppedPath(const char *path)
    {
        HandleDroppedPathInternal(path);
    }

    void HandlePcKeyboardEvent(const SDL_Event &event)
    {
        HandlePcKeyboardEventInternal(event);
    }

    /* File → Open / Windows "Open with" / argv paths (not tab-scoped DnD). */
    void OpenPathFromCommandLine(const char *path)
    {
        if (!path || !path[0])
        {
            return;
        }
        std::string cleaned = path;
        /* Windows sometimes quotes the path when launching via association. */
        if (cleaned.size() >= 2 && cleaned.front() == '"' && cleaned.back() == '"')
        {
            cleaned = cleaned.substr(1, cleaned.size() - 2);
        }
        if (cleaned.empty())
        {
            return;
        }
        std::snprintf(m_path_input, sizeof(m_path_input), "%s", cleaned.c_str());
        /* Explorer / argv launches always replace; Init already loaded the built-in
         * bank so ActiveBankHasContentToOverwrite() would otherwise always confirm. */
        ImportPathIntoSession(cleaned, true);
    }

    /* Returns true if at least one non-flag argv path was opened. */
    bool OpenCommandLineArgs(int argc, char **argv)
    {
        if (!argv || argc < 2)
        {
            return false;
        }
        bool opened_any = false;
        for (int i = 1; i < argc; ++i)
        {
            const char *arg = argv[i];
            if (!arg || !arg[0])
            {
                continue;
            }
            /* Skip common flag-style args; everything else is a file path. */
            if (arg[0] == '-')
            {
                continue;
            }
            OpenPathFromCommandLine(arg);
            opened_any = true;
        }
        return opened_any;
    }

private:
    enum class DialogAction
    {
        OpenImport = 0,
        ExportSong,
        ExportMidi,
        ExportOneShotRmf,
        ExportGroovoidRmf,
        ExportSample,
        ExportAudio,
        ExportBank,
        SaveSessionAs,
        AddSample,
        ReImportMidi,
    };

    enum class LongOp
    {
        None = 0,
        SaveSession,
        ExportBank,
        ApplyInstrument
    };

    static void SDLCALL OnFileDialogResult(void *userdata, const char *const *filelist, int)
    {
        NbEditorApp *app = static_cast<NbEditorApp *>(userdata);
        if (!app || !app->m_dialog_mutex)
        {
            return;
        }

        std::string selected_path;
        bool had_error = false;
        std::string error_message;

        if (!filelist)
        {
            had_error = true;
            const char *err = SDL_GetError();
            error_message = err ? err : "Unknown file dialog error";
        }
        else if (filelist[0])
        {
            selected_path = filelist[0];
        }

        SDL_LockMutex(app->m_dialog_mutex);
        app->m_dialog_result_ready = true;
        app->m_dialog_error = had_error;
        app->m_dialog_path = selected_path;
        app->m_dialog_error_message = error_message;
        SDL_UnlockMutex(app->m_dialog_mutex);
    }

    void OpenLoadDialog()
    {
        m_pending_dialog_action = DialogAction::OpenImport;
        static const SDL_DialogFileFilter filters[] = {
            {"All supported files", "rmf;zmf;mid;midi;kar;rmi;hsb;zsb;bsn;zsn;mod;s3m;xm;it;mtm;stm;669;far;ult;amf;dbm;imf;liq;med;mgt;okt;ptm"},
            {"Beatnik / NeoBAE Session (*.bsn;*.zsn)", "bsn;zsn"},
            {"RMF files (*.rmf;*.zmf)", "rmf;zmf"},
            {"Module Tracker file", "mod;s3m;xm;it;mtm;stm;669;far;ult;amf;dbm;imf;liq;med;mgt;okt;ptm"},
            {"MIDI files (*.mid;*.midi;*.kar;*.rmi)", "mid;midi;kar;rmi"},
            {"Bank files (*.hsb;*.zsb)", "hsb;zsb"},
            {"All files (*.*)", "*"},
        };

        SDL_ShowOpenFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               static_cast<int>(SDL_arraysize(filters)),
                               nullptr,
                               false);
    }

    void OpenAddSongFileDialog()
    {
        m_pending_dialog_action = DialogAction::OpenImport;
        static const SDL_DialogFileFilter filters[] = {
            {"Song files", "mid;midi;kar;rmi;rmf;zmf;mod;s3m;xm;it;mtm;stm;669;far;ult;amf;dbm;imf;liq;med;mgt;okt;ptm"},
            {"MIDI files (*.mid;*.midi;*.kar;*.rmi)", "mid;midi;kar;rmi"},
            {"RMF / ZMF (*.rmf;*.zmf)", "rmf;zmf"},
            {"Module Tracker file", "mod;s3m;xm;it;mtm;stm;669;far;ult;amf;dbm;imf;liq;med;mgt;okt;ptm"},
            {"All files (*.*)", "*"},
        };
        SDL_ShowOpenFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               static_cast<int>(SDL_arraysize(filters)),
                               nullptr,
                               false);
    }

    void OpenExportMidiDialog()
    {
        if (!m_document)
        {
            SetStatus("No song document to export");
            return;
        }
        m_pending_dialog_action = DialogAction::ExportMidi;
        static const SDL_DialogFileFilter filters[] = {
            {"MIDI (*.mid)", "mid"},
            {"All files (*.*)", "*"},
        };
        static char default_path[256];
        const std::string stem = SuggestedExportFileStem();
        std::snprintf(default_path,
                      sizeof(default_path),
                      "%s.mid",
                      stem.empty() ? "song" : stem.c_str());
        SDL_ShowSaveFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               static_cast<int>(SDL_arraysize(filters)),
                               default_path);
    }

    void OpenExportSongDialog()
    {
        if (!m_document)
        {
            SetStatus("No song document to export");
            return;
        }
        m_pending_dialog_action = DialogAction::ExportSong;
        uint32_t doc_reason = 0;
        uint32_t bank_reason = 0;
        bool want_zmf = false;
        if (BAERmfEditorDocument_RequiresZmf(m_document, &doc_reason) != FALSE)
        {
            doc_reason &= ~(uint32_t)BAEZMF_ALREADY_ZMF;
            want_zmf = (doc_reason != 0);
        }
        if (!want_zmf && BankRequiresZsb(&bank_reason))
        {
            /* Embedded bank instruments/samples may carry modern codecs / extended ADSR. */
            want_zmf = true;
        }
        static const SDL_DialogFileFilter filters_any[] = {
            {"RMF / ZMF (*.rmf;*.zmf)", "rmf;zmf"},
            {"MIDI (*.mid)", "mid"},
            {"All files (*.*)", "*"},
        };
        static const SDL_DialogFileFilter filters_zmf[] = {
            {"NeoBAE ZMF (*.zmf)", "zmf"},
            {"MIDI (*.mid)", "mid"},
            {"All files (*.*)", "*"},
        };
        static char default_path[256];
        const std::string stem = SuggestedExportFileStem();
        std::snprintf(default_path,
                      sizeof(default_path),
                      "%s.%s",
                      stem.c_str(),
                      want_zmf ? "zmf" : "rmf");
        SDL_ShowSaveFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               want_zmf ? filters_zmf : filters_any,
                               want_zmf ? static_cast<int>(SDL_arraysize(filters_zmf))
                                        : static_cast<int>(SDL_arraysize(filters_any)),
                               default_path);
    }

    void OpenSaveSessionDialog()
    {
        m_pending_dialog_action = DialogAction::SaveSessionAs;
        const bool want_zsn = SessionRequiresZsn(nullptr);
        static const SDL_DialogFileFilter filters_any[] = {
            {"Beatnik Session (*.bsn)", "bsn"},
            {"NeoBAE Session (*.zsn)", "zsn"},
            {"All files (*.*)", "*"},
        };
        static const SDL_DialogFileFilter filters_zsn[] = {
            {"NeoBAE Session (*.zsn)", "zsn"},
            {"All files (*.*)", "*"},
        };
        const SDL_DialogFileFilter *filters = want_zsn ? filters_zsn : filters_any;
        const int filter_count = want_zsn ? static_cast<int>(SDL_arraysize(filters_zsn))
                                          : static_cast<int>(SDL_arraysize(filters_any));
        static char default_path[1024];
        {
            /* Prefer a stem from the current song, not the previous session path
             * (Open as Session / New Session must not keep suggesting the old .zsn). */
            const std::string stem = SuggestedExportFileStem();
            const std::string leaf =
                (!stem.empty() ? stem : std::string("session")) + (want_zsn ? ".zsn" : ".bsn");
            if (!m_loaded_session_path.empty())
            {
                /* Keep directory of the open session when present. */
                std::string dir = m_loaded_session_path;
                const size_t slash = dir.find_last_of("/\\");
                if (slash != std::string::npos)
                {
                    dir = dir.substr(0, slash + 1);
                    std::snprintf(default_path, sizeof(default_path), "%s%s", dir.c_str(), leaf.c_str());
                }
                else
                {
                    std::snprintf(default_path, sizeof(default_path), "%s", leaf.c_str());
                }
            }
            else
            {
                std::snprintf(default_path, sizeof(default_path), "%s", leaf.c_str());
            }
        }
        SDL_ShowSaveFileDialog(OnFileDialogResult,
                               this,
                               m_main_window,
                               filters,
                               filter_count,
                               default_path);
    }

    void ConsumeDialogResult()
    {
        if (!m_dialog_mutex)
        {
            return;
        }

        bool ready = false;
        bool had_error = false;
        std::string selected_path;
        std::string error_message;

        SDL_LockMutex(m_dialog_mutex);
        if (m_dialog_result_ready)
        {
            ready = true;
            had_error = m_dialog_error;
            selected_path = m_dialog_path;
            error_message = m_dialog_error_message;
            m_dialog_result_ready = false;
            m_dialog_error = false;
            m_dialog_path.clear();
            m_dialog_error_message.clear();
        }
        SDL_UnlockMutex(m_dialog_mutex);

        if (!ready)
        {
            return;
        }

        if (had_error)
        {
            SetStatus(std::string("File dialog failed: ") + error_message);
            return;
        }

        if (selected_path.empty())
        {
            return;
        }

        if (m_pending_dialog_action == DialogAction::ExportSong)
        {
            ExportSessionSongToPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::ExportMidi)
        {
            ExportSessionSongMidiToPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::ExportOneShotRmf)
        {
            ExportOneShotRmfToPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::ExportGroovoidRmf)
        {
            ExportGroovoidRmfToPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::ExportSample)
        {
            ExportSampleToPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::SaveSessionAs)
        {
            ArmBusyProgress("Saving Session…");
            std::string out_path = selected_path;
            const bool want_zsn = SessionRequiresZsn(nullptr);
            const bool has_bsn = EndsWith(out_path, ".bsn");
            const bool has_zsn = EndsWith(out_path, ".zsn");
            if (!has_bsn && !has_zsn)
            {
                out_path += want_zsn ? ".zsn" : ".bsn";
            }
            out_path = EnforceSessionSavePath(out_path, nullptr, want_zsn ? 1 : 0);
            m_long_op = LongOp::SaveSession;
            m_long_op_path = out_path;
            return;
        }
        if (m_pending_dialog_action == DialogAction::ExportBank)
        {
            /* Arm first so DrawBusyProgressOverlay paints before path checks. */
            ArmBusyProgress("Exporting Bank…");
            m_long_op = LongOp::ExportBank;
            m_long_op_path = EnforceBankSavePath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::ExportAudio)
        {
            ExportSessionSongAsAudioToPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::ReImportMidi)
        {
            ReImportMidiFromPath(selected_path);
            return;
        }
        if (m_pending_dialog_action == DialogAction::AddSample)
        {
            BeginAddSampleFromPath(selected_path);
            return;
        }

        std::snprintf(m_path_input, sizeof(m_path_input), "%s", selected_path.c_str());
        ImportPathIntoSession(selected_path);
    }

    bool CreateMixer(bool initial)
    {
        BAEMixer old_mixer = m_mixer;

        if (m_preview_song)
        {
            BAESong_Stop(m_preview_song, FALSE);
            BAESong_Delete(m_preview_song);
            m_preview_song = nullptr;
        }
        for (SePreviewVoice &voice : m_se_preview_voices)
        {
            if (voice.sound)
            {
                BAESound_Stop(voice.sound, FALSE);
                BAESound_Delete(voice.sound);
            }
            voice = SePreviewVoice{};
        }
        if (m_song)
        {
            BAESong_Stop(m_song, FALSE);
            BAESong_Delete(m_song);
            m_song = nullptr;
            m_song_started = false;
        }
        if (m_midi_click_hi)
        {
            BAESound_Stop(m_midi_click_hi, FALSE);
            BAESound_Delete(m_midi_click_hi);
            m_midi_click_hi = nullptr;
        }
        if (m_midi_click_lo)
        {
            BAESound_Stop(m_midi_click_lo, FALSE);
            BAESound_Delete(m_midi_click_lo);
            m_midi_click_lo = nullptr;
        }
        m_midi_click_hi_pcm.clear();
        m_midi_click_lo_pcm.clear();

        if (old_mixer)
        {
            /* Drop delay lines before the device rate changes. */
            GM_CleanupReverb();
            ShutdownNewReverb();
#if USE_NEO_EFFECTS == TRUE
            ShutdownNeoReverb();
#endif
            BAEMixer_Delete(old_mixer);
            m_mixer = nullptr;
            m_bank_token = 0;
        }

        BAEMixer new_mixer = BAEMixer_New();
        if (!new_mixer)
        {
            SetStatus("BAEMixer_New failed");
            return false;
        }

        BAEAudioModifiers mods = BAE_USE_16;
        if (m_stereo)
        {
            mods |= BAE_USE_STEREO;
        }

        const int16_t midi_voices =
            static_cast<int16_t>(std::clamp(m_song_settings_voices, 8, 128));
        BAEResult open_result = BAEMixer_Open(new_mixer,
                                              RateFromHz(m_sample_rate_hz),
                                              BAE_LINEAR_INTERPOLATION,
                                              mods,
                                              midi_voices,
                                              16,
                                              32,
                                              TRUE);
        if (open_result != BAE_NO_ERROR)
        {
            BAEMixer_Delete(new_mixer);
            SetStatus(std::string("BAEMixer_Open failed: ") + FormatBAEError(open_result));
            return false;
        }

        BAEMixer_SetOutputGain(new_mixer, m_volume_percent);
        BAEMixer_SetDefaultReverb(new_mixer, static_cast<BAEReverbType>(m_reverb_type));

        BAESong new_preview = BAESong_New(new_mixer);
        if (!new_preview)
        {
            BAEMixer_Delete(new_mixer);
            SetStatus("BAESong_New for preview failed");
            return false;
        }
        EnableEditorZsCompatibility(new_preview);
        BAESong_Preroll(new_preview);
        /* Start+Pause: Preroll alone leaves IE/audition NoteOn silent. */
        BAESong_Start(new_preview, 0);
        BAESong_Pause(new_preview);

        std::array<BAESound, kSePreviewVoiceCount> new_se_sounds{};
        for (int i = 0; i < kSePreviewVoiceCount; ++i)
        {
            new_se_sounds[static_cast<size_t>(i)] = BAESound_New(new_mixer);
            if (!new_se_sounds[static_cast<size_t>(i)])
            {
                for (int j = 0; j < i; ++j)
                {
                    BAESound_Delete(new_se_sounds[static_cast<size_t>(j)]);
                }
                BAESong_Delete(new_preview);
                BAEMixer_Delete(new_mixer);
                SetStatus("BAESound_New for sample preview failed");
                return false;
            }
        }

        m_mixer = new_mixer;
        m_preview_song = new_preview;
        for (int i = 0; i < kSePreviewVoiceCount; ++i)
        {
            m_se_preview_voices[static_cast<size_t>(i)].sound = new_se_sounds[static_cast<size_t>(i)];
        }

        if (!LoadActiveBankIntoMixer())
        {
            SetStatus("Bank load failed");
        }
        RefreshListsFromBank();
        ApplyChannelMutes();

        if (m_document)
        {
            ReloadPlaybackFromDocument(true);
        }
        else if (!m_loaded_song_path.empty())
        {
            LoadSongFromPath(m_loaded_song_path.c_str());
        }

        if (!initial)
        {
            SetStatus("Engine reconfigured");
        }

        return true;
    }

    bool LoadBuiltinBankToMixer(BAEMixer mixer, BAEBankToken *out_token)
    {
        if (!mixer)
        {
            return false;
        }

#if _BUILT_IN_PATCHES == TRUE
        BAEBankToken token = 0;
        const BAEResult result = BAEMixer_LoadBuiltinBank(mixer, &token);
        if (out_token)
        {
            *out_token = token;
        }
        return (result == BAE_NO_ERROR && token != 0);
#else
        if (out_token)
        {
            *out_token = 0;
        }
        return false;
#endif
    }

    bool LoadActiveBankIntoMixer()
    {
        if (!m_mixer)
        {
            return false;
        }

        BAEMixer_UnloadBanks(m_mixer);

        BAEBankToken token = 0;
        if (!m_loaded_bank_path.empty())
        {
            const BAEResult bank_result = BAEMixer_AddBankFromFile(m_mixer,
                                                                   const_cast<char *>(m_loaded_bank_path.c_str()),
                                                                   &token);
            if (bank_result == BAE_NO_ERROR && token != 0)
            {
                m_bank_token = token;
                m_song_used_keys_dirty = true;
                EnableEditorBankZsCompatibility(m_bank_token);
                return true;
            }
            m_loaded_bank_path.clear();
            m_loaded_bank_display_name.clear();
        }

        if (LoadBuiltinBankToMixer(m_mixer, &token))
        {
            m_loaded_bank_display_name.clear();
            m_bank_token = token;
            EnableEditorBankZsCompatibility(m_bank_token);
            return true;
        }

        m_bank_token = 0;
        return false;
    }

    void SelectAuditionBank0AfterBankLoad()
    {
        m_selected_bank = 0;
        BAESong audition_song = GetAuditionSong();
        if (audition_song)
        {
            BAESong_ProgramBankChange(audition_song,
                                      0,
                                      static_cast<unsigned char>(m_selected_program),
                                      static_cast<unsigned char>(m_selected_bank),
                                      0);
        }
    }

    /* preserve_bank2: when an active bank exists, unload Bank 0/1 safely (same as
     * File→Unload Bank) then merge Bank 0/1 from the new file. Pass false for
     * session opens and for pure bank opens with nothing to keep (avoids merging
     * onto Init's built-in and leaking its unused samples). */
    bool LoadBankFromPath(const char *path, bool preserve_bank2 = true)
    {
        if (!m_mixer || !path || !path[0])
        {
            return false;
        }

        if (preserve_bank2 && m_bank_token)
        {
            const bool ok = MergeBank01FromPath(path);
            if (ok)
            {
                SelectAuditionBank0AfterBankLoad();
            }
            return ok;
        }

        bool restart_song = false;
        bool song_was_paused = false;
        uint32_t restore_pos_us = 0;
        std::string song_reload_path;
        if (m_song && !m_loaded_song_path.empty())
        {
            BAE_BOOL paused_flag = FALSE;
            if (BAESong_IsPaused(m_song, &paused_flag) == BAE_NO_ERROR)
            {
                song_was_paused = (paused_flag != FALSE);
            }
            (void)BAESong_GetMicrosecondPosition(m_song, &restore_pos_us);
            song_reload_path = m_loaded_song_path;
            BAESong_Stop(m_song, FALSE);
            restart_song = true;
        }

        BAEMixer_UnloadBanks(m_mixer);

        BAEBankToken token = 0;
        const BAEResult bank_result = BAEMixer_AddBankFromFile(m_mixer,
                                                               const_cast<char *>(path),
                                                               &token);
        if (bank_result != BAE_NO_ERROR || token == 0)
        {
            LoadActiveBankIntoMixer();
            SetStatus(std::string("Bank load failed: ") + FormatBAEError(bank_result));
            return false;
        }

        m_loaded_bank_path = path;
        m_loaded_bank_display_name = FileNameFromPath(m_loaded_bank_path);
        m_bank_token = token;
        m_song_used_keys_dirty = true;
        EnableEditorBankZsCompatibility(m_bank_token);
        /* Full token replace: previous session's Bank 0/1 promote / custom IDs
         * and deferred-encode caches must not bleed into the new bank. */
        m_session_custom_snd_ids.clear();
        m_session_custom_inst_ids.clear();
        m_session_pcm_cache.clear();
        m_app_compression_cache.clear();
        m_bank01_touched = false;
        m_instrument_filter_index = -1; /* full replace → default Custom Melodic */
        RefreshListsFromBank();
        SelectAuditionBank0AfterBankLoad();

        if (restart_song)
        {
            if (LoadSongFromPath(song_reload_path.c_str()) && m_song)
            {
                if (restore_pos_us > 0)
                {
                    BAESong_SetMicrosecondPosition(m_song, restore_pos_us);
                }
                if (song_was_paused)
                {
                    BAESong_Pause(m_song);
                }
                else
                {
                    BAESong_Resume(m_song);
                }
                m_song_started = true;
            }
            else
            {
                SetStatus("Bank loaded (song reload failed)");
                return true;
            }
        }

        SetStatus("Bank loaded");
        return true;
    }

    bool LoadBuiltinBankAsActive()
    {
        if (!m_mixer)
        {
            return false;
        }

        /* Prefer safe Bank 0/1 replace so Bank 2+ / custom samples survive. */
        if (m_bank_token)
        {
            const bool ok = MergeBank01FromBuiltin();
            if (ok)
            {
                m_loaded_bank_path.clear();
                m_loaded_bank_display_name.clear();
                SelectAuditionBank0AfterBankLoad();
            }
            return ok;
        }

        BAEMixer_UnloadBanks(m_mixer);
        BAEBankToken token = 0;
        if (!LoadBuiltinBankToMixer(m_mixer, &token) || token == 0)
        {
            SetStatus("Built-in bank unavailable");
            return false;
        }

        m_loaded_bank_path.clear();
        m_loaded_bank_display_name.clear();
        m_bank_token = token;
        m_song_used_keys_dirty = true;
        EnableEditorBankZsCompatibility(m_bank_token);
        RefreshListsFromBank();
        SelectAuditionBank0AfterBankLoad();

        SetStatus("Built-in bank loaded");
        return true;
    }

    void SetStatus(const std::string &text)
    {
        std::snprintf(m_status, sizeof(m_status), "%s", text.c_str());
    }

    /* Arm overlay without pumping — safe mid-DrawUI (menu / dialog result).
     * DrawBusyProgressOverlay paints it before the frame presents; the long-op
     * then runs on the next PumpPendingLongOp with progress already visible. */
    void ArmBusyProgress(const char *title)
    {
        m_busy_active = true;
        m_busy_title = title ? title : "Working…";
        m_busy_detail.clear();
        m_busy_fraction = 0.0f;
        m_busy_last_pump_ms = 0;
    }

    void BeginBusyProgress(const char *title)
    {
        ArmBusyProgress(title);
        /* Two pumps: first frame often only clears the backbuffer before the
         * overlay is ready; second paints title/progress immediately. */
        PumpBusyProgressFrame(true);
        PumpBusyProgressFrame(true);
    }

    void DrawBusyProgressOverlay()
    {
        if (!m_busy_active)
        {
            return;
        }
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.11f, 0.92f));
        ImGui::Begin("##nbeditor_busy_backdrop",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoInputs);
        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##nbeditor_busy",
                     nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking);
        ImGui::TextUnformatted(m_busy_title.c_str());
        ImGui::Spacing();
        ImGui::ProgressBar(m_busy_fraction, ImVec2(-1.0f, 0.0f));
        if (!m_busy_detail.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_busy_detail.c_str());
        }
        ImGui::End();
    }

    void UpdateBusyProgress(float fraction, const char *detail = nullptr)
    {
        if (!m_busy_active)
        {
            return;
        }
        m_busy_fraction = std::clamp(fraction, 0.0f, 1.0f);
        if (detail)
        {
            m_busy_detail = detail;
        }
        PumpBusyProgressFrame(false);
    }

    void EndBusyProgress()
    {
        if (!m_busy_active)
        {
            return;
        }
        m_busy_fraction = 1.0f;
        PumpBusyProgressFrame(true);
        m_busy_active = false;
        m_busy_detail.clear();
    }

    void PumpBusyProgressFrame(bool force)
    {
        if (!m_busy_active || !m_renderer || !m_main_window)
        {
            return;
        }
        /* Nested NewFrame inside DrawUI crashes ImGui (e.g. Add Instrument →
         * EnsureWritable "Preparing bank…" mid-dialog). LongOps pump between frames. */
        ImGuiContext *ctx = ImGui::GetCurrentContext();
        if (ctx && ctx->WithinFrameScope)
        {
            return;
        }
        const uint32_t now = SDL_GetTicks();
        if (!force && m_busy_last_pump_ms != 0 && (now - m_busy_last_pump_ms) < 50u)
        {
            return;
        }
        m_busy_last_pump_ms = now;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiIO &io = ImGui::GetIO();
        /* Full-window dim + centered card (regular window — OpenPopup often
         * fails to paint on the first pump, which looked like a blank window). */
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.11f, 0.92f));
        ImGui::Begin("##nbeditor_busy_backdrop",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoInputs);
        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##nbeditor_busy",
                     nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking);
        ImGui::TextUnformatted(m_busy_title.c_str());
        ImGui::Spacing();
        ImGui::ProgressBar(m_busy_fraction, ImVec2(-1.0f, 0.0f));
        if (!m_busy_detail.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_busy_detail.c_str());
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(m_renderer, 18, 22, 28, 255);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        SDL_RenderPresent(m_renderer);
    }

    bool LoadSongFromPath(const char *path)
    {
        if (!m_mixer || !path || !path[0])
        {
            return false;
        }

        SetStatus("");

        const std::string path_str(path);
    const size_t ext_dot = path_str.find_last_of('.');
    const std::string ext = (ext_dot == std::string::npos) ? std::string() : ToLower(path_str.substr(ext_dot + 1));
    const bool module_like = IsModuleExtension(ext);
        const bool rmf_like = EndsWith(path_str, ".rmf") || EndsWith(path_str, ".hsb") || EndsWith(path_str, ".zmf") || EndsWith(path_str, ".zsb");

        const bool had_song = (m_song != nullptr);
        if (m_song)
        {
            BAESong_Stop(m_song, FALSE);
            BAESong_Delete(m_song);
            m_song = nullptr;
            m_loaded_song_path.clear();
            m_song_started = false;
        }

        BAESong song = BAESong_New(m_mixer);
        if (!song)
        {
            SetStatus("BAESong_New failed");
            return false;
        }

        const BAEResult load_result = LoadSongData(song, path);

        if (load_result != BAE_NO_ERROR)
        {
            BAESong_Delete(song);
            SetStatus(had_song ? std::string("Song unloaded (load failed: ") + FormatBAEError(load_result) + ")"
                              : std::string("Load failed: ") + FormatBAEError(load_result));
            return false;
        }

        EnableEditorZsCompatibility(song);
        BAESong_Preroll(song);
        BAESong_SetLoops(song, m_loop_enabled ? 32767 : 0);
        BAEMixer_SetOutputGain(m_mixer, m_volume_percent);
        (void)BAESong_SetTranspose(song, std::clamp(m_transpose_semitones, -24, 24));
        ResetPeakVoices();

        /* Preroll only — Start+Pause dispatches and kills tick-0 notes before Play.
         * Preview song (RebuildPreviewSongFromPath) still Start+Pauses for audition. */
        m_song = song;
        m_loaded_song_path = path;
        m_song_started = false;
        RebuildPreviewSongFromPath(path);

        if (!rmf_like && !module_like)
        {
            if (m_document)
            {
                BAERmfEditorDocument_Delete(m_document);
                m_document = nullptr;
            }
            ClearSongOverrides();
        }

        ApplyChannelMutes();
        SetStatus("Song loaded");
        return true;
    }

    BAEResult LoadSongDataFromDocumentMemory(BAESong song)
    {
        if (!song || !m_document)
        {
            return BAE_PARAM_ERR;
        }

        unsigned char *rmf_data = nullptr;
        uint32_t rmf_size = 0;
        uint32_t zmf_reason = 0;
        const bool use_zmf = BAERmfEditorDocument_RequiresZmf(m_document, &zmf_reason) != FALSE;
        const BAEResult save_result = BAERmfEditorDocument_SaveAsRmfToMemoryEx(m_document,
                                                                              use_zmf,
                                                                              FALSE,
                                                                              &rmf_data,
                                                                              &rmf_size);
        if (save_result != BAE_NO_ERROR || !rmf_data || rmf_size == 0)
        {
            if (rmf_data)
            {
                XDisposePtr((XPTR)rmf_data);
            }
            return (save_result != BAE_NO_ERROR) ? save_result : BAE_BAD_FILE;
        }

        const BAEResult load_result = BAESong_LoadRmfFromMemory(song,
                                                                 rmf_data,
                                                                 rmf_size,
                                                                 0,
                                                                 TRUE);
        XDisposePtr((XPTR)rmf_data);
        return load_result;
    }

    BAEResult LoadSongData(BAESong song, const char *path)
    {
        if (!song || !path || !path[0])
        {
            return BAE_PARAM_ERR;
        }

        BAEResult load_result = BAE_BAD_FILE;
        const std::string path_str(path);
        if (EndsWith(path_str, ".rmf") || EndsWith(path_str, ".hsb") || EndsWith(path_str, ".zmf") || EndsWith(path_str, ".zsb"))
        {
            load_result = BAESong_LoadRmfFromFile(song, const_cast<char *>(path), 0, TRUE);
        }
        else
        {
            const size_t ext_dot = path_str.find_last_of('.');
            const std::string ext = (ext_dot == std::string::npos) ? std::string() : ToLower(path_str.substr(ext_dot + 1));
            if (IsModuleExtension(ext) && m_document)
            {
                // Module paths are converted to document form; playback uses serialized RMF/ZMF from that document.
                load_result = LoadSongDataFromDocumentMemory(song);
            }

            if (load_result == BAE_NO_ERROR)
            {
                return load_result;
            }

            load_result = BAESong_LoadMidiFromFile(song, const_cast<char *>(path), TRUE);
            if (load_result != BAE_NO_ERROR)
            {
                load_result = BAESong_LoadRmfFromFile(song, const_cast<char *>(path), 0, TRUE);
            }
        }
        return load_result;
    }

    void RebuildPreviewSongFromPath(const char *path)
    {
        if (!m_mixer)
        {
            return;
        }

        if (m_preview_song)
        {
            BAESong_Stop(m_preview_song, FALSE);
            BAESong_Delete(m_preview_song);
            m_preview_song = nullptr;
        }

        BAESong preview = BAESong_New(m_mixer);
        if (!preview)
        {
            return;
        }

        bool loaded_ok = true;
        if (path && path[0])
        {
            loaded_ok = (LoadSongData(preview, path) == BAE_NO_ERROR);
        }

        if (!loaded_ok)
        {
            BAESong_Delete(preview);
            return;
        }

        EnableEditorZsCompatibility(preview);
        BAESong_Preroll(preview);
        BAESong_Start(preview, 0);
        BAESong_Pause(preview);
        m_preview_song = preview;
    }

    /* Editor session always runs with ZMF/ZSB feature gates so stereo LPF,
     * short loops, etc. audition correctly. Export still uses RequiresZ*. */
    void EnableEditorZsCompatibility(BAESong song) const
    {
        if (song)
        {
            (void)BAESong_SetZmfCompatibilityMode(song, TRUE);
        }
    }

    void EnableEditorBankZsCompatibility(BAEBankToken token) const
    {
        if (token)
        {
            XFileSetForceZsbFeatures(reinterpret_cast<XFILE>(token), TRUE);
        }
    }

    /* Blank bank-audition song (not an RMF playback song). Session reload may
     * delete m_preview_song; recreate so IE keys keep working. */
    void EnsureBankAuditionSong()
    {
        if (!m_mixer)
        {
            return;
        }
        if (m_preview_song)
        {
            EnableEditorZsCompatibility(m_preview_song);
            return;
        }
        BAESong preview = BAESong_New(m_mixer);
        if (!preview)
        {
            return;
        }
        EnableEditorZsCompatibility(preview);
        BAESong_Preroll(preview);
        BAESong_Start(preview, 0);
        BAESong_Pause(preview);
        m_preview_song = preview;
    }

    BAESong GetAuditionSong() const
    {
        /* Always audition through the paused bank preview song — never the live
         * playback song. Selecting a Sample/Instrument (incl. double-click to
         * open Sample Editor) issues ProgramBankChange on ch1/ch10; routing that
         * into m_song remapped the playing song and looked like voice stealing.
         * Song-embedded customs are promoted into the bank on RMF open, so the
         * preview song can resolve them. */
        return m_preview_song ? m_preview_song : m_song;
    }

    bool LookupDocumentSampleDisplayNameForSnd(uint32_t snd_id, std::string *out_name) const
    {
        if (!m_document || !out_name || snd_id == kNoSndResourceId)
        {
            return false;
        }
        uint32_t sample_count = 0;
        if (BAERmfEditorDocument_GetSampleCount(m_document, &sample_count) != BAE_NO_ERROR)
        {
            return false;
        }
        for (uint32_t i = 0; i < sample_count; ++i)
        {
            uint32_t asset_id = 0;
            if (BAERmfEditorDocument_GetSampleAssetIDForSample(m_document, i, &asset_id) !=
                    BAE_NO_ERROR ||
                asset_id != snd_id)
            {
                continue;
            }
            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, i, &info) == BAE_NO_ERROR &&
                info.displayName && info.displayName[0])
            {
                *out_name = info.displayName;
                return true;
            }
        }
        return false;
    }

    bool LookupDocumentSampleDisplayNameForInst(uint32_t inst_id, std::string *out_name) const
    {
        /* INST id 0 is valid — only reject unset sentinel / missing doc. */
        if (!m_document || !out_name || inst_id == BAE_EDITOR_INST_ID_NONE)
        {
            return false;
        }
        uint32_t sample_count = 0;
        if (BAERmfEditorDocument_GetSampleCount(m_document, &sample_count) != BAE_NO_ERROR)
        {
            return false;
        }
        for (uint32_t i = 0; i < sample_count; ++i)
        {
            uint32_t doc_inst = BAE_EDITOR_INST_ID_NONE;
            if (BAERmfEditorDocument_GetInstIDForSample(m_document, i, &doc_inst) != BAE_NO_ERROR ||
                doc_inst != inst_id)
            {
                continue;
            }
            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, i, &info) == BAE_NO_ERROR &&
                info.displayName && info.displayName[0])
            {
                *out_name = info.displayName;
                return true;
            }
        }
        return false;
    }

    void ClearSongOverrides()
    {
        m_song_override_instruments.clear();
        m_loaded_doc_path.clear();
        m_instruments = m_bank_instruments;
        RebuildInstrumentFilters();
    }

    void ApplySongOverridesToInstrumentList()
    {
        m_instruments = m_bank_instruments;

        /* Key must include percussion: melodic B0P059 (INST 59) and drum
         * B0N059 (INST 187) share bank+program and must not collide. */
        using SlotKey = std::tuple<int, int, bool>;
        std::map<SlotKey, size_t> by_slot;
        std::map<uint32_t, size_t> by_inst_id;
        for (size_t i = 0; i < m_instruments.size(); ++i)
        {
            const InstrumentRow &inst = m_instruments[i];
            by_slot[SlotKey(inst.bank, inst.program, inst.percussion)] = i;
            /* INST id 0 is valid — only skip aliases / unset sentinel. */
            if (!inst.is_alias && inst.inst_id != BAE_EDITOR_INST_ID_NONE)
            {
                by_inst_id[inst.inst_id] = i;
            }
        }

        for (const InstrumentRow &song_inst : m_song_override_instruments)
        {
            const SlotKey key(song_inst.bank, song_inst.program, song_inst.percussion);
            size_t match_index = static_cast<size_t>(-1);
            if (song_inst.inst_id != BAE_EDITOR_INST_ID_NONE)
            {
                auto by_id = by_inst_id.find(song_inst.inst_id);
                if (by_id != by_inst_id.end())
                {
                    match_index = by_id->second;
                }
            }
            if (match_index == static_cast<size_t>(-1))
            {
                auto by_slot_it = by_slot.find(key);
                if (by_slot_it != by_slot.end())
                {
                    match_index = by_slot_it->second;
                }
            }

            if (match_index != static_cast<size_t>(-1))
            {
                InstrumentRow &base = m_instruments[match_index];
                base.has_song_override = true;
                base.is_custom = true;
                base.percussion = song_inst.percussion;
                /* Matched a bank list row — keep bank ops (Move/Clone/…) enabled. */
                base.from_song_document = false;
                if (song_inst.inst_id != BAE_EDITOR_INST_ID_NONE)
                {
                    base.inst_id = song_inst.inst_id;
                    base.target_inst_id = song_inst.inst_id;
                    /* Prefer the bank row that matches the promoted INST id so IE /
                     * keymap ops read RMF-promoted data, not a colliding GM slot. */
                    for (const InstrumentRow &bank_row : m_bank_instruments)
                    {
                        if (!bank_row.is_alias && bank_row.inst_id == song_inst.inst_id)
                        {
                            base.instrument_index = bank_row.instrument_index;
                            base.split_count = bank_row.split_count;
                            if (!bank_row.name.empty())
                            {
                                base.name = bank_row.name;
                            }
                            break;
                        }
                    }
                }
                base.split_count = std::max(base.split_count, song_inst.split_count);
                /* Fill name only when the bank INST has none; song_inst.name is
                 * often a sample display name and should not replace INST names. */
                if (base.name.empty() && !song_inst.name.empty())
                {
                    base.name = song_inst.name;
                }
                /* Only Bank 0/1 slots are GM overrides; Bank 2+ customs are native. */
                if (base.bank < 2 && base.name.find("(Override)") == std::string::npos)
                {
                    base.name += " (Override)";
                }
            }
            else
            {
                InstrumentRow merged = song_inst;
                merged.has_song_override = true;
                merged.is_custom = true;
                merged.from_song_document = true;
                if (song_inst.inst_id != BAE_EDITOR_INST_ID_NONE)
                {
                    for (const InstrumentRow &bank_row : m_bank_instruments)
                    {
                        if (!bank_row.is_alias && bank_row.inst_id == song_inst.inst_id)
                        {
                            merged.instrument_index = bank_row.instrument_index;
                            merged.from_song_document = false; /* promoted into bank */
                            merged.split_count = bank_row.split_count;
                            break;
                        }
                    }
                }
                /* No matching bank row — not an override of a Bank 0/1 INST. */
                by_slot[key] = m_instruments.size();
                if (merged.inst_id != BAE_EDITOR_INST_ID_NONE)
                {
                    by_inst_id[merged.inst_id] = m_instruments.size();
                }
                m_instruments.push_back(merged);
            }
        }

        std::sort(m_instruments.begin(), m_instruments.end(), [](const InstrumentRow &a, const InstrumentRow &b) {
            if (a.bank != b.bank)
            {
                return a.bank < b.bank;
            }
            if (a.percussion != b.percussion)
            {
                return static_cast<int>(a.percussion) < static_cast<int>(b.percussion);
            }
            return a.program < b.program;
        });

        RebuildInstrumentFilters();
    }

    void RefreshSongOverridesFromDocument()
    {
        m_song_override_instruments.clear();
        if (!m_document)
        {
            ApplySongOverridesToInstrumentList();
            return;
        }

        uint32_t sample_count = 0;
        if (BAERmfEditorDocument_GetSampleCount(m_document, &sample_count) != BAE_NO_ERROR)
        {
            ApplySongOverridesToInstrumentList();
            return;
        }

        /* Slot = bank + program + melodic/perc; melodic P059 ≠ drum N059. */
        using SlotKey = std::tuple<int, int, bool>;
        std::map<SlotKey, InstrumentRow> by_slot;
        for (uint32_t i = 0; i < sample_count; ++i)
        {
            BAERmfEditorSampleInfo info;
            std::memset(&info, 0, sizeof(info));
            if (BAERmfEditorDocument_GetSampleInfo(m_document, i, &info) != BAE_NO_ERROR)
            {
                continue;
            }

            uint32_t inst_id = BAE_EDITOR_INST_ID_NONE;
            (void)BAERmfEditorDocument_GetInstIDForSample(m_document, i, &inst_id);

            int bank = 2;
            int program = static_cast<int>(info.program);
            bool percussion = (program >= 128);
            if (inst_id != BAE_EDITOR_INST_ID_NONE)
            {
                bank = static_cast<int>(inst_id / 256u);
                program = static_cast<int>(inst_id % 128u);
                percussion = ((inst_id & 0x80u) != 0u);
            }
            else
            {
                /* info.program is 0..127 within its category; treat high bit
                 * form as percussion only when no INST id is available. */
                if (program >= 128)
                {
                    percussion = true;
                    program = program % 128;
                }
            }

            InstrumentRow &inst = by_slot[SlotKey(bank, program, percussion)];
            if (inst.split_count == 0)
            {
                inst.is_custom = true;
                inst.has_song_override = true;
                inst.from_song_document = true;
                inst.inst_id = (inst_id != BAE_EDITOR_INST_ID_NONE) ? inst_id
                                                                   : BAE_EDITOR_INST_ID_NONE;
                inst.target_inst_id = inst.inst_id;
                inst.program = program;
                inst.bank = bank;
                inst.percussion = percussion;
                /* Prefer INST resource name over per-sample display names.
                 * INST id 0 is valid — only skip the unset sentinel. */
                if (inst.inst_id != BAE_EDITOR_INST_ID_NONE && m_document)
                {
                    BAERmfEditorInstrumentExtInfo ext{};
                    if (BAERmfEditorDocument_GetInstrumentExtInfo(m_document,
                                                                  inst.inst_id,
                                                                  &ext) == BAE_NO_ERROR &&
                        ext.displayName && ext.displayName[0])
                    {
                        inst.name = ext.displayName;
                    }
                }
                if (inst.name.empty() && info.displayName && info.displayName[0])
                {
                    inst.name = info.displayName;
                }
                if (inst.name.empty())
                {
                    char generated[64];
                    std::snprintf(generated, sizeof(generated), "Inst B%dP%03d", bank, program);
                    inst.name = generated;
                }
            }
            inst.split_count += 1;
        }

        for (const auto &entry : by_slot)
        {
            m_song_override_instruments.push_back(entry.second);
        }

        ApplySongOverridesToInstrumentList();
    }

    bool LoadDocumentFromPath(const char *path)
    {
        if (!path || !path[0])
        {
            return false;
        }

        BAERmfEditorDocument *new_doc = BAERmfEditorDocument_LoadFromFile(const_cast<char *>(path));
        if (!new_doc)
        {
            SetStatus("RMF/ZMF document load failed (lists unchanged)");
            return false;
        }

        if (m_document)
        {
            BAERmfEditorDocument_Delete(m_document);
        }
        m_document = new_doc;
        m_loaded_doc_path = path;
        m_document_dirty = false;
        m_song_info_apply_player_reverb = false;
        m_song_info_apply_player_velocity_curve = false;
        SyncPlayerMixNrpnsFromDocument();
        ApplyPlayerMixSettings();
        RefreshSongOverridesFromDocument();
        RefreshSongSamplesFromDocument();
        SetStatus("Document loaded");
        return true;
    }

    bool LoadModuleFromPath(const char *path)
    {
        if (!path || !path[0])
        {
            return false;
        }

#if defined(LIBXMP_STATIC) || defined(LIBXMP_CORE_PLAYER)
        BAERmfEditorDocument *new_doc = nullptr;
        Mod2RmfLoadOptions mod_opts;
        mod2rmf_load_options_defaults(&mod_opts);
        mod_opts.useZmfContainer = true;
        mod_opts.useExtendedPitchRange = m_pref_mod_ext_pitch;
        mod_opts.useExtendedAdsr = m_pref_mod_ext_adsr;
        mod_opts.resamplerSettings.amigaFilter =
            static_cast<Mod2RmfAmigaFilter>(std::clamp(m_pref_mod_amiga_filter, 0, 2));
        mod_opts.resamplerSettings.resampleFilter =
            static_cast<Mod2RmfResampleFilter>(std::clamp(m_pref_mod_resample_filter, 0, 3));
        {
            int rate = m_pref_mod_resample_rate;
            if (rate != 0 && (rate < 1000 || rate > 384000))
            {
                rate = 0;
            }
            mod_opts.resamplerSettings.targetRate = static_cast<uint32_t>(std::max(0, rate));
        }
        mod_opts.stereoSeparation =
            static_cast<uint8_t>(std::clamp(m_pref_mod_stereo_sep, 0, 100));
        const BAEResult result =
            mod2rmf_load_module_to_document_ex(&new_doc, path, &mod_opts);
        if (result != BAE_NO_ERROR || !new_doc)
        {
            SetStatus(std::string("Module load failed: ") + FormatBAEError(result));
            return false;
        }

        if (m_document)
        {
            BAERmfEditorDocument_Delete(m_document);
        }
        m_document = new_doc;
        m_loaded_doc_path = path;
        m_document_dirty = false;
        RefreshSongOverridesFromDocument();
        UpdateSongRowFromDocument(path, "Module");
        const bool song_ok = ReloadPlaybackFromDocument(true);
        SetStatus(song_ok ? "Module loaded" : "Module loaded (playback failed)");
        return true;
#else
        SetStatus("Tracker module support not enabled in this build");
        return false;
#endif
    }

    /* Collect SND ids referenced by any INST (bank token = XFILE). */
    void CollectBankReferencedSndIdsAll(BAEBankToken token, std::set<uint32_t> &out) const
    {
        out.clear();
        if (!token)
        {
            return;
        }
        uint32_t inst_count = 0;
        if (BAERmfEditorBank_GetInstrumentCount(token, &inst_count) != BAE_NO_ERROR)
        {
            return;
        }
        for (uint32_t ii = 0; ii < inst_count; ++ii)
        {
            uint32_t split_count = 0;
            if (BAERmfEditorBank_GetInstrumentSampleCount(token, ii, &split_count) != BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t si = 0; si < split_count; ++si)
            {
                BAERmfEditorBankSampleInfo sinfo;
                std::memset(&sinfo, 0, sizeof(sinfo));
                if (BAERmfEditorBank_GetInstrumentSampleInfo(token, ii, si, &sinfo) ==
                        BAE_NO_ERROR &&
                    static_cast<uint16_t>(sinfo.sndResourceID) != 0xFFFFu)
                {
                    /* SND id 0 is valid — only 0xFFFF means "no sample". */
                    out.insert(static_cast<uint32_t>(static_cast<uint16_t>(sinfo.sndResourceID)));
                }
            }
        }
    }

    int CountUnreferencedBankSamples(BAEBankToken token) const
    {
        if (!token)
        {
            return 0;
        }
        XFILE bank = reinterpret_cast<XFILE>(token);
        std::set<uint32_t> referenced;
        CollectBankReferencedSndIdsAll(token, referenced);
        int unused = 0;
        /* Prefer access cache — XGetIndexedFileResource loads full sample bodies. */
        XFILENAME *ref = reinterpret_cast<XFILENAME *>(bank);
        if (ref && ref->pCache && ref->pCache->totalResources > 0)
        {
            for (int32_t i = 0; i < ref->pCache->totalResources; ++i)
            {
                const XFILE_CACHED_ITEM &item = ref->pCache->cached[i];
                if (item.resourceType != ID_SND && item.resourceType != ID_CSND &&
                    item.resourceType != ID_ESND)
                {
                    continue;
                }
                if (referenced.count(static_cast<uint32_t>(item.resourceID)) == 0)
                {
                    ++unused;
                }
            }
            return unused;
        }
        const XResourceType snd_types[] = {ID_ESND, ID_CSND, ID_SND};
        for (XResourceType stype : snd_types)
        {
            const int32_t count = XCountFileResourcesOfType(bank, stype);
            for (int32_t i = 0; i < count; ++i)
            {
                XLongResourceID rid = 0;
                int32_t size = 0;
                XPTR data = XGetIndexedFileResource(bank, stype, &rid, i, nullptr, &size);
                if (data)
                {
                    XDisposePtr(data);
                }
                if (referenced.count(static_cast<uint32_t>(rid)) == 0)
                {
                    ++unused;
                }
            }
        }
        return unused;
    }

    bool BankFileHasSongPayloads(XFILE bank) const
    {
        if (!bank)
        {
            return false;
        }
        XFileSetForceZsbFeatures(bank, TRUE);
        if (XCountFileResourcesOfType(bank, ID_SONG) > 0)
        {
            return true;
        }
        if (XCountFileResourcesOfType(bank, ID_ZSNG) > 0)
        {
            return true;
        }
        if (XCountFileResourcesOfType(bank, ID_MIDI) > 0 ||
            XCountFileResourcesOfType(bank, ID_MIDI_OLD) > 0 ||
            XCountFileResourcesOfType(bank, ID_EMID) > 0 ||
            XCountFileResourcesOfType(bank, ID_ECMI) > 0 ||
            XCountFileResourcesOfType(bank, ID_CMID) > 0)
        {
            return true;
        }
        /* DATe alone is metadata — do not force the slow omit/rebuild path. */
        return false;
    }

    /* Hard-omit SNDs not referenced by any INST (export clone — no Trash). */
    int DropUnreferencedSamplesFromBankFile(XFILE bank, BAEBankToken token)
    {
        if (!bank || !token)
        {
            return 0;
        }
        std::set<uint32_t> referenced;
        CollectBankReferencedSndIdsAll(token, referenced);

        std::vector<XResourceType> omit_types;
        std::vector<XLongResourceID> omit_ids;
        XFILENAME *ref = reinterpret_cast<XFILENAME *>(bank);
        if (ref && ref->pCache && ref->pCache->totalResources > 0)
        {
            for (int32_t i = 0; i < ref->pCache->totalResources; ++i)
            {
                const XFILE_CACHED_ITEM &item = ref->pCache->cached[i];
                if (item.resourceType != ID_SND && item.resourceType != ID_CSND &&
                    item.resourceType != ID_ESND)
                {
                    continue;
                }
                if (referenced.count(static_cast<uint32_t>(item.resourceID)) == 0)
                {
                    omit_types.push_back(static_cast<XResourceType>(item.resourceType));
                    omit_ids.push_back(item.resourceID);
                }
            }
        }
        else
        {
            const XResourceType snd_types[] = {ID_ESND, ID_CSND, ID_SND};
            for (XResourceType stype : snd_types)
            {
                const int32_t count = XCountFileResourcesOfType(bank, stype);
                for (int32_t i = 0; i < count; ++i)
                {
                    XLongResourceID rid = 0;
                    int32_t size = 0;
                    XPTR data = XGetIndexedFileResource(bank, stype, &rid, i, nullptr, &size);
                    if (data)
                    {
                        XDisposePtr(data);
                    }
                    if (referenced.count(static_cast<uint32_t>(rid)) == 0)
                    {
                        omit_types.push_back(stype);
                        omit_ids.push_back(rid);
                    }
                }
            }
        }
        if (omit_types.empty())
        {
            return 0;
        }
        if (XOmitFileResources(bank,
                               omit_types.data(),
                               omit_ids.data(),
                               static_cast<int32_t>(omit_types.size())) == FALSE)
        {
            return 0;
        }
        return static_cast<int>(omit_types.size());
    }

    /* Export: drop session PCM masters (not needed for playback banks/songs). */
    bool StripCasdFromBankFile(XFILE bank, const char **out_error = nullptr)
    {
        if (out_error)
        {
            *out_error = nullptr;
        }
        if (!bank)
        {
            return true;
        }
        const XResourceType casd_type = FOUR_CHAR('C', 'a', 'S', 'd');
        XFileSetForceZsbFeatures(bank, TRUE);
        const int32_t count = XCountFileResourcesOfType(bank, casd_type);
        if (count <= 0)
        {
            return true;
        }
        std::vector<XResourceType> omit_types;
        std::vector<XLongResourceID> omit_ids;
        omit_types.reserve(static_cast<size_t>(count));
        omit_ids.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i)
        {
            XLongResourceID rid = 0;
            int32_t size = 0;
            XPTR data = XGetIndexedFileResource(bank, casd_type, &rid, i, nullptr, &size);
            if (data)
            {
                XDisposePtr(data);
            }
            omit_types.push_back(casd_type);
            omit_ids.push_back(rid);
        }
        if (XOmitFileResources(bank,
                               omit_types.data(),
                               omit_ids.data(),
                               static_cast<int32_t>(omit_types.size())) == FALSE)
        {
            if (out_error)
            {
                *out_error = "could not strip CaSd from bank";
            }
            return false;
        }
        return true;
    }

    /* Export strip-all: one omit rebuild (flat SONG/Midi + packed ZSNG). */
    bool StripAllSongsFromBankFile(XFILE bank,
                                   const char **out_error = nullptr,
                                   bool also_strip_casd = false)
    {
        if (out_error)
        {
            *out_error = nullptr;
        }
        if (!bank)
        {
            return true;
        }
        XFileSetForceZsbFeatures(bank, TRUE);
        std::vector<XResourceType> omit_types;
        std::vector<XLongResourceID> omit_ids;
        auto collect_type = [&](XResourceType type) {
            const int32_t count = XCountFileResourcesOfType(bank, type);
            for (int32_t i = 0; i < count; ++i)
            {
                XLongResourceID rid = 0;
                int32_t size = 0;
                XPTR data = XGetIndexedFileResource(bank, type, &rid, i, nullptr, &size);
                if (data)
                {
                    XDisposePtr(data);
                }
                omit_types.push_back(type);
                omit_ids.push_back(rid);
            }
        };
        collect_type(ID_SONG);
        collect_type(ID_ZSNG);
        collect_type(ID_MIDI);
        collect_type(ID_MIDI_OLD);
        collect_type(ID_EMID);
        collect_type(ID_ECMI);
        collect_type(ID_CMID);
        collect_type(FOUR_CHAR('D', 'A', 'T', 'e'));
        if (also_strip_casd)
        {
            collect_type(FOUR_CHAR('C', 'a', 'S', 'd'));
        }
        CollectSessionDocumentOmitIds(bank, omit_types, omit_ids);
        if (omit_types.empty())
        {
            return true;
        }
        if (XOmitFileResources(bank,
                               omit_types.data(),
                               omit_ids.data(),
                               static_cast<int32_t>(omit_types.size())) == FALSE)
        {
            if (out_error)
            {
                *out_error = "could not strip songs from bank";
            }
            return false;
        }
        return true;
    }

    bool WriteBankFileImageToPath(XFILE bank, const std::string &out_path)
    {
        if (!bank || out_path.empty())
        {
            return false;
        }
        XFILENAME *ref = reinterpret_cast<XFILENAME *>(bank);
        if (!ref->pResourceData || ref->resMemLength <= 0)
        {
            return false;
        }
        XFILENAME xf_out;
        XConvertPathToXFILENAME(const_cast<char *>(out_path.c_str()), &xf_out);
        XFILE disk = XFileOpenForWrite(&xf_out, TRUE);
        if (!disk ||
            XFileSetLength(disk, 0) != 0 ||
            XFileSetPosition(disk, 0L) != 0 ||
            XFileWrite(disk, ref->pResourceData, ref->resMemLength) != 0)
        {
            if (disk)
            {
                XFileClose(disk);
            }
            return false;
        }
        XFileClose(disk);
        return true;
    }

    /* Remove open-document sample rows by document index (high→low). */
    int DeleteDocumentSamplesByIndex(std::vector<uint32_t> sample_indices)
    {
        if (!m_document || sample_indices.empty())
        {
            return 0;
        }
        std::sort(sample_indices.begin(),
                  sample_indices.end(),
                  [](uint32_t a, uint32_t b) { return a > b; });
        sample_indices.erase(std::unique(sample_indices.begin(), sample_indices.end()),
                             sample_indices.end());
        int removed = 0;
        for (uint32_t sample_index : sample_indices)
        {
            if (BAERmfEditorDocument_DeleteSample(m_document, sample_index) == BAE_NO_ERROR)
            {
                ++removed;
            }
        }
        if (removed > 0)
        {
            m_document_dirty = true;
        }
        return removed;
    }

    /* Drop song-doc sample rows that point at these bank SND / asset ids.
     * Needed after trashing a bank SND — otherwise RefreshSongSamplesFromDocument
     * resurrects the row as [Song] and refuses further deletes. */
    int DeleteDocumentSamplesWithAssetIDs(const std::set<uint32_t> &asset_ids)
    {
        if (!m_document || asset_ids.empty())
        {
            return 0;
        }
        std::vector<uint32_t> to_remove;
        uint32_t sample_count = 0;
        if (BAERmfEditorDocument_GetSampleCount(m_document, &sample_count) != BAE_NO_ERROR)
        {
            return 0;
        }
        for (uint32_t si = 0; si < sample_count; ++si)
        {
            uint32_t asset_id = BAE_EDITOR_SAMPLE_ASSET_ID_NONE;
            if (BAERmfEditorDocument_GetSampleAssetIDForSample(m_document, si, &asset_id) !=
                    BAE_NO_ERROR ||
                asset_id == BAE_EDITOR_SAMPLE_ASSET_ID_NONE)
            {
                continue;
            }
            if (asset_ids.count(asset_id) != 0)
            {
                to_remove.push_back(si);
            }
        }
        return DeleteDocumentSamplesByIndex(std::move(to_remove));
    }

    /* Remove every document sample tied to an instrument (e.g. oscillator on). */
    int DeleteDocumentSamplesForInstrument(uint32_t inst_id)
    {
        if (!m_document || inst_id == BAE_EDITOR_INST_ID_NONE)
        {
            return 0;
        }
        std::vector<uint32_t> to_remove;
        uint32_t sample_count = 0;
        if (BAERmfEditorDocument_GetSampleCount(m_document, &sample_count) != BAE_NO_ERROR)
        {
            return 0;
        }
        for (uint32_t si = 0; si < sample_count; ++si)
        {
            uint32_t sample_inst = BAE_EDITOR_INST_ID_NONE;
            if (BAERmfEditorDocument_GetInstIDForSample(m_document, si, &sample_inst) !=
                    BAE_NO_ERROR ||
                sample_inst != inst_id)
            {
                continue;
            }
            to_remove.push_back(si);
        }
        return DeleteDocumentSamplesByIndex(std::move(to_remove));
    }

    /* Song export: remove sample rows whose assets are no longer used.
     * GetSampleAssetUsageCount returns PARAM_ERR when usage is 0. */
    int DropUnreferencedSamplesFromDocument(BAERmfEditorDocument *doc)
    {
        if (!doc)
        {
            return 0;
        }
        int dropped = 0;
        /* Repeat until stable — deleting a sample shifts later indices. */
        for (;;)
        {
            uint32_t sample_count = 0;
            if (BAERmfEditorDocument_GetSampleCount(doc, &sample_count) != BAE_NO_ERROR ||
                sample_count == 0)
            {
                break;
            }
            int remove_index = -1;
            for (uint32_t si = 0; si < sample_count; ++si)
            {
                uint32_t asset_id = BAE_EDITOR_SAMPLE_ASSET_ID_NONE;
                if (BAERmfEditorDocument_GetSampleAssetIDForSample(doc, si, &asset_id) !=
                        BAE_NO_ERROR ||
                    asset_id == BAE_EDITOR_SAMPLE_ASSET_ID_NONE)
                {
                    continue;
                }
                uint32_t usage = 0;
                const BAEResult ur =
                    BAERmfEditorDocument_GetSampleAssetUsageCount(doc, asset_id, &usage);
                if (ur == BAE_NO_ERROR && usage > 0)
                {
                    continue;
                }
                remove_index = static_cast<int>(si);
                break;
            }
            if (remove_index < 0)
            {
                break;
            }
            if (BAERmfEditorDocument_DeleteSample(doc, static_cast<uint32_t>(remove_index)) !=
                BAE_NO_ERROR)
            {
                break;
            }
            ++dropped;
        }
        return dropped;
    }

    /* Remove songs from a bank file.
     * keep_session_songs: bank-merge path — drop groovoids, keep DATe Session songs.
     * keep_session_songs=false: bank export — drop Session Midi/SONG too (they
     * belong in .zsn; use "Convert Session songs to groovoids" to re-embed). */
    bool StripBankGroovoids(XFILE bank,
                            const char **out_error = nullptr,
                            bool keep_session_songs = true)
    {
        if (out_error)
        {
            *out_error = nullptr;
        }
        if (!bank)
        {
            return true;
        }

        /* Expose ZSNG/ZBNK logical SONG/Midi for enumeration on ZREZ. */
        XFileSetForceZsbFeatures(bank, TRUE);

        std::set<uint32_t> dated_song_ids;
        std::set<uint32_t> dated_midi_ids;
        {
            const XResourceType date_type = FOUR_CHAR('D', 'A', 'T', 'e');
            const int32_t date_count = XCountFileResourcesOfType(bank, date_type);
            for (int32_t di = 0; di < date_count; ++di)
            {
                XLongResourceID date_id = 0;
                int32_t date_size = 0;
                XPTR date_data =
                    XGetIndexedFileResource(bank, date_type, &date_id, di, nullptr, &date_size);
                if (!date_data || date_size <= 0)
                {
                    if (date_data)
                    {
                        XDisposePtr(date_data);
                    }
                    continue;
                }
                CollectDateStampedSongKeys(static_cast<const unsigned char *>(date_data),
                                           static_cast<size_t>(date_size),
                                           dated_song_ids,
                                           dated_midi_ids);
                XDisposePtr(date_data);
            }
        }
        const bool use_date_filter = !dated_song_ids.empty() || !dated_midi_ids.empty();

        /* Prefer Midi over emid — orphan built-in emids must not reclassify a
         * custom Midi body (same object id) as a groovoid. */
        auto resolve_song_object_type = [&](uint16_t object_id) -> XResourceType {
            const XLongResourceID oid = static_cast<XLongResourceID>(object_id);
            if (XExistsFileResource(bank, ID_MIDI, oid) != FALSE)
            {
                return ID_MIDI;
            }
            if (XExistsFileResource(bank, ID_MIDI_OLD, oid) != FALSE)
            {
                return ID_MIDI_OLD;
            }
            if (XExistsFileResource(bank, ID_EMID, oid) != FALSE)
            {
                return ID_EMID;
            }
            if (XExistsFileResource(bank, ID_ECMI, oid) != FALSE)
            {
                return ID_ECMI;
            }
            if (XExistsFileResource(bank, ID_CMID, oid) != FALSE)
            {
                return ID_CMID;
            }
            return 0;
        };

        /* Collect first — XPurgeFileResource expands ZSNG/ZBNK and must not
         * run while we are still indexing. */
        struct GroovoidPurge
        {
            XLongResourceID song_id;
            XResourceType object_type;
            XLongResourceID object_id;
        };
        std::vector<GroovoidPurge> to_purge;
        const int32_t song_count = XCountFileResourcesOfType(bank, ID_SONG);
        for (int32_t i = 0; i < song_count; ++i)
        {
            XLongResourceID song_id = 0;
            int32_t song_size = 0;
            char name_buf[256];
            name_buf[0] = 0;
            XPTR song_data =
                XGetIndexedFileResource(bank, ID_SONG, &song_id, i, name_buf, &song_size);
            if (!song_data || song_size < 8)
            {
                if (song_data)
                {
                    XDisposePtr(song_data);
                }
                continue;
            }
            const unsigned char *body = static_cast<const unsigned char *>(song_data);
            const uint16_t object_id =
                static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
            const unsigned char song_type = body[6];
            XDisposePtr(song_data);
            if (!BsnSongTypeHasMidiObject(song_type))
            {
                continue;
            }

            const bool is_session_song =
                use_date_filter &&
                (dated_song_ids.count(static_cast<uint32_t>(song_id)) != 0 ||
                 dated_midi_ids.count(object_id) != 0);

            if (keep_session_songs && is_session_song)
            {
                continue;
            }

            const XResourceType object_type = resolve_song_object_type(object_id);
            if (object_type == 0)
            {
                /* Still drop the SONG shell if we are stripping everything. */
                if (!keep_session_songs)
                {
                    to_purge.push_back(GroovoidPurge{song_id, 0, 0});
                }
                continue;
            }

            if (keep_session_songs)
            {
                if (object_type == ID_MIDI || object_type == ID_MIDI_OLD)
                {
                    if (!use_date_filter)
                    {
                        /* No DATe → treat plain Midi as Custom Songs. */
                        continue;
                    }
                    if (dated_midi_ids.count(object_id) != 0)
                    {
                        continue;
                    }
                }
                else if (object_type == ID_EMID || object_type == ID_ECMI ||
                         object_type == ID_CMID)
                {
                    if (use_date_filter && dated_midi_ids.count(object_id) != 0)
                    {
                        continue;
                    }
                }
            }

            to_purge.push_back(GroovoidPurge{song_id,
                                             object_type,
                                             static_cast<XLongResourceID>(object_id)});
        }

        for (const GroovoidPurge &g : to_purge)
        {
            /* Purge SONG first so ZSNG is reconstituted without it. */
            if (XPurgeFileResource(bank, ID_SONG, g.song_id) == FALSE)
            {
                if (out_error)
                {
                    *out_error = "could not remove groovoid SONG";
                }
                return false;
            }
            if (g.object_type != 0)
            {
                (void)XPurgeFileResource(bank, g.object_type, g.object_id);
            }
        }

        /* Orphan payloads: SONG removed but emid/ecmi/cmid (or Midi) left
         * behind. Never touch objects still referenced by a remaining SONG.
         * When keep_session_songs, also protect DATe-stamped Midi ids. */
        {
            std::set<uint32_t> referenced_object_ids;
            const int32_t keep_songs = XCountFileResourcesOfType(bank, ID_SONG);
            for (int32_t i = 0; i < keep_songs; ++i)
            {
                XLongResourceID song_id = 0;
                int32_t song_size = 0;
                XPTR song_data =
                    XGetIndexedFileResource(bank, ID_SONG, &song_id, i, nullptr, &song_size);
                if (!song_data || song_size < 2)
                {
                    if (song_data)
                    {
                        XDisposePtr(song_data);
                    }
                    continue;
                }
                const unsigned char *body = static_cast<const unsigned char *>(song_data);
                const uint16_t object_id =
                    static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
                XDisposePtr(song_data);
                referenced_object_ids.insert(object_id);
            }

            auto purge_unreferenced = [&](XResourceType type) {
                std::vector<XLongResourceID> ids;
                const int32_t count = XCountFileResourcesOfType(bank, type);
                ids.reserve(static_cast<size_t>(count > 0 ? count : 0));
                for (int32_t i = 0; i < count; ++i)
                {
                    XLongResourceID rid = 0;
                    int32_t rsize = 0;
                    XPTR data = XGetIndexedFileResource(bank, type, &rid, i, nullptr, &rsize);
                    if (data)
                    {
                        XDisposePtr(data);
                    }
                    if (referenced_object_ids.count(static_cast<uint32_t>(rid)) != 0)
                    {
                        continue;
                    }
                    if (keep_session_songs &&
                        dated_midi_ids.count(static_cast<uint32_t>(rid)) != 0)
                    {
                        continue;
                    }
                    ids.push_back(rid);
                }
                for (XLongResourceID rid : ids)
                {
                    (void)XPurgeFileResource(bank, type, rid);
                }
            };

            purge_unreferenced(ID_EMID);
            purge_unreferenced(ID_ECMI);
            purge_unreferenced(ID_CMID);
            purge_unreferenced(ID_MIDI);
            purge_unreferenced(ID_MIDI_OLD);
        }

        /* Session DATe stamps are meaningless without the songs they mark. */
        if (!keep_session_songs)
        {
            const XResourceType date_type = FOUR_CHAR('D', 'A', 'T', 'e');
            std::vector<XLongResourceID> date_ids;
            const int32_t date_count = XCountFileResourcesOfType(bank, date_type);
            for (int32_t di = 0; di < date_count; ++di)
            {
                XLongResourceID date_id = 0;
                int32_t date_size = 0;
                XPTR date_data =
                    XGetIndexedFileResource(bank, date_type, &date_id, di, nullptr, &date_size);
                if (date_data)
                {
                    XDisposePtr(date_data);
                }
                date_ids.push_back(date_id);
            }
            for (XLongResourceID date_id : date_ids)
            {
                (void)XPurgeFileResource(bank, date_type, date_id);
            }
        }
        return true;
    }

    /* True when the open bank still carries session-document markers. */
    bool BankFileHasSessionDocument(XFILE bank) const
    {
        if (!bank)
        {
            return false;
        }
        if (XCountFileResourcesOfType(bank, FOUR_CHAR('n', 'B', 'e', 'T')) > 0)
        {
            return true;
        }
        const XResourceType bepf = FOUR_CHAR('B', 'e', 'P', 'f');
        const int32_t count = XCountFileResourcesOfType(bank, bepf);
        for (int32_t i = 0; i < count; ++i)
        {
            XLongResourceID rid = 0;
            int32_t size = 0;
            char name[256];
            name[0] = 0;
            XPTR data = XGetIndexedFileResource(bank, bepf, &rid, i, name, &size);
            if (data)
            {
                XDisposePtr(data);
            }
            const unsigned char plen = static_cast<unsigned char>(name[0]);
            if (plen == 13 && std::memcmp(name + 1, "Session Prefs", 13) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void CollectSessionDocumentOmitIds(XFILE bank,
                                       std::vector<XResourceType> &omit_types,
                                       std::vector<XLongResourceID> &omit_ids) const
    {
        auto collect_type = [&](XResourceType type) {
            const int32_t count = XCountFileResourcesOfType(bank, type);
            for (int32_t i = 0; i < count; ++i)
            {
                XLongResourceID rid = 0;
                int32_t size = 0;
                XPTR data = XGetIndexedFileResource(bank, type, &rid, i, nullptr, &size);
                if (data)
                {
                    XDisposePtr(data);
                }
                omit_types.push_back(type);
                omit_ids.push_back(rid);
            }
        };
        collect_type(FOUR_CHAR('n', 'B', 'e', 'T'));
        collect_type(FOUR_CHAR('n', 'E', 'n', 'c'));
        collect_type(FOUR_CHAR('B', 'e', 'P', 'f'));
        collect_type(FOUR_CHAR('B', 'E', 'P', 'F'));
    }

    /* Bank export with Include groovoids on: drop Session songs only.
     * One XOmitFileResources — never loop XPurgeFileResource (each purge
     * rebuilt the whole bank and could silently drop kept groovoids).
     *
     * Identify session songs from m_session_songs (same as session save),
     * with DATe stamps as a supplement. Never fall back to "all Midi are
     * session" — that wiped converted SONG→Midi groovoids when DATe was
     * missing on the export clone. */
    bool StripBankSessionSongs(XFILE bank,
                               const char **out_error = nullptr,
                               bool also_strip_casd = false)
    {
        if (out_error)
        {
            *out_error = nullptr;
        }
        if (!bank)
        {
            return true;
        }
        XFileSetForceZsbFeatures(bank, TRUE);

        std::set<uint32_t> session_song_ids;
        std::set<uint32_t> session_midi_ids;
        for (const SessionSongEntry &entry : m_session_songs)
        {
            /* SONG id 0 is valid (e.g. M_IN_CIT). */
            session_song_ids.insert(entry.song_resource_id);
            session_midi_ids.insert(entry.midi_resource_id);
        }
        {
            const XResourceType date_type = FOUR_CHAR('D', 'A', 'T', 'e');
            const int32_t date_count = XCountFileResourcesOfType(bank, date_type);
            for (int32_t di = 0; di < date_count; ++di)
            {
                XLongResourceID date_id = 0;
                int32_t date_size = 0;
                XPTR date_data =
                    XGetIndexedFileResource(bank, date_type, &date_id, di, nullptr, &date_size);
                if (!date_data || date_size <= 0)
                {
                    if (date_data)
                    {
                        XDisposePtr(date_data);
                    }
                    continue;
                }
                CollectDateStampedSongKeys(static_cast<const unsigned char *>(date_data),
                                           static_cast<size_t>(date_size),
                                           session_song_ids,
                                           session_midi_ids);
                XDisposePtr(date_data);
            }
        }

        std::vector<XResourceType> omit_types;
        std::vector<XLongResourceID> omit_ids;
        auto omit_one = [&](XResourceType t, XLongResourceID id) {
            omit_types.push_back(t);
            omit_ids.push_back(id);
        };

        const int32_t song_count = XCountFileResourcesOfType(bank, ID_SONG);
        for (int32_t i = 0; i < song_count; ++i)
        {
            XLongResourceID song_id = 0;
            int32_t song_size = 0;
            XPTR song_data =
                XGetIndexedFileResource(bank, ID_SONG, &song_id, i, nullptr, &song_size);
            if (!song_data || song_size < 8)
            {
                if (song_data)
                {
                    XDisposePtr(song_data);
                }
                continue;
            }
            const unsigned char *body = static_cast<const unsigned char *>(song_data);
            const uint16_t object_id =
                static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
            const unsigned char song_type = body[6];
            XDisposePtr(song_data);
            if (!BsnSongTypeHasMidiObject(song_type))
            {
                continue;
            }

            const bool is_session =
                session_song_ids.count(static_cast<uint32_t>(song_id)) != 0 ||
                session_midi_ids.count(object_id) != 0;
            if (!is_session)
            {
                continue;
            }

            XResourceType object_type = 0;
            const XLongResourceID oid = static_cast<XLongResourceID>(object_id);
            if (XExistsFileResource(bank, ID_MIDI, oid) != FALSE)
            {
                object_type = ID_MIDI;
            }
            else if (XExistsFileResource(bank, ID_MIDI_OLD, oid) != FALSE)
            {
                object_type = ID_MIDI_OLD;
            }
            else if (XExistsFileResource(bank, ID_EMID, oid) != FALSE)
            {
                object_type = ID_EMID;
            }
            else if (XExistsFileResource(bank, ID_ECMI, oid) != FALSE)
            {
                object_type = ID_ECMI;
            }
            else if (XExistsFileResource(bank, ID_CMID, oid) != FALSE)
            {
                object_type = ID_CMID;
            }

            omit_one(ID_SONG, song_id);
            if (object_type != 0)
            {
                omit_one(object_type, oid);
            }
        }

        /* Session DATe stamps are meaningless without the songs they mark. */
        {
            const XResourceType date_type = FOUR_CHAR('D', 'A', 'T', 'e');
            const int32_t date_count = XCountFileResourcesOfType(bank, date_type);
            for (int32_t di = 0; di < date_count; ++di)
            {
                XLongResourceID date_id = 0;
                int32_t date_size = 0;
                XPTR date_data =
                    XGetIndexedFileResource(bank, date_type, &date_id, di, nullptr, &date_size);
                if (date_data)
                {
                    XDisposePtr(date_data);
                }
                omit_one(date_type, date_id);
            }
        }

        if (also_strip_casd)
        {
            const XResourceType casd_type = FOUR_CHAR('C', 'a', 'S', 'd');
            const int32_t casd_count = XCountFileResourcesOfType(bank, casd_type);
            for (int32_t i = 0; i < casd_count; ++i)
            {
                XLongResourceID rid = 0;
                int32_t size = 0;
                XPTR data = XGetIndexedFileResource(bank, casd_type, &rid, i, nullptr, &size);
                if (data)
                {
                    XDisposePtr(data);
                }
                omit_one(casd_type, rid);
            }
        }

        /* Drop session markers so the export is a playback bank; otherwise
         * RefreshGroovoids would still treat undated Midi as Custom Songs. */
        CollectSessionDocumentOmitIds(bank, omit_types, omit_ids);

        if (omit_types.empty())
        {
            return true;
        }
        if (XOmitFileResources(bank,
                               omit_types.data(),
                               omit_ids.data(),
                               static_cast<int32_t>(omit_types.size())) == FALSE)
        {
            if (out_error)
            {
                *out_error = "could not strip Session songs from bank";
            }
            return false;
        }
        return true;
    }

    void RefreshGroovoidsFromBank()
    {
        m_groovoids.clear();
        m_groovoid_index = -1;
        if (!m_bank_token)
        {
            return;
        }

        XFILE bank = reinterpret_cast<XFILE>(m_bank_token);

        /* Custom songs are DATe-stamped Midi/SONG; exclude those from Groovoids. */
        std::set<uint32_t> dated_song_ids;
        std::set<uint32_t> dated_midi_ids;
        {
            const XResourceType date_type = FOUR_CHAR('D', 'A', 'T', 'e');
            const int32_t date_count = XCountFileResourcesOfType(bank, date_type);
            for (int32_t di = 0; di < date_count; ++di)
            {
                XLongResourceID date_id = 0;
                int32_t date_size = 0;
                XPTR date_data =
                    XGetIndexedFileResource(bank, date_type, &date_id, di, nullptr, &date_size);
                if (!date_data || date_size <= 0)
                {
                    if (date_data)
                    {
                        XDisposePtr(date_data);
                    }
                    continue;
                }
                CollectDateStampedSongKeys(static_cast<const unsigned char *>(date_data),
                                           static_cast<size_t>(date_size),
                                           dated_song_ids,
                                           dated_midi_ids);
                XDisposePtr(date_data);
            }
        }
        const bool use_date_filter = !dated_song_ids.empty() || !dated_midi_ids.empty();

        const int32_t song_count = XCountFileResourcesOfType(bank, ID_SONG);
        for (int32_t i = 0; i < song_count; ++i)
        {
            XLongResourceID song_id = 0;
            int32_t song_size = 0;
            char name_buf[256];
            name_buf[0] = 0;
            XPTR song_data =
                XGetIndexedFileResource(bank, ID_SONG, &song_id, i, name_buf, &song_size);
            if (!song_data || song_size < 8)
            {
                if (song_data)
                {
                    XDisposePtr(song_data);
                }
                continue;
            }

            const unsigned char *body = static_cast<const unsigned char *>(song_data);
            const uint16_t object_id =
                static_cast<uint16_t>((static_cast<uint16_t>(body[0]) << 8) | body[1]);
            const unsigned char song_type = body[6];
            XDisposePtr(song_data);

            if (!BsnSongTypeHasMidiObject(song_type))
            {
                continue;
            }

            if (use_date_filter &&
                (dated_song_ids.count(static_cast<uint32_t>(song_id)) != 0 ||
                 dated_midi_ids.count(object_id) != 0))
            {
                /* DATe-stamped = Custom Session song, never a Groovoid. */
                continue;
            }

            /* Prefer Midi when both exist — orphan emid must not steal a custom
             * Midi object id. Classic groovoids: SONG → emid only.
             * Converted bank groovoids: SONG → Midi without DATe stamp. */
            const bool has_midi =
                XExistsFileResource(bank, ID_MIDI, static_cast<XLongResourceID>(object_id)) != FALSE ||
                XExistsFileResource(bank, ID_MIDI_OLD, static_cast<XLongResourceID>(object_id)) != FALSE;
            const bool has_emid =
                XExistsFileResource(bank, ID_EMID, static_cast<XLongResourceID>(object_id)) != FALSE ||
                XExistsFileResource(bank, ID_ECMI, static_cast<XLongResourceID>(object_id)) != FALSE ||
                XExistsFileResource(bank, ID_CMID, static_cast<XLongResourceID>(object_id)) != FALSE;
            if (!has_emid && !has_midi)
            {
                continue;
            }

            bool object_is_midi = false;
            if (has_midi)
            {
                if (BankFileHasSessionDocument(bank))
                {
                    /* Session banks: plain Midi is a Custom Song whether or not
                     * some other Midi/SONG already has a DATe stamp. Playback
                     * banks (no BePf/nBeT) keep undated SONG→Midi as groovoids. */
                    continue;
                }
                object_is_midi = true;
            }
            else
            {
                object_is_midi = false;
            }

            /* Align with export: never list an in-memory Session song as a Groovoid. */
            bool is_session_song = false;
            for (const SessionSongEntry &ss : m_session_songs)
            {
                if ((ss.song_resource_id != 0 &&
                     ss.song_resource_id == static_cast<uint32_t>(song_id)) ||
                    (ss.midi_resource_id != 0 && ss.midi_resource_id == object_id))
                {
                    is_session_song = true;
                    break;
                }
            }
            if (is_session_song)
            {
                continue;
            }

            GroovoidEntry entry;
            entry.song_resource_id = static_cast<uint32_t>(song_id);
            entry.emid_resource_id = object_id;
            entry.object_is_midi = object_is_midi;
            const unsigned char plen = static_cast<unsigned char>(name_buf[0]);
            if (plen > 0 && plen < 255)
            {
                entry.name.assign(name_buf + 1, name_buf + 1 + plen);
            }
            if (entry.name.empty())
            {
                char fallback[64];
                std::snprintf(fallback, sizeof(fallback), "Groovoid %u", entry.song_resource_id);
                entry.name = fallback;
            }
            m_groovoids.push_back(std::move(entry));
        }

        std::sort(m_groovoids.begin(),
                  m_groovoids.end(),
                  [](const GroovoidEntry &a, const GroovoidEntry &b) {
                      return a.name < b.name;
                  });
    }

    void RefreshListsFromBank()
    {
        m_samples.clear();
        m_instruments.clear();
        m_selected_sample = -1;
        m_selected_instrument = -1;
        /* Indices into m_samples / m_instruments are invalid after rebuild+sort. */
        m_multi_samples.clear();
        m_multi_instruments.clear();
        RebuildInstrumentFilters();
        RefreshGroovoidsFromBank();

        if (!m_bank_token)
        {
            return;
        }

        uint32_t instrument_count = 0;
        if (BAERmfEditorBank_GetInstrumentCount(m_bank_token, &instrument_count) != BAE_NO_ERROR)
        {
            return;
        }

        uint32_t sample_row_index = 0;
        for (uint32_t inst_idx = 0; inst_idx < instrument_count; ++inst_idx)
        {
            BAERmfEditorBankInstrumentInfo inst_info;
            std::memset(&inst_info, 0, sizeof(inst_info));
            if (BAERmfEditorBank_GetInstrumentInfo(m_bank_token, inst_idx, &inst_info) != BAE_NO_ERROR)
            {
                continue;
            }

            /* Skip duplicate INST ids (stale in-memory banks from failed deletes). */
            {
                const bool already = std::any_of(
                    m_instruments.begin(),
                    m_instruments.end(),
                    [&](const InstrumentRow &row) {
                        return !row.is_alias && row.inst_id == inst_info.instID;
                    });
                if (already)
                {
                    continue;
                }
            }

            InstrumentRow inst_row;
            inst_row.is_custom =
                (inst_info.bank >= 2) ||
                (m_session_custom_inst_ids.count(inst_info.instID) != 0);
            inst_row.from_song_document = false;
            inst_row.is_alias = false;
            inst_row.instrument_index = inst_idx;
            inst_row.inst_id = inst_info.instID;
            inst_row.target_inst_id = inst_info.instID;
            inst_row.program = static_cast<int>(inst_info.program);
            inst_row.split_count = (inst_info.keySplitCount <= 0) ? 1 : static_cast<int>(inst_info.keySplitCount);
            inst_row.bank = static_cast<int>(inst_info.bank);
            inst_row.percussion = ((inst_info.instID & 0x80u) != 0u);
            if (inst_info.name[0])
            {
                inst_row.name = inst_info.name;
            }
            else
            {
                std::string from_doc;
                if (m_document &&
                    LookupDocumentSampleDisplayNameForInst(inst_info.instID, &from_doc) &&
                    !from_doc.empty())
                {
                    inst_row.name = from_doc;
                }
                else
                {
                    char fallback[64];
                    std::snprintf(fallback, sizeof(fallback), "Inst %u", inst_info.instID);
                    inst_row.name = fallback;
                }
            }
            m_instruments.push_back(inst_row);

            uint32_t split_count = 0;
            if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token, inst_idx, &split_count) != BAE_NO_ERROR)
            {
                continue;
            }

            for (uint32_t split_idx = 0; split_idx < split_count; ++split_idx)
            {
                BAERmfEditorBankSampleInfo sample_info;
                std::memset(&sample_info, 0, sizeof(sample_info));
                if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                             inst_idx,
                                                             split_idx,
                                                             &sample_info) != BAE_NO_ERROR)
                {
                    continue;
                }

                const uint32_t snd_id = static_cast<uint32_t>(
                    static_cast<uint16_t>(sample_info.sndResourceID));
                /* Empty INST uses 0xFFFF (no sample); do not invent a Samples-tab row. */
                if (snd_id == kNoSndResourceId)
                {
                    continue;
                }
                /* Ghost keysplit (SND deleted, INST still points at it): stay off Samples
                 * list; keymap shows SND N [missing]. */
                if (!BankSndResourceExists(snd_id))
                {
                    continue;
                }
                /* Bank 2+, session-added SNDs, or samples belonging to session-custom INST. */
                const bool row_custom =
                    (inst_info.bank >= 2) ||
                    (m_session_custom_snd_ids.count(snd_id) != 0) ||
                    (m_session_custom_inst_ids.count(inst_info.instID) != 0);

                /* One list row per SND (shared splits/instruments collapse). */
                auto existing = std::find_if(m_samples.begin(),
                                             m_samples.end(),
                                             [snd_id](const SampleRow &s) {
                                                 return s.source == SampleSource::Bank &&
                                                        s.snd_resource_id == snd_id;
                                             });
                if (existing != m_samples.end())
                {
                    existing->is_custom = existing->is_custom || row_custom;
                    continue;
                }

                SampleRow sample_row;
                sample_row.index = sample_row_index++;
                sample_row.instrument_index = inst_idx;
                sample_row.split_index = split_idx;
                sample_row.snd_resource_id = snd_id;
                sample_row.sample_rate_hz = sample_info.sampleRate;
                sample_row.frame_count = sample_info.frameCount;
                sample_row.loop_start = sample_info.loopStart;
                sample_row.loop_end = sample_info.loopEnd;
                sample_row.bit_depth = static_cast<int>(sample_info.bitDepth);
                sample_row.channels = static_cast<int>(sample_info.channels);
                sample_row.is_custom = row_custom;
                sample_row.source = SampleSource::Bank;
                sample_row.bank = static_cast<int>(inst_info.bank);
                sample_row.program = static_cast<int>(inst_info.program);
                sample_row.root_key = static_cast<int>(sample_info.rootKey);
                sample_row.low_key = static_cast<int>(sample_info.lowKey);
                sample_row.high_key = static_cast<int>(sample_info.highKey);
                sample_row.codec_label =
                    FormatVisibleCodecLabel(static_cast<uint32_t>(sample_info.compressionType),
                                            sample_info.compressionSubType,
                                            sample_info.opusRoundTripResample);
                /* Bank stays PCM after deferred encode — show pending export codec. */
                {
                    auto pref = m_session_pcm_cache.find(snd_id);
                    if (pref != m_session_pcm_cache.end() && pref->second.has_export_target &&
                        pref->second.export_compression != BAE_EDITOR_COMPRESSION_PCM &&
                        pref->second.export_compression != BAE_EDITOR_COMPRESSION_DONT_CHANGE)
                    {
                        sample_row.codec_label =
                            FormatVisibleCodecLabelFromEditorType(pref->second.export_compression);
                    }
                }

                /* Prefer the SND resource name (BE2-style); fall back to instrument name. */
                char snd_name[256];
                snd_name[0] = 0;
                {
                    XFILE bank_file = reinterpret_cast<XFILE>(m_bank_token);
                    static const XResourceType kSndTypes[] = {ID_ESND, ID_CSND, ID_SND, 0};
                    for (int t = 0; kSndTypes[t] != 0; ++t)
                    {
                        if (XGetFileResourceName(bank_file,
                                                 kSndTypes[t],
                                                 static_cast<XLongResourceID>(snd_id),
                                                 snd_name))
                        {
                            break;
                        }
                        snd_name[0] = 0;
                    }
                }
                if (snd_name[0])
                {
                    sample_row.name = snd_name;
                }
                else
                {
                    std::string from_doc;
                    if (m_document &&
                        LookupDocumentSampleDisplayNameForSnd(snd_id, &from_doc) &&
                        !from_doc.empty())
                    {
                        sample_row.name = from_doc;
                    }
                    else if (!inst_row.name.empty() &&
                             inst_row.name.rfind("Inst ", 0) != 0)
                    {
                        sample_row.name = inst_row.name;
                    }
                    else
                    {
                        char fallback[64];
                        std::snprintf(fallback, sizeof(fallback), "Sample %u", snd_id);
                        sample_row.name = fallback;
                    }
                }
                m_samples.push_back(sample_row);
            }
        }

        /* Alias INST slots (ID_ALIAS) → extra list rows pointing at the concrete instrument. */
        {
            std::vector<InstrumentRow> alias_rows;
            XFILE bank_file = reinterpret_cast<XFILE>(m_bank_token);
            XAliasLinkResource *alias = XGetAliasLinkFromFile(bank_file);
            if (alias)
            {
                const uint32_t alias_count = static_cast<uint32_t>(XGetLong(&alias->numberOfAliases));
                for (uint32_t a = 0; a < alias_count; ++a)
                {
                    const uint32_t from_id = static_cast<uint32_t>(XGetLong(&alias->list[a].aliasFrom));
                    const uint32_t to_id = static_cast<uint32_t>(XGetLong(&alias->list[a].aliasTo));
                    bool found = false;
                    for (const InstrumentRow &e : m_instruments)
                    {
                        if (e.is_alias || e.target_inst_id != to_id)
                        {
                            continue;
                        }
                        InstrumentRow alias_row = e;
                        alias_row.is_alias = true;
                        alias_row.inst_id = from_id;
                        alias_row.target_inst_id = to_id;
                        alias_row.bank = static_cast<int>(from_id / 256u);
                        alias_row.program = static_cast<int>(from_id % 128u);
                        alias_row.percussion = ((from_id & 0x80u) != 0u);
                        if (alias_row.name.find("(Alias)") == std::string::npos)
                        {
                            alias_row.name += " (Alias)";
                        }
                        alias_rows.push_back(alias_row);
                        found = true;
                        break;
                    }
                    if (!found)
                    {
                        InstrumentRow alias_row;
                        alias_row.is_alias = true;
                        alias_row.inst_id = from_id;
                        alias_row.target_inst_id = to_id;
                        alias_row.bank = static_cast<int>(from_id / 256u);
                        alias_row.program = static_cast<int>(from_id % 128u);
                        alias_row.percussion = ((from_id & 0x80u) != 0u);
                        char missing[80];
                        std::snprintf(missing,
                                      sizeof(missing),
                                      "Empty alias (missing INST %u)",
                                      to_id);
                        alias_row.name = missing;
                        alias_rows.push_back(alias_row);
                    }
                }
                XDisposePtr(reinterpret_cast<XPTR>(alias));
            }
            m_instruments.insert(m_instruments.end(), alias_rows.begin(), alias_rows.end());
        }

        /* Orphan SNDs: present in bank but not referenced by any instrument. */
        {
            std::set<uint32_t> referenced_snds;
            for (const SampleRow &row : m_samples)
            {
                /* ID 0 is a valid assigned SND — do not treat it as unreferenced. */
                if (row.source == SampleSource::Bank && row.snd_resource_id != 0xFFFFu)
                {
                    referenced_snds.insert(row.snd_resource_id);
                }
            }

            XFILE bank_file = reinterpret_cast<XFILE>(m_bank_token);
            static const XResourceType kSndTypes[] = {ID_SND, ID_CSND, ID_ESND, 0};
            for (int t = 0; kSndTypes[t] != 0; ++t)
            {
                const int32_t count = XCountFileResourcesOfType(bank_file, kSndTypes[t]);
                for (int32_t i = 0; i < count; ++i)
                {
                    XLongResourceID snd_id = 0;
                    int32_t snd_size = 0;
                    char raw_name[256];
                    raw_name[0] = 0;
                    XPTR data = XGetIndexedFileResource(bank_file,
                                                        kSndTypes[t],
                                                        &snd_id,
                                                        i,
                                                        raw_name,
                                                        &snd_size);
                    if (!data)
                    {
                        continue;
                    }
                    XDisposePtr(data);
                    /* ID 0 is a valid SND resource id; only skip the -1 sentinel. */
                    if (snd_id < 0 || snd_id == static_cast<XLongResourceID>(0xFFFF) ||
                        referenced_snds.count(static_cast<uint32_t>(snd_id)) != 0)
                    {
                        continue;
                    }
                    referenced_snds.insert(static_cast<uint32_t>(snd_id)); /* de-dupe across types */

                    SampleRow orphan;
                    orphan.index = sample_row_index++;
                    orphan.instrument_index = 0;
                    orphan.split_index = 0;
                    orphan.snd_resource_id = static_cast<uint32_t>(snd_id);
                    /* Unassigned bank SNDs are Built-in unless session-added. */
                    orphan.is_custom =
                        (m_session_custom_snd_ids.count(static_cast<uint32_t>(snd_id)) != 0);
                    orphan.unassigned = true;
                    orphan.source = SampleSource::Bank;
                    orphan.bank = -1;
                    orphan.program = -1;
                    orphan.root_key = 60;
                    orphan.low_key = 0;
                    orphan.high_key = 127;
                    orphan.frame_count = 0;
                    {
                        char c_name[256];
                        c_name[0] = 0;
                        const unsigned char plen = static_cast<unsigned char>(raw_name[0]);
                        if (plen > 0 && plen < 255)
                        {
                            std::memcpy(c_name, raw_name + 1, plen);
                            c_name[plen] = 0;
                        }
                        if (c_name[0])
                        {
                            orphan.name = c_name;
                        }
                        else
                        {
                            char fallback[64];
                            std::snprintf(fallback, sizeof(fallback), "Sample %u",
                                          static_cast<unsigned>(snd_id));
                            orphan.name = fallback;
                        }
                    }
                    {
                        XPTR snd_data = XGetFileResource(bank_file,
                                                         kSndTypes[t],
                                                         snd_id,
                                                         nullptr,
                                                         &snd_size);
                        if (snd_data)
                        {
                            SampleDataInfo sdi;
                            std::memset(&sdi, 0, sizeof(sdi));
                            if (XGetSampleInfoFromSnd(snd_data, &sdi) == 0)
                            {
                                orphan.codec_label =
                                    FormatVisibleCodecLabel(static_cast<uint32_t>(sdi.compressionType),
                                                            0,
                                                            XGetSoundOpusRoundTripFlag(snd_data) != FALSE);
                                orphan.frame_count = sdi.frames;
                                orphan.bit_depth = sdi.bitSize ? sdi.bitSize : 16;
                                orphan.channels = sdi.channels ? sdi.channels : 1;
                                orphan.loop_start = sdi.loopStart;
                                orphan.loop_end = sdi.loopEnd;
                                {
                                    const uint32_t hz =
                                        static_cast<uint32_t>(XFIXED_TO_UNSIGNED_LONG(sdi.rate));
                                    orphan.sample_rate_hz = (hz > 0) ? hz : 44100;
                                }
                                if (sdi.baseKey >= 0 && sdi.baseKey <= 127)
                                {
                                    orphan.root_key = sdi.baseKey;
                                }
                                auto pref = m_session_pcm_cache.find(static_cast<uint32_t>(snd_id));
                                if (pref != m_session_pcm_cache.end() &&
                                    pref->second.has_export_target &&
                                    pref->second.export_compression != BAE_EDITOR_COMPRESSION_PCM &&
                                    pref->second.export_compression !=
                                        BAE_EDITOR_COMPRESSION_DONT_CHANGE)
                                {
                                    orphan.codec_label = FormatVisibleCodecLabelFromEditorType(
                                        pref->second.export_compression);
                                }
                            }
                            XDisposePtr(snd_data);
                        }
                    }
                    m_samples.push_back(orphan);
                }
            }
        }

        std::sort(m_instruments.begin(), m_instruments.end(), [](const InstrumentRow &a, const InstrumentRow &b) {
            if (a.bank != b.bank)
            {
                return a.bank < b.bank;
            }
            if (a.percussion != b.percussion)
            {
                return static_cast<int>(a.percussion) < static_cast<int>(b.percussion);
            }
            return a.program < b.program;
        });
        m_bank_instruments = m_instruments;

        if (!m_song_override_instruments.empty())
        {
            ApplySongOverridesToInstrumentList();
        }
        else
        {
            RebuildInstrumentFilters();
        }
        RefreshSongSamplesFromDocument();
        SortSampleListAlphabetical();
        if (m_sample_editor_open)
        {
            RefreshSampleEditorUsageText();
        }
    }

    /* Which bank instruments reference this SND (shared samples may appear in many). */
    std::string FormatSampleInstrumentUsage(uint32_t snd_id,
                                            bool unassigned,
                                            SampleSource source) const
    {
        if (source == SampleSource::Song)
        {
            return "Attached to: song document sample";
        }
        if (unassigned || !m_bank_token)
        {
            return "Attached to: (none — unassigned)";
        }

        std::vector<std::string> refs;
        std::set<uint32_t> seen_inst;
        uint32_t instrument_count = 0;
        if (BAERmfEditorBank_GetInstrumentCount(m_bank_token, &instrument_count) != BAE_NO_ERROR)
        {
            return "Attached to: (unknown)";
        }

        for (uint32_t inst_idx = 0; inst_idx < instrument_count; ++inst_idx)
        {
            BAERmfEditorBankInstrumentInfo inst_info;
            std::memset(&inst_info, 0, sizeof(inst_info));
            if (BAERmfEditorBank_GetInstrumentInfo(m_bank_token, inst_idx, &inst_info) != BAE_NO_ERROR)
            {
                continue;
            }
            uint32_t split_count = 0;
            if (BAERmfEditorBank_GetInstrumentSampleCount(m_bank_token, inst_idx, &split_count) !=
                BAE_NO_ERROR)
            {
                continue;
            }
            bool uses = false;
            for (uint32_t split_idx = 0; split_idx < split_count; ++split_idx)
            {
                BAERmfEditorBankSampleInfo sample_info;
                std::memset(&sample_info, 0, sizeof(sample_info));
                if (BAERmfEditorBank_GetInstrumentSampleInfo(m_bank_token,
                                                             inst_idx,
                                                             split_idx,
                                                             &sample_info) != BAE_NO_ERROR)
                {
                    continue;
                }
                if (static_cast<uint32_t>(sample_info.sndResourceID) == snd_id)
                {
                    uses = true;
                    break;
                }
            }
            if (!uses || !seen_inst.insert(inst_info.instID).second)
            {
                continue;
            }

            const bool perc = ((inst_info.instID % 256u) >= 128u);
            const int program = static_cast<int>(inst_info.instID % 128u);
            const int bank = static_cast<int>(inst_info.instID / 256u);
            char buf[192];
            if (inst_info.name[0])
            {
                std::snprintf(buf,
                              sizeof(buf),
                              "B%d%s P%03d %s",
                              bank,
                              perc ? " Perc" : "",
                              program,
                              inst_info.name);
            }
            else
            {
                std::snprintf(buf,
                              sizeof(buf),
                              "B%d%s P%03d (instID %u)",
                              bank,
                              perc ? " Perc" : "",
                              program,
                              inst_info.instID);
            }
            refs.emplace_back(buf);
        }

        if (refs.empty())
        {
            return "Attached to: (none — unassigned)";
        }

        std::string out = "Attached to: ";
        for (size_t i = 0; i < refs.size(); ++i)
        {
            if (i > 0)
            {
                out += "; ";
            }
            out += refs[i];
        }
        return out;
    }

    void RefreshSampleEditorUsageText()
    {
        if (m_sample_editor_is_song_sample)
        {
            m_sample_editor_usage_text = FormatSampleInstrumentUsage(0, false, SampleSource::Song);
            return;
        }
        bool unassigned = true;
        for (const SampleRow &row : m_samples)
        {
            if (row.source == SampleSource::Bank &&
                row.snd_resource_id == m_sample_editor_snd_resource_id)
            {
                unassigned = row.unassigned;
                break;
            }
        }
        m_sample_editor_usage_text =
            FormatSampleInstrumentUsage(m_sample_editor_snd_resource_id,
                                        unassigned,
                                        SampleSource::Bank);
    }

    void SortSampleListAlphabetical()
    {
        std::sort(m_samples.begin(), m_samples.end(), [](const SampleRow &a, const SampleRow &b) {
            const int name_cmp = strcasecmp(a.name.c_str(), b.name.c_str());
            if (name_cmp != 0)
            {
                return name_cmp < 0;
            }
            if (a.snd_resource_id != b.snd_resource_id)
            {
                return a.snd_resource_id < b.snd_resource_id;
            }
            return a.document_sample_index < b.document_sample_index;
        });
        for (size_t i = 0; i < m_samples.size(); ++i)
        {
            m_samples[i].index = static_cast<uint32_t>(i);
        }
    }

    void RebuildInstrumentFilters()
    {
        /* Rematch by (bank, category) — raw indices shifted when Custom filters
         * were inserted (old default index 5 is now Bank 1 Melodic). */
        int prefer_bank = -2;
        int prefer_category = 0;
        if (m_instrument_filter_index >= 0 &&
            m_instrument_filter_index < static_cast<int>(m_instrument_filters.size()))
        {
            prefer_bank = m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)].bank;
            prefer_category =
                m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)].category;
        }

        m_instrument_filters.clear();
        InstrumentFilterOption all;
        all.bank = -1;
        all.category = -1;
        all.label = "All";
        m_instrument_filters.push_back(all);

        /* Custom = session-owned / promoted at any bank (not only Bank 2). */
        InstrumentFilterOption custom_melodic;
        custom_melodic.bank = -2;
        custom_melodic.category = 0;
        custom_melodic.label = "Custom Melodic (any bank)";
        m_instrument_filters.push_back(custom_melodic);
        InstrumentFilterOption custom_perc;
        custom_perc.bank = -2;
        custom_perc.category = 1;
        custom_perc.label = "Custom Percussion (any bank)";
        m_instrument_filters.push_back(custom_perc);

        bool seen_banks[128];
        std::memset(seen_banks, 0, sizeof(seen_banks));
        seen_banks[0] = seen_banks[1] = seen_banks[2] = true;
        for (const InstrumentRow &inst : m_instruments)
        {
            if (inst.bank >= 0 && inst.bank < 128)
            {
                seen_banks[inst.bank] = true;
            }
        }
        for (const InstrumentRow &inst : m_bank_instruments)
        {
            if (inst.bank >= 0 && inst.bank < 128)
            {
                seen_banks[inst.bank] = true;
            }
        }

        for (int bank = 0; bank < 128; ++bank)
        {
            if (!seen_banks[bank])
            {
                continue;
            }
            InstrumentFilterOption melodic;
            melodic.bank = bank;
            melodic.category = 0;
            melodic.label = MakeBankCategoryLabel(bank, 0);
            m_instrument_filters.push_back(melodic);

            InstrumentFilterOption percussion;
            percussion.bank = bank;
            percussion.category = 1;
            percussion.label = MakeBankCategoryLabel(bank, 1);
            m_instrument_filters.push_back(percussion);
        }

        InstrumentFilterOption used_by_song;
        used_by_song.bank = -3;
        used_by_song.category = -1;
        used_by_song.label = "Used by current song";
        m_instrument_filters.push_back(used_by_song);

        m_instrument_filter_index = -1;
        for (size_t i = 0; i < m_instrument_filters.size(); ++i)
        {
            if (m_instrument_filters[i].bank == prefer_bank &&
                m_instrument_filters[i].category == prefer_category)
            {
                m_instrument_filter_index = static_cast<int>(i);
                break;
            }
        }
        if (m_instrument_filter_index < 0)
        {
            m_instrument_filter_index = DefaultInstrumentFilterIndex();
        }
    }

    void SelectInstrumentFilterByBankCategory(int bank, int category)
    {
        for (size_t i = 0; i < m_instrument_filters.size(); ++i)
        {
            if (m_instrument_filters[i].bank == bank &&
                m_instrument_filters[i].category == category)
            {
                m_instrument_filter_index = static_cast<int>(i);
                return;
            }
        }
        m_instrument_filter_index = DefaultInstrumentFilterIndex();
    }

    void ApplyRestoredSessionListUi()
    {
        if (!m_nbet_restore_has_list_ui)
        {
            return;
        }
        SelectInstrumentFilterByBankCategory(m_nbet_restore_inst_filter_bank,
                                             m_nbet_restore_inst_filter_category);
        m_nbet_restore_has_list_ui = false;
    }

    int DefaultInstrumentFilterIndex() const
    {
        /* Prefer Custom Melodic (any bank) so Bank 2+ / promoted customs show. */
        for (size_t i = 0; i < m_instrument_filters.size(); ++i)
        {
            if (m_instrument_filters[i].bank == -2 && m_instrument_filters[i].category == 0)
            {
                return static_cast<int>(i);
            }
        }
        for (size_t i = 0; i < m_instrument_filters.size(); ++i)
        {
            if (m_instrument_filters[i].bank == 2 && m_instrument_filters[i].category == 0)
            {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    bool InstrumentMatchesFilter(const InstrumentRow &inst) const
    {
        if (m_instrument_filter_index < 0 || m_instrument_filter_index >= static_cast<int>(m_instrument_filters.size()))
        {
            return true;
        }

        const InstrumentFilterOption &opt = m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)];
        /* All (-1/-1) and Used by current song (-3): bank/category don't restrict
         * here; song-used membership is applied in DrawInstrumentListWindow. */
        if ((opt.bank < 0 && opt.category < 0) || opt.bank == -3)
        {
            return true;
        }
        const int category = inst.percussion ? 1 : 0;
        if (opt.bank == -2)
        {
            return inst.is_custom && category == opt.category;
        }
        return inst.bank == opt.bank && category == opt.category;
    }

    /* Solo overrides pad mutes: only the solo channel is audible. */
    bool ChannelEffectivelyMuted(int ch) const
    {
        if (ch < 0 || ch >= 16)
        {
            return true;
        }
        if (m_channel_solo >= 0)
        {
            return ch != m_channel_solo;
        }
        return m_channel_muted[static_cast<size_t>(ch)];
    }

    void ApplyChannelMutes()
    {
        for (int ch = 0; ch < 16; ++ch)
        {
            const bool muted = ChannelEffectivelyMuted(ch);
            /* Mute/solo applies to playback only. Preview song stays open so the
             * audition keyboard (ch 1 / ch 10) keeps working while the song is muted. */
            if (m_song)
            {
                if (muted)
                {
                    BAESong_MuteChannel(m_song, static_cast<unsigned char>(ch));
                }
                else
                {
                    BAESong_UnmuteChannel(m_song, static_cast<unsigned char>(ch));
                }
            }
            if (m_preview_song)
            {
                BAESong_UnmuteChannel(m_preview_song, static_cast<unsigned char>(ch));
            }
        }
    }

    /* Declared before session.inc — helpers there take IePristineSplit& in
     * parameter types (not a complete-class context for nested types). */
    struct IePristineSplit
    {
        unsigned char lowKey = 0;
        unsigned char highKey = 0;
        unsigned char rootKey = 0;
        int16_t splitVolume = 0;
        uint16_t sndResourceID = 0;
    };
    std::vector<IePristineSplit> m_ie_pristine_splits;
    /* Last non-empty sample keymap before oscillator cleared SND links.
     * Pristine alone is wrong after Apply+generator: it captures SND=none, so
     * turning the generator off would "restore" none over the user's sample. */
    std::vector<IePristineSplit> m_ie_sample_splits_backup;

    // Session / IE / Song Info / Export helpers
#include "nbeditor_session.inc"
#include "nbeditor_bsn_session.inc"
#include "nbeditor_midi_editor.inc"
#include "nbeditor_lyrics.inc"
#include "nbeditor_keyboard_input.inc"

    /* Player floating min matches SetNextWindowSizeConstraints in DrawPlayerWindow.
     * Height fits transport + channels + audition keyboard (~500 logical px). */
    static constexpr float kPlayerFloatMinW = 780.0f;
    static constexpr float kPlayerFloatMinH = 500.0f;

    /* Rebuild workspace for m_session_undocked_layout without changing that flag. */
    void ApplyWorkspaceLayoutMode()
    {
        m_pending_apply_nbet = false;
        m_nbet_apply_delay_frames = 0;
        m_pending_imgui_ini.clear();
        m_force_reset_dock_layout = true;
        m_dock_layout_initialized = false;
        m_floating_layout_initialized = false;
        m_floating_placement_frames = 0;
        SyncDockingConfigForLayoutMode();
    }

    /* BE2-style floating Player / Session / Status. Call SetNext* before each Begin. */
    void ApplyPendingFloatingWindowPlacement(const char *which)
    {
        if (m_floating_placement_frames <= 0 || !m_session_undocked_layout || !which)
        {
            return;
        }
        ImGuiViewport *vp = ImGui::GetMainViewport();
        const ImVec2 work_pos = vp->WorkPos;
        const ImVec2 work_size = vp->WorkSize;
        const float margin = 8.0f;
        const float gap = 6.0f;
        const float status_h = std::max(110.0f, work_size.y * 0.14f);
        /* Player shrinks to its minimum; Session fills remaining width beside it. */
        const float player_w = kPlayerFloatMinW;
        const float player_h = kPlayerFloatMinH;
        const float session_x = work_pos.x + margin + player_w + gap;
        const float session_w =
            std::max(320.0f, work_size.x - (session_x - work_pos.x) - margin);
        const float session_h =
            std::max(player_h, work_size.y - status_h - margin * 2.0f - gap);

        /* DockID 0 = floating (docking branch). */
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        if (std::strcmp(which, "Player") == 0)
        {
            ImGui::SetNextWindowPos(ImVec2(work_pos.x + margin, work_pos.y + margin),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(player_w, player_h), ImGuiCond_Always);
        }
        else if (std::strcmp(which, "Session") == 0)
        {
            ImGui::SetNextWindowPos(ImVec2(session_x, work_pos.y + margin), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(session_w, session_h), ImGuiCond_Always);
        }
        else if (std::strcmp(which, "Status") == 0)
        {
            ImGui::SetNextWindowPos(
                ImVec2(work_pos.x + margin, work_pos.y + work_size.y - margin - status_h),
                ImGuiCond_Always);
        }
    }

    void SetupFloatingLayout(ImGuiID dockspace_id)
    {
        if (m_floating_layout_initialized)
        {
            return;
        }
        /* Empty dock host — main windows stay floating. */
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderFinish(dockspace_id);
        m_floating_placement_frames = 3;
        m_floating_layout_initialized = true;
        /* Prevent SetupInitialDockLayout from redocking while undocked. */
        m_dock_layout_initialized = true;
    }

    void DrawDockspace()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGuiViewport *viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoNavFocus |
                                        ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("nbeditor_virtual_desktop", nullptr, window_flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("nbeditor_dockspace_id");
        if (m_force_reset_dock_layout)
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            m_dock_layout_initialized = false;
            m_floating_layout_initialized = false;
            m_force_reset_dock_layout = false;
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        /* While waiting to apply nBeT, install a mode-appropriate default once so
         * windows are not stuck mid-transition for the delay frames. */
        if (m_session_undocked_layout)
        {
            if (!m_pending_apply_nbet || !m_floating_layout_initialized)
            {
                SetupFloatingLayout(dockspace_id);
            }
        }
        else if (!m_pending_apply_nbet || !m_dock_layout_initialized)
        {
            SetupInitialDockLayout(dockspace_id, viewport->Size);
        }

        ImGui::End();

        ApplyPendingFloatingWindowPlacement("Status");
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGuiWindowFlags status_flags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
        if (m_session_undocked_layout)
        {
            status_flags |= ImGuiWindowFlags_NoDocking;
        }
        if (ImGui::Begin("Status", nullptr, status_flags))
        {
            ImGui::Text("%s", m_status);
            if (m_loaded_session_path.empty())
            {
                ImGui::Text("Session file: (unsaved)");
            }
            else
            {
                ImGui::Text("Session file: %s", FileNameFromPath(m_loaded_session_path).c_str());
                ImGui::Text("  %s", m_loaded_session_path.c_str());
            }
            if (!m_song_row.name.empty())
            {
                ImGui::Text("Session song: %s [%s]%s",
                            m_song_row.name.c_str(),
                            m_song_row.source_type.empty() ? "?" : m_song_row.source_type.c_str(),
                            m_document_dirty ? " *" : "");
            }
            else
            {
                ImGui::Text("Session song: (none)");
            }
            ImGui::Text("Song path: %s",
                        m_loaded_song_path.empty() ? "(none)" : m_loaded_song_path.c_str());
            std::string bank_display_storage;
            const char *bank_display = "Built-in Bank";
            if (!m_loaded_bank_display_name.empty())
            {
                bank_display = m_loaded_bank_display_name.c_str();
            }
            else if (!m_loaded_bank_path.empty())
            {
                bank_display = m_loaded_bank_path.c_str();
            }
            else if (m_mixer && m_bank_token)
            {
                char friendly_name[256] = {0};
                if (BAE_GetBankFriendlyName(m_mixer, m_bank_token, friendly_name, static_cast<uint32_t>(sizeof(friendly_name))) == BAE_NO_ERROR && friendly_name[0])
                {
                    bank_display_storage = friendly_name;
                    bank_display = bank_display_storage.c_str();
                }
            }
            ImGui::Text("Loaded bank: %s", bank_display);
            ImGui::Text("FPS %.1f", io.Framerate);
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
            {
                /* Status drops use File → Open (RMF/modules ask Open as Session / Add). */
                m_dnd_hover_target = SessionDropTarget::FileOpen;
            }
        }
        ImGui::End();
    }

    void SetupInitialDockLayout(ImGuiID dockspace_id, const ImVec2 &dock_size)
    {
        if (m_dock_layout_initialized || m_session_undocked_layout)
        {
            return;
        }

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

        ImGuiID dock_status = 0;
        ImGuiID dock_main = 0;
        /* ~5 status lines; Liberation reads larger than DejaVu at the same px size. */
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.16f, &dock_status, &dock_main);

        ImGuiID dock_sidebar = 0;
        ImGuiID dock_player = 0;
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.38f, &dock_sidebar, &dock_player);

        ImGui::DockBuilderDockWindow("Player", dock_player);
        ImGui::DockBuilderDockWindow("Session", dock_sidebar);
        ImGui::DockBuilderDockWindow("Status", dock_status);
        ImGui::DockBuilderFinish(dockspace_id);

        m_dock_layout_initialized = true;
        m_floating_layout_initialized = false;
    }

#include "nbeditor_player.inc"

    void DrawKeyboard(bool for_instrument_editor = false)
    {
        struct KeyRect
        {
            int note = 0;
            ImVec2 min;
            ImVec2 max;
        };
        static const int kFirstNote = 21;
        static const int kLastNote = 108;
        auto is_black = [](int midi_note) {
            const int pitch = midi_note % 12;
            return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
        };

        ImGui::PushID(for_instrument_editor ? "ie_vkbd" : "player_vkbd");
        if (ImGui::Button("Panic"))
        {
            PanicPcKeyboardNotes();
            PanicAuditionNotes();
#if NBEDITOR_MVP
            StopSampleEditorPreview();
#endif
        }
        ImGui::SameLine();
        ImGui::Text("Oct %d  (,/.  |  A–K musical typing)", m_pc_keyboard_octave);
        ImGui::SameLine();
        ImGui::Text(for_instrument_editor
                        ? "Preview uses live (unapplied) instrument edits."
                        : "Click/hold keys or type A–K to audition.");

        const float width = std::max(520.0f, ImGui::GetContentRegionAvail().x);
        const float white_h = for_instrument_editor ? 52.0f : 60.0f;
        const float black_h = for_instrument_editor ? 34.0f : 38.0f;
        int white_count = 0;

        for (int note = kFirstNote; note <= kLastNote; ++note)
        {
            if (!is_black(note))
            {
                ++white_count;
            }
        }

        const float white_w = width / static_cast<float>(white_count);
        const float black_w = white_w * 0.64f;
        const ImVec2 keyboard_pos = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##keyboard88", ImVec2(width, white_h));
        const ImRect key_area(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList *draw = ImGui::GetWindowDrawList();

        /* Accept clicks when hovered; keep tracking while the button is active or a
         * note is held so click-drag can wash across keys.
         * Main player vkbd is suppressed while IE is open — must not touch shared
         * m_mouse_active_note or IE hold/drag dies every frame. */
        const bool suppressed_by_ie = !for_instrument_editor && m_instrument_editor_open;
        const bool modal_blocking = (ImGui::GetTopMostPopupModal() != nullptr);
        const bool editor_blocking =
            m_sample_editor_open || m_song_info_open || m_sample_editor_compression_open ||
            suppressed_by_ie;
        const bool item_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_None);
        const bool item_active = ImGui::IsItemActive();
        const bool holding_mouse_note =
            m_mouse_active_note >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool keyboard_tracking =
            !modal_blocking && !editor_blocking && (item_hovered || item_active || holding_mouse_note);
        (void)item_hovered;

        std::vector<KeyRect> white_keys;
        std::vector<KeyRect> black_keys;
        white_keys.reserve(static_cast<size_t>(white_count));
        black_keys.reserve(36);

        int white_idx = 0;
        for (int note = kFirstNote; note <= kLastNote; ++note)
        {
            if (is_black(note))
            {
                continue;
            }
            const float x0 = keyboard_pos.x + (static_cast<float>(white_idx) * white_w);
            const float x1 = x0 + white_w;
            white_keys.push_back({note, ImVec2(x0, keyboard_pos.y), ImVec2(x1, keyboard_pos.y + white_h)});
            ++white_idx;
        }

        white_idx = 0;
        for (int note = kFirstNote; note <= kLastNote; ++note)
        {
            if (!is_black(note))
            {
                ++white_idx;
                continue;
            }
            const float center = keyboard_pos.x + (static_cast<float>(white_idx) * white_w);
            const float x0 = center - (black_w * 0.5f);
            const float x1 = center + (black_w * 0.5f);
            black_keys.push_back({note, ImVec2(x0, keyboard_pos.y), ImVec2(x1, keyboard_pos.y + black_h)});
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int hovered_note = -1;
        if (keyboard_tracking)
        {
            for (const KeyRect &k : black_keys)
            {
                if (mouse.x >= k.min.x && mouse.x <= k.max.x && mouse.y >= k.min.y && mouse.y <= k.max.y)
                {
                    hovered_note = k.note;
                    break;
                }
            }
            if (hovered_note == -1)
            {
                for (const KeyRect &k : white_keys)
                {
                    if (mouse.x >= k.min.x && mouse.x <= k.max.x && mouse.y >= k.min.y && mouse.y <= k.max.y)
                    {
                        hovered_note = k.note;
                        break;
                    }
                }
            }
        }

        const bool left_clicked = keyboard_tracking && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool left_down = keyboard_tracking && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool left_released = keyboard_tracking && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        const bool in_key_area = key_area.Contains(mouse);

        if (!suppressed_by_ie)
        {
            if (left_clicked && in_key_area && hovered_note >= 0)
            {
                if (m_mouse_active_note >= 0 && m_mouse_active_note != hovered_note)
                {
                    DispatchKeyboardNoteOff(m_mouse_active_note);
                    m_key_mouse_held[static_cast<size_t>(m_mouse_active_note)] = false;
                }
                m_mouse_active_note = hovered_note;
                m_key_mouse_held[static_cast<size_t>(hovered_note)] = true;
                DispatchKeyboardNoteOn(hovered_note);
            }
            else if (left_down && m_mouse_active_note >= 0 && hovered_note >= 0 &&
                     hovered_note != m_mouse_active_note)
            {
                DispatchKeyboardNoteOff(m_mouse_active_note);
                m_key_mouse_held[static_cast<size_t>(m_mouse_active_note)] = false;
                m_mouse_active_note = hovered_note;
                m_key_mouse_held[static_cast<size_t>(hovered_note)] = true;
                DispatchKeyboardNoteOn(hovered_note);
            }

            if ((left_released || !keyboard_tracking ||
                 (left_down && !in_key_area && hovered_note < 0)) &&
                m_mouse_active_note >= 0)
            {
                DispatchKeyboardNoteOff(m_mouse_active_note);
                m_key_mouse_held[static_cast<size_t>(m_mouse_active_note)] = false;
                m_mouse_active_note = -1;
            }
        }

        const uint32_t now_ms = SDL_GetTicks();
        auto note_is_lit = [&](int note) {
            if (note < 0 || note > 127)
            {
                return false;
            }
            return m_key_mouse_held[static_cast<size_t>(note)] ||
                   m_hw_note_held[static_cast<size_t>(note)] ||
                   m_key_active_until_ms[static_cast<size_t>(note)] > now_ms;
        };

        for (const KeyRect &k : white_keys)
        {
            const bool lit = note_is_lit(k.note);
            const ImU32 fill = lit ? IM_COL32(144, 229, 190, 255) : IM_COL32(248, 248, 248, 255);
            draw->AddRectFilled(k.min, k.max, fill, 2.0f);
            draw->AddRect(k.min, k.max, IM_COL32(34, 34, 34, 255), 2.0f);

            if ((k.note % 12) == 0)
            {
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), "C%d", (k.note / 12) - 1);
                draw->AddText(ImVec2(k.min.x + 4.0f, k.max.y - 18.0f), IM_COL32(45, 45, 45, 255), lbl);
            }
        }

        for (const KeyRect &k : black_keys)
        {
            const bool lit = note_is_lit(k.note);
            const ImU32 fill = lit ? IM_COL32(104, 196, 158, 255) : IM_COL32(24, 24, 24, 255);
            draw->AddRectFilled(k.min, k.max, fill, 2.0f);
            draw->AddRect(k.min, k.max, IM_COL32(5, 5, 5, 255), 2.0f);
        }

        ImGui::Dummy(ImVec2(width, 2.0f));
        ImGui::PopID();
    }

    void UpdateKeyboardVoiceActivity(const BAEAudioInfo &info)
    {
        const uint32_t now_ms = SDL_GetTicks();
        const int voice_count = std::clamp(static_cast<int>(info.voicesActive), 0, static_cast<int>(BAE_MAX_VOICES));
        /* IE piano should only reflect the instrument under edit, not every
         * song voice. Player keyboard keeps the full mix. */
        const bool filter_ie = m_instrument_editor_open;
        const int32_t ie_patch =
            filter_ie ? static_cast<int32_t>(InstrumentEditorPatchId()) : -1;
        for (int i = 0; i < voice_count; ++i)
        {
            if (info.voiceType[i] != BAE_MIDI_PCM_VOICE)
            {
                continue;
            }
            if (filter_ie && info.instrument[i] != ie_patch)
            {
                continue;
            }
            const int note = static_cast<int>(info.midiNote[i]);
            if (note >= 0 && note < 128)
            {
                m_key_active_until_ms[static_cast<size_t>(note)] = now_ms + 120;
            }
        }
    }

    void UpdateOscilloscopeFromMixer()
    {
        if (!m_mixer)
        {
            return;
        }

        static int16_t left[16384] = {0};
        static int16_t right[16384] = {0};
        int16_t frame = 0;
        if (BAEMixer_GetAudioSampleFrame(m_mixer, left, right, &frame) != BAE_NO_ERROR)
        {
            return;
        }

        const int max_idx = static_cast<int>(SDL_arraysize(left)) - 1;
        int idx = 0;
        if (frame > 0 && frame < static_cast<int16_t>(SDL_arraysize(left)))
        {
            idx = static_cast<int>(frame) / 2;
        }
        idx = std::clamp(idx, 0, max_idx);
        const float l = static_cast<float>(left[idx]) / 32768.0f;
        const float r = static_cast<float>(right[idx]) / 32768.0f;

        // Compute a tiny local envelope so very quiet sections still animate visibly.
        float env_accum = 0.0f;
        constexpr int kEnvWindow = 48;
        for (int i = 0; i < kEnvWindow; ++i)
        {
            const int sidx = std::clamp(idx - i, 0, max_idx);
            const float sl = static_cast<float>(left[sidx]) / 32768.0f;
            const float sr = static_cast<float>(right[sidx]) / 32768.0f;
            env_accum += std::fabs((sl + sr) * 0.5f);
        }
        const float env = env_accum / static_cast<float>(kEnvWindow);
        const float dynamic_gain = std::clamp(4.4f - (env * 8.0f), 1.8f, 4.4f);
        const float mono = std::clamp(((l + r) * 0.5f) * dynamic_gain, -1.0f, 1.0f);

        m_scope_write_index = (m_scope_write_index + 1) % static_cast<int>(m_scope_history.size());
        m_scope_history[static_cast<size_t>(m_scope_write_index)] = mono;
    }

    void DrawOscilloscope()
    {
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImVec2(ImGui::GetContentRegionAvail().x, 44.0f);
        if (canvas_size.x < 50.0f)
        {
            canvas_size.x = 50.0f;
        }

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(canvas_pos,
                                 ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                                 NeoBaeTheme::ScopeBg());
        draw_list->AddRect(canvas_pos,
                           ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                           NeoBaeTheme::ScopeBorder());

        const int points = static_cast<int>(m_scope_history.size());
        const float mid_y = canvas_pos.y + (canvas_size.y * 0.5f);
        const float a = 22.0f;

        ImVec2 prev = ImVec2(canvas_pos.x, mid_y);
        for (int i = 1; i < points; ++i)
        {
            const float x = canvas_pos.x + (canvas_size.x * (static_cast<float>(i) / static_cast<float>(points - 1)));
            const int history_idx = (m_scope_write_index + i) % points;
            const float y = mid_y - (a * m_scope_history[static_cast<size_t>(history_idx)]);
            ImVec2 cur = ImVec2(x, y);
            draw_list->AddLine(prev, cur, NeoBaeTheme::ScopeWave(), 1.6f);
            prev = cur;
        }

        ImGui::Dummy(canvas_size);
    }


    static bool SampleIsCustom(const SampleRow &sample)
    {
        /* Custom: bank 2+ / session-added SNDs / song samples.
         * Unassigned bank orphans stay Built-in unless session-added. */
        return sample.is_custom || sample.source == SampleSource::Song;
    }

    bool SampleMatchesShowFilter(const SampleRow &sample) const
    {
        switch (m_sample_show_filter)
        {
        case SampleShowFilter::Custom:
            return SampleIsCustom(sample);
        case SampleShowFilter::BuiltIn:
            return !SampleIsCustom(sample);
        case SampleShowFilter::Unassigned:
            return sample.unassigned;
        case SampleShowFilter::All:
        default:
            return true;
        }
    }

    bool SampleMatchesTextFilter(const SampleRow &sample) const
    {
        if (!m_sample_text_filter[0])
        {
            return true;
        }
        if (ContainsIgnoreCase(sample.name.c_str(), m_sample_text_filter))
        {
            return true;
        }
        if (ContainsIgnoreCase(sample.codec_label.c_str(), m_sample_text_filter))
        {
            return true;
        }
        char id_buf[32];
        std::snprintf(id_buf, sizeof(id_buf), "%u", sample.snd_resource_id);
        return ContainsIgnoreCase(id_buf, m_sample_text_filter);
    }

    bool InstrumentMatchesTextFilter(const InstrumentRow &inst) const
    {
        if (!m_instrument_text_filter[0])
        {
            return true;
        }
        if (ContainsIgnoreCase(inst.name.c_str(), m_instrument_text_filter))
        {
            return true;
        }
        char slot[32];
        std::snprintf(slot, sizeof(slot), "B%dP%03d", inst.bank, inst.program);
        if (ContainsIgnoreCase(slot, m_instrument_text_filter))
        {
            return true;
        }
        std::snprintf(slot, sizeof(slot), "%d", inst.program);
        return ContainsIgnoreCase(slot, m_instrument_text_filter);
    }

    /* Switch Session→Samples, pick a Show: filter that includes the row, select + scroll. */
    void RevealSampleInSamplesTab(int sample_row)
    {
        if (sample_row < 0 || sample_row >= static_cast<int>(m_samples.size()))
        {
            SetStatus("Sample not found in Samples list");
            return;
        }
        const SampleRow &sample = m_samples[static_cast<size_t>(sample_row)];
        if (sample.unassigned)
        {
            m_sample_show_filter = SampleShowFilter::Unassigned;
        }
        else if (SampleIsCustom(sample))
        {
            m_sample_show_filter = SampleShowFilter::Custom;
        }
        else
        {
            m_sample_show_filter = SampleShowFilter::BuiltIn;
        }
        m_selected_sample = sample_row;
        m_pending_session_tab = static_cast<int>(SessionTab::Samples);
        m_scroll_to_selected_sample = true;
        char status[160];
        std::snprintf(status,
                      sizeof(status),
                      "Samples: selected SND %u (%s)",
                      sample.snd_resource_id,
                      sample.name.c_str());
        SetStatus(status);
    }

    void DrawSampleListWindow()
    {
        static const char *kSampleShowLabels[] = {
            "All Samples",
            "Custom Samples",
            "Built-in Samples",
            "Unassigned Samples",
        };
        static const char *kSampleSortLabels[] = {
            "By Name",
            "By sndID",
        };
        const int filter_index = static_cast<int>(m_sample_show_filter);
        const char *preview =
            kSampleShowLabels[filter_index >= 0 && filter_index < 4 ? filter_index : 0];

        ImGui::Text("Show:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##SampleShowFilter", preview))
        {
            for (int i = 0; i < 4; ++i)
            {
                const bool selected = (filter_index == i);
                if (ImGui::Selectable(kSampleShowLabels[i], selected))
                {
                    m_sample_show_filter = static_cast<SampleShowFilter>(i);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Sort:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        const int sort_index = static_cast<int>(m_sample_list_sort);
        if (ImGui::BeginCombo("##SampleListSort",
                              kSampleSortLabels[sort_index >= 0 && sort_index < 2 ? sort_index : 0]))
        {
            for (int i = 0; i < 2; ++i)
            {
                const bool selected = (sort_index == i);
                if (ImGui::Selectable(kSampleSortLabels[i], selected))
                {
                    m_sample_list_sort = static_cast<SampleListSort>(i);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##SampleTextFilter",
                                 "name or sndID…",
                                 m_sample_text_filter,
                                 sizeof(m_sample_text_filter));

        std::vector<size_t> visible;
        visible.reserve(m_samples.size());
        for (size_t i = 0; i < m_samples.size(); ++i)
        {
            const SampleRow &sample = m_samples[i];
            if (SampleMatchesShowFilter(sample) && SampleMatchesTextFilter(sample))
            {
                visible.push_back(i);
            }
        }
        std::sort(visible.begin(),
                  visible.end(),
                  [this](size_t ia, size_t ib) {
                      const SampleRow &a = m_samples[ia];
                      const SampleRow &b = m_samples[ib];
                      if (m_sample_list_sort == SampleListSort::BySndId)
                      {
                          if (a.snd_resource_id != b.snd_resource_id)
                          {
                              return a.snd_resource_id < b.snd_resource_id;
                          }
                      }
                      const int name_cmp = strcasecmp(a.name.c_str(), b.name.c_str());
                      if (name_cmp != 0)
                      {
                          return name_cmp < 0;
                      }
                      if (a.snd_resource_id != b.snd_resource_id)
                      {
                          return a.snd_resource_id < b.snd_resource_id;
                      }
                      return ia < ib;
                  });

        const int visible_count = static_cast<int>(visible.size());
        ImGui::Text("Samples: %d", visible_count);
        ImGui::Separator();

        if (visible_count == 0)
        {
            ImGui::TextDisabled(m_sample_text_filter[0]
                                    ? "No samples match this filter."
                                    : "No samples in this category. Try another Show: option.");
            if (ImGui::BeginPopupContextWindow("sample_list_empty_ctx", ImGuiPopupFlags_MouseButtonRight))
            {
                if (ImGui::MenuItem("Paste", nullptr, false, CanPasteSamples()))
                {
                    PasteSamplesSelection();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Sample...", nullptr, false, m_bank_token != 0))
                {
                    OpenAddSampleFileDialog();
                }
                ImGui::EndPopup();
            }
            return;
        }

        for (size_t vi = 0; vi < visible.size(); ++vi)
        {
            const size_t i = visible[vi];
            const SampleRow &sample = m_samples[i];

            char label[384];
            const char *codec_tag = sample.codec_label.empty() ? "" : sample.codec_label.c_str();
            char codec_bracket[64] = {0};
            if (codec_tag[0])
            {
                std::snprintf(codec_bracket, sizeof(codec_bracket), " [%s]", codec_tag);
            }
            /* BE2-style: SND id first. Root/range omitted — a shared SND can
             * differ per instrument; usage is shown in the Sample Editor. */
            if (sample.unassigned)
            {
                std::snprintf(label,
                              sizeof(label),
                              "[SND:%u]  %s%s  [Unassigned]",
                              sample.snd_resource_id,
                              sample.name.c_str(),
                              codec_bracket);
            }
            else if (sample.source == SampleSource::Song)
            {
                std::snprintf(label,
                              sizeof(label),
                              "[SND:%u]  %s%s  [Song]",
                              sample.snd_resource_id,
                              sample.name.c_str(),
                              codec_bracket);
            }
            else
            {
                std::snprintf(label,
                              sizeof(label),
                              "[SND:%u]  %s%s",
                              sample.snd_resource_id,
                              sample.name.c_str(),
                              codec_bracket);
            }
            const bool sample_selected = (m_multi_samples.count(static_cast<int>(i)) != 0) ||
                                         (m_selected_sample == static_cast<int>(i));
            if (ImGui::Selectable(label, sample_selected))
            {
                const ImGuiIO &io = ImGui::GetIO();
                ApplyListSelection(m_multi_samples,
                                   m_selected_sample,
                                   static_cast<int>(i),
                                   m_multi_sample_anchor,
                                   io.KeyCtrl,
                                   io.KeyShift);
                m_multi_sample_anchor = static_cast<int>(i);
                if (!sample.unassigned)
                {
                    m_selected_bank = sample.bank;
                    m_selected_program = sample.program;
                    m_selected_instrument = -1;
                    for (size_t ii = 0; ii < m_instruments.size(); ++ii)
                    {
                        if (m_instruments[ii].bank == m_selected_bank &&
                            m_instruments[ii].program == m_selected_program)
                        {
                            m_selected_instrument = static_cast<int>(ii);
                            break;
                        }
                    }
                    BAESong audition_song = GetAuditionSong();
                    if (audition_song)
                    {
                        BAESong_ProgramBankChange(audition_song,
                                                  AuditionMidiChannel(),
                                                  static_cast<unsigned char>(m_selected_program),
                                                  static_cast<unsigned char>(m_selected_bank),
                                                  0);
                    }
                }
            }
            if (m_scroll_to_selected_sample && m_selected_sample == static_cast<int>(i))
            {
                ImGui::SetScrollHereY(0.25f);
                m_scroll_to_selected_sample = false;
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_selected_sample = static_cast<int>(i);
                if (sample.unassigned)
                {
                    (void)MakeInstrumentUsingSelectedSampleAuto();
                }
#if NBEDITOR_MVP
                else
                {
                    OpenSampleEditorForSelection();
                }
#endif
            }

            char popup_id[64];
            std::snprintf(popup_id, sizeof(popup_id), "sample_ctx_%zu", i);
            if (ImGui::BeginPopupContextItem(popup_id, ImGuiPopupFlags_MouseButtonRight))
            {
#if NBEDITOR_MVP
                if (ImGui::MenuItem("Open Sample Editor",
                                    nullptr,
                                    false,
                                    sample.source == SampleSource::Bank ||
                                        sample.source == SampleSource::Song))
                {
                    m_selected_sample = static_cast<int>(i);
                    OpenSampleEditorForSelection();
                }
#endif
                if (sample.unassigned)
                {
                    /* Double-click also runs auto; menu offers the slot dialog. */
                    if (ImGui::MenuItem("Make Instrument Using..."))
                    {
                        m_selected_sample = static_cast<int>(i);
                        OpenMakeInstrumentUsingDialog();
                    }
                }
                const bool can_rename_sample =
                    (sample.source == SampleSource::Bank && m_bank_token != 0 &&
                     sample.snd_resource_id != 0xFFFFu) ||
                    (sample.source == SampleSource::Song && m_document != nullptr);
                if (ImGui::MenuItem("Rename...", nullptr, false, can_rename_sample))
                {
                    OpenRenameDialog("sample", static_cast<int>(i), sample.name);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Cut",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && sample.source == SampleSource::Bank))
                {
                    m_selected_sample = static_cast<int>(i);
                    m_multi_samples.insert(static_cast<int>(i));
                    CopySamplesSelection(true);
                }
                if (ImGui::MenuItem("Copy",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && sample.source == SampleSource::Bank))
                {
                    m_selected_sample = static_cast<int>(i);
                    m_multi_samples.insert(static_cast<int>(i));
                    CopySamplesSelection(false);
                }
                if (ImGui::MenuItem("Paste", nullptr, false, CanPasteSamples()))
                {
                    PasteSamplesSelection();
                }
                if (ImGui::MenuItem("Select All"))
                {
                    SessionSelectAllVisible();
                }
                if (ImGui::MenuItem("Export Sample...",
                                    nullptr,
                                    false,
                                    (sample.source == SampleSource::Bank && m_bank_token != 0 &&
                                     sample.snd_resource_id != 0xFFFFu) ||
                                        (sample.source == SampleSource::Song && m_document != nullptr)))
                {
                    m_selected_sample = static_cast<int>(i);
                    OpenExportSampleDialog(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Export as RMF...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && sample.source == SampleSource::Bank &&
                                        sample.snd_resource_id != 0xFFFFu))
                {
                    m_selected_sample = static_cast<int>(i);
                    OpenExportSampleRmfDialog(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Get Resource Usage..."))
                {
                    OpenResourceUsageForSample(static_cast<int>(i));
                }
                if (ImGui::MenuItem(sample.source == SampleSource::Song
                                        ? "Remove Song Sample"
                                        : "Move Sample to Trash",
                                    nullptr,
                                    false,
                                    (m_bank_token != 0 && sample.source == SampleSource::Bank) ||
                                        (m_document != nullptr &&
                                         sample.source == SampleSource::Song)))
                {
                    DeleteSampleAt(static_cast<int>(i));
                    m_multi_samples.erase(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Clean Unused Samples",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && CountUnusedBankSamples() > 0))
                {
                    CleanUnusedSamplesToTrash();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Sample...", nullptr, false, m_bank_token != 0))
                {
                    OpenAddSampleFileDialog();
                }
                ImGui::EndPopup();
            }
        }

        if (ImGui::BeginPopupContextWindow("sample_list_bg_ctx",
                                           ImGuiPopupFlags_MouseButtonRight |
                                               ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Paste", nullptr, false, CanPasteSamples()))
            {
                PasteSamplesSelection();
            }
            if (ImGui::MenuItem("Select All", nullptr, false, !m_samples.empty()))
            {
                SessionSelectAllVisible();
            }
            if (ImGui::MenuItem("Clean Unused Samples",
                                nullptr,
                                false,
                                m_bank_token != 0 && CountUnusedBankSamples() > 0))
            {
                CleanUnusedSamplesToTrash();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Add Sample...", nullptr, false, m_bank_token != 0))
            {
                OpenAddSampleFileDialog();
            }
            ImGui::EndPopup();
        }

    }

#if NBEDITOR_MVP
    // Phase 2 BE2-style Sample Editor
#include "nbeditor_sample_editor.inc"
#endif

#include "nbeditor_bank_add.inc"
#include "nbeditor_instrument_ops.inc"
#include "nbeditor_note_remapper.inc"
#include "nbeditor_trash.inc"
#include "nbeditor_resource_usage.inc"
#include "nbeditor_session_extras.inc"
#include "nbeditor_midi_hw.inc"
#include "nbeditor_export_prefs.inc"
#include "nbeditor_tips_dlg.inc"
#include "nbeditor_about_dlg.inc"
#include "nbeditor_dnd.inc"
#include "nbeditor_clip_xfer.inc"

    /* Same note→ResolveInstID mapping as RebuildExportInstrumentList. */
    void CollectCurrentSongUsedInstrumentKeys(std::set<uint32_t> &out_inst_ids,
                                              std::set<uint32_t> &out_instrument_indexes) const
    {
        out_inst_ids.clear();
        out_instrument_indexes.clear();
        if (!m_document || !m_bank_token)
        {
            return;
        }

        std::set<uint32_t> requested_seen;
        uint16_t track_count = 0;
        if (BAERmfEditorDocument_GetTrackCount(m_document, &track_count) != BAE_NO_ERROR)
        {
            return;
        }
        for (uint16_t t = 0; t < track_count; ++t)
        {
            uint32_t note_count = 0;
            if (BAERmfEditorDocument_GetNoteCount(m_document, t, &note_count) != BAE_NO_ERROR)
            {
                continue;
            }
            for (uint32_t n = 0; n < note_count; ++n)
            {
                BAERmfEditorNoteInfo note;
                std::memset(&note, 0, sizeof(note));
                if (BAERmfEditorDocument_GetNoteInfo(m_document, t, n, &note) != BAE_NO_ERROR)
                {
                    continue;
                }
                const int bank_group = ExportBankGroupFromNoteBank(note.bank);
                /* All CH10 hits use kit encoding (group*256 + 128 + note), including
                 * custom bank-2 kits (INST 640+). Melodic bank+program on CH10 would
                 * collide with pitched INST 512+p (e.g. spacerock drums vs rhodes). */
                const bool percussion = (note.channel == 9);
                const uint32_t requested_inst_id =
                    percussion ? (static_cast<uint32_t>(bank_group) * 256u) + 128u + note.note
                               : (static_cast<uint32_t>(bank_group) * 256u) + note.program;
                if (!requested_seen.insert(requested_inst_id).second)
                {
                    continue;
                }

                out_inst_ids.insert(requested_inst_id);
                uint32_t resolved_inst_id = requested_inst_id;
                uint32_t instrument_index = 0;
                if (BAERmfEditorBank_ResolveInstID(m_bank_token,
                                                   requested_inst_id,
                                                   &resolved_inst_id,
                                                   &instrument_index) == BAE_NO_ERROR)
                {
                    out_inst_ids.insert(resolved_inst_id);
                    out_instrument_indexes.insert(instrument_index);
                }
            }
        }
    }

    bool InstrumentUsedByCurrentSong(const InstrumentRow &inst,
                                     const std::set<uint32_t> &used_inst_ids,
                                     const std::set<uint32_t> &used_instrument_indexes) const
    {
        if (!used_inst_ids.empty())
        {
            if (inst.inst_id != BAE_EDITOR_INST_ID_NONE &&
                used_inst_ids.count(inst.inst_id) != 0)
            {
                return true;
            }
            if (inst.target_inst_id != BAE_EDITOR_INST_ID_NONE &&
                used_inst_ids.count(inst.target_inst_id) != 0)
            {
                return true;
            }
        }
        if (!inst.from_song_document &&
            used_instrument_indexes.count(inst.instrument_index) != 0)
        {
            return true;
        }
        return false;
    }

    void DrawInstrumentListWindow()
    {
        static const char *kInstrumentSortLabels[] = {
            "By Program",
            "By Name",
        };

        if (!m_instrument_filters.empty())
        {
            ImGui::Text("Show:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const char *preview = m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)].label.c_str();
            if (ImGui::BeginCombo("##InstrumentFilter", preview))
            {
                for (size_t i = 0; i < m_instrument_filters.size(); ++i)
                {
                    const bool selected = (m_instrument_filter_index == static_cast<int>(i));
                    if (ImGui::Selectable(m_instrument_filters[i].label.c_str(), selected))
                    {
                        m_instrument_filter_index = static_cast<int>(i);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Text("Sort:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        const int sort_index = static_cast<int>(m_instrument_list_sort);
        if (ImGui::BeginCombo(
                "##InstrumentListSort",
                kInstrumentSortLabels[sort_index >= 0 && sort_index < 2 ? sort_index : 0]))
        {
            for (int i = 0; i < 2; ++i)
            {
                const bool selected = (sort_index == i);
                if (ImGui::Selectable(kInstrumentSortLabels[i], selected))
                {
                    m_instrument_list_sort = static_cast<InstrumentListSort>(i);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##InstrumentTextFilter",
                                 "name or B0P000…",
                                 m_instrument_text_filter,
                                 sizeof(m_instrument_text_filter));

        const InstrumentFilterOption *active_filter = nullptr;
        if (m_instrument_filter_index >= 0 &&
            m_instrument_filter_index < static_cast<int>(m_instrument_filters.size()))
        {
            active_filter = &m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)];
        }
        const bool filter_is_group =
            active_filter && active_filter->bank >= 0 && active_filter->category >= 0;
        const bool used_by_song = active_filter && active_filter->bank == -3;

        std::set<uint32_t> song_used_inst_ids;
        std::set<uint32_t> song_used_instrument_indexes;
        if (used_by_song)
        {
            if (m_document_dirty)
            {
                m_song_used_keys_dirty = true;
            }
            if (m_song_used_keys_dirty)
            {
                CollectCurrentSongUsedInstrumentKeys(m_song_used_inst_ids,
                                                     m_song_used_instrument_indexes);
                m_song_used_keys_dirty = false;
            }
            song_used_inst_ids = m_song_used_inst_ids;
            song_used_instrument_indexes = m_song_used_instrument_indexes;
        }

        std::vector<size_t> visible;
        visible.reserve(m_instruments.size());
        bool used_programs[128];
        std::memset(used_programs, 0, sizeof(used_programs));
        for (size_t i = 0; i < m_instruments.size(); ++i)
        {
            const InstrumentRow &inst = m_instruments[i];
            if (!InstrumentMatchesFilter(inst))
            {
                continue;
            }
            if (filter_is_group && inst.program >= 0 && inst.program < 128)
            {
                used_programs[inst.program] = true;
            }
            if (used_by_song &&
                !InstrumentUsedByCurrentSong(inst,
                                             song_used_inst_ids,
                                             song_used_instrument_indexes))
            {
                continue;
            }
            if (!InstrumentMatchesTextFilter(inst))
            {
                continue;
            }
            visible.push_back(i);
        }
        std::sort(visible.begin(),
                  visible.end(),
                  [this](size_t ia, size_t ib) {
                      const InstrumentRow &a = m_instruments[ia];
                      const InstrumentRow &b = m_instruments[ib];
                      if (m_instrument_list_sort == InstrumentListSort::ByName)
                      {
                          const int name_cmp = strcasecmp(a.name.c_str(), b.name.c_str());
                          if (name_cmp != 0)
                          {
                              return name_cmp < 0;
                          }
                      }
                      if (a.bank != b.bank)
                      {
                          return a.bank < b.bank;
                      }
                      if (a.percussion != b.percussion)
                      {
                          return static_cast<int>(a.percussion) < static_cast<int>(b.percussion);
                      }
                      if (a.program != b.program)
                      {
                          return a.program < b.program;
                      }
                      return ia < ib;
                  });

        int empty_count = 0;
        std::vector<int> empty_programs;
        /* Empty slots are meaningless when filtering to song-used instruments. */
        if (m_show_empty_instrument_slots && filter_is_group && !used_by_song)
        {
            const int bank = active_filter->bank;
            for (int p = 0; p < 128; ++p)
            {
                if (used_programs[p])
                {
                    continue;
                }
                char empty_label[64];
                std::snprintf(empty_label, sizeof(empty_label), "B%dP%03d  (empty)", bank, p);
                char prog_buf[16];
                std::snprintf(prog_buf, sizeof(prog_buf), "%d", p);
                if (m_instrument_text_filter[0] &&
                    !ContainsIgnoreCase(empty_label, m_instrument_text_filter) &&
                    !ContainsIgnoreCase(prog_buf, m_instrument_text_filter) &&
                    !ContainsIgnoreCase("empty", m_instrument_text_filter))
                {
                    continue;
                }
                empty_programs.push_back(p);
            }
            empty_count = static_cast<int>(empty_programs.size());
            if (m_instrument_list_sort == InstrumentListSort::ByName)
            {
                /* All empty labels share the same name; keep program order. */
            }
        }

        const int visible_count = static_cast<int>(visible.size());
        if (used_by_song && !m_document)
        {
            ImGui::TextDisabled("Instruments: 0 (no active song)");
        }
        else if (m_show_empty_instrument_slots && filter_is_group && !used_by_song)
        {
            ImGui::Text("Instruments: %d (+ %d empty)", visible_count, empty_count);
        }
        else if (used_by_song)
        {
            ImGui::Text("Instruments: %d (used by current song)", visible_count);
        }
        else
        {
            ImGui::Text("Instruments: %d", visible_count);
        }
        ImGui::Separator();

        for (size_t vi = 0; vi < visible.size(); ++vi)
        {
            const size_t i = visible[vi];
            const InstrumentRow &inst = m_instruments[i];

            char label[256];
            std::snprintf(label,
                          sizeof(label),
                          "B%dP%03d  %s  (%d split%s)",
                          inst.bank,
                          inst.program,
                          inst.name.c_str(),
                          inst.split_count,
                          inst.split_count == 1 ? "" : "s");
            const bool alias_italic = inst.is_alias && m_font_italic != nullptr;
            if (alias_italic)
            {
                ImGui::PushFont(m_font_italic);
            }
            else if (inst.is_alias)
            {
                /* Fallback when oblique TTF failed to load. */
                ImGui::PushStyleColor(ImGuiCol_Text, NeoBaeTheme::AliasTextFallback());
            }
            const bool inst_selected =
                (m_multi_instruments.count(static_cast<int>(i)) != 0) ||
                (m_selected_instrument == static_cast<int>(i) && !m_selected_empty_slot);
            if (ImGui::Selectable(label, inst_selected))
            {
                const ImGuiIO &io = ImGui::GetIO();
                ApplyListSelection(m_multi_instruments,
                                   m_selected_instrument,
                                   static_cast<int>(i),
                                   m_multi_instrument_anchor,
                                   io.KeyCtrl,
                                   io.KeyShift);
                m_multi_instrument_anchor = static_cast<int>(i);
                m_selected_empty_slot = false;
                m_selected_bank = inst.bank;
                m_selected_program = inst.program;
                BAESong audition_song = GetAuditionSong();
                if (audition_song)
                {
                    /* Perc: keep channel program 0 (note selects the drum INST). */
                    const unsigned char prog =
                        inst.percussion ? static_cast<unsigned char>(0)
                                        : static_cast<unsigned char>(m_selected_program);
                    BAESong_ProgramBankChange(audition_song,
                                              AuditionMidiChannel(),
                                              prog,
                                              static_cast<unsigned char>(m_selected_bank),
                                              0);
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_selected_instrument = static_cast<int>(i);
                m_selected_empty_slot = false;
                if (!inst.is_alias)
                {
                    OpenInstrumentEditorForSelection();
                }
            }
            if (alias_italic)
            {
                ImGui::PopFont();
            }
            else if (inst.is_alias)
            {
                ImGui::PopStyleColor();
            }
            char popup_id[64];
            std::snprintf(popup_id, sizeof(popup_id), "inst_ctx_%zu", i);
            if (ImGui::BeginPopupContextItem(popup_id, ImGuiPopupFlags_MouseButtonRight))
            {
                if (ImGui::MenuItem("Edit Instrument", nullptr, false, !inst.is_alias))
                {
                    m_selected_instrument = static_cast<int>(i);
                    m_selected_empty_slot = false;
                    OpenInstrumentEditorForSelection();
                }
                if (ImGui::MenuItem("Get Resource Usage..."))
                {
                    OpenResourceUsageForInstrument(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Export as RMF...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document))
                {
                    m_selected_instrument = static_cast<int>(i);
                    m_selected_empty_slot = false;
                    OpenExportInstrumentAsRmf(static_cast<int>(i));
                }
                if (ImGui::MenuItem("Make Song Using",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document &&
                                        !inst.is_alias))
                {
                    m_selected_instrument = static_cast<int>(i);
                    m_selected_empty_slot = false;
                    (void)MakeSongUsingInstrumentAt(static_cast<int>(i));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document && !inst.is_alias))
                {
                    OpenRenameDialog("instrument", static_cast<int>(i), inst.name);
                }
                if (ImGui::MenuItem("Cut",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document))
                {
                    m_selected_instrument = static_cast<int>(i);
                    m_multi_instruments.insert(static_cast<int>(i));
                    CutInstrumentsSelection();
                }
                if (ImGui::MenuItem("Copy Instrument",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document))
                {
                    m_selected_instrument = static_cast<int>(i);
                    m_multi_instruments.insert(static_cast<int>(i));
                    BeginCopyInstrumentsForClip();
                }
                if (ImGui::MenuItem("Paste", nullptr, false, CanPasteInstruments()))
                {
                    PasteInstrumentsSelection();
                }
                if (ImGui::MenuItem("Select All"))
                {
                    SessionSelectAllVisible();
                }
                if (ImGui::MenuItem("Create Alias...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document))
                {
                    OpenInstDestDialog(InstDestMode::Alias, static_cast<int>(i));
                }
                if (ImGui::MenuItem("Clone Instrument...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.from_song_document))
                {
                    OpenInstDestDialog(InstDestMode::Clone, static_cast<int>(i));
                }
                if (ImGui::MenuItem("Move Instrument...",
                                    nullptr,
                                    false,
                                    m_bank_token != 0 && !inst.is_alias && !inst.from_song_document))
                {
                    OpenInstDestDialog(InstDestMode::Move, static_cast<int>(i));
                }
                if (inst.is_alias)
                {
                    if (ImGui::MenuItem("Remove Alias", nullptr, false, m_bank_token != 0))
                    {
                        RemoveAliasAt(static_cast<int>(i));
                    }
                }
                else if (ImGui::MenuItem(inst.from_song_document ? "Delete Song Instrument" : "Move Instrument to Trash",
                                         nullptr,
                                         false,
                                         true))
                {
                    DeleteInstrumentAt(static_cast<int>(i));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Instrument...", nullptr, false, m_bank_token != 0))
                {
                    OpenAddInstrumentDialog();
                }
                if (ImGui::MenuItem("Show Empty Instrument Slots",
                                    nullptr,
                                    m_show_empty_instrument_slots,
                                    filter_is_group))
                {
                    m_show_empty_instrument_slots = !m_show_empty_instrument_slots;
                }
                ImGui::EndPopup();
            }
        }

        if (m_show_empty_instrument_slots && filter_is_group && !empty_programs.empty())
        {
            const int bank = active_filter->bank;
            for (int p : empty_programs)
            {
                char label[128];
                std::snprintf(label, sizeof(label), "B%dP%03d  (empty)", bank, p);
                const bool selected =
                    m_selected_empty_slot && m_selected_bank == bank && m_selected_program == p;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
                if (ImGui::Selectable(label, selected))
                {
                    m_selected_instrument = -1;
                    m_selected_empty_slot = true;
                    m_selected_bank = bank;
                    m_selected_program = p;
                }
                ImGui::PopStyleColor();
                char empty_popup[64];
                std::snprintf(empty_popup, sizeof(empty_popup), "inst_empty_ctx_%d_%d", bank, p);
                if (ImGui::BeginPopupContextItem(empty_popup, ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::MenuItem("Add Instrument...", nullptr, false, m_bank_token != 0))
                    {
                        m_selected_instrument = -1;
                        m_selected_empty_slot = true;
                        m_selected_bank = bank;
                        m_selected_program = p;
                        OpenAddInstrumentDialog();
                    }
                    if (ImGui::MenuItem("Show Empty Instrument Slots",
                                        nullptr,
                                        m_show_empty_instrument_slots))
                    {
                        m_show_empty_instrument_slots = !m_show_empty_instrument_slots;
                    }
                    ImGui::EndPopup();
                }
            }
        }

        if (ImGui::BeginPopupContextWindow("inst_list_bg_ctx",
                                           ImGuiPopupFlags_MouseButtonRight |
                                               ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Paste", nullptr, false, CanPasteInstruments()))
            {
                PasteInstrumentsSelection();
            }
            if (ImGui::MenuItem("Select All", nullptr, false, !m_instruments.empty()))
            {
                SessionSelectAllVisible();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Add Instrument...", nullptr, false, m_bank_token != 0))
            {
                OpenAddInstrumentDialog();
            }
            if (ImGui::MenuItem("Show Empty Instrument Slots",
                                nullptr,
                                m_show_empty_instrument_slots,
                                filter_is_group))
            {
                m_show_empty_instrument_slots = !m_show_empty_instrument_slots;
            }
            ImGui::EndPopup();
        }

    }

    bool SelectedInstrumentIsPercussion() const
    {
        if (m_instrument_editor_open)
        {
            return (m_ie_inst_id & 0x80u) != 0u;
        }
        if (m_selected_instrument >= 0 &&
            m_selected_instrument < static_cast<int>(m_instruments.size()))
        {
            return m_instruments[static_cast<size_t>(m_selected_instrument)].percussion;
        }
        for (const InstrumentRow &inst : m_instruments)
        {
            if (inst.bank == m_selected_bank && inst.program == m_selected_program)
            {
                return inst.percussion;
            }
        }
        /* Empty slot under a Percussion filter group. */
        if (m_selected_empty_slot && m_instrument_filter_index >= 0 &&
            m_instrument_filter_index < static_cast<int>(m_instrument_filters.size()))
        {
            return m_instrument_filters[static_cast<size_t>(m_instrument_filter_index)].category == 1;
        }
        return false;
    }

    /* GM channel 10 (index 9) for percussion; otherwise ch 1 (index 0). */
    unsigned char AuditionMidiChannel() const
    {
        return SelectedInstrumentIsPercussion() ? static_cast<unsigned char>(9)
                                                : static_cast<unsigned char>(0);
    }

    /* On ch10 the engine maps note→drum INST. For audition we always fire the
     * selected percussion slot's note so every VKBD key plays that one drum. */
    unsigned char AuditionPlayNote(int vkbd_note, int program) const
    {
        if (SelectedInstrumentIsPercussion())
        {
            int drum = program;
            if (drum < 0)
            {
                drum = 0;
            }
            if (drum > 127)
            {
                drum = 127;
            }
            return static_cast<unsigned char>(drum);
        }
        if (vkbd_note < 0)
        {
            vkbd_note = 0;
        }
        if (vkbd_note > 127)
        {
            vkbd_note = 127;
        }
        return static_cast<unsigned char>(vkbd_note);
    }

    void RememberAuditionSoundingNote(int vkbd_note, unsigned char play_note)
    {
        if (vkbd_note >= 0 && vkbd_note < 128)
        {
            m_vkbd_sounding_note[static_cast<size_t>(vkbd_note)] = static_cast<int>(play_note);
        }
    }

    void NoteOn(int midi_note, unsigned char velocity = 127)
    {
        if (velocity == 0)
        {
            NoteOff(midi_note);
            return;
        }
        if (m_instrument_editor_open)
        {
            EnsureBankAuditionSong();
        }
        BAESong audition_song = GetAuditionSong();
        if (!audition_song)
        {
            return;
        }

        const int bank = m_instrument_editor_open ? m_ie_bank : m_selected_bank;
        const int program = m_instrument_editor_open ? m_ie_program : m_selected_program;
        const bool is_perc = SelectedInstrumentIsPercussion();
        const unsigned char ch = AuditionMidiChannel();
        /* GM perc bank uses note as INST; keep program 0 so NoteOnWithLoad does
         * not add program+note into the wrong instrument id. */
        const unsigned char prog_for_channel =
            is_perc ? static_cast<unsigned char>(0) : static_cast<unsigned char>(program);
        const unsigned char play_note = AuditionPlayNote(midi_note, program);
        RememberAuditionSoundingNote(midi_note, play_note);

        BAESong_ProgramBankChange(audition_song,
                      ch,
                      prog_for_channel,
                      static_cast<unsigned char>(bank),
                      0);
        /* Load by explicit patch id — do not rely on NoteOnWithLoad reading
         * queued ProgramBankChange (paused audition song + mel↔perc moves). */
        const uint32_t patch_id =
            m_instrument_editor_open
                ? InstrumentEditorPatchId()
                : BankInstIdFromSlot(bank, is_perc, program);
        if (m_bank_token)
        {
            XFileUseThisResourceFile(reinterpret_cast<XFILE>(m_bank_token));
        }
        if (m_instrument_editor_open)
        {
            /* Inst ID 0 is valid (B0P000). Load + live-patch, then NoteOn (not
             * WithLoad) so ADSR edits are audible. Always ProgramBankChange on
             * the song that actually receives the note. */
            BAEResult load_r =
                BAESong_LoadInstrument(audition_song, static_cast<BAE_INSTRUMENT>(patch_id));
            /* Document-only INST (not yet in bank): fall back to playback song. */
            if (load_r != BAE_NO_ERROR && m_song && audition_song != m_song)
            {
                audition_song = m_song;
                BAESong_ProgramBankChange(audition_song,
                                          ch,
                                          prog_for_channel,
                                          static_cast<unsigned char>(bank),
                                          0);
                load_r = BAESong_LoadInstrument(audition_song,
                                                static_cast<BAE_INSTRUMENT>(patch_id));
            }
            if (load_r == BAE_NO_ERROR)
            {
                (void)BAESong_PatchLoadedInstrumentExtInfo(audition_song,
                                                           static_cast<BAE_INSTRUMENT>(patch_id),
                                                           &m_ie_ext);
                BAESong_NoteOn(audition_song,
                               ch,
                               play_note,
                               velocity,
                               0);
            }
            else if (m_song)
            {
                /* Bank load failed: play through the RMF song (has embeds).
                 * Still live-patch so oscillator / ADSR edits are audible. */
                audition_song = m_song;
                BAESong_ProgramBankChange(audition_song,
                                          ch,
                                          prog_for_channel,
                                          static_cast<unsigned char>(bank),
                                          0);
                (void)BAESong_LoadInstrument(audition_song,
                                             static_cast<BAE_INSTRUMENT>(patch_id));
                (void)BAESong_PatchLoadedInstrumentExtInfo(audition_song,
                                                           static_cast<BAE_INSTRUMENT>(patch_id),
                                                           &m_ie_ext);
                BAESong_NoteOn(audition_song,
                               ch,
                               play_note,
                               velocity,
                               0);
            }
            m_ie_note_song = audition_song;
        }
        else
        {
            BAEResult load_r =
                BAESong_LoadInstrument(audition_song, static_cast<BAE_INSTRUMENT>(patch_id));
            if (load_r != BAE_NO_ERROR)
            {
                char buf[160];
                std::snprintf(buf,
                              sizeof(buf),
                              "Can't load instrument id %u for audition (%s)",
                              patch_id,
                              FormatBAEError(load_r).c_str());
                SetStatus(buf);
            }
            BAESong_NoteOn(audition_song,
                           ch,
                           play_note,
                           velocity,
                           0);
            m_ie_note_song = nullptr;
        }
    }

    void NoteOff(int midi_note)
    {
        BAESong audition_song = m_ie_note_song ? m_ie_note_song : GetAuditionSong();
        if (!audition_song)
        {
            return;
        }
        unsigned char play_note = static_cast<unsigned char>(
            (midi_note < 0) ? 0 : (midi_note > 127 ? 127 : midi_note));
        if (midi_note >= 0 && midi_note < 128)
        {
            const int remembered = m_vkbd_sounding_note[static_cast<size_t>(midi_note)];
            if (remembered >= 0 && remembered <= 127)
            {
                play_note = static_cast<unsigned char>(remembered);
            }
            m_vkbd_sounding_note[static_cast<size_t>(midi_note)] = -1;
        }
        /* Clear both audition channels so switching melodic↔drums can't hang notes. */
        const unsigned char channels[2] = {0, 9};
        for (unsigned char ch : channels)
        {
            BAESong_NoteOff(audition_song, ch, play_note, 0, 0);
            if (play_note != static_cast<unsigned char>(midi_note) && midi_note >= 0 && midi_note <= 127)
            {
                BAESong_NoteOff(audition_song,
                                ch,
                                static_cast<unsigned char>(midi_note),
                                0,
                                0);
            }
        }
        /* Also clear the other song in case IE fell back mid-session. */
        if (m_instrument_editor_open)
        {
            if (m_preview_song && m_preview_song != audition_song)
            {
                for (unsigned char ch : channels)
                {
                    BAESong_NoteOff(m_preview_song, ch, play_note, 0, 0);
                }
            }
            if (m_song && m_song != audition_song)
            {
                for (unsigned char ch : channels)
                {
                    BAESong_NoteOff(m_song, ch, play_note, 0, 0);
                }
            }
        }
    }

    void PanicAuditionNotes()
    {
        if (m_midi_record_take_open || m_midi_record_armed)
        {
            MeFinishRecordTake();
        }
        MeStopRecordFreeRun();
        m_midi_record_held_start.fill(-1);
        m_mouse_active_note = -1;
        m_key_mouse_held.fill(false);
        m_hw_note_held.fill(false);
        for (auto &ch_notes : m_hw_full_ch_note_held)
        {
            ch_notes.fill(false);
        }
        m_keyboard_note_target.fill(KeyboardInputTarget::None);
        m_pc_key_note.fill(-1);
        m_key_active_until_ms.fill(0);
        m_vkbd_sounding_note.fill(-1);
        /* Hard-kill (not AllNotesOff): IE live ADSR often has long release. */
        if (m_ie_note_song)
        {
            BAESong_KillActiveNotes(m_ie_note_song);
        }
        if (m_preview_song)
        {
            BAESong_KillActiveNotes(m_preview_song);
        }
        if (m_song)
        {
            BAESong_KillActiveNotes(m_song);
        }
        m_ie_note_song = nullptr;
    }

private:
    bool m_bae_initialized = false;
    BAEMixer m_mixer = nullptr;
    BAESong m_song = nullptr;
    BAESong m_preview_song = nullptr;
    BAESong m_ie_note_song = nullptr; /* song that received the last IE NoteOn */
    BAERmfEditorDocument *m_document = nullptr;
    BAEBankToken m_bank_token = 0;

    std::vector<SampleRow> m_samples;
    std::vector<InstrumentRow> m_instruments;
    std::vector<InstrumentRow> m_bank_instruments;
    std::vector<InstrumentRow> m_song_override_instruments;
    std::vector<InstrumentFilterOption> m_instrument_filters;

    int m_selected_sample = -1;
    int m_selected_instrument = -1;
    int m_instrument_filter_index = -1; /* rebuilt → Custom Melodic (any bank) */
    SampleShowFilter m_sample_show_filter = SampleShowFilter::Custom;
    SampleListSort m_sample_list_sort = SampleListSort::ByName;
    char m_sample_text_filter[64] = {};
    InstrumentListSort m_instrument_list_sort = InstrumentListSort::ByProgram;
    char m_instrument_text_filter[64] = {};
    SongShowFilter m_song_show_filter = SongShowFilter::Custom;
    bool m_show_empty_instrument_slots = false;
    bool m_selected_empty_slot = false;
    int m_selected_bank = 0;
    int m_selected_program = 0;
    bool m_song_started = false;

    bool m_confirm_clear_cache_open = false;
    bool m_confirm_delete_pcm_open = false;
    bool m_confirm_replace_session_open = false;
    std::string m_pending_replace_session_path;
    bool m_confirm_rmf_open_open = false;
    std::string m_pending_rmf_open_path;
    bool m_confirm_quit_open = false;
    bool m_confirm_unload_bank_open = false;
    bool m_confirm_load_builtin_open = false;
    bool m_confirm_new_session_open = false;
    bool m_confirm_delete_song_open = false;
    bool m_confirm_delete_inst_aliases_open = false;
    std::vector<uint32_t> m_pending_delete_inst_ids;
    uint32_t m_pending_delete_inst_alias_count = 0;
    bool m_confirm_remove_orphan_aliases_open = false;
    uint32_t m_confirm_remove_orphan_aliases_count = 0;
    bool m_confirm_export_midi_open = false;
    std::vector<int> m_pending_delete_song_indices;
    bool m_pending_delete_song_trash_unused = true;
    size_t m_pending_delete_song_unused_inst = 0;
    size_t m_pending_delete_song_unused_snd = 0;
    bool m_resource_usage_open = false;
    std::string m_resource_usage_title;
    std::vector<std::string> m_resource_usage_lines;

    bool m_session_info_open = false;
    std::vector<std::string> m_recent_sessions;
    std::set<uint32_t> m_session_custom_snd_ids;
    /* Session-owned / RMF-promoted instruments at any bank (not only Bank 2+). */
    std::set<uint32_t> m_session_custom_inst_ids;

    SessionTab m_session_tab = SessionTab::Songs;
    bool m_session_window_focused = false;
    int m_pending_session_tab = -1; /* one-shot ImGuiTabItemFlags_SetSelected */
    bool m_scroll_to_selected_sample = false;
    SessionDropTarget m_dnd_hover_target = SessionDropTarget::None;
    bool m_bank01_touched = false;
    bool m_playback_stale = false;
    bool m_confirm_bank_merge_open = false;
    std::string m_pending_bank_merge_path;
    bool m_sample_editor_has_uncompressed_master = false;

    bool m_inst_dest_open = false;
    InstDestMode m_inst_dest_mode = InstDestMode::Alias;
    uint32_t m_inst_dest_source_index = 0;
    uint32_t m_inst_dest_source_inst_id = 0;
    uint32_t m_inst_dest_source_display_id = 0;
    int m_inst_dest_bank = 2;
    bool m_inst_dest_percussion = false;
    int m_inst_dest_program = 0;
    bool m_inst_dest_deep_clone = false;
    bool m_inst_dest_is_clip_package = false;
    bool m_inst_dest_clip_delete = false;
    int m_inst_dest_clip_inst_count = 0;
    std::string m_inst_dest_clip_path;
    bool m_inst_clipboard_valid = false;
    uint32_t m_inst_clipboard_inst_id = 0;
    std::string m_inst_clipboard_name;

    bool m_inst_move_remap_confirm_open = false;
    bool m_inst_move_remap_pending_apply = false;
    uint32_t m_inst_move_remap_note_count = 0;
    int m_inst_move_remap_old_bank = 0;
    int m_inst_move_remap_old_program = 0;
    bool m_inst_move_remap_old_percussion = false;
    int m_inst_move_remap_new_bank = 0;
    int m_inst_move_remap_new_program = 0;
    bool m_inst_move_remap_new_percussion = false;

    bool m_note_remap_open = false;
    int m_note_remap_src_bank = 0;
    int m_note_remap_src_program = 0;
    bool m_note_remap_src_percussion = false;
    int m_note_remap_dst_bank = 0;
    int m_note_remap_dst_program = 0;
    bool m_note_remap_dst_percussion = false;
    bool m_note_remap_scanned = false;
    uint32_t m_note_remap_count = 0;
    /* Set while Move cloned to dest but source delete is deferred until
     * the song-remap dialog is answered (Yes -> delete; No -> keep original).
     * INST id 0 is valid (B0P000); NONE means no pending move. */
    uint32_t m_inst_move_pending_delete_source_id = BAE_EDITOR_INST_ID_NONE;
    uint32_t m_inst_move_pending_dest_id = BAE_EDITOR_INST_ID_NONE;

    int m_sample_rate_hz = 44100;
    int m_sample_rate_index = 2;
    int m_peak_voices = 0;
    bool m_show_peak_voices = false;
    bool m_stereo = true;
    int m_reverb_type = static_cast<int>(BAE_REVERB_TYPE_1);
    int m_volume_percent = 100;
    int m_transpose_semitones = 0; /* song transpose; -24..24 */
    int m_velocity_curve = 1; /* 0..5 Beatnik velocity curve; default Peaky S */
    bool m_loop_enabled = true;

    std::array<bool, 16> m_channel_muted = {};
    int m_channel_solo = -1; /* -1 = none; 0..15 = solo that channel */
    std::array<float, 16> m_channel_vu = {};
    uint32_t m_channel_vu_last_tick_ms = 0;
    std::array<bool, 128> m_key_mouse_held = {};
    std::array<bool, 128> m_hw_note_held = {};
    /* Main-preview HW MIDI: held notes per channel (zefidi-style). */
    std::array<std::array<bool, 128>, 16> m_hw_full_ch_note_held = {};
    std::array<KeyboardInputTarget, 128> m_keyboard_note_target = {};
    std::array<uint32_t, 128> m_key_active_until_ms = {};

#if SUPPORT_MIDI_HW == TRUE
    bool m_midi_hw_enabled = false;
    bool m_midi_hw_open = false;
    int m_midi_hw_device_index = -1;
    bool m_prefs_draft_midi_hw_enabled = false;
    int m_prefs_draft_midi_hw_device_index = -1;
    std::vector<MidiHwDevice> m_midi_hw_devices;
    std::string m_midi_hw_open_device_name;
    uint32_t m_midi_hw_last_alive_check_ms = 0;
    uint32_t m_midi_hw_last_prefs_enum_ms = 0;
#endif
    /* VKBD key → actual MIDI note sent (percussion remaps every key to the drum slot). */
    std::array<int, 128> m_vkbd_sounding_note = [] {
        std::array<int, 128> notes{};
        notes.fill(-1);
        return notes;
    }();
    int m_mouse_active_note = -1;

    std::array<float, 128> m_scope_history = {};
    int m_scope_write_index = 0;
    uint32_t m_seek_min_us = 0;

#if NBEDITOR_MVP
    bool m_sample_editor_open = false;
    bool m_sample_editor_focused = false;
    bool m_sample_editor_is_song_sample = false;
    uint32_t m_sample_editor_document_sample_index = 0;
    int m_sample_editor_sample_row = -1;
    bool m_sample_editor_preview_needs_reencode = false;
    bool m_sample_editor_playing = false;
    int m_sample_editor_preview_note = 60;
    uint32_t m_sample_editor_play_started_ms = 0;
    uint32_t m_sample_editor_playhead_frame = 0;
    uint32_t m_sample_editor_frame_count = 0;
    uint32_t m_sample_editor_sample_rate_hz = 44100;
    uint32_t m_sample_editor_loop_start = 0;
    uint32_t m_sample_editor_loop_end = 0;
    int m_sample_editor_channels = 1;
    int m_sample_editor_bit_depth = 16;
    int m_sample_editor_codec_index = 0;
    BAERmfEditorCompressionType m_sample_editor_codec_type = BAE_EDITOR_COMPRESSION_PCM;
    BAERmfEditorSndStorageType m_sample_editor_snd_storage_type = BAE_EDITOR_SND_STORAGE_ESND;
    BAERmfEditorOpusMode m_sample_editor_opus_mode = BAE_EDITOR_OPUS_MODE_AUDIO;
    std::string m_sample_editor_sample_name;
    std::vector<float> m_sample_editor_wave_min;
    std::vector<float> m_sample_editor_wave_max;
    std::vector<unsigned char> m_sample_editor_cached_pcm;
    uint32_t m_sample_editor_cached_frame_count = 0;
    uint16_t m_sample_editor_cached_bit_size = 0;
    uint16_t m_sample_editor_cached_channels = 0;
    BAE_UNSIGNED_FIXED m_sample_editor_cached_sample_rate = 0;
    std::vector<unsigned char> m_sample_editor_original_pcm; /* in-memory uncompressed master */
    uint32_t m_sample_editor_original_frame_count = 0;
    uint16_t m_sample_editor_original_bit_size = 0;
    uint16_t m_sample_editor_original_channels = 0;
    BAE_UNSIGNED_FIXED m_sample_editor_original_sample_rate = 0;
    BAERmfEditorCompressionType m_sample_editor_original_codec_type = BAE_EDITOR_COMPRESSION_PCM;
    BAERmfEditorSndStorageType m_sample_editor_original_snd_storage_type = BAE_EDITOR_SND_STORAGE_ESND;
    BAERmfEditorOpusMode m_sample_editor_original_opus_mode = BAE_EDITOR_OPUS_MODE_AUDIO;
    std::vector<unsigned char> m_sample_editor_audition_pcm; /* compression preview decode only */
    uint32_t m_sample_editor_audition_frame_count = 0;
    uint16_t m_sample_editor_audition_bit_size = 0;
    uint16_t m_sample_editor_audition_channels = 0;
    BAE_UNSIGNED_FIXED m_sample_editor_audition_sample_rate = 0;
    bool m_sample_editor_loop_dirty = false;
    std::array<SePreviewVoice, kSePreviewVoiceCount> m_se_preview_voices{};
    int m_pc_keyboard_octave = 4;
    std::array<int, 512> m_pc_key_note = [] {
        std::array<int, 512> notes{};
        notes.fill(-1);
        return notes;
    }();

    bool m_sample_editor_dirty = false;
    bool m_sample_editor_loop_enabled = false;
    int m_sample_editor_root_key = 60;
    uint32_t m_sample_editor_snd_resource_id = kNoSndResourceId;
    std::string m_sample_editor_usage_text;
    int m_sample_editor_rate_preset = 1;
    uint32_t m_sample_editor_view_start = 0;
    uint32_t m_sample_editor_view_end = 0;
    float m_sample_editor_zoom_y = 1.0f;
    int m_sample_editor_sel_start = -1;
    int m_sample_editor_sel_end = -1;
    uint32_t m_sample_editor_fade_in_frames = 0;
    uint32_t m_sample_editor_fade_out_frames = 0;
    float m_sample_editor_gain = 1.0f;
    uint32_t m_sample_editor_compressed_bytes = 0;
    bool m_sample_editor_choose_note_open = false;
    bool m_sample_editor_compression_open = false;
    bool m_sample_editor_player20_compat = false;
    int m_sample_editor_comp_codec_index_on_open = 0;
    BAERmfEditorSndStorageType m_sample_editor_comp_storage_on_open = BAE_EDITOR_SND_STORAGE_ESND;
    BAERmfEditorOpusMode m_sample_editor_comp_opus_on_open = BAE_EDITOR_OPUS_MODE_AUDIO;
    bool m_sample_editor_comp_dirty_on_open = false;
    bool m_sample_editor_comp_preview_needs_on_open = false;
    bool m_sample_editor_snd_snapshot_valid = false;
    XResourceType m_sample_editor_snd_snapshot_type = ID_ESND;
    XLongResourceID m_sample_editor_snd_snapshot_id = 0;
    std::vector<unsigned char> m_sample_editor_snd_snapshot_data;
    std::string m_sample_editor_snd_snapshot_name;
    SeDragMode m_sample_editor_drag_mode = SeDragMode::None;
    uint32_t m_sample_editor_drag_anchor_frame = 0;
    uint32_t m_sample_editor_loop_start_at_drag = 0;
    uint32_t m_sample_editor_loop_end_at_drag = 0;
    float m_sample_editor_drag_start_mouse_x = 0.0f;
    uint32_t m_sample_editor_pan_view_start_at_drag = 0;
    std::deque<SampleEditorUndoState> m_sample_editor_undo;
    std::deque<SampleEditorUndoState> m_sample_editor_redo;
    SampleEditorUndoState m_sample_editor_pristine;
#endif

    char m_status[256] = {0};
    char m_path_input[1024] = {0};
    std::string m_loaded_song_path;
    std::string m_loaded_doc_path;
    std::string m_loaded_bank_path;
    std::string m_loaded_bank_display_name;

    SDL_Window *m_main_window = nullptr; // not owned
    SDL_Renderer *m_renderer = nullptr;  // not owned
    SDL_Mutex *m_dialog_mutex = nullptr;
    bool m_dialog_result_ready = false;
    bool m_dialog_error = false;
    std::string m_dialog_path;
    std::string m_dialog_error_message;
    DialogAction m_pending_dialog_action = DialogAction::OpenImport;
    LongOp m_long_op = LongOp::None;
    std::string m_long_op_path;

    bool m_busy_active = false;
    std::string m_busy_title;
    std::string m_busy_detail;
    float m_busy_fraction = 0.0f;
    uint32_t m_busy_last_pump_ms = 0;

    bool m_bank_add_open = false;
    BankAddMode m_bank_add_mode = BankAddMode::Instrument;
    int m_bank_add_bank = 2;
    bool m_bank_add_percussion = false;
    int m_bank_add_program = 0;
    int m_bank_add_root_key = 60;
    uint32_t m_bank_add_snd_id = kNoSndResourceId;
    int m_bank_add_alias_source_index = -1;
    char m_bank_add_name[256] = {0};

    ImFont *m_font_regular = nullptr;
    ImFont *m_font_italic = nullptr;

    SongRow m_song_row;
    std::vector<SessionSongEntry> m_session_songs;
    int m_session_song_index = -1;
    std::vector<GroovoidEntry> m_groovoids;
    int m_groovoid_index = -1;
    int m_pending_groovoid_export_index = -1;

#if USE_NEO_EFFECTS == TRUE
    bool m_custom_reverb_open = false;
    int m_custom_reverb_comb_count = 4;
    int m_custom_reverb_delays[NEO_CUSTOM_MAX_COMBS] = {50, 75, 100, 125};
    int m_custom_reverb_feedback[NEO_CUSTOM_MAX_COMBS] = {90, 90, 90, 90};
    int m_custom_reverb_gain[NEO_CUSTOM_MAX_COMBS] = {127, 127, 127, 127};
    int m_custom_reverb_lowpass = 64;
    int m_custom_reverb_mix = 110;
#endif
    int m_session_casd_count = 0;
    std::string m_loaded_session_path;
    std::map<uint32_t, SessionPcmCacheEntry> m_session_pcm_cache;
    std::map<uint32_t, std::vector<unsigned char>> m_app_compression_cache;
    /* RMF/ZMF export dialog: instruments selected for embedding / ghost. */
    bool m_export_rmf_open = false;
    bool m_export_rmf_prepare_pending = false;
    bool m_export_rmf_needs_zmf = false;
    uint32_t m_export_rmf_z_reason = 0;
    std::vector<ExportInstrumentItem> m_export_items;
    std::set<uint32_t> m_export_embed_inst_ids;
    std::set<uint32_t> m_export_ghost_inst_ids;
    /* Combo index matches BAERmfEditorSndStorageType (ESND/CSND/SND). */
    int m_export_snd_storage_index = static_cast<int>(BAE_EDITOR_SND_STORAGE_ESND);

    /* Editor Preferences (<configRoot>/nbeditor_prefs). */
    bool m_prefs_open = false;
    bool m_mod_import_prefs_open = false;
    bool m_about_open = false;
    bool m_tips_open = false;
    bool m_tips_startup_pending = false;
    int m_tips_startup_delay_frames = 0;
    int m_tips_index = 0;
    bool m_tips_dont_show_again = false;
    bool m_pref_show_tips_at_startup = true;
    bool m_prefs_draft_show_tips = true;
    bool m_prefs_draft_show_peak_voices = false;
    int m_prefs_draft_sample_rate_hz = 44100;
    int m_tips_next = 0; /* next tip index to show on startup (persisted) */
    std::vector<uint8_t> m_tips_seen;
    bool m_trash_open = false;
    int m_trash_selected = -1;
    bool m_confirm_empty_trash_open = false;
    bool m_confirm_empty_trash_popup = false;
    bool m_confirm_purge_trash_open = false;
    bool m_confirm_purge_trash_popup = false;
    int m_confirm_purge_trash_index = -1;
    bool m_pref_reopen_last_session = false;
    bool m_pref_save_session_layout = true;   /* write nBeT on Save Session */
    bool m_pref_ignore_session_layout = false; /* skip nBeT on Open Session */
    /* Opt-in: LZMA Pack on .zsn / LZSS CSND on .bsn at Save Session (slow). */
    bool m_pref_compress_session_on_save = false;
    /* App default for new sessions / ignore-layout loads. Session override below. */
    bool m_pref_default_undocked_layout = false;
    bool m_session_undocked_layout = false;
    bool m_floating_layout_initialized = false;
    /* Apply SetNextWindowPos/Size for Player/Session/Status for N frames. */
    int m_floating_placement_frames = 0;
    bool m_pref_adsr_default_valid = false;
    BAERmfEditorADSRInfo m_pref_adsr_default = {};
    std::string m_ie_adsr_default_focus_id;

    /* Session multi-select + clip transfer */
    std::set<int> m_multi_songs;
    std::set<int> m_multi_instruments;
    std::set<int> m_multi_samples;
    int m_multi_song_anchor = -1;
    int m_multi_instrument_anchor = -1;
    int m_multi_sample_anchor = -1;
    std::vector<SessionSongEntry> m_song_clipboard;
    std::vector<uint32_t> m_sample_clipboard_snd_ids;
    bool m_rename_open = false;
    char m_rename_buf[256] = {};
    std::string m_rename_kind;
    int m_rename_index = -1;
    std::vector<std::string> m_clip_published_paths;
    std::string m_clip_active_package_path;
    uint32_t m_clip_seq = 0;
    bool m_clip_include_samples_prompt = false;
    bool m_clip_cut_instruments_after_publish = false;
    std::vector<int> m_clip_pending_inst_list_indices;
    std::string m_clip_offer_uri;
    std::string m_clip_offer_path;
    bool m_prefs_draft_reopen = false;
    bool m_prefs_draft_save_layout = true;
    bool m_prefs_draft_ignore_layout = false;
    bool m_prefs_draft_compress_session = false;
    bool m_prefs_draft_default_undocked = false;
    /* Module import settings (mod2rmf) — live + dialog drafts. */
    bool m_pref_mod_ext_pitch = false;
    bool m_pref_mod_ext_adsr = true; /* ZMF import; preserve IT envelope detail */
    int m_pref_mod_resample_filter = static_cast<int>(MOD2RMF_RESAMPLE_SINC_8TAP);
    int m_pref_mod_resample_rate = 0; /* 0 = native */
    int m_pref_mod_amiga_filter = static_cast<int>(MOD2RMF_AMIGA_FILTER_NONE);
    int m_pref_mod_stereo_sep = 75;
    bool m_mod_import_draft_ext_pitch = false;
    bool m_mod_import_draft_ext_adsr = false;
    int m_mod_import_draft_resample_filter = static_cast<int>(MOD2RMF_RESAMPLE_SINC_8TAP);
    int m_mod_import_draft_resample_rate = 0;
    int m_mod_import_draft_amiga_filter = static_cast<int>(MOD2RMF_AMIGA_FILTER_NONE);
    int m_mod_import_draft_stereo_sep = 75;
    char m_pref_song_title[256] = {0};
    char m_pref_song_composer[256] = {0};
    char m_pref_song_copyright[256] = {0};
    char m_pref_song_performed[256] = {0};
    char m_pref_song_publisher[256] = {0};
    char m_pref_song_license_url[256] = {0};
    char m_pref_song_genre[256] = {0};
    char m_pref_song_subgenre[256] = {0};
    char m_pref_song_notes[512] = {0};

    /* nBeT session layout restore (applied before NewFrame, after a short delay). */
    bool m_pending_apply_nbet = false;
    int m_nbet_apply_delay_frames = 0;
    std::string m_pending_imgui_ini;
    bool m_force_reset_dock_layout = false;
    uint32_t m_nbet_restore_open_flags = 0;
    uint32_t m_nbet_restore_ie_inst_id = 0;
    uint32_t m_nbet_restore_ie_instrument_index = 0;
    int m_nbet_restore_ie_bank = 0;
    int m_nbet_restore_ie_program = 0;
    bool m_nbet_restore_ie_from_song = false;
    int m_nbet_restore_se_sample_row = -1;
    bool m_nbet_restore_se_is_song = false;
    bool m_nbet_restore_has_list_ui = false;
    int m_nbet_restore_inst_filter_bank = -2;
    int m_nbet_restore_inst_filter_category = 0;
    int m_nbet_restore_session_song_index = -1;

    /* Export Bank dialog options. */
    bool m_export_bank_open = false;
    bool m_export_bank_needs_zsb = false;
    uint32_t m_export_bank_zsb_reason = 0;
    bool m_export_bank_encrypt = true;
    bool m_export_bank_include_groovoids = false;
    bool m_export_bank_drop_unref = true; /* exclude unassigned SNDs from bank export */
    bool m_export_bank_songs_to_groovoids = false;

    /* Export as Audio dialog. */
    bool m_export_audio_open = false;
    int m_export_audio_codec_index = 0;

    /* Song Settings dialog. */
    bool m_song_settings_open = false;
    int m_song_settings_tempo_percent = 100;
    int m_song_settings_voices = 64;
    /* Sample list → Export Sample (native codec file). */
    int m_export_native_sample_row = -1;
    std::string m_export_native_sample_ext;
    /* Sample list → Export as RMF (temp doc only; does not mutate session). */
    bool m_export_sample_rmf_open = false;
    int m_export_sample_row = -1;
    int m_export_sample_bank = 2;
    bool m_export_sample_percussion = false;
    int m_export_sample_program = 0;
    /* Pending one-shot RMF after Save As path is chosen. */
    bool m_oneshot_export_pending = false;
    uint32_t m_oneshot_bank_instrument_index = 0; /* instrument export / assigned sample */
    uint32_t m_oneshot_sample_split_index = 0;
    uint32_t m_oneshot_snd_id = kNoSndResourceId;
    bool m_oneshot_sample_unassigned = false;
    bool m_oneshot_percussion = false;
    bool m_oneshot_from_sample = false; /* sample length note; else fixed 1s/2s */
    int m_oneshot_target_program = 0;
    int m_oneshot_note = 60;
    uint32_t m_oneshot_sample_frames = 0;
    uint32_t m_oneshot_sample_rate_hz = 0;
    std::string m_oneshot_title;
    bool m_document_dirty = false;
    bool m_song_used_keys_dirty = true;
    std::set<uint32_t> m_song_used_inst_ids;
    std::set<uint32_t> m_song_used_instrument_indexes;
    bool m_request_quit = false;

    /* MIDI Editor (GarageBand-style track timeline + piano roll). */
    bool m_midi_editor_open = false;
    bool m_midi_editor_focused = false;
    int m_midi_selected_track = -1;
    std::unordered_set<int> m_midi_selected_notes;
    int m_midi_primary_note = -1;
    float m_midi_scroll_tick = 0.0f;
    bool m_midi_hscroll_sync = true; /* push m_midi_scroll_tick into the H scrollbar */
    float m_midi_scroll_pitch = 40.0f; /* rows from top; ~C4 centered-ish */
    float m_midi_px_per_tick = 0.12f;
    float m_midi_px_per_pitch = 14.0f;
    float m_midi_track_lane_h = 48.0f; /* timeline lane row height */
    float m_midi_timeline_panel_h = 112.0f; /* ~ruler + 2 lanes; piano roll flexes */
    bool m_midi_snap_enabled = true;
    int m_midi_snap_division = 4;
    int m_midi_snap_division_index = 2;
    bool m_midi_follow_playhead = true;
    uint32_t m_midi_playhead_tick = 0;
    MeDragMode m_midi_drag_mode = MeDragMode::None;
    uint32_t m_midi_drag_origin_tick = 0;
    int m_midi_drag_origin_pitch = 60;
    int32_t m_midi_drag_tick_delta = 0;
    int m_midi_drag_pitch_delta = 0;
    int m_midi_eot_drag_track = -1;
    uint32_t m_midi_eot_drag_tick = 0;
    ImVec2 m_midi_marquee_a = ImVec2(0, 0);
    int m_midi_auto_drag_lane = -1;
    int m_midi_auto_drag_index = -1;
    bool m_midi_vel_undo_open = false;
    bool m_midi_note_bank_undo_open = false;
    bool m_midi_note_prog_undo_open = false;
    bool m_midi_record_armed = false;
    bool m_midi_record_take_open = false; /* undo begun for current take */
    bool m_midi_record_was_playing = false;
    bool m_midi_record_free_run = false; /* wall-clock record when Play is not running */
    uint32_t m_midi_record_clock_origin_tick = 0;
    uint32_t m_midi_record_clock_start_ms = 0;
    std::array<int32_t, 128> m_midi_record_held_start = [] {
        std::array<int32_t, 128> a{};
        a.fill(-1);
        return a;
    }();
    std::array<unsigned char, 128> m_midi_record_held_vel = {};
    std::array<int32_t, 128> m_midi_record_last_cc_tick = [] {
        std::array<int32_t, 128> a{};
        a.fill(-1);
        return a;
    }();
    std::array<int, 128> m_midi_record_last_cc_val = [] {
        std::array<int, 128> a{};
        a.fill(-1);
        return a;
    }();
    int32_t m_midi_record_last_pb_tick = -1;
    int m_midi_record_last_pb_val = -1;
    bool m_midi_metronome = false;
    int m_midi_count_in_bars = 1; /* 0 / 1 / 2 */
    bool m_midi_count_in_active = false;
    uint32_t m_midi_count_in_start_ms = 0;
    int m_midi_count_in_last_beat = -1;
    int m_midi_metro_last_song_beat = -1;
    BAESound m_midi_click_hi = nullptr;
    BAESound m_midi_click_lo = nullptr;
    std::vector<int16_t> m_midi_click_hi_pcm;
    std::vector<int16_t> m_midi_click_lo_pcm;
    /* Default: only Volume visible so the piano roll keeps most of the height. */
    std::array<bool, 16> m_midi_auto_lane_visible = {
        false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false};
    bool m_midi_auto_collapsed = false;
    float m_midi_auto_panel_h = 100.0f; /* user-resizable automation strip height */
    std::vector<bool> m_midi_track_muted;
    std::vector<bool> m_midi_track_solo;
    bool m_midi_note_cache_dirty = true;
    std::vector<BAERmfEditorNoteInfo> m_midi_cached_notes;
    /* Song length + track-lane overview — rebuilt on note/EOT/track edits only. */
    mutable bool m_midi_layout_cache_dirty = true;
    mutable uint32_t m_midi_cached_length_ticks = 0;
    mutable std::vector<std::vector<MeOverviewStub>> m_midi_overview;
    /* MePlayheadTick() memo for the current ImGui frame. */
    mutable int m_midi_playhead_sample_frame = -1;
    mutable uint32_t m_midi_playhead_sampled = 0;
    /* Automation lane node cache (per selected track). */
    mutable int m_midi_auto_cache_track = -1;
    mutable std::array<bool, 16> m_midi_auto_lane_dirty = {};
    mutable std::array<std::vector<MeAutoNode>, 16> m_midi_auto_nodes;
    std::deque<MeMidiUndoState> m_midi_undo;
    std::deque<MeMidiUndoState> m_midi_redo;
    MeMidiUndoState m_midi_undo_pending;
    bool m_midi_undo_pending_valid = false;
    bool m_midi_undo_restoring = false;
    std::vector<MeNoteClip> m_midi_note_clipboard;
    int m_midi_rename_track = -1;
    char m_midi_rename_buf[128] = {0};
    int m_midi_ctx_track = -1;
    uint32_t m_midi_ctx_tick = 0;
    bool m_midi_ctx_has_tick = false;
    bool m_midi_set_eot_confirm_open = false;
    bool m_midi_set_eot_is_song = false; /* false = one track, true = whole song */
    int m_midi_set_eot_track = -1;
    uint32_t m_midi_set_eot_tick = 0;
    uint32_t m_midi_set_eot_note_count = 0;

    /* Beatnik / NeoBAE (N)RPN manager (MIDI Editor). */
    bool m_midi_nrpn_open = false;
    int m_midi_nrpn_track = -1;
    int m_midi_nrpn_tick = 0;
    int m_midi_nrpn_preset = 1; /* NrpnChannelInstMode */
    int m_midi_nrpn_curve = 0;
    int m_midi_nrpn_mode = 3;
    int m_midi_nrpn_sample_offset = 0;
    int m_midi_nrpn_reverb = 1; /* BAE_REVERB_TYPE_1..19 */
    int m_midi_nrpn_custom_msb = 5;
    int m_midi_nrpn_custom_lsb = 0;
    int m_midi_nrpn_custom_data = 3;
    int m_midi_rpn_pb_semitones = 2;
    int m_midi_rpn_fine_14 = 8192;
    int m_midi_rpn_coarse = 64;
    int m_midi_rpn_custom_msb = 0;
    int m_midi_rpn_custom_lsb = 0;
    int m_midi_rpn_custom_data = 2;
    bool m_midi_nrpn_append_null = true;

    /* Karaoke lyric editor */
    bool m_lyrics_open = false;
    bool m_lyrics_tap_sync = false;
    bool m_lyrics_dirty = false;
    bool m_midi_lyrics_lane_visible = true;
    int m_lyrics_track = -1;
    int m_lyrics_sync_cursor = 0;
    char m_lyrics_text_buf[8192] = {0};
    std::vector<LyFragment> m_lyrics_fragments;
    std::unordered_set<int> m_lyrics_selected;
    bool m_lyrics_drag_active = false;
    bool m_lyrics_drag_undo_open = false;
    uint32_t m_lyrics_drag_origin_tick = 0;
    int32_t m_lyrics_drag_last_delta = 0;
    char m_lyrics_preview_current[256] = {0};
    char m_lyrics_preview_previous[256] = {0};
    size_t m_lyrics_preview_current_len = 0;
#if USE_LIB_HYPHEN == TRUE
    std::vector<LyHyphLang> m_hyph_langs;
    std::map<std::string, HyphenDict *> m_hyph_dicts;
    int m_lyrics_split_mode = LySplit_Words;
    char m_lyrics_hyph_lang[32] = "auto";
    bool m_hyph_langs_scanned = false;
    bool m_hyph_get_more_open = false;
    std::vector<LyHyphCatalogEntry> m_hyph_catalog;
    bool m_hyph_catalog_loaded = false;
    std::atomic<bool> m_hyph_dl_busy{false};
    std::string m_hyph_dl_status;
    std::mutex m_hyph_dl_mutex;
#endif

    bool m_song_info_open = false;
    char m_song_info_title[256] = {0};
    char m_song_info_composer[256] = {0};
    char m_song_info_copyright[256] = {0};
    char m_song_info_performed[256] = {0};
    char m_song_info_publisher[256] = {0};
    char m_song_info_license_url[256] = {0};
    char m_song_info_genre[256] = {0};
    char m_song_info_subgenre[256] = {0};
    char m_song_info_notes[512] = {0};
    int m_song_info_bpm = 120;
    int m_song_info_tpq = 480;
    bool m_song_info_loop_enabled = false;
    int m_song_info_loop_start = 0;
    int m_song_info_loop_end = 0;
    int m_song_info_loop_count = 0;
    int m_song_info_storage = 1;
    bool m_song_info_classic_chorus = false;
    bool m_song_info_pan_fix = false;
    bool m_song_info_apply_player_reverb = false;          /* Song Info → NRPN 7,0 */
    bool m_song_info_apply_player_velocity_curve = false; /* Song Info → NRPN 4,0 */

    bool m_instrument_editor_open = false;
    bool m_instrument_editor_focused = false;
    int m_ie_page = 0;
    bool m_ie_dirty = false;
    /* True after a live IE write pushed OSCL into bank/doc (or instrument
     * opened already using a generator). Lets toggle-off / Revert / Close
     * persist useOscillator=false so the bank is not left stuck on. */
    bool m_ie_live_osc_persisted = false;
    bool m_confirm_ie_close_open = false;
    bool m_ie_close_after_apply = false;
    bool m_ie_from_song = false;
    uint32_t m_ie_inst_id = 0;
    uint32_t m_ie_instrument_index = 0;
    int m_ie_bank = 0;
    int m_ie_program = 0;
    int m_ie_selected_split = -1;
    BAERmfEditorInstrumentExtInfo m_ie_ext = {};
    BAERmfEditorInstrumentExtInfo m_ie_ext_pristine = {};
    BAERmfEditorInstrumentExtInfo m_ie_ext_pre_edit = {};
    /* Bank GetInstrumentExtInfo returns displayName pointing at a locals buffer.
     * Keep a stable copy for the IE lifetime (keyboard/stack reuse was ???-corrupting it). */
    std::string m_ie_display_name;
    std::deque<BAERmfEditorInstrumentExtInfo> m_ie_undo;
    std::deque<BAERmfEditorInstrumentExtInfo> m_ie_redo;
    bool m_ie_undo_pushed_for_gesture = false;
    int m_ie_adsr_selected_stage = -1;
    ImGuiID m_ie_adsr_selected_owner = 0;
    /* Per-LFO UI: ADSR-only / Wave-only / Both (explicit). Not persisted in INST. */
    int m_ie_lfo_control_mode[BAE_EDITOR_MAX_LFOS] = {};

    bool m_dock_layout_initialized = false;

public:
    bool WantsQuit() const { return m_request_quit; }
    void RequestQuit() { RequestQuitConfirm(); }
};

} // namespace

int main(int argc, char **argv)
{
#if NBEDITOR
    NbEditorInstallCrashHandler();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    char versionString[128];
#if _DEBUG == TRUE
    const char *debug_suffix = " (pre-release debug build)";
#else
    const char *debug_suffix = "";
#endif
    std::snprintf(versionString, sizeof(versionString), "NeoBAE Editor - %s - %s%s", BAE_GetCurrentCPUArchitecture(), _VERSION, debug_suffix);
    SDL_Window *window = SDL_CreateWindow(versionString, static_cast<int>(1280 * main_scale), static_cast<int>(820 * main_scale), window_flags);
    if (!window)
    {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        std::printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    // Window stays hidden until after engine Init() succeeds.

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    /* Session layout lives in nBeT; do not auto-load/save imgui.ini. */
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    NeoBaeTheme::ApplyImGuiStyle();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    const float font_size = 14.0f * main_scale;
    ImFont *font_regular = nullptr;
    ImFont *font_italic = nullptr;

#if defined(NBEDITOR_EMBED_FONTS)
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false; /* static const embed blobs */
        font_regular = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char *>(liberation_sans_regular_data),
            static_cast<int>(liberation_sans_regular_size),
            font_size,
            &cfg);
        ImFontConfig cfg_italic;
        cfg_italic.FontDataOwnedByAtlas = false;
        font_italic = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char *>(liberation_sans_italic_data),
            static_cast<int>(liberation_sans_italic_size),
            font_size,
            &cfg_italic);
    }
#else
    /* ImGui asserts if AddFontFromFileTTF cannot open the file — probe first. */
    auto try_font = [&](const char *path) -> ImFont * {
        if (!path || !path[0])
        {
            return nullptr;
        }
        FILE *fp = std::fopen(path, "rb");
        if (!fp)
        {
            return nullptr;
        }
        std::fclose(fp);
        return io.Fonts->AddFontFromFileTTF(path, font_size);
    };
    font_regular = try_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    if (!font_regular)
    {
        font_regular = try_font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }
#if defined(_WIN32)
    if (!font_regular)
    {
        font_regular = try_font("C:\\Windows\\Fonts\\segoeui.ttf");
    }
#endif
    /* Prefer plain italic — never BoldOblique (aliases should not look bold). */
    font_italic = try_font("/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf");
    if (!font_italic)
    {
        font_italic = try_font("/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf");
    }
#if defined(_WIN32)
    if (!font_italic)
    {
        font_italic = try_font("C:\\Windows\\Fonts\\segoeuiz.ttf");
    }
#endif
#endif

    if (!font_regular)
    {
        font_regular = io.Fonts->AddFontDefault();
    }
    io.FontDefault = font_regular;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    NbEditorApp app;
    app.SetMainWindow(window);
    app.SetRenderer(renderer);
    app.SetUiFonts(font_regular, font_italic);
    if (!app.Init())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "NeoBAE nbeditor",
                                 app.GetStatus(),
                                 nullptr);
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    (void)app.OpenCommandLineArgs(argc, argv);

    SDL_ShowWindow(window);

    /* Windows (and some other hosts) run a modal move/size loop inside
     * SDL_PollEvent that never returns until the drag ends — freezing UI and
     * MIDI-in. SDL fires SDL_EVENT_WINDOW_EXPOSED from a timer during that
     * loop; handle it via an event watch so we keep iterating.
     * Maximize/restore are one-shot size snaps (PIXEL_SIZE_CHANGED etc.);
     * redraw those from the watch too so the first painted frame matches.
     * See https://wiki.libsdl.org/SDL3/AppFreezeDuringDrag */
    struct ModalLoopFrameCtx
    {
        NbEditorApp *app = nullptr;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        ImGuiIO *io = nullptr;
        bool *done = nullptr;
        int frame_depth = 0;
        Uint64 last_watch_frame_ns = 0;
    };

    bool done = false;
    ModalLoopFrameCtx frame_ctx;
    frame_ctx.app = &app;
    frame_ctx.window = window;
    frame_ctx.renderer = renderer;
    frame_ctx.io = &io;
    frame_ctx.done = &done;

    auto run_nbeditor_frame = [](ModalLoopFrameCtx *ctx) {
        if (!ctx || !ctx->app || !ctx->window || !ctx->renderer || !ctx->io || !ctx->done)
        {
            return;
        }
        if (ctx->frame_depth > 0)
        {
            return;
        }
        if (SDL_GetWindowFlags(ctx->window) & SDL_WINDOW_MINIMIZED)
        {
            return;
        }

        ++ctx->frame_depth;

        /* Apply nBeT ImGui/dock ini before NewFrame — mid-frame load undocks windows. */
        ctx->app->PumpPendingNbetLayout();
        /* Session save / bank export (progress modal); must not run mid-DrawUI. */
        ctx->app->PumpPendingLongOp();
        /* Undocked sessions turn off DockingEnable so drag-dock previews stay gone. */
        ctx->app->SyncDockingConfigForLayoutMode();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ctx->app->DrawUI(); /* includes PollHardwareMidiInput */
        if (ctx->app->WantsQuit())
        {
            *ctx->done = true;
        }

        ImGui::Render();
        SDL_SetRenderScale(ctx->renderer,
                           ctx->io->DisplayFramebufferScale.x,
                           ctx->io->DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(ctx->renderer, 18, 22, 28, 255);
        SDL_RenderClear(ctx->renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ctx->renderer);
        SDL_RenderPresent(ctx->renderer);

        --ctx->frame_depth;
    };

    struct ModalLoopWatchPack
    {
        ModalLoopFrameCtx *ctx;
        void (*run_frame)(ModalLoopFrameCtx *);
    };
    ModalLoopWatchPack watch_pack{&frame_ctx, +run_nbeditor_frame};

    auto modal_loop_watch = [](void *userdata, SDL_Event *event) -> bool {
        if (!userdata || !event)
        {
            return true;
        }
        const Uint32 t = event->type;
        const bool size_snap = (t == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                                t == SDL_EVENT_WINDOW_RESIZED ||
                                t == SDL_EVENT_WINDOW_MAXIMIZED ||
                                t == SDL_EVENT_WINDOW_RESTORED);
        if (t != SDL_EVENT_WINDOW_EXPOSED && !size_snap)
        {
            return true;
        }

        auto *pack = static_cast<ModalLoopWatchPack *>(userdata);
        ModalLoopFrameCtx *ctx = pack->ctx;
        /* Maximize fires several size events at once — one frame is enough. */
        const Uint64 now = SDL_GetTicksNS();
        if (ctx->last_watch_frame_ns != 0 &&
            (now - ctx->last_watch_frame_ns) < 4000000ull)
        {
            return true;
        }
        ctx->last_watch_frame_ns = now;

        /* Let ImGui see PlatformRequestResize before NewFrame. */
        ImGui_ImplSDL3_ProcessEvent(event);

        /* Skip a VSync wait on big snaps so the first correct frame shows sooner. */
        if (size_snap)
        {
            SDL_SetRenderVSync(ctx->renderer, 0);
        }
        pack->run_frame(ctx);
        if (size_snap)
        {
            SDL_SetRenderVSync(ctx->renderer, 1);
        }
        return true;
    };
    SDL_AddEventWatch(modal_loop_watch, &watch_pack);

    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
            {
                app.RequestQuit();
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
            {
                app.RequestQuit();
            }
            if (event.type == SDL_EVENT_DROP_FILE)
            {
                const char *dropped = event.drop.data;
                if (dropped && dropped[0])
                {
                    app.HandleDroppedPath(dropped);
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
            {
                app.HandlePcKeyboardEvent(event);
            }
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        run_nbeditor_frame(&frame_ctx);
    }

    SDL_RemoveEventWatch(modal_loop_watch, &watch_pack);

    app.Shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
#else
    (void)argc;
    (void)argv;
    return 0;
#endif
}

#if defined(_WIN32) && !defined(NBEDITOR_NO_WINMAIN)
/* MinGW -Wl,--subsystem,windows entry: forward shell/Open-with args into main. */
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}
#endif
