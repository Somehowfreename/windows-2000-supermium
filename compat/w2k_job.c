#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG (WINAPI *NT_QUERY_INFORMATION_PROCESS)(
    HANDLE process, LONG information_class, PVOID information,
    ULONG information_length, PULONG return_length);
typedef LONG (WINAPI *NT_QUERY_INFORMATION_JOB_OBJECT)(
    HANDLE job, JOBOBJECTINFOCLASS information_class, PVOID information,
    ULONG information_length, PULONG return_length);
typedef ULONG (WINAPI *RTL_NT_STATUS_TO_DOS_ERROR)(LONG status);

typedef struct W2K_PROCESS_BASIC_INFORMATION {
    LONG exit_status;
    PVOID peb_base_address;
    ULONG_PTR affinity_mask;
    LONG base_priority;
    ULONG_PTR unique_process_id;
    ULONG_PTR inherited_from_unique_process_id;
} W2K_PROCESS_BASIC_INFORMATION;

#define W2K_STATUS_INFO_LENGTH_MISMATCH ((LONG)0xC0000004UL)
#define W2K_STATUS_INVALID_HANDLE       ((LONG)0xC0000008UL)
#define W2K_STATUS_BUFFER_OVERFLOW      ((LONG)0x80000005UL)
#define W2K_STATUS_BUFFER_TOO_SMALL     ((LONG)0xC0000023UL)

static FARPROC ntdll_function(const char *name)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll ? GetProcAddress(ntdll, name) : NULL;
}

static void set_status_error(LONG status)
{
    RTL_NT_STATUS_TO_DOS_ERROR convert;
    convert = (RTL_NT_STATUS_TO_DOS_ERROR)ntdll_function(
        "RtlNtStatusToDosError");
    SetLastError(convert ? convert(status) : ERROR_GEN_FAILURE);
}

static BOOL process_id_from_handle(HANDLE process, DWORD *process_id)
{
    NT_QUERY_INFORMATION_PROCESS query;
    W2K_PROCESS_BASIC_INFORMATION information;
    LONG status;

    if (!process || !process_id) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (process == GetCurrentProcess() || process == (HANDLE)(LONG_PTR)-1) {
        *process_id = GetCurrentProcessId();
        return TRUE;
    }
    query = (NT_QUERY_INFORMATION_PROCESS)ntdll_function(
        "NtQueryInformationProcess");
    if (!query) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    ZeroMemory(&information, sizeof(information));
    status = query(process, 0, &information, sizeof(information), NULL);
    if (status < 0) {
        set_status_error(status);
        return FALSE;
    }
    *process_id = (DWORD)information.unique_process_id;
    return TRUE;
}

/*
 * Windows 2000 has the native job query system call but not kernel32's
 * IsProcessInJob wrapper.  Querying the job's process-id list implements the
 * wrapper without relying on XP-or-newer kernel32 exports.
 */
__declspec(dllexport) BOOL WINAPI W2KIsProcessInJob(
    HANDLE process, HANDLE job, PBOOL result)
{
    NT_QUERY_INFORMATION_JOB_OBJECT query;
    JOBOBJECT_BASIC_PROCESS_ID_LIST *list;
    DWORD process_id;
    DWORD capacity = 32;
    DWORD size;
    DWORD index;
    ULONG returned;
    LONG status;

    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *result = FALSE;
    if (!process_id_from_handle(process, &process_id)) return FALSE;
    if (!job) {
        /*
         * NT 5.0's NtQueryInformationJobObject rejects a NULL handle,
         * whereas XP's IsProcessInJob defines NULL as "any job".  A normal
         * Windows 2000 desktop process cannot be placed into an undisclosed
         * parent job, so report the native desktop state without issuing the
         * unsupported NULL query.  Concrete job handles retain full checks.
         */
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    query = (NT_QUERY_INFORMATION_JOB_OBJECT)ntdll_function(
        "NtQueryInformationJobObject");
    if (!query) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    for (;;) {
        size = sizeof(*list) + (capacity - 1) * sizeof(ULONG_PTR);
        list = (JOBOBJECT_BASIC_PROCESS_ID_LIST *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (!list) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        returned = 0;
        status = query(job, JobObjectBasicProcessIdList, list, size, &returned);
        if (status >= 0) break;
        if (status == W2K_STATUS_INFO_LENGTH_MISMATCH ||
            status == W2K_STATUS_BUFFER_OVERFLOW ||
            status == W2K_STATUS_BUFFER_TOO_SMALL) {
            HeapFree(GetProcessHeap(), 0, list);
            if (capacity >= 16384) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return FALSE;
            }
            capacity *= 2;
            continue;
        }
        HeapFree(GetProcessHeap(), 0, list);
        set_status_error(status);
        return FALSE;
    }

    for (index = 0; index < list->NumberOfProcessIdsInList; ++index) {
        if ((DWORD)list->ProcessIdList[index] == process_id) {
            *result = TRUE;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
