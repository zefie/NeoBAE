/*
 * SDL2 replacement for SDL3's SDL_ShowOpenFileDialog / SDL_ShowSaveFileDialog.
 * Only compiled when NBEDITOR_USE_SDL2 is defined (see nbeditor_sdl_compat.h).
 *
 * Backends (same approach as zefidi's gui_dialogs.c):
 *   Windows : GetOpenFileNameA / GetSaveFileNameA
 *   macOS   : osascript
 *   others  : zenity, then kdialog, then yad
 * The dialog runs on a detached worker thread and invokes the callback from
 * that thread, like SDL3 does on several platforms.
 */
#ifdef NBEDITOR_USE_SDL2

#include "nbeditor_sdl_compat.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#else
#include <sys/wait.h>
#endif

namespace
{
struct Filter
{
    std::string name;
    std::vector<std::string> exts; /* empty => all files */
};

struct DialogRequest
{
    SDL_DialogFileCallback callback = nullptr;
    void *userdata = nullptr;
    bool save = false;
    std::string default_location;
    std::vector<Filter> filters;
};

std::vector<Filter> ParseFilters(const SDL_DialogFileFilter *filters, int n)
{
    std::vector<Filter> out;
    for (int i = 0; filters && i < n; ++i)
    {
        Filter f;
        f.name = filters[i].name ? filters[i].name : "";
        std::string pat = filters[i].pattern ? filters[i].pattern : "*";
        std::stringstream ss(pat);
        std::string tok;
        while (std::getline(ss, tok, ';'))
        {
            if (tok.empty())
            {
                continue;
            }
            if (tok == "*" || tok == "*.*")
            {
                f.exts.clear();
                break;
            }
            f.exts.push_back(tok);
        }
        out.push_back(std::move(f));
    }
    return out;
}

#ifndef _WIN32
std::string ShellQuote(const std::string &s)
{
    std::string r = "'";
    for (char c : s)
    {
        if (c == '\'')
        {
            r += "'\\''";
        }
        else
        {
            r += c;
        }
    }
    r += "'";
    return r;
}

/* Returns true if the command could be launched. 'path' empty => cancelled. */
bool RunChooser(const std::string &cmd, std::string &path)
{
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp)
    {
        return false;
    }
    char buf[4096];
    path.clear();
    if (fgets(buf, sizeof(buf), fp))
    {
        path = buf;
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
        {
            path.pop_back();
        }
    }
    /* 127 = command not found (from the shell). Anything else = tool ran. */
    const int status = pclose(fp);
    return !(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 127);
}
#endif

/* 1 = got a path, 0 = cancelled, -1 = no usable chooser */
int RunDialog(const DialogRequest &req, std::string &path)
{
#if defined(_WIN32)
    std::string filter_buf;
    for (const Filter &f : req.filters)
    {
        std::string spec;
        for (const std::string &e : f.exts)
        {
            if (!spec.empty())
            {
                spec += ';';
            }
            spec += "*." + e;
        }
        if (spec.empty())
        {
            spec = "*.*";
        }
        filter_buf += f.name + " (" + spec + ")";
        filter_buf.push_back('\0');
        filter_buf += spec;
        filter_buf.push_back('\0');
    }
    filter_buf.push_back('\0');

    char file[MAX_PATH] = {0};
    if (!req.default_location.empty())
    {
        std::strncpy(file, req.default_location.c_str(), sizeof(file) - 1);
    }
    OPENFILENAMEA ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter_buf.size() > 1 ? filter_buf.c_str() : nullptr;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY |
                (req.save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    const BOOL ok = req.save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (ok)
    {
        path = file;
        return 1;
    }
    return 0;
#elif defined(__APPLE__)
    std::string types;
    for (const Filter &f : req.filters)
    {
        for (const std::string &e : f.exts)
        {
            types += (types.empty() ? "" : ", ") + std::string("\"") + e + "\"";
        }
    }
    std::string script;
    if (req.save)
    {
        std::string name = req.default_location;
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos)
        {
            name = name.substr(slash + 1);
        }
        script = "POSIX path of (choose file name";
        if (!name.empty())
        {
            script += " default name \"" + name + "\"";
        }
        script += ")";
    }
    else
    {
        script = "POSIX path of (choose file";
        if (!types.empty())
        {
            script += " of type {" + types + "}";
        }
        script += ")";
    }
    const std::string cmd = "osascript -e " + ShellQuote(script) + " 2>/dev/null";
    return RunChooser(cmd, path) ? (path.empty() ? 0 : 1) : -1;
#else
    /* zenity */
    std::string z = "zenity --file-selection";
    if (req.save)
    {
        z += " --save --confirm-overwrite";
    }
    if (!req.default_location.empty())
    {
        z += " --filename=" + ShellQuote(req.default_location);
    }
    /* kdialog */
    std::string k = req.save ? "kdialog --getsavefilename " : "kdialog --getopenfilename ";
    k += ShellQuote(req.default_location.empty() ? "." : req.default_location) + " ";
    std::string kf;
    for (const Filter &f : req.filters)
    {
        std::string spec;
        for (const std::string &e : f.exts)
        {
            spec += (spec.empty() ? "" : " ") + std::string("*.") + e;
        }
        if (spec.empty())
        {
            spec = "*";
        }
        z += " --file-filter=" + ShellQuote(f.name + " | " + spec);
        kf += (kf.empty() ? "" : "\n") + spec + "|" + f.name;
    }
    k += ShellQuote(kf);
    z += " 2>/dev/null";
    k += " 2>/dev/null";
    std::string y = "yad --file-selection";
    if (req.save)
    {
        y += " --save --confirm-overwrite";
    }
    if (!req.default_location.empty())
    {
        y += " --filename=" + ShellQuote(req.default_location);
    }
    y += " 2>/dev/null";

    for (const std::string *cmd : {&z, &k, &y})
    {
        if (RunChooser(*cmd, path))
        {
            return path.empty() ? 0 : 1;
        }
    }
    return -1;
#endif
}

void DialogThread(DialogRequest req)
{
    std::string path;
    const int r = RunDialog(req, path);
    if (r < 0)
    {
        SDL_SetError("No GUI file chooser available (install zenity, kdialog or yad)");
        req.callback(req.userdata, nullptr, -1);
        return;
    }
    const char *list[2] = {r == 1 ? path.c_str() : nullptr, nullptr};
    req.callback(req.userdata, list, -1);
}

void StartExternal(DialogRequest req)
{
    std::thread(DialogThread, std::move(req)).detach();
}

void Launch(SDL_DialogFileCallback cb,
            void *ud,
            bool save,
            const SDL_DialogFileFilter *filters,
            int n,
            const char *def)
{
    if (!cb)
    {
        return;
    }
    DialogRequest req;
    req.callback = cb;
    req.userdata = ud;
    req.save = save;
    req.default_location = def ? def : "";
    req.filters = ParseFilters(filters, n);
    StartExternal(std::move(req));
}
} // namespace

void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback,
                            void *userdata,
                            SDL_Window *,
                            const SDL_DialogFileFilter *filters,
                            int nfilters,
                            const char *default_location,
                            bool)
{
    Launch(callback, userdata, false, filters, nfilters, default_location);
}

void SDL_ShowSaveFileDialog(SDL_DialogFileCallback callback,
                            void *userdata,
                            SDL_Window *,
                            const SDL_DialogFileFilter *filters,
                            int nfilters,
                            const char *default_location)
{
    Launch(callback, userdata, true, filters, nfilters, default_location);
}

#endif /* NBEDITOR_USE_SDL2 */
