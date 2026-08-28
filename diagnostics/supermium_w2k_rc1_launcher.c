#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define APP_TITLE "Supermium for Windows 2000 - Release Candidate 1"
#define RELEASE_BUILD "144.0.7559.256-R5-W2K-RC1"
#define DIAGNOSTIC_SCHEMA "1"
#define MEDIA_PORT 9223
#define MEDIA_HEADER "X-Supermium-Diagnostics: 144-r5-w2k-rc1"

#ifndef FILE_ATTRIBUTE_REPARSE_POINT
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#endif

#define AF_INET_W2K 2
#define SOCK_STREAM_W2K 1
#define IPPROTO_TCP_W2K 6
#define SOCKET_ERROR_W2K (-1)
#define INVALID_SOCKET_W2K ((UINT_PTR)(~0))
#define SD_BOTH_W2K 2
#define WSA_VERSION_W2K 0x0202

typedef UINT_PTR W2K_SOCKET;
typedef struct {
    short sin_family;
    unsigned short sin_port;
    unsigned long sin_addr;
    char sin_zero[8];
} W2K_SOCKADDR_IN;
typedef struct {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} W2K_WSADATA;

typedef int (WINAPI *PFN_WSAStartup)(WORD, W2K_WSADATA *);
typedef int (WINAPI *PFN_WSACleanup)(void);
typedef W2K_SOCKET (WINAPI *PFN_socket)(int, int, int);
typedef int (WINAPI *PFN_bind)(W2K_SOCKET, const void *, int);
typedef int (WINAPI *PFN_listen)(W2K_SOCKET, int);
typedef W2K_SOCKET (WINAPI *PFN_accept)(W2K_SOCKET, void *, int *);
typedef int (WINAPI *PFN_recv)(W2K_SOCKET, char *, int, int);
typedef int (WINAPI *PFN_send)(W2K_SOCKET, const char *, int, int);
typedef int (WINAPI *PFN_shutdown)(W2K_SOCKET, int);
typedef int (WINAPI *PFN_closesocket)(W2K_SOCKET);

static char g_base_dir[MAX_PATH];
static char g_diagnostics_dir[MAX_PATH];
static char g_log_root[MAX_PATH];
static char g_session_dir[MAX_PATH];
static char g_browser_log_path[MAX_PATH];
static char g_media_log_path[MAX_PATH];
static char g_username[256];
static char g_computer_name[256];
static volatile LONG g_server_stop = 0;
static W2K_SOCKET g_listen_socket = INVALID_SOCKET_W2K;
static HANDLE g_media_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_media_lock;
static int g_media_lock_ready = 0;
static int g_media_server_ready = 0;

static PFN_WSAStartup p_WSAStartup;
static PFN_WSACleanup p_WSACleanup;
static PFN_socket p_socket;
static PFN_bind p_bind;
static PFN_listen p_listen;
static PFN_accept p_accept;
static PFN_recv p_recv;
static PFN_send p_send;
static PFN_shutdown p_shutdown;
static PFN_closesocket p_closesocket;

static int ci_equal_char(char a, char b) {
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int ci_starts_with(const char *value, const char *prefix) {
    while (*prefix) {
        if (!*value || !ci_equal_char(*value, *prefix)) return 0;
        ++value;
        ++prefix;
    }
    return 1;
}

static const char *ci_find(const char *value, const char *needle) {
    const char *cursor;
    if (!needle[0]) return value;
    for (cursor = value; *cursor; ++cursor) {
        if (ci_starts_with(cursor, needle)) return cursor;
    }
    return NULL;
}

static void copy_string(char *destination, DWORD capacity, const char *source) {
    DWORD index = 0;
    if (!capacity) return;
    while (source && source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = 0;
}

static int append_string(char *destination, DWORD capacity, const char *source) {
    DWORD used = (DWORD)lstrlenA(destination);
    DWORD index = 0;
    if (used >= capacity) return 0;
    while (source && source[index] && used + index + 1 < capacity) {
        destination[used + index] = source[index];
        ++index;
    }
    destination[used + index] = 0;
    return source == NULL || source[index] == 0;
}

static int join_path(char *destination, DWORD capacity, const char *left, const char *right) {
    DWORD length;
    copy_string(destination, capacity, left);
    length = (DWORD)lstrlenA(destination);
    if (length && destination[length - 1] != '\\' && destination[length - 1] != '/') {
        if (!append_string(destination, capacity, "\\")) return 0;
    }
    return append_string(destination, capacity, right);
}

static void initialize_paths(void) {
    char module_path[MAX_PATH];
    char *slash;
    DWORD size = GetModuleFileNameA(NULL, module_path, MAX_PATH);
    if (!size || size >= MAX_PATH) {
        copy_string(module_path, MAX_PATH, ".\\Supermium W2K RC1.exe");
    }
    slash = module_path + lstrlenA(module_path);
    while (slash > module_path && slash[-1] != '\\' && slash[-1] != '/') --slash;
    if (slash > module_path) slash[-1] = 0;
    else copy_string(module_path, MAX_PATH, ".");
    copy_string(g_base_dir, MAX_PATH, module_path);
    join_path(g_diagnostics_dir, MAX_PATH, g_base_dir, "Diagnostics");
    join_path(g_log_root, MAX_PATH, g_base_dir, "Diagnostic Logs");
}

static int ensure_directory(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return CreateDirectoryA(path, NULL) != 0;
}

static int delete_directory_tree(const char *path) {
    char search[MAX_PATH];
    char child[MAX_PATH];
    WIN32_FIND_DATAA entry;
    HANDLE find;
    DWORD attributes = GetFileAttributesA(path);
    int ok = 1;
    if (attributes == INVALID_FILE_ATTRIBUTES) return 1;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
        return DeleteFileA(path) != 0;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
        return RemoveDirectoryA(path) != 0;
    }
    if (!join_path(search, MAX_PATH, path, "*")) return 0;
    find = FindFirstFileA(search, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((entry.cFileName[0] == '.' && entry.cFileName[1] == 0) ||
                (entry.cFileName[0] == '.' && entry.cFileName[1] == '.' &&
                 entry.cFileName[2] == 0)) continue;
            if (!join_path(child, MAX_PATH, path, entry.cFileName)) {
                ok = 0;
                continue;
            }
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                    if (!RemoveDirectoryA(child)) ok = 0;
                } else if (!delete_directory_tree(child)) {
                    ok = 0;
                }
            } else {
                SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileA(child)) ok = 0;
            }
        } while (FindNextFileA(find, &entry));
        FindClose(find);
    }
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryA(path)) ok = 0;
    return ok;
}

