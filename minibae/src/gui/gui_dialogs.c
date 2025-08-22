// gui_dialogs.c - GUI Dialogs

#include "gui_dialogs.h"
#include "gui_common.h"
#include "gui_widgets.h"
#include "gui_text.h"
#include "gui_theme.h"
#include "MiniBAE.h"
#include "BAE_API.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dialog state
bool g_show_rmf_info_dialog = false;
bool g_rmf_info_loaded = false;
char g_rmf_info_values[INFO_TYPE_COUNT][512];

bool g_show_about_dialog = false;
int g_about_page = 0;

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#ifdef SUPPORT_MIDI_HW
#include "rtmidi_c.h"
#include "gui_midi_hw.h"
#endif

// Forward declare the BAEGUI structure (defined in gui_main.old.c)
typedef struct
{
    BAEMixer mixer;
    BAESong song;
    BAESound sound;
    uint32_t song_length_us;
    bool song_loaded;
    bool is_audio_file;
    bool is_rmf_file;
    bool paused;
    bool is_playing;
    bool was_playing_before_export;
    bool loop_enabled_gui;
    bool loop_was_enabled_before_export;
    uint32_t position_us_before_export;
    bool audio_engaged_before_export;
    char loaded_path[1024];
    bool preserve_position_on_next_start;
    uint32_t preserved_start_position_us;
    bool song_finished;
    BAEBankToken bank_token;
    char bank_name[256];
    bool bank_loaded;
    char status_message[256];
    Uint32 status_message_time;
} BAEGUI;

// External globals
extern BAEGUI g_bae;
extern int g_window_h;

// Tooltip state
bool g_bank_tooltip_visible = false;
Rect g_bank_tooltip_rect;
char g_bank_tooltip_text[520];

bool g_file_tooltip_visible = false;
Rect g_file_tooltip_rect;
char g_file_tooltip_text[520];

// External references
extern bool g_exporting;
extern bool g_exportDropdownOpen;
extern int g_exportCodecIndex;
extern const char *g_exportCodecNames[];

#ifdef SUPPORT_MIDI_HW
extern bool g_midi_input_enabled;
extern bool g_midi_output_enabled;
extern bool g_midi_input_device_dd_open;
extern bool g_midi_output_device_dd_open;
extern int g_midi_input_device_index;
extern int g_midi_output_device_index;
extern int g_midi_input_device_count;
extern int g_midi_output_device_count;
extern char g_midi_device_name_cache[64][128];
extern bool g_master_muted_for_midi_out;
extern BAESong g_live_song;
#endif

// RMF info functions
const char *rmf_info_label(BAEInfoType t)
{
    switch (t)
    {
    case TITLE_INFO:
        return "Title";
    case PERFORMED_BY_INFO:
        return "Performed By";
    case COMPOSER_INFO:
        return "Composer";
    case COPYRIGHT_INFO:
        return "Copyright";
    case PUBLISHER_CONTACT_INFO:
        return "Publisher";
    case USE_OF_LICENSE_INFO:
        return "Use Of License";
    case LICENSED_TO_URL_INFO:
        return "Licensed URL";
    case LICENSE_TERM_INFO:
        return "License Term";
    case EXPIRATION_DATE_INFO:
        return "Expiration";
    case COMPOSER_NOTES_INFO:
        return "Composer Notes";
    case INDEX_NUMBER_INFO:
        return "Index Number";
    case GENRE_INFO:
        return "Genre";
    case SUB_GENRE_INFO:
        return "Sub-Genre";
    case TEMPO_DESCRIPTION_INFO:
        return "Tempo";
    case ORIGINAL_SOURCE_INFO:
        return "Source";
    default:
        return "Unknown";
    }
}

void rmf_info_reset(void)
{
    for (int i = 0; i < INFO_TYPE_COUNT; i++)
    {
        g_rmf_info_values[i][0] = '\0';
    }
    g_rmf_info_loaded = false;
}

