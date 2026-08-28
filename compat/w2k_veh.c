#include <windows.h>

#ifndef EXCEPTION_CONTINUE_EXECUTION
#define EXCEPTION_CONTINUE_EXECUTION ((LONG)-1)
#endif

typedef LONG (WINAPI *W2K_VECTORED_HANDLER)(PEXCEPTION_POINTERS ExceptionInfo);
typedef BYTE (WINAPI *W2K_RTL_DISPATCH_EXCEPTION)(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord);
typedef BOOL (WINAPI *W2K_CONVERT_STRING_SID_TO_SID_W)(
    LPCWSTR StringSid,
    PSID *Sid);

typedef struct _W2K_VEH_ENTRY {
    LIST_ENTRY Links;
    W2K_VECTORED_HANDLER Handler;
    LONG References;
    BOOL Deleted;
} W2K_VEH_ENTRY;

typedef struct _W2K_DELAY_IMPORT_DESCRIPTOR {
    DWORD Attributes;
    DWORD DllNameRva;
    DWORD ModuleHandleRva;
    DWORD ImportAddressTableRva;
    DWORD ImportNameTableRva;
    DWORD BoundImportAddressTableRva;
    DWORD UnloadImportAddressTableRva;
    DWORD TimeStamp;
} W2K_DELAY_IMPORT_DESCRIPTOR;

static CRITICAL_SECTION g_lock;
static LIST_ENTRY g_handlers;
static BOOL g_initialized;
static BOOL g_hook_installed;
static BYTE *g_dispatch_call_operand;
static LONG g_original_call_displacement;
static W2K_RTL_DISPATCH_EXCEPTION g_original_dispatch;
static W2K_CONVERT_STRING_SID_TO_SID_W g_original_convert_string_sid_to_sid_w;

#define W2K_GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001UL
#define W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002UL
#define W2K_GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004UL

#ifndef W2K_SID_DIAGNOSTICS
#define W2K_SID_DIAGNOSTICS 0
#endif

