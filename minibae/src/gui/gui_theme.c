// gui_theme.c - GUI theme management

#include "gui_theme.h"
#include <stdio.h>
#include "X_Assert.h"

// Theme globals - defined here, declared as extern in header
bool g_is_dark_mode = true;
SDL_Color g_accent_color = {50, 130, 200, 255};
SDL_Color g_text_color = {240, 240, 240, 255};
SDL_Color g_bg_color = {30, 30, 35, 255};
SDL_Color g_panel_bg = {45, 45, 50, 255};
SDL_Color g_panel_border = {80, 80, 90, 255};
SDL_Color g_header_color = {180, 200, 255, 255};
// A highlight color that reads well on both dark and light themes
SDL_Color g_highlight_color = {50, 130, 200, 255};
// Button colors
SDL_Color g_button_base = {70, 70, 80, 255};
SDL_Color g_button_hover = {90, 90, 100, 255};
SDL_Color g_button_press = {50, 50, 60, 255};
SDL_Color g_button_text = {250, 250, 250, 255};
SDL_Color g_button_border = {120, 120, 130, 255};

#ifdef _WIN32
WindowsTheme g_theme = {0};

bool get_registry_dword(HKEY hkey, const char *subkey, const char *value, DWORD *result)
{
    HKEY key;
    if (RegOpenKeyExA(hkey, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type, size = sizeof(DWORD);
    bool success = (RegQueryValueExA(key, value, NULL, &type, (BYTE *)result, &size) == ERROR_SUCCESS && type == REG_DWORD);
    RegCloseKey(key);
    return success;
}

void detect_windows_theme()
{
    // Default light theme
    g_theme.is_dark_mode = false;
    g_theme.is_high_contrast = false;
    g_theme.accent_color = (SDL_Color){0, 120, 215, 255}; // Default Windows blue
    g_theme.text_color = (SDL_Color){32, 32, 32, 255};
    g_theme.bg_color = (SDL_Color){248, 248, 248, 255};
    g_theme.panel_bg = (SDL_Color){255, 255, 255, 255};
    g_theme.border_color = (SDL_Color){200, 200, 200, 255};
    // Mirror to local theme globals for use by widgets
    g_is_dark_mode = g_theme.is_dark_mode;
    g_accent_color = g_theme.accent_color;
    g_text_color = g_theme.text_color;
    g_bg_color = g_theme.bg_color;
    g_panel_bg = g_theme.panel_bg;
    g_panel_border = g_theme.border_color;
    g_header_color = g_theme.accent_color;
    // Button colors for light mode
    if (!g_theme.is_dark_mode)
    {
        g_button_base = (SDL_Color){230, 230, 230, 255};
        g_button_hover = (SDL_Color){210, 210, 210, 255};
        g_button_press = (SDL_Color){190, 190, 190, 255};
        g_button_text = (SDL_Color){32, 32, 32, 255};
        g_button_border = (SDL_Color){160, 160, 160, 255};
    }

    DWORD value;

    // Check for dark mode (Windows 10/11)
    if (get_registry_dword(HKEY_CURRENT_USER,
                           "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                           "AppsUseLightTheme", &value))
    {
        g_theme.is_dark_mode = (value == 0);
    }

    // Check for high contrast mode
    if (get_registry_dword(HKEY_CURRENT_USER,
                           "Control Panel\\Accessibility\\HighContrast",
                           "Flags", &value))
    {
        g_theme.is_dark_mode = (value == 1);
    }
    // Check for high contrast mode
    if (get_registry_dword(HKEY_CURRENT_USER,
                           "Control Panel\\Accessibility\\HighContrast",
                           "Flags", &value))
    {
        g_theme.is_high_contrast = (value & 1);
    }

    // Get accent color
    if (get_registry_dword(HKEY_CURRENT_USER,
                           "Software\\Microsoft\\Windows\\DWM",
                           "AccentColor", &value))
    {
        // Windows stores color as AABBGGRR, we need RRGGBB
        g_theme.accent_color.r = (value >> 0) & 0xFF;
        g_theme.accent_color.g = (value >> 8) & 0xFF;
        g_theme.accent_color.b = (value >> 16) & 0xFF;
        g_theme.accent_color.a = 255;
    }

    // Adjust colors based on theme
    if (g_theme.is_dark_mode)
    {
        // Dark theme colors
        g_theme.text_color = (SDL_Color){240, 240, 240, 255};
        g_theme.bg_color = (SDL_Color){32, 32, 32, 255};
        g_theme.panel_bg = (SDL_Color){45, 45, 45, 255};
        g_theme.border_color = (SDL_Color){85, 85, 85, 255};
        g_is_dark_mode = true;
        g_accent_color = g_theme.accent_color;
        g_text_color = g_theme.text_color;
        g_bg_color = g_theme.bg_color;
        g_panel_bg = g_theme.panel_bg;
        g_panel_border = g_theme.border_color;
        g_header_color = (SDL_Color){180, 200, 255, 255};
        // Button colors for dark mode
        g_button_base = (SDL_Color){70, 70, 80, 255};
        g_button_hover = (SDL_Color){90, 90, 100, 255};
        g_button_press = (SDL_Color){50, 50, 60, 255};
        g_button_text = (SDL_Color){250, 250, 250, 255};
        g_button_border = (SDL_Color){120, 120, 130, 255};
    }
    else
    {
        g_accent_color = g_theme.accent_color;
    }

    if (g_theme.is_high_contrast)
    {
        // High contrast overrides
        g_theme.text_color = (SDL_Color){255, 255, 255, 255};
        g_theme.bg_color = (SDL_Color){0, 0, 0, 255};
        g_theme.panel_bg = (SDL_Color){0, 0, 0, 255};
        g_theme.border_color = (SDL_Color){255, 255, 255, 255};
        g_theme.accent_color = (SDL_Color){255, 255, 0, 255}; // Yellow for high contrast
    }

    // Compute a highlight color that is readable on both light and dark themes.
    // For dark mode prefer the header color (brighter, not the system accent) so
    // it stands out without relying on the accent. For light mode use a
    // darkened version of the accent so it contrasts against pale backgrounds.
    if (g_theme.is_high_contrast)
    {
        // High contrast needs a very strong readable highlight
        g_highlight_color = (SDL_Color){255, 255, 0, 255};
    }
    else if (g_theme.is_dark_mode)
    {
        // Use the header color in dark mode to remain readable and distinct
        g_highlight_color = g_header_color;
    }
    else
    {
        // Light mode: darken the accent for contrast against light panels
        g_highlight_color = g_accent_color;
        g_highlight_color.r = (Uint8)(g_accent_color.r > 80 ? g_accent_color.r - 80 : 0);
        g_highlight_color.g = (Uint8)(g_accent_color.g > 80 ? g_accent_color.g - 80 : 0);
        g_highlight_color.b = (Uint8)(g_accent_color.b > 80 ? g_accent_color.b - 80 : 0);
    }

    BAE_PRINTF("Windows theme detected: %s mode, accent: R%d G%d B%d\n",
               g_theme.is_dark_mode ? "dark" : "light",
               g_theme.accent_color.r, g_theme.accent_color.g, g_theme.accent_color.b);
}
#else
// Dummy theme detection for non-Windows
void detect_windows_theme()
{
    // Use default dark theme colors for non-Windows
}
#endif