void rmf_info_load_if_needed(void)
{
    if (!g_bae.is_rmf_file || !g_bae.song_loaded)
        return;
    if (g_rmf_info_loaded)
        return;
    // Iterate all known info types, fetch if fits
    for (int i = 0; i < INFO_TYPE_COUNT; i++)
    {
        BAEInfoType it = (BAEInfoType)i;
        char buf[512];
        buf[0] = '\0';
        if (BAEUtil_GetRmfSongInfoFromFile((BAEPathName)g_bae.loaded_path, 0, it, buf, sizeof(buf) - 1) == BAE_NO_ERROR)
        {
            // Only store if non-empty and printable
            if (buf[0] != '\0')
            {
                strncpy(g_rmf_info_values[i], buf, sizeof(g_rmf_info_values[i]) - 1);
                g_rmf_info_values[i][sizeof(g_rmf_info_values[i]) - 1] = '\0';
            }
        }
    }
    g_rmf_info_loaded = true;
}

// Platform file dialog abstraction
char *open_file_dialog(void)
{
#ifdef _WIN32
    char fileBuf[1024] = {0};
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "All Supported\0*.mid;*.midi;*.kar;*.rmf;*.wav;*.aif;*.aiff;*.au;*.mp2;*.mp3\0"
                      "MIDI Files\0*.mid;*.midi;*.kar\0"
                      "RMF Files\0*.rmf\0"
                      "Audio Files\0*.wav;*.aif;*.aiff;*.au;*.mp2;*.mp3\0"
                      "All Files\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = sizeof(fileBuf);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn))
    {
        size_t len = strlen(fileBuf);
        char *ret = (char *)malloc(len + 1);
        if (ret)
        {
            memcpy(ret, fileBuf, len + 1);
        }
        return ret;
    }
    return NULL;
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Open Media File' --file-filter='Supported Files | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' --file-filter='All Files | *' 2>/dev/null",
        "kdialog --getopenfilename . '*.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
        "yad --file-selection --title='Open Media File' 2>/dev/null",
        NULL};
    for (int i = 0; cmds[i]; ++i)
    {
        FILE *p = popen(cmds[i], "r");
        if (!p)
            continue;
        char buf[1024];
        if (fgets(buf, sizeof(buf), p))
        {
            pclose(p);
            // strip newline
            size_t l = strlen(buf);
            while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
                buf[--l] = '\0';
            if (l > 0)
            {
                char *ret = (char *)malloc(l + 1);
                if (ret)
                {
                    memcpy(ret, buf, l + 1);
                }
                return ret;
            }
        }
        else
        {
            pclose(p);
        }
    }
    BAE_PRINTF("No GUI file chooser available (zenity/kdialog/yad). Drag & drop still works for media and bank files.\n");
    return NULL;
#endif
}

