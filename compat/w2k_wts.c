#include <windows.h>

typedef BOOL (WINAPI *WTS_QUERY_SESSION_INFORMATION_W)(
    HANDLE server,
    DWORD session_id,
    int info_class,
    LPWSTR *buffer,
    DWORD *bytes_returned);
typedef VOID (WINAPI *WTS_FREE_MEMORY)(PVOID memory);

static HMODULE g_system_wts;
static WTS_QUERY_SESSION_INFORMATION_W g_query_session_information_w;
static WTS_FREE_MEMORY g_free_memory;

static HMODULE load_system_wts(void)
{
    CHAR path[MAX_PATH];
    UINT length;

    if (g_system_wts) return g_system_wts;

    length = GetSystemDirectoryA(path, sizeof(path));
    if (!length || length + sizeof("\\WTSAPI32.dll") > sizeof(path)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return NULL;
    }
    lstrcatA(path, "\\WTSAPI32.dll");
    g_system_wts = LoadLibraryA(path);
    return g_system_wts;
}

static FARPROC resolve_system_wts(LPCSTR name)
{
    HMODULE module = load_system_wts();
    if (!module) return NULL;
    return GetProcAddress(module, name);
}

__declspec(dllexport) BOOL WINAPI WTSQuerySessionInformationW(
    HANDLE server,
    DWORD session_id,
    int info_class,
    LPWSTR *buffer,
    DWORD *bytes_returned)
{
    if (buffer) *buffer = NULL;
    if (bytes_returned) *bytes_returned = 0;
    if (!buffer || !bytes_returned) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!g_query_session_information_w) {
        g_query_session_information_w = (WTS_QUERY_SESSION_INFORMATION_W)
            resolve_system_wts("WTSQuerySessionInformationW");
    }
    if (!g_query_session_information_w) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    return g_query_session_information_w(
        server, session_id, info_class, buffer, bytes_returned);
}

__declspec(dllexport) VOID WINAPI WTSFreeMemory(PVOID memory)
{
    if (!memory) return;
    if (!g_free_memory) {
        g_free_memory = (WTS_FREE_MEMORY)resolve_system_wts("WTSFreeMemory");
    }
    if (g_free_memory) g_free_memory(memory);
}

__declspec(dllexport) BOOL WINAPI WTSRegisterSessionNotification(
    HWND window,
    DWORD flags)
{
    (void)window;
    (void)flags;

    /*
     * Windows 2000 has no WTS session-notification registration API and
     * Professional has no fast-user-switching session transition to report.
     * Treat registration as successful so callers keep their ordinary window
     * lifecycle; no WM_WTSSESSION_CHANGE messages will be generated.
     */
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI WTSUnRegisterSessionNotification(HWND window)
{
    (void)window;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