/*
 * Chromium can leave utility processes alive briefly after its main process
 * exits.  Those processes may still hold files in the disposable diagnostic
 * profile.  Retry for a bounded period so the profile is removed without
 * ever touching a user's normal browser data.
 */
static int delete_directory_tree_with_retries(const char *path) {
    int attempt;
    for (attempt = 0; attempt < 30; ++attempt) {
        if (delete_directory_tree(path)) return 1;
        Sleep(500);
    }
    return delete_directory_tree(path);
}

static int write_all(HANDLE file, const void *data, DWORD size) {
    const BYTE *cursor = (const BYTE *)data;
    while (size) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, size, &written, NULL) || !written) return 0;
        cursor += written;
        size -= written;
    }
    return 1;
}

static int write_text_file(const char *path, const char *text) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    int ok;
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = write_all(file, text, (DWORD)lstrlenA(text));
    CloseHandle(file);
    return ok;
}

static int write_neutral_uao(const char *profile_path) {
    char path[MAX_PATH];
    char value[32];
    HANDLE file;
    int ok;
    memset(value, ';', 31);
    value[31] = 0;
    if (!ensure_directory(profile_path)) return 0;
    if (!join_path(path, MAX_PATH, profile_path, "uao")) return 0;
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = write_all(file, value, sizeof(value));
    CloseHandle(file);
    return ok;
}

static void append_text_file(const char *path, const char *text) {
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    write_all(file, text, (DWORD)lstrlenA(text));
    CloseHandle(file);
}

static void utc_timestamp(char *output, DWORD capacity) {
    SYSTEMTIME time;
    GetSystemTime(&time);
    _snprintf(output, capacity - 1, "%04u-%02u-%02uT%02u:%02u:%02uZ",
              time.wYear, time.wMonth, time.wDay,
              time.wHour, time.wMinute, time.wSecond);
    output[capacity - 1] = 0;
}

static int is_token_boundary(char value) {
    return value == 0 || !(isalnum((unsigned char)value) || value == '_' || value == '-');
}

static int exact_sensitive_at(const char *input, const char *sensitive) {
    DWORD length;
    if (!sensitive || !sensitive[0]) return 0;
    length = (DWORD)lstrlenA(sensitive);
    if (!ci_starts_with(input, sensitive)) return 0;
    return is_token_boundary(input[length]);
}

static int parse_ipv4_length(const char *input) {
    int groups = 0;
    int position = 0;
    const char *cursor = input;
    if (!isdigit((unsigned char)*cursor)) return 0;
    while (groups < 4) {
        int value = 0;
        int digits = 0;
        while (isdigit((unsigned char)*cursor) && digits < 3) {
            value = value * 10 + (*cursor - '0');
            ++cursor;
            ++digits;
            ++position;
        }
        if (!digits || value > 255 || isdigit((unsigned char)*cursor)) return 0;
        ++groups;
        if (groups == 4) break;
        if (*cursor != '.') return 0;
        ++cursor;
        ++position;
    }
    if (!is_token_boundary(*cursor) && *cursor != ':' && *cursor != '/' && *cursor != ',') return 0;
    return position;
}

static int strong_value_delimiter(char value) {
    return value == 0 || value == '\r' || value == '\n' || value == '"' ||
           value == '\'' || value == '<' || value == '>' || value == '|' ||
           value == ',' || value == ';' || value == ')' || value == ']' || value == '}';
}

static void output_append(char *output, DWORD capacity, DWORD *used, const char *text) {
    DWORD index = 0;
    while (text[index] && *used + 1 < capacity) {
        output[(*used)++] = text[index++];
    }
    output[*used] = 0;
}

