#ifndef GUI_DIALOGS_H
#define GUI_DIALOGS_H

#include "gui_common.h"
#include "MiniBAE.h"

// Dialog state globals
extern bool g_show_rmf_info_dialog;
extern bool g_rmf_info_loaded;
extern char g_rmf_info_values[INFO_TYPE_COUNT][512];

extern bool g_show_about_dialog;
extern int g_about_page;

// Tooltip state
extern bool g_bank_tooltip_visible;
extern Rect g_bank_tooltip_rect;
extern char g_bank_tooltip_text[520];

extern bool g_file_tooltip_visible;
extern Rect g_file_tooltip_rect;
extern char g_file_tooltip_text[520];

// Function declarations
const char *rmf_info_label(BAEInfoType t);
void rmf_info_reset(void);
void rmf_info_load_if_needed(void);

// Platform file dialogs
char *open_file_dialog(void);
char *open_folder_dialog(void); // For folder selection to add all files
char *open_playlist_dialog(void); // For M3U playlist files
char *save_playlist_dialog(void); // For saving M3U playlist files

// Dialog rendering functions
void render_rmf_info_dialog(SDL_Renderer *R, int mx, int my, bool mclick);
void render_about_dialog(SDL_Renderer *R, int mx, int my, bool mclick);

// Dialog system initialization/cleanup
void dialogs_init(void);
void dialogs_cleanup(void);

#ifdef _WIN32
// Windows file dialog functions
void show_open_file_dialog(char *result_path, int result_size, const char *filter, const char *title);
void show_save_file_dialog(char *result_path, int result_size, const char *filter, const char *title, const char *default_name);
#endif

#endif // GUI_DIALOGS_H
