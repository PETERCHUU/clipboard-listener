#include <stdio.h>
#include <shlobj.h>
#include <windows.h>
#include "sqlite3.h"

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

#define len _countof



#define WM_USER_SHELLICON (WM_USER + 1) // define a custom message

// each message from custom message
#define IDM_START 101

#define IDM_STOP  102

#define IDM_SCAN  103

#define IDM_EXIT  104



#define Name_Max_Length 25

#define SERVICE_START_LOADING L"Clipboard： Listening..."

#define SERVICE_START_STOP L"Clipboard： Stopped"




NOTIFYICONDATA nid = { 0 };

BOOL isListening = TRUE; // control listener

sqlite3* STATUS_DB;

wchar_t Message_Buffer[256];




int Init_Database() {

    char* zErrMsg = 0;

    int rc = sqlite3_open("clip_history.db", &STATUS_DB);

    if (rc) {

        const wchar_t* wMsg = (const wchar_t*)sqlite3_errmsg16(STATUS_DB);

        swprintf_s(Message_Buffer, len(Message_Buffer), L"Fail to open Database: %s", wMsg);

        MessageBoxW(NULL, L"insert clip Fail!", L"Clipboard Listener", MB_OK | MB_ICONINFORMATION);

        return 0;
    }


    const char* sql_create = "CREATE TABLE IF NOT EXISTS CLIPBOARD (" \
        "CONTENT TEXT PRIMARY KEY ," \
        "TIMESTAMP DATETIME DEFAULT CURRENT_TIMESTAMP);";


    sqlite3_exec(STATUS_DB, sql_create, NULL, 0, &zErrMsg);

    return rc;
}



void SaveToDatabase(const char* content) {

    sqlite3_stmt* res;

    // use OR IGNORE：if CONTENT exist，do nothing
    const char* sql = "INSERT OR IGNORE INTO CLIPBOARD (CONTENT) VALUES (?);";

    if (sqlite3_prepare_v2(STATUS_DB, sql, -1, &res, 0) == SQLITE_OK) {

        // bind 16 bytes ASCII char for insert
        sqlite3_bind_text(res, 1, content, -1, SQLITE_STATIC);

        int step = sqlite3_step(res);

        if (step != SQLITE_DONE) {

            MessageBoxW(NULL, L"insert clip Fail!", L"Clipboard Listener", MB_OK | MB_ICONINFORMATION);

        }

        sqlite3_finalize(res);

    }

}



int CheckIfExistInDatabase(const char* content) {

    sqlite3_stmt* res;

    int exists = 0;

    const char* sql = "SELECT EXISTS(SELECT 1 FROM CLIPBOARD WHERE CONTENT = ? LIMIT 1);";

    if (sqlite3_prepare_v2(STATUS_DB, sql, -1, &res, 0) == SQLITE_OK) {

        sqlite3_bind_text(res, 1, content, -1, SQLITE_STATIC);

        if (sqlite3_step(res) == SQLITE_ROW) {

            exists = sqlite3_column_int(res, 0);

        }
    }
    sqlite3_finalize(res);
    return exists;

}



void UpdateTrayTip(HWND hwnd, const wchar_t* text) {

    wcsncpy_s(nid.szTip, len(nid.szTip), text, _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);

}

void Check_Clip_Board(HWND hwnd) {

    if (!isListening) return;

    Sleep(100);

    if (!IsClipboardFormatAvailable(CF_TEXT)) return;

    int retryCount = 0;

    while (!OpenClipboard(hwnd) && retryCount < 5) {
        retryCount++;
        Sleep(20);
    }
    

    // if clipboard get message
    if (retryCount < 5) {

        HANDLE hData = GetClipboardData(CF_TEXT);

        if (hData != NULL) {

            size_t dataSize = GlobalSize(hData);

            if (dataSize > 0 && dataSize < Name_Max_Length) {

                char* pszText = (char*)GlobalLock(hData);

                if (pszText != NULL) {

                    if (CheckIfExistInDatabase(pszText)) {

                        MessageBoxW(NULL, L"String existing... ", L"Clipboard Listener", MB_OK | MB_ICONINFORMATION);

                    }
                    else {

                        SaveToDatabase(pszText);

                    }


                    GlobalUnlock(hData);
                }
            }
        }

        CloseClipboard();
    }
}


