#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <string.h>

#ifndef ERROR_FILENAME_EXCED_RANGE
#define ERROR_FILENAME_EXCED_RANGE 206L
#endif
#define BIF_RETURNONLYFSDIRS 0x0001
#define SPFILENOTIFY_FILEINCABINET 0x00000011
#define SPFILENOTIFY_FILEEXTRACTED 0x00000013
#define FILEOP_ABORT 0
#define FILEOP_DOIT 1

typedef unsigned long W2K_UINT_PTR;
typedef struct W2K_BROWSEINFOA {
    HWND hwndOwner;
    const void *pidlRoot;
    LPSTR pszDisplayName;
    LPCSTR lpszTitle;
    UINT ulFlags;
    void *lpfn;
    LPARAM lParam;
    int iImage;
} W2K_BROWSEINFOA;
typedef struct W2K_FILE_IN_CABINET_INFOA {
    LPCSTR NameInCabinet;
    DWORD FileSize;
    DWORD Win32Error;
    WORD DosDate;
    WORD DosTime;
    WORD DosAttribs;
    char FullTargetName[MAX_PATH];
} W2K_FILE_IN_CABINET_INFOA;
typedef struct W2K_FILEPATHS_A {
    LPCSTR Target;
    LPCSTR Source;
    UINT Win32Error;
    DWORD Flags;
} W2K_FILEPATHS_A;
typedef UINT (CALLBACK *W2K_CAB_CALLBACK)(PVOID, UINT, W2K_UINT_PTR, W2K_UINT_PTR);
typedef BOOL (WINAPI *PFN_SETUP_ITERATE_CABINET_A)(PCSTR, DWORD, W2K_CAB_CALLBACK, PVOID);
typedef void *(WINAPI *PFN_SH_BROWSE_FOR_FOLDER_A)(W2K_BROWSEINFOA *);
typedef BOOL (WINAPI *PFN_SH_GET_PATH_FROM_ID_LIST_A)(const void *, LPSTR);

#define FOOTER_MAGIC "W2KSFX1!"
#define FOOTER_SIZE 208
#define COPY_BUFFER_SIZE 65536

#pragma pack(push, 1)
typedef struct SfxFooter {
    char magic[8];
    DWORD payload_size;
    DWORD payload_crc32;
    char folder_name[96];
    char display_name[96];
} SfxFooter;
#pragma pack(pop)

typedef struct ExtractContext {
    char destination[MAX_PATH];
    DWORD files_seen;
    DWORD files_extracted;
    DWORD failed;
} ExtractContext;

static DWORD crc_table[256];