static void sid_hook_diagnostic(const CHAR *message)
{
#if W2K_SID_DIAGNOSTICS
    HANDLE file;
    DWORD length = 0;
    DWORD written;
    if (!message) return;
    while (message[length]) ++length;
    file = CreateFileA("C:\\W2KLAB\\sid-hook-c10.log", GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    SetFilePointer(file, 0, NULL, FILE_END);
    WriteFile(file, message, length, &written, NULL);
    CloseHandle(file);
#else
    (void)message;
#endif
}

static BOOL ascii_equal(const CHAR *left, const CHAR *right)
{
    if (!left || !right) return FALSE;
    while (*left && *right) {
        CHAR a = *left++;
        CHAR b = *right++;
        if (a >= 'A' && a <= 'Z') a = (CHAR)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (CHAR)(b - 'A' + 'a');
        if (a != b) return FALSE;
    }
    return *left == *right;
}

static BOOL is_everyone_sddl_alias(LPCWSTR value)
{
    return value && value[0] == L'W' && value[1] == L'D' && value[2] == L'\0';
}

__declspec(dllexport) BOOL WINAPI W2KConvertStringSidToSidW(
    LPCWSTR string_sid,
    PSID *sid)
{
    HMODULE advapi;

    if (!g_original_convert_string_sid_to_sid_w) {
        advapi = GetModuleHandleA("ADVAPI32.dll");
        if (!advapi) advapi = LoadLibraryA("ADVAPI32.dll");
        if (advapi) {
            g_original_convert_string_sid_to_sid_w =
                (W2K_CONVERT_STRING_SID_TO_SID_W)GetProcAddress(
                    advapi, "ConvertStringSidToSidW");
        }
    }
    if (!g_original_convert_string_sid_to_sid_w) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    /* Windows 2000 accepts numeric SIDs but not the newer two-letter SDDL
       aliases. Chromium uses WD (Everyone) while applying its deny-execute
       ACE to writable handles passed to sandboxed processes. */
    sid_hook_diagnostic("CALL\r\n");
    if (is_everyone_sddl_alias(string_sid)) {
        sid_hook_diagnostic("ALIAS_WD\r\n");
        string_sid = L"S-1-1-0";
    }
    if (g_original_convert_string_sid_to_sid_w(string_sid, sid)) {
        sid_hook_diagnostic("CONVERT_PASS\r\n");
        return TRUE;
    }
    sid_hook_diagnostic("CONVERT_FAIL\r\n");
    return FALSE;
}

static BOOL patch_convert_string_sid_import(HMODULE module)
{
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;
    IMAGE_THUNK_DATA *names;
    IMAGE_THUNK_DATA *iat;
    DWORD old_protection;
    DWORD ignored_protection;

    if (!module) return FALSE;
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        !nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress) {
        return FALSE;
    }

    descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(
        (BYTE *)module +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    while (descriptor->Name) {
        CHAR *dll_name = (CHAR *)module + descriptor->Name;
        if (!ascii_equal(dll_name, "ADVAPI32.dll")) {
            ++descriptor;
            continue;
        }

        names = (IMAGE_THUNK_DATA *)((BYTE *)module + descriptor->OriginalFirstThunk);
        iat = (IMAGE_THUNK_DATA *)((BYTE *)module + descriptor->FirstThunk);
        if (!descriptor->OriginalFirstThunk) return FALSE;
        while (names->u1.AddressOfData) {
            if (!(names->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                IMAGE_IMPORT_BY_NAME *import_name = (IMAGE_IMPORT_BY_NAME *)(
                    (BYTE *)module + names->u1.AddressOfData);
                if (ascii_equal((CHAR *)import_name->Name,
                                "ConvertStringSidToSidW")) {
                    g_original_convert_string_sid_to_sid_w =
                        (W2K_CONVERT_STRING_SID_TO_SID_W)(ULONG_PTR)iat->u1.Function;
                    if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function),
                                        PAGE_READWRITE, &old_protection)) {
                        return FALSE;
                    }
                    iat->u1.Function = (DWORD)(ULONG_PTR)W2KConvertStringSidToSidW;
                    FlushInstructionCache(GetCurrentProcess(), &iat->u1.Function,
                                          sizeof(iat->u1.Function));
                    VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function),
                                   old_protection, &ignored_protection);
                    return TRUE;
                }
            }
            ++names;
            ++iat;
        }
        return FALSE;
    }
    return FALSE;
}

static BOOL patch_convert_string_sid_delay_import(HMODULE module)
{
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    W2K_DELAY_IMPORT_DESCRIPTOR *descriptor;
    IMAGE_THUNK_DATA *names;
    IMAGE_THUNK_DATA *iat;
    DWORD old_protection;
    DWORD ignored_protection;

    if (!module) return FALSE;
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        !nt->OptionalHeader.DataDirectory[13].VirtualAddress) {
        return FALSE;
    }

    descriptor = (W2K_DELAY_IMPORT_DESCRIPTOR *)(
        (BYTE *)module + nt->OptionalHeader.DataDirectory[13].VirtualAddress);
    while (descriptor->DllNameRva) {
        CHAR *dll_name;
        if (!(descriptor->Attributes & 1)) return FALSE;
        dll_name = (CHAR *)module + descriptor->DllNameRva;
        if (!ascii_equal(dll_name, "ADVAPI32.dll")) {
            ++descriptor;
            continue;
        }

        names = (IMAGE_THUNK_DATA *)(
            (BYTE *)module + descriptor->ImportNameTableRva);
        iat = (IMAGE_THUNK_DATA *)(
            (BYTE *)module + descriptor->ImportAddressTableRva);
        while (names->u1.AddressOfData) {
            if (!(names->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                IMAGE_IMPORT_BY_NAME *import_name = (IMAGE_IMPORT_BY_NAME *)(
                    (BYTE *)module + names->u1.AddressOfData);
                if (ascii_equal((CHAR *)import_name->Name,
                                "ConvertStringSidToSidW")) {
                    /* Do not retain the delay-loader thunk as the original.
                       The bridge resolves the native ADVAPI32 export lazily. */
                    if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function),
                                        PAGE_READWRITE, &old_protection)) {
                        return FALSE;
                    }
                    iat->u1.Function = (DWORD)(ULONG_PTR)W2KConvertStringSidToSidW;
                    FlushInstructionCache(GetCurrentProcess(), &iat->u1.Function,
                                          sizeof(iat->u1.Function));
                    VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function),
                                   old_protection, &ignored_protection);
                    return TRUE;
                }
            }
            ++names;
            ++iat;
        }
        return FALSE;
    }
    return FALSE;
}