BOOL SelectFolder(HWND hwnd,wchar_t* path, size_t length) {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select a folder for file name scanning：";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE; // only folder in dialog

	// 1. popup folder selection dialog and get the selected folder's ID List
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);

    if (pidl != NULL) {
        // 2. makgin ID List into real folder path
        BOOL success = SHGetPathFromIDListW(pidl, path);

		// 3. release the memory allocated for the ID List
        CoTaskMemFree(pidl);
		return success;
    }
	return FALSE;
}


void ScanFolderFiles(const wchar_t* folderPath) {

    wchar_t searchPath[MAX_PATH];
    WIN32_FIND_DATAW findData;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    // 1. binding the folder name with search option \*
    swprintf_s(searchPath, MAX_PATH, L"%s\\*", folderPath);

    // 2. start scanning
    hFind = FindFirstFileW(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        MessageBoxW(NULL, L"Folder Fail to Scan, or empty\n", L"Clipboard Listener", MB_OK);
        return;
    }
    char asciiFileName[260];

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {

            size_t nameLen = wcslen(findData.cFileName);
            if ( nameLen > 0 && nameLen < Name_Max_Length) {

                if (IsPureAscii(findData.cFileName)) {

                    WideCharToMultiByte(CP_ACP, 0, findData.cFileName, -1, asciiFileName, 260, NULL, NULL);

                    SaveToDatabase(asciiFileName);
                }

            }

        }
    } while (FindNextFileW(hFind, &findData) != 0);

    MessageBoxW(NULL, L"Folder Scan finixh\n", L"Clipboard Listener", MB_OK);

    FindClose(hFind);
}

BOOL IsPureAscii(const wchar_t* str) {
    while (*str) {
        if (*str > 127) {
            return FALSE;
        }
        str++;
    }
    return TRUE;
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    switch (msg) {

    case WM_CLIPBOARDUPDATE:
        Check_Clip_Board(hwnd);
        break;

    case WM_USER_SHELLICON:

        if (lParam == WM_RBUTTONUP) { // icon right client manu

            POINT pt;

            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();

            // creating a selection for 
            if (isListening) {

                AppendMenu(hMenu, MF_STRING, IDM_STOP, L"Stop Listening (Stop)");

            }
            else {

                AppendMenu(hMenu, MF_STRING, IDM_START, L"Start Listening (Start)");

            }


            AppendMenu(hMenu, MF_STRING, IDM_SCAN, L"Folder Scanning (Scan)");

            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

            AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Close Application (Exit)");





            // destory the manu after selection
            SetForegroundWindow(hwnd);

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:

        switch (LOWORD(wParam)) {

        case IDM_START:

            isListening = TRUE;

            UpdateTrayTip(hwnd, SERVICE_START_LOADING);

            break;
        case IDM_STOP:

            isListening = FALSE;

            UpdateTrayTip(hwnd, SERVICE_START_STOP);

            break;
        case IDM_SCAN:

            SelectFolder(hwnd, Message_Buffer, 256);

            ScanFolderFiles(Message_Buffer);
			break;
        
        case IDM_EXIT:

            DestroyWindow(hwnd);

            break;
        }
        break;

    case WM_DESTROY:

        Shell_NotifyIcon(NIM_DELETE, &nid); // destory icon in start bar

        RemoveClipboardFormatListener(hwnd);

        PostQuitMessage(0);

        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}


int main() {
    
    // 1. define and create hiden windows for listening clipboard
    const char CLASS_NAME[] = "ClipboardListener";

    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = WndProc;

    wc.hInstance = GetModuleHandle(NULL);

    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Clip Listener", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);



    if (hwnd == NULL) return 1;




    // 3. define icon tray
    nid.cbSize = sizeof(NOTIFYICONDATA);

    nid.hWnd = hwnd;

    nid.uID = 1;

    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

    nid.uCallbackMessage = WM_USER_SHELLICON;

    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); // default Icon

    wcsncpy_s(nid.szTip, len(nid.szTip), SERVICE_START_LOADING, _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &nid);




    if (Init_Database()) {

        return 0;

    }



    // 3. Start listening clipboard
    if (AddClipboardFormatListener(hwnd)) {

        printf("listener Started，press Ctrl+C close the app...\n");

    }




    // 4. important: loop for getting command
    MSG msg;



    // GetMessage will block the main thread until it get command from icon
    while (GetMessage(&msg, NULL, 0, 0)) {

        TranslateMessage(&msg);

        DispatchMessage(&msg);

    }




    // 4. close datbase if exist
    sqlite3_close(STATUS_DB);

    return 0;
}

