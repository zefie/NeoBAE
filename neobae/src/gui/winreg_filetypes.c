#define _WIN32_IE 0x0500
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <tchar.h>

// Link against necessary libraries
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// --- Configuration ---
#define APP_FILENAME    _T("zefidi.exe")
#define PROGID_PREFIX   _T("NeoBAE.Assoc")
#define APP_NAME        _T("NeoBAE")

// --- UI Constants ---
#define ID_LISTVIEW     1001
#define ID_BTN_APPLY    1002
#define ID_BTN_SELALL   1003
#define ID_LBL_STATUS   1004

// --- Data Structures ---
typedef struct {
    TCHAR* ext;
    int iconResId; // Resource ID in zefidi.exe
    TCHAR* desc;
} FileType;

FileType supportedTypes[] = {
    { _T(".mid"),  102, _T("MIDI Sequence") },
    { _T(".midi"), 102, _T("MIDI Sequence") },
    { _T(".kar"),  103, _T("MIDI Karaoke") },
    { _T(".rmf"),  104, _T("Rich Music Format") },
    { _T(".rmi"),  105, _T("RIFF MIDI") },
    { _T(".xmf"),  106, _T("Extensible Music Format") },
    { _T(".mxmf"), 107, _T("Mobile XMF") },
    { _T(".wav"),  108, _T("PCM WAV Audio") },
    { _T(".au"),   109, _T("Sun Audio") },
    { _T(".aiff"), 110, _T("AIFF Audio") },
    { _T(".aif"),  110, _T("AIFF Audio") },
    { _T(".flac"), 111, _T("FLAC Audio") },
    { _T(".ogg"),  112, _T("Ogg Vorbis") },
    { _T(".mp2"),  113, _T("MPEG-2 Audio") },
    { _T(".mp3"),  114, _T("MPEG-3 Audio") }
};

int typeCount = sizeof(supportedTypes) / sizeof(supportedTypes[0]);

// --- Global Variables ---
HWND hListView, hStatusLabel;
HINSTANCE hInst;

// --- Helper: Get path to zefidi.exe ---
BOOL GetZefidiPath(TCHAR* buffer, DWORD size) {
    if (GetModuleFileName(NULL, buffer, size) == 0) return FALSE;
    PathRemoveFileSpec(buffer); // Strip this helper's exe name
    PathAppend(buffer, APP_FILENAME); // Add zefidi.exe
    return PathFileExists(buffer); // Function provided by shlwapi (or we check attribs)
}

// --- Registry: Check if associated ---
BOOL IsAssociated(const TCHAR* ext) {
    TCHAR keyPath[512];
    HKEY hKey;
    TCHAR progId[256];
    DWORD dataSize = sizeof(progId);
    BOOL result = FALSE;

    // Check UserChoice (Windows 10/11 method)
    // HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.ext\UserChoice
    _stprintf_s(keyPath, 512, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoice"), ext);

    if (RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueEx(hKey, _T("ProgId"), NULL, NULL, (LPBYTE)progId, &dataSize) == ERROR_SUCCESS) {
            // Check if it contains our exe name (e.g., Applications\zefidi.exe)
            if (_tcsstr(progId, APP_FILENAME) != NULL) {
                result = TRUE;
            }
        }
        RegCloseKey(hKey);
    }

    // If not found in UserChoice, check the default association (legacy method)
    if (!result) {
        _stprintf_s(keyPath, 512, _T("Software\\Classes\\%s"), ext);
        if (RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            dataSize = sizeof(progId);
            if (RegQueryValueEx(hKey, NULL, NULL, NULL, (LPBYTE)progId, &dataSize) == ERROR_SUCCESS) {
                // Check if it starts with our prefix
                if (_tcsstr(progId, PROGID_PREFIX) == progId) {
                    result = TRUE;
                }
            }
            RegCloseKey(hKey);
        }
    }

    return result;
}