static void sanitize_text(const char *input, char *output, DWORD capacity) {
    DWORD used = 0;
    const char *cursor = input;
    output[0] = 0;
    while (*cursor && used + 1 < capacity) {
        int ipv4_length;
        const char *token_end;
        const char *at;

        if (ci_starts_with(cursor, "http://") || ci_starts_with(cursor, "https://") ||
            ci_starts_with(cursor, "file://") || ci_starts_with(cursor, "blob:")) {
            output_append(output, capacity, &used, "<web-url-redacted>");
            while (*cursor && !isspace((unsigned char)*cursor) && !strong_value_delimiter(*cursor)) ++cursor;
            continue;
        }
        if ((isalpha((unsigned char)cursor[0]) && cursor[1] == ':' &&
             (cursor[2] == '\\' || cursor[2] == '/')) ||
            (cursor[0] == '\\' && cursor[1] == '\\')) {
            output_append(output, capacity, &used, "<local-path-redacted>");
            while (*cursor && !strong_value_delimiter(*cursor)) ++cursor;
            continue;
        }
        if (ci_starts_with(cursor, "username=") || ci_starts_with(cursor, "user_name=") ||
            ci_starts_with(cursor, "user-name=") || ci_starts_with(cursor, "computername=")) {
            const char *equals = strchr(cursor, '=');
            while (cursor <= equals && *cursor && used + 1 < capacity) output[used++] = *cursor++;
            output[used] = 0;
            output_append(output, capacity, &used, "<redacted>");
            while (*cursor && !isspace((unsigned char)*cursor) && !strong_value_delimiter(*cursor)) ++cursor;
            continue;
        }
        if ((cursor == input || is_token_boundary(cursor[-1])) &&
            exact_sensitive_at(cursor, g_computer_name)) {
            output_append(output, capacity, &used, "<computer-name-redacted>");
            cursor += lstrlenA(g_computer_name);
            continue;
        }
        if ((cursor == input || is_token_boundary(cursor[-1])) &&
            exact_sensitive_at(cursor, g_username)) {
            output_append(output, capacity, &used, "<username-redacted>");
            cursor += lstrlenA(g_username);
            continue;
        }
        ipv4_length = parse_ipv4_length(cursor);
        if (ipv4_length) {
            output_append(output, capacity, &used, "<ip-address-redacted>");
            cursor += ipv4_length;
            continue;
        }

        token_end = cursor;
        at = NULL;
        while (*token_end && !isspace((unsigned char)*token_end) &&
               !strong_value_delimiter(*token_end)) {
            if (*token_end == '@') at = token_end;
            ++token_end;
        }
        if (at && at > cursor && at + 1 < token_end) {
            output_append(output, capacity, &used, "<email-redacted>");
            cursor = token_end;
            continue;
        }

        output[used++] = *cursor++;
        output[used] = 0;
    }
}

static const char *skip_first_argument(const char *command_line) {
    const char *cursor = command_line;
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor && *cursor != '"') ++cursor;
        if (*cursor == '"') ++cursor;
    } else {
        while (*cursor && !isspace((unsigned char)*cursor)) ++cursor;
    }
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    return cursor;
}

static void load_sensitive_values(void) {
    DWORD size;
    g_username[0] = 0;
    g_computer_name[0] = 0;
    size = GetEnvironmentVariableA("USERNAME", g_username, sizeof(g_username));
    if (!size || size >= sizeof(g_username)) g_username[0] = 0;
    size = sizeof(g_computer_name);
    if (!GetComputerNameA(g_computer_name, &size)) g_computer_name[0] = 0;
}