static BOOL g_chrome_sid_hook_installed;

static void ensure_chrome_sid_hook(void)
{
    HMODULE chrome_module;
    if (g_chrome_sid_hook_installed) return;
    chrome_module = GetModuleHandleA("chrome.dll");
    if (!chrome_module) return;
    sid_hook_diagnostic("ENSURE_CHROME_FOUND\r\n");
    if (patch_convert_string_sid_import(chrome_module) ||
        patch_convert_string_sid_delay_import(chrome_module)) {
        g_chrome_sid_hook_installed = TRUE;
        sid_hook_diagnostic("ENSURE_CHROME_PATCH_PASS\r\n");
    } else {
        sid_hook_diagnostic("ENSURE_CHROME_PATCH_FAIL\r\n");
    }
}

static void remove_list_entry(LIST_ENTRY *entry)
{
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
}

static void insert_list_head(LIST_ENTRY *head, LIST_ENTRY *entry)
{
    entry->Flink = head->Flink;
    entry->Blink = head;
    head->Flink->Blink = entry;
    head->Flink = entry;
}

static void insert_list_tail(LIST_ENTRY *head, LIST_ENTRY *entry)
{
    entry->Flink = head;
    entry->Blink = head->Blink;
    head->Blink->Flink = entry;
    head->Blink = entry;
}

static BOOL call_vectored_handlers(
    PEXCEPTION_RECORD exception_record,
    PCONTEXT context_record)
{
    LIST_ENTRY *current;
    W2K_VEH_ENTRY *entry;
    EXCEPTION_POINTERS pointers;
    LONG disposition = EXCEPTION_CONTINUE_SEARCH;

    pointers.ExceptionRecord = exception_record;
    pointers.ContextRecord = context_record;

    EnterCriticalSection(&g_lock);
    current = g_handlers.Flink;
    while (current != &g_handlers) {
        entry = CONTAINING_RECORD(current, W2K_VEH_ENTRY, Links);
        if (entry->Deleted) {
            current = current->Flink;
            continue;
        }

        ++entry->References;
        LeaveCriticalSection(&g_lock);
        disposition = entry->Handler(&pointers);
        EnterCriticalSection(&g_lock);
        --entry->References;

        if (entry->References == 0) {
            current = entry->Links.Flink;
            remove_list_entry(&entry->Links);
            LeaveCriticalSection(&g_lock);
            HeapFree(GetProcessHeap(), 0, entry);
            if (disposition == EXCEPTION_CONTINUE_EXECUTION) return TRUE;
            EnterCriticalSection(&g_lock);
        } else {
            if (disposition == EXCEPTION_CONTINUE_EXECUTION) {
                LeaveCriticalSection(&g_lock);
                return TRUE;
            }
            current = entry->Links.Flink;
        }
    }
    LeaveCriticalSection(&g_lock);
    return FALSE;
}

static BYTE WINAPI w2k_dispatch_exception(
    PEXCEPTION_RECORD exception_record,
    PCONTEXT context_record)
{
    if (call_vectored_handlers(exception_record, context_record)) return TRUE;
    return g_original_dispatch(exception_record, context_record);
}