// --- Registry: Register ProgID and Association ---
BOOL RegisterFileType(const FileType* ft, const TCHAR* exePath) {
    HKEY hKey;
    TCHAR progId[256];
    TCHAR regPath[512];
    TCHAR val[512];

    // 1. Create ProgID: NeoBAE.Assoc.EXT
    _stprintf_s(progId, 256, _T("%s%s"), PROGID_PREFIX, ft->ext); // e.g. NeoBAE.Assoc.mid

    // Write to HKCU\Software\Classes (No Admin req for HKCU, usually)
    _stprintf_s(regPath, 512, _T("Software\\Classes\\%s"), progId);
    
    // Create ProgID Key
    if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        // Set Description
        RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)ft->desc, (_tcslen(ft->desc) + 1) * sizeof(TCHAR));
        
        // Set DefaultIcon
        HKEY hIconKey;
        if (RegCreateKeyEx(hKey, _T("DefaultIcon"), 0, NULL, 0, KEY_WRITE, NULL, &hIconKey, NULL) == ERROR_SUCCESS) {
            // Format: "path\zefidi.exe",-102
            _stprintf_s(val, 512, _T("\"%s\",-%d"), exePath, ft->iconResId);
            RegSetValueEx(hIconKey, NULL, 0, REG_SZ, (const BYTE*)val, (_tcslen(val) + 1) * sizeof(TCHAR));
            RegCloseKey(hIconKey);
        }

        // Set Open Command
        HKEY hShellKey;
        // shell\open\command
        if (RegCreateKeyEx(hKey, _T("shell\\open\\command"), 0, NULL, 0, KEY_WRITE, NULL, &hShellKey, NULL) == ERROR_SUCCESS) {
            // Format: "path\zefidi.exe" "%1"
            _stprintf_s(val, 512, _T("\"%s\" \"%%1\""), exePath);
            RegSetValueEx(hShellKey, NULL, 0, REG_SZ, (const BYTE*)val, (_tcslen(val) + 1) * sizeof(TCHAR));
            RegCloseKey(hShellKey);
        }
        RegCloseKey(hKey);
    } else {
        return FALSE;
    }

    // 2. Map Extension to ProgID (OpenWithProgids)
    // We modify HKCU\Software\Classes\.ext\OpenWithProgids
    _stprintf_s(regPath, 512, _T("Software\\Classes\\%s\\OpenWithProgids"), ft->ext);
    if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        // Add value: Name = ProgID, Value = empty
        RegSetValueEx(hKey, progId, 0, REG_NONE, NULL, 0);
        RegCloseKey(hKey);
    }
    
    // Also set the default key for the extension (Legacy fallback)
    _stprintf_s(regPath, 512, _T("Software\\Classes\\%s"), ft->ext);
    if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)progId, (_tcslen(progId) + 1) * sizeof(TCHAR));
        RegCloseKey(hKey);
    }

    return TRUE;
}