static void spawn_simple_process(const char *application, const char *argument) {
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command[2 * MAX_PATH + 64];
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    _snprintf(command, sizeof(command) - 1, "\"%s\" \"%s\"", application, argument);
    command[sizeof(command) - 1] = 0;
    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, g_base_dir,
                       &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

static void open_disclosure(void) {
    char path[MAX_PATH];
    join_path(path, MAX_PATH, g_diagnostics_dir, "COLLECTED-DATA.txt");
    spawn_simple_process("notepad.exe", path);
}

static void open_reporting_guide(void) {
    char path[MAX_PATH];
    join_path(path, MAX_PATH, g_diagnostics_dir, "TESTING-AND-REPORTING.txt");
    spawn_simple_process("notepad.exe", path);
}

static void open_log_folder(void) {
    ensure_directory(g_log_root);
    spawn_simple_process("explorer.exe", g_log_root);
}

static int confirm_diagnostics_session(void) {
    const char *message =
        "You explicitly started Supermium in diagnostic mode.\r\n\r\n"
        "If you continue, this one session will create a new folder under "
        "'Diagnostic Logs' beside 'Supermium W2K RC1.exe'. Nothing is uploaded "
        "automatically. Every generated file is readable and can be reviewed, "
        "edited, deleted, or withheld before you choose whether to attach it to "
        "a GitHub issue.\r\n\r\n"
        "The logs contain non-identifying OS, CPU, memory, graphics, sound, driver, "
        "browser, sandbox, and HTML media state. They are designed to exclude "
        "usernames, computer names, network addresses, URLs, YouTube titles and IDs, "
        "accounts, cookies, passwords, form text, history, personal file lists, and "
        "personal file contents. No screenshots, microphone/camera data, audio "
        "recordings, or automatic crash dumps are collected.\r\n\r\n"
        "A separate temporary browser profile is used and removed when the browser "
        "closes. Do not sign in or enter private data during this diagnostic session. "
        "Read Diagnostics\\COLLECTED-DATA.txt for the full field-level disclosure.\r\n\r\n"
        "Start this diagnostic session?";
    return MessageBoxA(NULL, message, APP_TITLE,
                       MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDYES;
}

static int create_session_directory(void) {
    SYSTEMTIME time;
    char name[96];
    if (!ensure_directory(g_log_root)) return 0;
    GetSystemTime(&time);
    _snprintf(name, sizeof(name) - 1, "%04u%02u%02u-%02u%02u%02uZ-%lu",
              time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
              time.wSecond, GetCurrentProcessId());
    name[sizeof(name) - 1] = 0;
    join_path(g_session_dir, MAX_PATH, g_log_root, name);
    if (!ensure_directory(g_session_dir)) return 0;
    join_path(g_browser_log_path, MAX_PATH, g_session_dir, "browser-technical.log");
    join_path(g_media_log_path, MAX_PATH, g_session_dir, "youtube-media.jsonl");
    return 1;
}

static void write_summary_start(void) {
    char path[MAX_PATH];
    char timestamp[64];
    char text[4096];
    OSVERSIONINFOA version;
    SYSTEM_INFO system_info;
    MEMORYSTATUS memory;
    HDC display;
    int bits = 0;
    utc_timestamp(timestamp, sizeof(timestamp));
    ZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    GetVersionExA(&version);
    GetSystemInfo(&system_info);
    ZeroMemory(&memory, sizeof(memory));
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    display = GetDC(NULL);
    if (display) {
        bits = GetDeviceCaps(display, BITSPIXEL) * GetDeviceCaps(display, PLANES);
        ReleaseDC(NULL, display);
    }
    join_path(path, MAX_PATH, g_session_dir, "session-summary.txt");
    _snprintf(text, sizeof(text) - 1,
              "Supermium Windows 2000 Release Candidate 1 diagnostic session\r\n"
              "schema=%s\r\nbuild=%s\r\nstarted_utc=%s\r\n"
              "diagnostics_mode=explicitly_requested_for_this_session\r\nautomatic_upload=never\r\n"
              "os_version=%lu.%lu.%lu\r\nos_platform=%lu\r\nos_service_pack=%s\r\n"
              "logical_processors=%lu\r\nprocessor_architecture=%u\r\n"
              "physical_memory_mb=%lu\r\ndisplay_width=%d\r\ndisplay_height=%d\r\n"
              "display_color_bits=%d\r\n"
              "personal_data_policy=usernames/computer-names/network-addresses/urls/account-data/history/file-content excluded\r\n",
              DIAGNOSTIC_SCHEMA, RELEASE_BUILD, timestamp,
              version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber,
              version.dwPlatformId, version.szCSDVersion,
              system_info.dwNumberOfProcessors, system_info.wProcessorArchitecture,
              (DWORD)(memory.dwTotalPhys / (1024UL * 1024UL)),
              GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), bits);
    text[sizeof(text) - 1] = 0;
    write_text_file(path, text);

    join_path(path, MAX_PATH, g_session_dir, "REVIEW-BEFORE-SHARING.txt");
    write_text_file(path,
        "REVIEW THIS FOLDER BEFORE SHARING\r\n"
        "=================================\r\n\r\n"
        "All files are plain text or JSON Lines and can be opened in Notepad.\r\n"
        "Nothing is uploaded automatically. You choose whether to share, delete, or keep them.\r\n\r\n"
        "The diagnostic design excludes usernames, computer names, IP/MAC addresses, URLs,\r\n"
        "YouTube titles and video IDs, account information, cookies, passwords, form data,\r\n"
        "browsing history, bookmarks, personal file lists and personal file contents.\r\n\r\n"
        "Files in this session:\r\n"
        "- session-summary.txt: build, OS basics, resource counts, exit result\r\n"
        "- system-hardware.txt: non-identifying hardware and driver information\r\n"
        "- browser-technical.log: generated category/severity events; raw Chromium text discarded\r\n"
        "- youtube-media.jsonl: technical HTML media state only; never the page URL/title/ID\r\n\r\n"
        "Crash dumps are not collected automatically because they can contain private memory.\r\n"
        "Diagnostic browsing uses a separate temporary test profile outside this folder.\r\n"
        "Do not share a Private Temporary Profile folder if cleanup was incomplete.\r\n");

    {
        char source[MAX_PATH];
        char destination[MAX_PATH];
        join_path(source, MAX_PATH, g_diagnostics_dir, "COLLECTED-DATA.txt");
        join_path(destination, MAX_PATH, g_session_dir, "COLLECTED-DATA.txt");
        CopyFileA(source, destination, FALSE);
        join_path(source, MAX_PATH, g_diagnostics_dir, "TESTING-AND-REPORTING.txt");
        join_path(destination, MAX_PATH, g_session_dir, "TESTING-AND-REPORTING.txt");
        CopyFileA(source, destination, FALSE);
    }
}

static void write_summary_end(DWORD exit_code, DWORD duration_ms,
                              int temporary_profile_removed) {
    char path[MAX_PATH];
    char timestamp[64];
    char text[512];
    utc_timestamp(timestamp, sizeof(timestamp));
    join_path(path, MAX_PATH, g_session_dir, "session-summary.txt");
    _snprintf(text, sizeof(text) - 1,
              "ended_utc=%s\r\nbrowser_exit_code=%lu\r\nbrowser_runtime_seconds=%lu\r\n"
              "youtube_monitor_server=%s\r\n"
              "temporary_private_profile_cleanup=%s\r\n",
              timestamp, exit_code, duration_ms / 1000UL,
              g_media_server_ready ? "available" : "unavailable",
              temporary_profile_removed ? "complete" : "incomplete-see-disclosure");
    text[sizeof(text) - 1] = 0;
    append_text_file(path, text);
}

static void run_system_collector(void) {
    char script[MAX_PATH];
    char output[MAX_PATH];
    char command[3 * MAX_PATH + 64];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    join_path(script, MAX_PATH, g_diagnostics_dir, "collect-system.vbs");
    join_path(output, MAX_PATH, g_session_dir, "system-hardware.txt");
    _snprintf(command, sizeof(command) - 1,
              "cscript.exe //B //nologo \"%s\" \"%s\"", script, output);
    command[sizeof(command) - 1] = 0;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, g_base_dir, &startup, &process)) {
        WaitForSingleObject(process.hProcess, 30000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        write_text_file(output, "system_inventory=unavailable\r\n");
    }
}