// RMF Info dialog rendering
void render_rmf_info_dialog(SDL_Renderer *R, int mx, int my, bool mclick)
{
    if (!g_show_rmf_info_dialog || !g_bae.is_rmf_file)
        return;

    // Dim entire background first (drawn before dialog contents)
    SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 120} : (SDL_Color){0, 0, 0, 90};
    draw_rect(R, (Rect){0, 0, WINDOW_W, g_window_h}, dim);

    rmf_info_load_if_needed();

    int pad = 8;
    int lineH = 16;
    // Determine inner content width needed so the longest metadata line does not wrap (within limits)
    int minOuterW = 340; // previous base width
    int maxOuterW = WINDOW_W - 20;
    if (maxOuterW < minOuterW)
        maxOuterW = minOuterW; // keep on-screen
    int longestInner = 0;      // width without wrapping (text only)
    // Measure title too so dialog is never narrower than it
    int titleW = 0, titleH = 0;
    measure_text("RMF Metadata", &titleW, &titleH);
    if (titleW > longestInner)
        longestInner = titleW;
    for (int i = 0; i < INFO_TYPE_COUNT; i++)
    {
        if (g_rmf_info_values[i][0])
        {
            char tmp[1024];
            snprintf(tmp, sizeof(tmp), "%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
            int w = 0, h = 0;
            measure_text(tmp, &w, &h);
            if (w > longestInner)
                longestInner = w;
        }
    }
    // Convert inner width (text) to outer dialog width used by existing wrapping helpers
    // inner width passed to draw_wrapped_text is (dlgW - pad*2 - 8)
    int desiredOuterW = longestInner + pad * 2 + 8;
    if (desiredOuterW < minOuterW)
        desiredOuterW = minOuterW;
    if (desiredOuterW > maxOuterW)
        desiredOuterW = maxOuterW;
    int dlgW = desiredOuterW;

    // Now compute total wrapped lines for chosen width
    int totalLines = 0;
    for (int i = 0; i < INFO_TYPE_COUNT; i++)
    {
        if (g_rmf_info_values[i][0])
        {
            char tmp[1024];
            snprintf(tmp, sizeof(tmp), "%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
            int count = count_wrapped_lines(tmp, dlgW - pad * 2 - 8);
            if (count <= 0)
                count = 1;
            totalLines += count;
        }
    }
    if (totalLines == 0)
        totalLines = 1;                                // placeholder
    int dlgH = pad * 2 + 24 + totalLines * lineH + 10; // title + fields

    // If dialog would exceed window height, attempt one more widening (if possible) to reduce wrapping
    if (dlgH > g_window_h - 20 && dlgW < maxOuterW)
    {
        int extra = maxOuterW - dlgW; // try expanding to max
        int newDlgW = dlgW + extra;
        int newTotalLines = 0;
        for (int i = 0; i < INFO_TYPE_COUNT; i++)
        {
            if (g_rmf_info_values[i][0])
            {
                char tmp[1024];
                snprintf(tmp, sizeof(tmp), "%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                int count = count_wrapped_lines(tmp, newDlgW - pad * 2 - 8);
                if (count <= 0)
                    count = 1;
                newTotalLines += count;
            }
        }
        int newDlgH = pad * 2 + 24 + newTotalLines * lineH + 10;
        if (newDlgH < dlgH)
        { // only adopt if improves
            dlgW = newDlgW;
            totalLines = newTotalLines;
            dlgH = newDlgH;
        }
    }

    Rect dlg = {WINDOW_W - dlgW - 10, 10, dlgW, dlgH};
    // Theme-aware dialog background and border (keep slight translucency)
    SDL_Color dlgBg = g_panel_bg;
    dlgBg.a = 230;
    SDL_Color dlgBorder = g_panel_border;
    draw_rect(R, dlg, dlgBg);
    draw_frame(R, dlg, dlgBorder);

    // Title uses header color
    draw_text(R, dlg.x + 10, dlg.y + 8, "RMF Metadata", g_header_color);

    // Close button (simple X) styled with button colors so it fits theme
    Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 6, 16, 16};
    bool overClose = point_in(mx, my, closeBtn);
    SDL_Color cbg = overClose ? g_button_hover : g_button_base;
    draw_rect(R, closeBtn, cbg);
    draw_frame(R, closeBtn, g_button_border);
    // Nudge X up ~3px for better visual alignment
    draw_text(R, closeBtn.x + 4, closeBtn.y - 1, "X", g_button_text);
    if (mclick && overClose)
    {
        g_show_rmf_info_dialog = false;
    }

    // Render wrapped fields
    int y = dlg.y + 32;
    int rendered = 0;
    for (int i = 0; i < INFO_TYPE_COUNT; i++)
    {
        if (g_rmf_info_values[i][0])
        {
            char full[1024];
            snprintf(full, sizeof(full), "%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
            // Use theme text color for wrapped fields
            int drawn = draw_wrapped_text(R, dlg.x + 10, y, full, g_text_color, dlgW - pad * 2 - 8, lineH);
            y += drawn * lineH;
            rendered += drawn;
        }
    }
    if (rendered == 0)
    {
        SDL_Color placeholder = g_is_dark_mode ? (SDL_Color){160, 160, 170, 255} : (SDL_Color){100, 100, 100, 255};
        draw_text(R, dlg.x + 10, y, "(No metadata fields present)", placeholder);
    }

    // Clicking outside dialog (and not on its opener button) closes it
    Rect rmfOpener = {440, 215, 80, 22};
    if (mclick && !point_in(mx, my, dlg) && !point_in(mx, my, rmfOpener))
    {
        g_show_rmf_info_dialog = false;
    }
}

// About dialog rendering
void render_about_dialog(SDL_Renderer *R, int mx, int my, bool mclick)
{
    if (!g_show_about_dialog)
        return;

    SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 120} : (SDL_Color){0, 0, 0, 90};
    draw_rect(R, (Rect){0, 0, WINDOW_W, g_window_h}, dim);
    int dlgW = 560;
    int dlgH = 280;
    int pad = 10;
    Rect dlg = {(WINDOW_W - dlgW) / 2, (g_window_h - dlgH) / 2, dlgW, dlgH};
    SDL_Color dlgBg = g_panel_bg;
    dlgBg.a = 240;
    draw_rect(R, dlg, dlgBg);
    draw_frame(R, dlg, g_panel_border);
    draw_text(R, dlg.x + pad, dlg.y + 8, "About", g_header_color);
    // Close X (slightly larger for better hit/visibility)
    Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 6, 16, 16};
    bool overClose = point_in(mx, my, closeBtn);
    draw_rect(R, closeBtn, overClose ? g_button_hover : g_button_base);
    draw_frame(R, closeBtn, g_button_border);
    // Nudge X up ~3px for better visual alignment
    draw_text(R, closeBtn.x + 4, closeBtn.y - 1, "X", g_button_text);
    if (mclick && overClose)
    {
        g_show_about_dialog = false;
    }

    // About dialog content: paged. Page 0 = main info, Page 1 = credits/licenses
    // Navigation controls drawn bottom-right
    const char *cpuArch = BAE_GetCurrentCPUArchitecture();
    char *baeVersion = (char *)BAE_GetVersion();   /* malloc'd by engine */
    char *compInfo = (char *)BAE_GetCompileInfo(); /* malloc'd by engine */

    char line1[256];
    if (baeVersion && cpuArch)
        snprintf(line1, sizeof(line1), "miniBAE Player (%s) %s", cpuArch, baeVersion);
    else if (baeVersion)
        snprintf(line1, sizeof(line1), "miniBAE Player %s", baeVersion);
    else if (cpuArch)
        snprintf(line1, sizeof(line1), "miniBAE Player (%s)", cpuArch);
    else
        snprintf(line1, sizeof(line1), "miniBAE Player");

    char line2[256];
    if (compInfo && compInfo[0])
        snprintf(line2, sizeof(line2), "built with %s", compInfo);
    else
        line2[0] = '\0';

    int y = dlg.y + 40;
    // Page 0: main info
    if (g_about_page == 0)
    {
        // Make version text clickable and link to GitHub (commit or tree)
        int vw = 0, vh = 0;
        measure_text(line1, &vw, &vh);
        Rect verLinkRect = {dlg.x + pad, y, vw, vh > 0 ? vh : 14};
        bool overVerLink = point_in(mx, my, verLinkRect);
        SDL_Color verLinkCol = overVerLink ? g_accent_color : g_text_color;
        draw_text(R, verLinkRect.x, verLinkRect.y, line1, verLinkCol);
        if (overVerLink)
        {
            SDL_SetRenderDrawColor(R, verLinkCol.r, verLinkCol.g, verLinkCol.b, verLinkCol.a);
            SDL_RenderDrawLine(R, verLinkRect.x, verLinkRect.y + verLinkRect.h - 2, verLinkRect.x + verLinkRect.w, verLinkRect.y + verLinkRect.h - 2);
        }
        if (mclick && overVerLink)
        {
            const char *raw = baeVersion ? baeVersion : _VERSION;
            char url[256];
            url[0] = '\0';
            if (strstr(raw, "-dirty"))
            {
                size_t len = strlen(raw);
                if (len > 6)
                {
                    strncpy(url, raw, len - 6);
                    url[len - 6] = '\0';
                }
                else
                {
                    snprintf(url, sizeof(url), "%s", raw);
                }
            }
            else
            {
                snprintf(url, sizeof(url), "%s", raw);
            }
            if (strncmp(raw, "git-", 4) == 0)
            {
                const char *sha = raw + 4;
                char shortSha[64];
                int i = 0;
                while (sha[i] && sha[i] != '-' && i < (int)sizeof(shortSha) - 1)
                {
                    shortSha[i] = sha[i];
                    i++;
                }
                shortSha[i] = '\0';
                snprintf(url, sizeof(url), "https://github.com/zefie/miniBAE/commit/%s", shortSha);
            }
            else
            {
                snprintf(url, sizeof(url), "https://github.com/zefie/miniBAE/tree/%s", raw);
            }
            if (url[0])
            {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", url, url);
                system(cmd);
#endif
            }
        }
        y += 20;
        if (line2[0])
        {
            draw_text(R, dlg.x + pad, y, line2, g_text_color);
            y += 20;
        }
        draw_text(R, dlg.x + pad, y, "", g_text_color); /* spacer */
        y += 6;
        draw_text(R, dlg.x + pad, y, "(C) 2025 Zefie Networks", g_text_color);
        y += 18;
        const char *urls[] = {"https://www.soundmusicsys.com/", "https://github.com/zefie/miniBAE/", NULL};
        for (int i = 0; urls[i]; ++i)
        {
            const char *u = urls[i];
            int tw = 0, th = 0;
            measure_text(u, &tw, &th);
            Rect r = {dlg.x + pad, y, tw, th > 0 ? th : 14};
            bool over = point_in(mx, my, r);
            SDL_Color col = over ? g_accent_color : g_highlight_color;
            draw_text(R, r.x, r.y, u, col);
            if (over)
            {
                SDL_SetRenderDrawColor(R, col.r, col.g, col.b, col.a);
                SDL_RenderDrawLine(R, r.x, r.y + r.h - 2, r.x + r.w, r.y + r.h - 2);
            }
            if (mclick && over)
            {
                if (strncmp(u, "http", 4) == 0)
                {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", u, NULL, NULL, SW_SHOWNORMAL);
#else
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", u, u);
                    system(cmd);
#endif
                }
            }
            y += 18;
        }
    }
    // Page 1: credits/licenses (part 1)
    else if (g_about_page == 1)
    {
        draw_text(R, dlg.x + pad, y, "This software makes use of the following software:", g_text_color);
        y += 18;
        const char *credits_page1[] = {
            // miniBAE is obviously required
            "",
            "miniBAE",
            "Copyright (c) 2009 Beatnik, Inc All rights reserved.",
            "Original miniBAE source code available at:",
            "https://github.com/heyigor/miniBAE/",
            // SDL is also required for this GUI so no #ifdef is necessary
            "",
            "SDL2 & SDL2_ttf",
            "Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>",
            "https://www.libsdl.org/",
            "",
            NULL};
        for (int i = 0; credits_page1[i]; ++i)
        {
            const char *txt = credits_page1[i];
            if (strncmp(txt, "http", 4) == 0)
            {
                int tw = 0, th = 0;
                measure_text(txt, &tw, &th);
                Rect r = {dlg.x + pad + 8, y, tw, th > 0 ? th : 14};
                bool over = point_in(mx, my, r);
                SDL_Color col = over ? g_accent_color : g_highlight_color;
                draw_text(R, r.x, r.y, txt, col);
                if (over)
                {
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, col.a);
                    SDL_RenderDrawLine(R, r.x, r.y + r.h - 2, r.x + r.w, r.y + r.h - 2);
                }
                if (mclick && over)
                {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", txt, NULL, NULL, SW_SHOWNORMAL);
#else
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", txt, txt);
                    system(cmd);
#endif
                }
            }
            else
            {
                draw_text(R, dlg.x + pad + 8, y, txt, g_text_color);
            }
            y += 16;
            if (y > dlg.y + dlg.h - 36)
                break;
        }
    }