static void init_crc32(void) {
    DWORD i, j, c;
    for (i = 0; i < 256; ++i) {
        c = i;
        for (j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
}

static DWORD update_crc32(DWORD crc, const BYTE *data, DWORD size) {
    DWORD i;
    for (i = 0; i < size; ++i)
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static int safe_relative_name(const char *name) {
    const char *p;
    if (!name || !*name) return 0;
    if (name[0] == '\\' || name[0] == '/' || name[0] == '.') {
        if (name[0] != '.' || name[1] != '\0') return 0;
    }
    if (lstrlenA(name) >= MAX_PATH - 2) return 0;
    for (p = name; *p; ++p) {
        if (*p == ':') return 0;
        if ((p == name || p[-1] == '\\' || p[-1] == '/') &&
            p[0] == '.' && p[1] == '.' &&
            (p[2] == '\0' || p[2] == '\\' || p[2] == '/')) return 0;
    }
    return 1;
}

static int ensure_directory_tree(const char *path) {
    char copy[MAX_PATH];
    char *p;
    DWORD attrs;
    if (lstrlenA(path) >= MAX_PATH) return 0;
    lstrcpyA(copy, path);
    for (p = copy + 3; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';
            attrs = GetFileAttributesA(copy);
            if (attrs == INVALID_FILE_ATTRIBUTES && !CreateDirectoryA(copy, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS) return 0;
            *p = saved;
        }
    }
    attrs = GetFileAttributesA(copy);
    if (attrs == INVALID_FILE_ATTRIBUTES && !CreateDirectoryA(copy, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    return 1;
}

static int ensure_parent_directory(const char *file_path) {
    char parent[MAX_PATH];
    char *p;
    if (lstrlenA(file_path) >= MAX_PATH) return 0;
    lstrcpyA(parent, file_path);
    p = parent + lstrlenA(parent);
    while (p > parent && p[-1] != '\\' && p[-1] != '/') --p;
    if (p <= parent + 3) return 1;
    p[-1] = '\0';
    return ensure_directory_tree(parent);
}

static UINT CALLBACK cabinet_callback(PVOID context_ptr, UINT notification,
                                      W2K_UINT_PTR param1, W2K_UINT_PTR param2) {
    ExtractContext *context = (ExtractContext *)context_ptr;
    if (notification == SPFILENOTIFY_FILEINCABINET) {
        W2K_FILE_IN_CABINET_INFOA *info = (W2K_FILE_IN_CABINET_INFOA *)param1;
        char full[MAX_PATH];
        const char *name = info->NameInCabinet;
        int base_len, name_len;
        context->files_seen++;
        if (!safe_relative_name(name)) {
            context->failed = ERROR_INVALID_NAME;
            return FILEOP_ABORT;
        }
        base_len = lstrlenA(context->destination);
        name_len = lstrlenA(name);
        if (base_len + 1 + name_len >= MAX_PATH) {
            context->failed = ERROR_FILENAME_EXCED_RANGE;
            return FILEOP_ABORT;
        }
        lstrcpyA(full, context->destination);
        if (base_len && full[base_len - 1] != '\\') lstrcatA(full, "\\");
        lstrcatA(full, name);
        if (!ensure_parent_directory(full)) {
            context->failed = GetLastError();
            return FILEOP_ABORT;
        }
        lstrcpynA(info->FullTargetName, full, MAX_PATH);
        return FILEOP_DOIT;
    }
    if (notification == SPFILENOTIFY_FILEEXTRACTED) {
        W2K_FILEPATHS_A *paths = (W2K_FILEPATHS_A *)param1;
        if (paths && paths->Win32Error != NO_ERROR) {
            context->failed = paths->Win32Error;
            return FILEOP_ABORT;
        }
        context->files_extracted++;
    }
    return NO_ERROR;
}

static int choose_destination(char *selected, const char *display_name) {
    W2K_BROWSEINFOA browse;
    PFN_SH_BROWSE_FOR_FOLDER_A browse_for_folder;
    PFN_SH_GET_PATH_FROM_ID_LIST_A get_path;
    HMODULE shell;
    void *item;
    char prompt[256];
    int ok = 0;
    shell = LoadLibraryA("shell32.dll");
    if (!shell) return 0;
    browse_for_folder = (PFN_SH_BROWSE_FOR_FOLDER_A)GetProcAddress(shell, "SHBrowseForFolderA");
    get_path = (PFN_SH_GET_PATH_FROM_ID_LIST_A)GetProcAddress(shell, "SHGetPathFromIDListA");
    if (!browse_for_folder || !get_path) { FreeLibrary(shell); return 0; }
    ZeroMemory(&browse, sizeof(browse));
    wsprintfA(prompt, "Choose the parent folder for %s", display_name);
    browse.hwndOwner = NULL;
    browse.pszDisplayName = selected;
    browse.lpszTitle = prompt;
    browse.ulFlags = BIF_RETURNONLYFSDIRS;
    item = browse_for_folder(&browse);
    if (item) ok = get_path(item, selected) != 0;
    FreeLibrary(shell);
    return ok;
}

static void show_win32_error(const char *prefix, DWORD error) {
    char message[512];
    char detail[320];
    DWORD got = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               NULL, error, 0, detail, sizeof(detail), NULL);
    if (!got) wsprintfA(detail, "Windows error %lu", error);
    wsprintfA(message, "%s\r\n\r\n%s", prefix, detail);
    MessageBoxA(NULL, message, "Windows 2000 Self-Extractor", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show) {
    char self[MAX_PATH], temp_dir[MAX_PATH], temp_cab[MAX_PATH];
    char selected[MAX_PATH], destination[MAX_PATH], message[512];
    SfxFooter footer;
    ExtractContext context;
    HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    BYTE *buffer = NULL;
    DWORD file_size, read_bytes, written, remaining, crc, error = NO_ERROR;
    LONG payload_offset;
    BOOL iterated;
    HMODULE setupapi = NULL;
    PFN_SETUP_ITERATE_CABINET_A iterate_cabinet = NULL;

    (void)instance; (void)previous; (void)command_line; (void)show;
    ZeroMemory(&footer, sizeof(footer));
    ZeroMemory(&context, sizeof(context));
    init_crc32();
    temp_cab[0] = '\0';

    if (!GetModuleFileNameA(NULL, self, MAX_PATH)) {
        show_win32_error("Could not locate the self-extractor.", GetLastError());
        return 2;
    }
    input = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        show_win32_error("Could not open the self-extractor.", GetLastError());
        return 3;
    }
    file_size = GetFileSize(input, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size < FOOTER_SIZE) {
        error = ERROR_BAD_FORMAT;
        goto fail;
    }
    if (SetFilePointer(input, -(LONG)FOOTER_SIZE, NULL, FILE_END) == INVALID_SET_FILE_POINTER ||
        !ReadFile(input, &footer, sizeof(footer), &read_bytes, NULL) || read_bytes != sizeof(footer) ||
        memcmp(footer.magic, FOOTER_MAGIC, 8) != 0 ||
        footer.payload_size == 0 || footer.payload_size > file_size - FOOTER_SIZE ||
        footer.folder_name[0] == '\0' || footer.display_name[0] == '\0') {
        error = ERROR_BAD_FORMAT;
        goto fail;
    }
    footer.folder_name[95] = '\0';
    footer.display_name[95] = '\0';
    if (!safe_relative_name(footer.folder_name) ||
        strchr(footer.folder_name, '\\') || strchr(footer.folder_name, '/')) {
        error = ERROR_INVALID_NAME;
        goto fail;
    }

    wsprintfA(message,
        "%s\r\n\r\nThis self-extractor runs on an unmodified Windows 2000 installation. "
        "It only extracts files; it does not install or change Windows.\r\n\r\n"
        "You will now choose a parent folder. A new folder named:\r\n%s\r\n"
        "will be created inside it.", footer.display_name, footer.folder_name);
    if (MessageBoxA(NULL, message, "Windows 2000 Self-Extractor",
                    MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        CloseHandle(input);
        return 0;
    }
    selected[0] = '\0';
    if (!choose_destination(selected, footer.display_name)) {
        CloseHandle(input);
        return 0;
    }
    if (lstrlenA(selected) + 1 + lstrlenA(footer.folder_name) >= MAX_PATH) {
        error = ERROR_FILENAME_EXCED_RANGE;
        goto fail;
    }
    lstrcpyA(destination, selected);
    if (destination[lstrlenA(destination) - 1] != '\\') lstrcatA(destination, "\\");
    lstrcatA(destination, footer.folder_name);
    if (GetFileAttributesA(destination) != INVALID_FILE_ATTRIBUTES) {
        if (MessageBoxA(NULL,
            "The destination folder already exists. Existing files with matching names may be replaced. Continue?",
            "Windows 2000 Self-Extractor", MB_YESNO | MB_ICONWARNING) != IDYES) {
            CloseHandle(input);
            return 0;
        }
    }
    if (!ensure_directory_tree(destination)) {
        error = GetLastError();
        goto fail;
    }

    if (!GetTempPathA(MAX_PATH, temp_dir) ||
        !GetTempFileNameA(temp_dir, "W2K", 0, temp_cab)) {
        error = GetLastError();
        goto fail;
    }
    output = CreateFileA(temp_cab, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto fail;
    }
    buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, COPY_BUFFER_SIZE);
    if (!buffer) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto fail;
    }
    payload_offset = (LONG)(file_size - FOOTER_SIZE - footer.payload_size);
    if (SetFilePointer(input, payload_offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        error = GetLastError();
        goto fail;
    }
    remaining = footer.payload_size;
    crc = 0xFFFFFFFFUL;
    while (remaining) {
        DWORD request = remaining > COPY_BUFFER_SIZE ? COPY_BUFFER_SIZE : remaining;
        if (!ReadFile(input, buffer, request, &read_bytes, NULL) || read_bytes != request) {
            error = GetLastError();
            if (!error) error = ERROR_READ_FAULT;
            goto fail;
        }
        crc = update_crc32(crc, buffer, read_bytes);
        if (!WriteFile(output, buffer, read_bytes, &written, NULL) || written != read_bytes) {
            error = GetLastError();
            if (!error) error = ERROR_WRITE_FAULT;
            goto fail;
        }
        remaining -= read_bytes;
    }
    crc ^= 0xFFFFFFFFUL;
    CloseHandle(output); output = INVALID_HANDLE_VALUE;
    CloseHandle(input); input = INVALID_HANDLE_VALUE;
    HeapFree(GetProcessHeap(), 0, buffer); buffer = NULL;
    if (crc != footer.payload_crc32) {
        error = ERROR_CRC;
        goto fail;
    }

    setupapi = LoadLibraryA("setupapi.dll");
    if (!setupapi) { error = GetLastError(); goto fail; }
    iterate_cabinet = (PFN_SETUP_ITERATE_CABINET_A)GetProcAddress(setupapi, "SetupIterateCabinetA");
    if (!iterate_cabinet) { error = GetLastError(); goto fail; }
    lstrcpynA(context.destination, destination, MAX_PATH);
    iterated = iterate_cabinet(temp_cab, 0, cabinet_callback, &context);
    FreeLibrary(setupapi); setupapi = NULL;
    DeleteFileA(temp_cab); temp_cab[0] = '\0';
    if (!iterated || context.failed) {
        error = context.failed ? context.failed : GetLastError();
        if (!error) error = ERROR_INVALID_DATA;
        goto fail_no_temp;
    }
    wsprintfA(message,
        "Extraction completed successfully.\r\n\r\nFiles extracted: %lu\r\nDestination:\r\n%s",
        context.files_extracted, destination);
    MessageBoxA(NULL, message, "Windows 2000 Self-Extractor", MB_OK | MB_ICONINFORMATION);
    return 0;

fail:
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    if (setupapi) FreeLibrary(setupapi);
    if (temp_cab[0]) DeleteFileA(temp_cab);
fail_no_temp:
    show_win32_error("Extraction failed. No system settings were changed.", error);
    return 1;
}