static const char *browser_line_category(const char *line) {
    if (ci_find(line, "webaudio") || ci_find(line, "audio")) return "audio";
    if (ci_find(line, "video")) return "video";
    if (ci_find(line, "media")) return "media";
    if (ci_find(line, "decoder") || ci_find(line, "decode")) return "decoder";
    if (ci_find(line, "gpu") || ci_find(line, "graphics")) return "graphics";
    if (ci_find(line, "sandbox")) return "sandbox";
    if (ci_find(line, "crash")) return "crash";
    if (ci_find(line, "extension")) return "extension";
    if (ci_find(line, "network") || ci_find(line, "net::")) return "network";
    return NULL;
}

static const char *browser_line_severity(const char *line) {
    if (ci_find(line, "fatal")) return "fatal";
    if (ci_find(line, "error")) return "error";
    if (ci_find(line, "warning") || ci_find(line, "warn")) return "warning";
    return "information";
}

static void write_browser_category_event(HANDLE log, const char *line,
                                         DWORD seen, DWORD *kept) {
    const char *category = browser_line_category(line);
    const char *severity = browser_line_severity(line);
    char timestamp[64];
    char event[256];
    if (!category && severity[0] == 'i') return;
    if (*kept >= 20000) return;
    if (!category) category = "browser";
    utc_timestamp(timestamp, sizeof(timestamp));
    _snprintf(event, sizeof(event) - 1,
              "utc=%s sequence=%lu category=%s severity=%s\r\n",
              timestamp, seen, category, severity);
    event[sizeof(event) - 1] = 0;
    write_all(log, event, (DWORD)lstrlenA(event));
    ++(*kept);
}

static DWORD WINAPI browser_log_reader_thread(LPVOID parameter) {
    const char *header =
        "Supermium W2K privacy-safe browser event trace\r\n"
        "Raw Chromium text is discarded; only generated category/severity events follow.\r\n";
    HANDLE pipe = (HANDLE)parameter;
    HANDLE log;
    char input[2048];
    char line[16384];
    DWORD line_used = 0;
    DWORD read;
    DWORD seen = 0;
    DWORD kept = 0;
    char footer[256];
    log = CreateFileA(g_browser_log_path, GENERIC_WRITE, FILE_SHARE_READ,
                      NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) return 1;
    write_all(log, header, (DWORD)lstrlenA(header));
    while (ReadFile(pipe, input, sizeof(input), &read, NULL) && read) {
        DWORD index;
        for (index = 0; index < read; ++index) {
            char value = input[index];
            if (line_used + 2 >= sizeof(line) || value == '\n') {
                line[line_used] = 0;
                ++seen;
                write_browser_category_event(log, line, seen, &kept);
                line_used = 0;
                if (value == '\n') continue;
            }
            if (value != '\r') line[line_used++] = value;
        }
    }
    if (line_used) {
        line[line_used] = 0;
        ++seen;
        write_browser_category_event(log, line, seen, &kept);
    }
    _snprintf(footer, sizeof(footer) - 1,
              "source_lines_seen=%lu\r\nevents_retained=%lu\r\nevent_limit=%lu\r\n",
              seen, kept, 20000UL);
    footer[sizeof(footer) - 1] = 0;
    write_all(log, footer, (DWORD)lstrlenA(footer));
    CloseHandle(log);
    CloseHandle(pipe);
    return 0;
}

static unsigned short network_short(unsigned short host_value) {
    return (unsigned short)((host_value >> 8) | (host_value << 8));
}