static BOOL install_dispatch_hook_locked(void)
{
    HMODULE ntdll;
    BYTE *dispatcher;
    LONG new_displacement;
    DWORD old_protection;
    DWORD ignored_protection;

    if (g_hook_installed) return TRUE;

    ntdll = GetModuleHandleA("ntdll.dll");
    dispatcher = (BYTE *)GetProcAddress(ntdll, "KiUserExceptionDispatcher");
    if (!dispatcher ||
        dispatcher[0] != 0x8B || dispatcher[1] != 0x4C ||
        dispatcher[2] != 0x24 || dispatcher[3] != 0x04 ||
        dispatcher[4] != 0x8B || dispatcher[5] != 0x1C ||
        dispatcher[6] != 0x24 || dispatcher[7] != 0x51 ||
        dispatcher[8] != 0x53 || dispatcher[9] != 0xE8) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    g_dispatch_call_operand = dispatcher + 10;
    g_original_call_displacement = *(LONG *)g_dispatch_call_operand;
    g_original_dispatch = (W2K_RTL_DISPATCH_EXCEPTION)(
        dispatcher + 14 + g_original_call_displacement);
    new_displacement = (LONG)((BYTE *)w2k_dispatch_exception - (dispatcher + 14));

    if (!VirtualProtect(g_dispatch_call_operand, sizeof(LONG),
                        PAGE_EXECUTE_READWRITE, &old_protection)) {
        return FALSE;
    }
    *(LONG *)g_dispatch_call_operand = new_displacement;
    FlushInstructionCache(GetCurrentProcess(), g_dispatch_call_operand, sizeof(LONG));
    VirtualProtect(g_dispatch_call_operand, sizeof(LONG),
                   old_protection, &ignored_protection);
    g_hook_installed = TRUE;
    return TRUE;
}