#if defined(USE_MPEG_DECODER) || defined(USE_MPEG_ENCODER) || defined(SUPPORT_MIDI_HW)
    // Page 2: credits/licenses (part 2)
    else if (g_about_page == 2)
    {
        draw_text(R, dlg.x + pad, y, "Additional credits and licenses:", g_text_color);
        y += 18;
        const char *credits_page2[] = {
#ifdef USE_MPEG_DECODER
            "",
            "minimp3",
            "Licensed under the CC0",
            "http://creativecommons.org/publicdomain/zero/1.0/",
#endif
#ifdef USE_MPEG_ENCODER
            "",
            "libmp3lame",
            "https://lame.sourceforge.io/",
#endif
#ifdef SUPPORT_MIDI_HW
            "",
            "RtMidi: realtime MIDI i/o C++ classes",
            "Copyright (c) 2003-2023 Gary P. Scavone",
            "https://github.com/thestk/rtmidi",
#endif
            NULL};
        for (int i = 0; credits_page2[i]; ++i)
        {
            const char *txt = credits_page2[i];
            if (strncmp(txt, "http", 4) == 0)
            {
                int tw = 0, th = 0;
                measure_text(txt, &tw, &th);
                Rect r = {dlg.x + pad + 8, y, tw, th > 0 ? th : 14};
                bool over = point_in(mx, my, r);
                SDL_Color col = over ? g_accent_color : g_highlight_color;
                draw_text(R, r.x, r.y, txt, col);
                if (over)
                {
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, col.a);
                    SDL_RenderDrawLine(R, r.x, r.y + r.h - 2, r.x + r.w, r.y + r.h - 2);
                }
                if (mclick && over)
                {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", txt, NULL, NULL, SW_SHOWNORMAL);
#else
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", txt, txt);
                    system(cmd);
#endif
                }
            }
            else
            {
                draw_text(R, dlg.x + pad + 8, y, txt, g_text_color);
            }
            y += 16;
            if (y > dlg.y + dlg.h - 36)
                break;
        }
    }