static int load_winsock(void) {
    HMODULE module = LoadLibraryA("ws2_32.dll");
    if (!module) return 0;
    p_WSAStartup = (PFN_WSAStartup)GetProcAddress(module, "WSAStartup");
    p_WSACleanup = (PFN_WSACleanup)GetProcAddress(module, "WSACleanup");
    p_socket = (PFN_socket)GetProcAddress(module, "socket");
    p_bind = (PFN_bind)GetProcAddress(module, "bind");
    p_listen = (PFN_listen)GetProcAddress(module, "listen");
    p_accept = (PFN_accept)GetProcAddress(module, "accept");
    p_recv = (PFN_recv)GetProcAddress(module, "recv");
    p_send = (PFN_send)GetProcAddress(module, "send");
    p_shutdown = (PFN_shutdown)GetProcAddress(module, "shutdown");
    p_closesocket = (PFN_closesocket)GetProcAddress(module, "closesocket");
    return p_WSAStartup && p_WSACleanup && p_socket && p_bind && p_listen &&
           p_accept && p_recv && p_send && p_shutdown && p_closesocket;
}

static const char *find_header_value(const char *request, const char *name) {
    const char *found = ci_find(request, name);
    if (!found) return NULL;
    found += lstrlenA(name);
    while (*found == ' ' || *found == '\t') ++found;
    return found;
}

static int origin_is_extension(const char *request) {
    const char *origin = find_header_value(request, "Origin:");
    if (!origin) return 1;
    return ci_starts_with(origin, "chrome-extension://");
}

static void send_http(W2K_SOCKET client, const char *status, const char *extra) {
    char response[1024];
    _snprintf(response, sizeof(response) - 1,
              "HTTP/1.1 %s\r\nConnection: close\r\nContent-Length: 0\r\n"
              "Cache-Control: no-store\r\n%s\r\n",
              status, extra ? extra : "");
    response[sizeof(response) - 1] = 0;
    p_send(client, response, (int)lstrlenA(response), 0);
}

static void append_media_record(const char *body) {
    char sanitized[65536];
    char timestamp[64];
    char prefix[128];
    if (g_media_log == INVALID_HANDLE_VALUE || !g_media_lock_ready) return;
    sanitize_text(body, sanitized, sizeof(sanitized));
    utc_timestamp(timestamp, sizeof(timestamp));
    _snprintf(prefix, sizeof(prefix) - 1, "{\"received_utc\":\"%s\",\"payload\":", timestamp);
    prefix[sizeof(prefix) - 1] = 0;
    EnterCriticalSection(&g_media_lock);
    write_all(g_media_log, prefix, (DWORD)lstrlenA(prefix));
    write_all(g_media_log, sanitized, (DWORD)lstrlenA(sanitized));
    write_all(g_media_log, "}\r\n", 3);
    LeaveCriticalSection(&g_media_lock);
}

static void handle_http_client(W2K_SOCKET client) {
    char request[65536];
    int used = 0;
    int header_end = -1;
    int content_length = 0;
    const char *length_value;
    const char *body;
    const char *cors =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, X-Supermium-Diagnostics\r\n"
        "Access-Control-Allow-Private-Network: true\r\n";

    while (used + 1 < sizeof(request)) {
        int received = p_recv(client, request + used, (int)sizeof(request) - used - 1, 0);
        if (received <= 0) break;
        used += received;
        request[used] = 0;
        if (header_end < 0) {
            char *found = strstr(request, "\r\n\r\n");
            if (found) {
                header_end = (int)(found - request) + 4;
                length_value = find_header_value(request, "Content-Length:");
                if (length_value) content_length = atoi(length_value);
                if (content_length < 0 || content_length > 32768) break;
            }
        }
        if (header_end >= 0 && used >= header_end + content_length) break;
    }
    request[used] = 0;
    if (ci_starts_with(request, "OPTIONS ")) {
        if (origin_is_extension(request)) send_http(client, "204 No Content", cors);
        else send_http(client, "403 Forbidden", NULL);
        return;
    }
    if (!ci_starts_with(request, "POST /youtube ") || !origin_is_extension(request) ||
        !ci_find(request, MEDIA_HEADER) || header_end < 0 || content_length <= 0 ||
        used < header_end + content_length) {
        send_http(client, "403 Forbidden", NULL);
        return;
    }
    body = request + header_end;
    request[header_end + content_length] = 0;
    append_media_record(body);
    send_http(client, "204 No Content", cors);
}

static DWORD WINAPI media_server_thread(LPVOID ignored) {
    W2K_WSADATA data;
    W2K_SOCKADDR_IN address;
    (void)ignored;
    if (!load_winsock()) return 1;
    if (p_WSAStartup(WSA_VERSION_W2K, &data) != 0) return 2;
    g_listen_socket = p_socket(AF_INET_W2K, SOCK_STREAM_W2K, IPPROTO_TCP_W2K);
    if (g_listen_socket == INVALID_SOCKET_W2K) {
        p_WSACleanup();
        return 3;
    }
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET_W2K;
    address.sin_port = network_short(MEDIA_PORT);
    address.sin_addr = 0x0100007FUL;
    if (p_bind(g_listen_socket, &address, sizeof(address)) == SOCKET_ERROR_W2K ||
        p_listen(g_listen_socket, 8) == SOCKET_ERROR_W2K) {
        p_closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET_W2K;
        p_WSACleanup();
        return 4;
    }
    g_media_server_ready = 1;
    while (!g_server_stop) {
        W2K_SOCKET client = p_accept(g_listen_socket, NULL, NULL);
        if (client == INVALID_SOCKET_W2K) break;
        handle_http_client(client);
        p_shutdown(client, SD_BOTH_W2K);
        p_closesocket(client);
    }
    if (g_listen_socket != INVALID_SOCKET_W2K) {
        p_closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET_W2K;
    }
    p_WSACleanup();
    return 0;
}