__declspec(dllexport) PVOID WINAPI W2KAddVectoredExceptionHandler(
    ULONG first_handler,
    W2K_VECTORED_HANDLER handler)
{
    W2K_VEH_ENTRY *entry;

    ensure_chrome_sid_hook();

    if (!handler) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    entry = (W2K_VEH_ENTRY *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
    if (!entry) return NULL;
    entry->Handler = handler;
    entry->References = 1;

    EnterCriticalSection(&g_lock);
    if (!install_dispatch_hook_locked()) {
        LeaveCriticalSection(&g_lock);
        HeapFree(GetProcessHeap(), 0, entry);
        return NULL;
    }
    if (first_handler) insert_list_head(&g_handlers, &entry->Links);
    else insert_list_tail(&g_handlers, &entry->Links);
    LeaveCriticalSection(&g_lock);
    return entry;
}

__declspec(dllexport) ULONG WINAPI W2KRemoveVectoredExceptionHandler(PVOID handle)
{
    LIST_ENTRY *current;
    W2K_VEH_ENTRY *entry;
    W2K_VEH_ENTRY *free_entry = NULL;
    ULONG found = FALSE;

    ensure_chrome_sid_hook();

    if (!handle) return FALSE;

    EnterCriticalSection(&g_lock);
    current = g_handlers.Flink;
    while (current != &g_handlers) {
        entry = CONTAINING_RECORD(current, W2K_VEH_ENTRY, Links);
        if (entry == (W2K_VEH_ENTRY *)handle && !entry->Deleted) {
            entry->Deleted = TRUE;
            --entry->References;
            if (entry->References == 0) {
                remove_list_entry(&entry->Links);
                free_entry = entry;
            }
            found = TRUE;
            break;
        }
        current = current->Flink;
    }
    LeaveCriticalSection(&g_lock);

    if (free_entry) HeapFree(GetProcessHeap(), 0, free_entry);
    return found;
}

__declspec(dllexport) BOOL WINAPI W2KGetModuleHandleExA(
    DWORD flags,
    LPCSTR module_name,
    HMODULE *module_out)
{
    HMODULE module;
    MEMORY_BASIC_INFORMATION memory;
    CHAR path[MAX_PATH];
    DWORD supported_flags =
        W2K_GET_MODULE_HANDLE_EX_FLAG_PIN |
        W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
        W2K_GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;

    ensure_chrome_sid_hook();

    if (!module_out || (flags & ~supported_flags) ||
        ((flags & W2K_GET_MODULE_HANDLE_EX_FLAG_PIN) &&
         (flags & W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (flags & W2K_GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        if (!module_name ||
            VirtualQuery((LPCVOID)module_name, &memory, sizeof(memory)) == 0 ||
            memory.Type != MEM_IMAGE) {
            SetLastError(ERROR_MOD_NOT_FOUND);
            return FALSE;
        }
        module = (HMODULE)memory.AllocationBase;
    } else {
        module = GetModuleHandleA(module_name);
        if (!module) return FALSE;
    }

    if (!(flags & W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) {
        if (!GetModuleFileNameA(module, path, sizeof(path))) return FALSE;
        module = LoadLibraryA(path);
        if (!module) return FALSE;
    }

    *module_out = module;
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI W2KGetModuleHandleExW(
    DWORD flags,
    LPCWSTR module_name,
    HMODULE *module_out)
{
    HMODULE module;
    MEMORY_BASIC_INFORMATION memory;
    WCHAR path[MAX_PATH];
    DWORD supported_flags =
        W2K_GET_MODULE_HANDLE_EX_FLAG_PIN |
        W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
        W2K_GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;

    ensure_chrome_sid_hook();

    if (!module_out || (flags & ~supported_flags) ||
        ((flags & W2K_GET_MODULE_HANDLE_EX_FLAG_PIN) &&
         (flags & W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (flags & W2K_GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        if (!module_name ||
            VirtualQuery((LPCVOID)module_name, &memory, sizeof(memory)) == 0 ||
            memory.Type != MEM_IMAGE) {
            SetLastError(ERROR_MOD_NOT_FOUND);
            return FALSE;
        }
        module = (HMODULE)memory.AllocationBase;
    } else {
        module = GetModuleHandleW(module_name);
        if (!module) return FALSE;
    }

    if (!(flags & W2K_GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) {
        if (!GetModuleFileNameW(module, path, sizeof(path) / sizeof(path[0]))) {
            return FALSE;
        }
        module = LoadLibraryW(path);
        if (!module) return FALSE;
    }

    *module_out = module;
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    DWORD old_protection;
    DWORD ignored_protection;

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE main_module;
        HMODULE chrome_module;
        BOOL main_static;
        BOOL main_delay;
        BOOL chrome_static;
        BOOL chrome_delay;
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
        g_handlers.Flink = &g_handlers;
        g_handlers.Blink = &g_handlers;
        g_initialized = TRUE;
        sid_hook_diagnostic("ATTACH\r\n");
        main_module = GetModuleHandleA(NULL);
        chrome_module = GetModuleHandleA("chrome.dll");
        main_static = patch_convert_string_sid_import(main_module);
        main_delay = patch_convert_string_sid_delay_import(main_module);
        chrome_static = patch_convert_string_sid_import(chrome_module);
        chrome_delay = patch_convert_string_sid_delay_import(chrome_module);
        sid_hook_diagnostic(main_module ? "MAIN_FOUND\r\n" : "MAIN_MISSING\r\n");
        sid_hook_diagnostic(chrome_module ? "CHROME_FOUND\r\n" : "CHROME_MISSING\r\n");
        sid_hook_diagnostic(main_static ? "MAIN_STATIC_PASS\r\n" : "MAIN_STATIC_FAIL\r\n");
        sid_hook_diagnostic(main_delay ? "MAIN_DELAY_PASS\r\n" : "MAIN_DELAY_FAIL\r\n");
        sid_hook_diagnostic(chrome_static ? "CHROME_STATIC_PASS\r\n" : "CHROME_STATIC_FAIL\r\n");
        sid_hook_diagnostic(chrome_delay ? "CHROME_DELAY_PASS\r\n" : "CHROME_DELAY_FAIL\r\n");
    } else if (reason == DLL_PROCESS_DETACH && g_initialized) {
        if (g_hook_installed && g_dispatch_call_operand &&
            VirtualProtect(g_dispatch_call_operand, sizeof(LONG),
                           PAGE_EXECUTE_READWRITE, &old_protection)) {
            *(LONG *)g_dispatch_call_operand = g_original_call_displacement;
            FlushInstructionCache(GetCurrentProcess(),
                                  g_dispatch_call_operand, sizeof(LONG));
            VirtualProtect(g_dispatch_call_operand, sizeof(LONG),
                           old_protection, &ignored_protection);
        }
        DeleteCriticalSection(&g_lock);
    }
    return TRUE;
}