// --- Registry: Unregister ProgID and Association ---
BOOL UnregisterFileType(const FileType* ft) {
    HKEY hKey;
    TCHAR progId[256];
    TCHAR regPath[512];

    _stprintf_s(progId, 256, _T("%s%s"), PROGID_PREFIX, ft->ext);

    // Remove from OpenWithProgids
    _stprintf_s(regPath, 512, _T("Software\\Classes\\%s\\OpenWithProgids"), ft->ext);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, regPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValue(hKey, progId);
        RegCloseKey(hKey);
    }

    // Remove default association if it's ours
    _stprintf_s(regPath, 512, _T("Software\\Classes\\%s"), ft->ext);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, regPath, 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        TCHAR currentProgId[256];
        DWORD dataSize = sizeof(currentProgId);
        if (RegQueryValueEx(hKey, NULL, NULL, NULL, (LPBYTE)currentProgId, &dataSize) == ERROR_SUCCESS) {
            if (_tcscmp(currentProgId, progId) == 0) {
                // It's ours, remove it
                RegDeleteValue(hKey, NULL);
            }
        }
        RegCloseKey(hKey);
    }

    // Remove UserChoice if it's ours
    _stprintf_s(regPath, 512, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoice"), ft->ext);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        TCHAR currentProgId[256];
        DWORD dataSize = sizeof(currentProgId);
        if (RegQueryValueEx(hKey, _T("ProgId"), NULL, NULL, (LPBYTE)currentProgId, &dataSize) == ERROR_SUCCESS) {
            if (_tcsstr(currentProgId, APP_FILENAME) != NULL || _tcsstr(currentProgId, PROGID_PREFIX) == currentProgId) {
                // It's ours, delete the UserChoice key
                RegCloseKey(hKey);
                RegDeleteKey(HKEY_CURRENT_USER, regPath);
            } else {
                RegCloseKey(hKey);
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    // Remove UserChoiceLatest if it's ours
    _stprintf_s(regPath, 512, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoiceLatest"), ft->ext);
    TCHAR regPathFull[512];
    _stprintf_s(regPathFull, 512, _T("%s\\ProgID"), regPath);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, regPathFull, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        TCHAR currentProgId[256];
        DWORD dataSize = sizeof(currentProgId);
        if (RegQueryValueEx(hKey, _T("ProgId"), NULL, NULL, (LPBYTE)currentProgId, &dataSize) == ERROR_SUCCESS) {
            if (_tcsstr(currentProgId, PROGID_PREFIX) == currentProgId) {
                // It's ours, delete the UserChoiceLatest key
                RegCloseKey(hKey);
                RegDeleteKey(HKEY_CURRENT_USER, regPath);
            } else {
                RegCloseKey(hKey);
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    return TRUE;
}

// --- Logic: Apply Changes ---
void OnApply(HWND hwnd) {
    TCHAR exePath[MAX_PATH];
    if (!GetZefidiPath(exePath, MAX_PATH)) {
        MessageBox(hwnd, _T("Could not find zefidi.exe in the current folder."), _T("Error"), MB_ICONERROR);
        return;
    }

    SetWindowText(hStatusLabel, _T("Updating Registry..."));
    
    int count = ListView_GetItemCount(hListView);
    for (int i = 0; i < count; i++) {
        if (ListView_GetCheckState(hListView, i)) {
            // Register the file type
            RegisterFileType(&supportedTypes[i], exePath);
        } else {
            // Unregister the file type
            UnregisterFileType(&supportedTypes[i]);
        }
    }

    // Notify Windows that associations changed
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    SetWindowText(hStatusLabel, _T("Done. Associations updated."));
    MessageBox(hwnd, 
        _T("Associations updated.\n\nOn Windows 11, you may need to select NeoBAE and click 'Always' the first time you open a file."), 
        _T("Success"), MB_ICONINFORMATION);
}

// --- UI: Scan and Populate List ---
void RefreshList() {
    ListView_DeleteAllItems(hListView);
    
    for (int i = 0; i < typeCount; i++) {
        LVITEM lvI = {0};
        lvI.mask = LVIF_TEXT | LVIF_STATE;
        lvI.iItem = i;
        lvI.pszText = supportedTypes[i].ext;
        
        ListView_InsertItem(hListView, &lvI);
        ListView_SetItemText(hListView, i, 1, supportedTypes[i].desc);

        // Check if associated
        if (IsAssociated(supportedTypes[i].ext)) {
            ListView_SetCheckState(hListView, i, TRUE);
        }
    }
}

// --- Window Procedure ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            // Create ListView
            hListView = CreateWindowEx(0, WC_LISTVIEW, _T(""),
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
                10, 10, 360, 300, hwnd, (HMENU)ID_LISTVIEW, hInst, NULL);
            
            ListView_SetExtendedListViewStyle(hListView, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
            
            // Add Columns
            LVCOLUMN lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.pszText = _T("Extension");
            lvc.cx = 100;
            ListView_InsertColumn(hListView, 0, &lvc);
            
            lvc.pszText = _T("Description");
            lvc.cx = 230;
            ListView_InsertColumn(hListView, 1, &lvc);

            // Create Buttons
            CreateWindow(_T("BUTTON"), _T("Select All"),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                10, 320, 100, 30, hwnd, (HMENU)ID_BTN_SELALL, hInst, NULL);

            CreateWindow(_T("BUTTON"), _T("Apply Associations"),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                220, 320, 150, 30, hwnd, (HMENU)ID_BTN_APPLY, hInst, NULL);

            // Create Status Label
            hStatusLabel = CreateWindow(_T("STATIC"), _T("Ready."),
                WS_VISIBLE | WS_CHILD,
                10, 360, 360, 20, hwnd, (HMENU)ID_LBL_STATUS, hInst, NULL);

            // Initial Scan
            RefreshList();
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_BTN_APPLY) {
                OnApply(hwnd);
            } else if (id == ID_BTN_SELALL) {
                int count = ListView_GetItemCount(hListView);
                for(int i=0; i<count; i++) ListView_SetCheckState(hListView, i, TRUE);
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Entry Point ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = _T("zefidiHelperClass");

    if(!RegisterClassEx(&wc)) return -1;

    HWND hwnd = CreateWindowEx(0, _T("zefidiHelperClass"), _T("zefidi File Association Helper"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 430,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}