static HANDLE start_media_server(void) {
    DWORD thread_id;
    InitializeCriticalSection(&g_media_lock);
    g_media_lock_ready = 1;
    g_media_log = CreateFileA(g_media_log_path, GENERIC_WRITE, FILE_SHARE_READ,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_media_log == INVALID_HANDLE_VALUE) return NULL;
    return CreateThread(NULL, 0, media_server_thread, NULL, 0, &thread_id);
}

static void stop_media_server(HANDLE thread) {
    g_server_stop = 1;
    if (g_listen_socket != INVALID_SOCKET_W2K && p_closesocket) {
        p_closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET_W2K;
    }
    if (thread) {
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
    }
    if (g_media_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_media_log);
        g_media_log = INVALID_HANDLE_VALUE;
    }
    if (g_media_lock_ready) {
        DeleteCriticalSection(&g_media_lock);
        g_media_lock_ready = 0;
    }
}

static int launch_browser_disabled(const char *forwarded) {
    char chrome[MAX_PATH];
    char *command;
    DWORD capacity;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    join_path(chrome, MAX_PATH, g_base_dir, "chrome.exe");
    capacity = (DWORD)lstrlenA(chrome) + (DWORD)lstrlenA(forwarded) + 16;
    command = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
    if (!command) return 1;
    _snprintf(command, capacity - 1, "\"%s\" %s", chrome, forwarded);
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessA(chrome, command, NULL, NULL, FALSE, 0, NULL, g_base_dir,
                        &startup, &process)) {
        char error[256];
        _snprintf(error, sizeof(error) - 1, "Could not start chrome.exe. Windows error %lu.", GetLastError());
        MessageBoxA(NULL, error, APP_TITLE, MB_OK | MB_ICONERROR);
        HeapFree(GetProcessHeap(), 0, command);
        return 2;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    HeapFree(GetProcessHeap(), 0, command);
    return 0;
}