#endif
    // Page navigation controls (bottom-right)
    Rect navPrev = {dlg.x + dlg.w - 70, dlg.y + dlg.h - 34, 24, 20};
    Rect navNext = {dlg.x + dlg.w - 34, dlg.y + dlg.h - 34, 24, 20};
    bool overPrev = point_in(mx, my, navPrev);
    bool overNext = point_in(mx, my, navNext);
    draw_rect(R, navPrev, overPrev ? g_button_hover : g_button_base);
    draw_frame(R, navPrev, g_button_border);
    draw_text(R, navPrev.x + 6, navPrev.y, "<", g_button_text);
    draw_rect(R, navNext, overNext ? g_button_hover : g_button_base);
    draw_frame(R, navNext, g_button_border);
    draw_text(R, navNext.x + 6, navNext.y, ">", g_button_text);
    // Page indicator
    char pg[32];
    snprintf(pg, sizeof(pg), "%d / %d", g_about_page + 1, 3);
    int pw = 0, ph = 0;
    measure_text(pg, &pw, &ph);
    draw_text(R, dlg.x + dlg.w - 100 - pw / 2, dlg.y + dlg.h - 32, pg, g_text_color);
    if (mclick)
    {
        if (overPrev && g_about_page > 0)
        {
            g_about_page--;
        }
        else if (overNext && g_about_page < 2)
        {
            g_about_page++;
        }
    }

    if (baeVersion)
        free(baeVersion);
    if (compInfo)
        free(compInfo);

    // Note: deliberately do NOT close About dialog when clicking outside to
    // avoid immediate close when the About button (outside the dialog) is clicked.
}

// Dialog initialization
void dialogs_init(void)
{
    g_show_rmf_info_dialog = false;
    g_rmf_info_loaded = false;
    g_show_about_dialog = false;
    g_about_page = 0;
    g_bank_tooltip_visible = false;
    g_file_tooltip_visible = false;
}

// Dialog cleanup
void dialogs_cleanup(void)
{
    g_show_rmf_info_dialog = false;
    g_show_about_dialog = false;
    g_bank_tooltip_visible = false;
    g_file_tooltip_visible = false;
}