static int launch_browser_diagnostics(const char *forwarded) {
    char chrome[MAX_PATH];
    char extension[MAX_PATH];
    char private_profile[MAX_PATH];
    char private_profile_name[96];
    char *command;
    DWORD capacity;
    SECURITY_ATTRIBUTES security;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    HANDLE reader_thread = NULL;
    HANDLE server_thread = NULL;
    DWORD reader_id;
    DWORD exit_code = 0;
    DWORD started = GetTickCount();
    int private_profile_removed = 0;

    if (!create_session_directory()) {
        MessageBoxA(NULL,
            "Diagnostic logging was enabled, but the Diagnostic Logs folder could not be created. "
            "Supermium will start without diagnostics for this session.",
            APP_TITLE, MB_OK | MB_ICONWARNING);
        return launch_browser_disabled(forwarded);
    }
    write_summary_start();
    run_system_collector();
    server_thread = start_media_server();

    join_path(chrome, MAX_PATH, g_base_dir, "chrome.exe");
    join_path(extension, MAX_PATH, g_diagnostics_dir, "YouTube Extension");
    _snprintf(private_profile_name, sizeof(private_profile_name) - 1,
              "Private Temporary Profile-%lu", GetCurrentProcessId());
    private_profile_name[sizeof(private_profile_name) - 1] = 0;
    join_path(private_profile, MAX_PATH, g_diagnostics_dir, private_profile_name);
    if (!write_neutral_uao(private_profile)) {
        int removed;
        MessageBoxA(NULL,
            "The isolated diagnostic profile could not be prepared. No browser was started.",
            APP_TITLE, MB_OK | MB_ICONERROR);
        stop_media_server(server_thread);
        removed = delete_directory_tree_with_retries(private_profile);
        write_summary_end(1, GetTickCount() - started, removed);
        return 4;
    }
    capacity = (DWORD)lstrlenA(chrome) + (DWORD)lstrlenA(extension) +
               (DWORD)lstrlenA(private_profile) + (DWORD)lstrlenA(forwarded) + 768;
    command = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity);
    if (!command) {
        int removed;
        stop_media_server(server_thread);
        removed = delete_directory_tree_with_retries(private_profile);
        write_summary_end(1, GetTickCount() - started, removed);
        return 1;
    }
    _snprintf(command, capacity - 1,
              "\"%s\" %s --enable-logging=stderr --log-level=0 "
              "--vmodule=media*=2,audio*=2,video*=2,webaudio*=1,gpu*=1,sandbox*=1,crash*=1 "
              "--no-first-run --disable-sync "
              "\"--user-data-dir=%s\" \"--load-extension=%s\"",
              chrome, forwarded, private_profile, extension);

    ZeroMemory(&security, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        int removed;
        HeapFree(GetProcessHeap(), 0, command);
        stop_media_server(server_thread);
        removed = delete_directory_tree_with_retries(private_profile);
        write_summary_end(2, GetTickCount() - started, removed);
        return 2;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;

    if (!CreateProcessA(chrome, command, NULL, NULL, TRUE, 0, NULL, g_base_dir,
                        &startup, &process)) {
        char error[256];
        int removed;
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        HeapFree(GetProcessHeap(), 0, command);
        stop_media_server(server_thread);
        removed = delete_directory_tree_with_retries(private_profile);
        write_summary_end(3, GetTickCount() - started, removed);
        _snprintf(error, sizeof(error) - 1, "Could not start chrome.exe. Windows error %lu.", GetLastError());
        MessageBoxA(NULL, error, APP_TITLE, MB_OK | MB_ICONERROR);
        return 3;
    }
    CloseHandle(write_pipe);
    reader_thread = CreateThread(NULL, 0, browser_log_reader_thread,
                                 read_pipe, 0, &reader_id);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    if (reader_thread) {
        WaitForSingleObject(reader_thread, 5000);
        CloseHandle(reader_thread);
    } else if (read_pipe) {
        CloseHandle(read_pipe);
    }
    stop_media_server(server_thread);
    private_profile_removed = delete_directory_tree_with_retries(private_profile);
    write_summary_end(exit_code, GetTickCount() - started, private_profile_removed);
    HeapFree(GetProcessHeap(), 0, command);

    if (MessageBoxA(NULL,
            "Diagnostic logs for this session are ready. Nothing has been uploaded.\r\n\r\n"
            "Would you like to open the folder and review every file now?",
            APP_TITLE, MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        spawn_simple_process("explorer.exe", g_session_dir);
    }
    return (int)exit_code;
}

static int run_sanitizer_self_test(const char *output_path) {
    char input[2048];
    char output[4096];
    _snprintf(input, sizeof(input) - 1,
              "username=%s\r\ncomputername=%s\r\n"
              "path=C:\\Users\\%s\\Private File.txt\r\n"
              "url=https://www.youtube.com/watch?v=private-id\r\n"
              "email= tester@example.com\r\nip=192.168.1.25\r\n"
              "computer_token=[%s]\r\nuser_token=[%s]\r\n"
              "safe=URL,media-stalled",
              g_username, g_computer_name, g_username,
              g_computer_name, g_username);
    input[sizeof(input) - 1] = 0;
    sanitize_text(input, output, sizeof(output));
    if (!write_text_file(output_path, output)) return 2;
    if (!ci_find(output, "username=<redacted>") ||
        !ci_find(output, "computername=<redacted>") ||
        !ci_find(output, "path=<local-path-redacted>") ||
        !ci_find(output, "url=<web-url-redacted>") ||
        !ci_find(output, "email= <email-redacted>") ||
        !ci_find(output, "ip=<ip-address-redacted>") ||
        !ci_find(output, "safe=URL,media-stalled") ||
        (g_computer_name[0] && !ci_find(output, "computer_token=[<computer-name-redacted>]")) ||
        (g_username[0] && !ci_find(output, "user_token=[<username-redacted>]")) ||
        ci_find(output, "youtube.com/watch") || ci_find(output, "example.com") ||
        ci_find(output, "192.168.1.25") || ci_find(output, "Private File")) {
        return 3;
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR ignored, int show) {
    const char *forwarded;
    const char *diagnostic_arguments;
    (void)previous;
    (void)ignored;
    (void)show;
    (void)instance;
    initialize_paths();
    load_sensitive_values();
    SetCurrentDirectoryA(g_base_dir);
    forwarded = skip_first_argument(GetCommandLineA());

    if (ci_starts_with(forwarded, "--diagnostics-self-test=")) {
        const char *path = forwarded + lstrlenA("--diagnostics-self-test=");
        if (*path == '"') {
            static char clean[MAX_PATH];
            DWORD i = 0;
            ++path;
            while (*path && *path != '"' && i + 1 < MAX_PATH) clean[i++] = *path++;
            clean[i] = 0;
            return run_sanitizer_self_test(clean);
        }
        return run_sanitizer_self_test(path);
    }
    if (ci_starts_with(forwarded, "--diagnostics-review")) {
        open_log_folder();
        return 0;
    }
    if (ci_starts_with(forwarded, "--diagnostics-disclosure")) {
        open_disclosure();
        return 0;
    }
    if (ci_starts_with(forwarded, "--diagnostics-reporting-guide")) {
        open_reporting_guide();
        return 0;
    }

    if (ci_starts_with(forwarded, "--diagnostics") &&
        (forwarded[lstrlenA("--diagnostics")] == 0 ||
         isspace((unsigned char)forwarded[lstrlenA("--diagnostics")]))) {
        diagnostic_arguments = skip_first_argument(forwarded);
        if (!confirm_diagnostics_session()) return 0;
        return launch_browser_diagnostics(diagnostic_arguments);
    }
    return launch_browser_disabled(forwarded);
}
