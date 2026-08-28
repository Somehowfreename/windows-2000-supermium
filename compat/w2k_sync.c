#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/*
 * Native Windows 2000 implementation of the Vista SRW-lock and condition-
 * variable APIs used by Chromium.  The public Windows objects are one pointer
 * wide, so each slot lazily owns a separately allocated state object.
 */

#define W2K_CONDITION_VARIABLE_LOCKMODE_SHARED 0x00000001UL

typedef struct W2K_SRW_STATE {
    CRITICAL_SECTION gate;
    HANDLE can_read;
    HANDLE can_write;
    LONG readers;
    LONG writer_active;
    LONG writers_waiting;
} W2K_SRW_STATE;

typedef struct W2K_CV_WAITER {
    struct W2K_CV_WAITER *next;
    HANDLE event;
    BOOL queued;
} W2K_CV_WAITER;

typedef struct W2K_CV_STATE {
    CRITICAL_SECTION gate;
    W2K_CV_WAITER *head;
    W2K_CV_WAITER *tail;
} W2K_CV_STATE;

typedef LONG (WINAPI *W2K_NT_QUERY_INFORMATION_PROCESS)(
    HANDLE process, LONG information_class, PVOID information,
    ULONG information_length, PULONG return_length);

typedef BOOL (WINAPI *W2K_QUERY_INFORMATION_JOB_OBJECT)(
    HANDLE job, JOBOBJECTINFOCLASS information_class, PVOID information,
    DWORD information_length, LPDWORD return_length);

typedef struct W2K_PROCESS_BASIC_INFORMATION {
    LONG exit_status;
    PVOID peb_base_address;
    ULONG_PTR affinity_mask;
    LONG base_priority;
    ULONG_PTR unique_process_id;
    ULONG_PTR inherited_from_unique_process_id;
} W2K_PROCESS_BASIC_INFORMATION;

static void destroy_srw_state(W2K_SRW_STATE *state)
{
    if (!state) return;
    if (state->can_read) CloseHandle(state->can_read);
    if (state->can_write) CloseHandle(state->can_write);
    DeleteCriticalSection(&state->gate);
    HeapFree(GetProcessHeap(), 0, state);
}

static W2K_SRW_STATE *get_srw_state(void **slot)
{
    W2K_SRW_STATE *state;
    W2K_SRW_STATE *winner;

    state = (W2K_SRW_STATE *)*slot;
    if (state) return state;
    state = (W2K_SRW_STATE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       sizeof(*state));
    if (!state) return NULL;
    InitializeCriticalSection(&state->gate);
    state->can_read = CreateEventA(NULL, TRUE, TRUE, NULL);
    state->can_write = CreateEventA(NULL, TRUE, TRUE, NULL);
    if (!state->can_read || !state->can_write) {
        destroy_srw_state(state);
        return NULL;
    }
    winner = (W2K_SRW_STATE *)(LONG_PTR)InterlockedCompareExchange(
        (LONG volatile *)slot, (LONG)(LONG_PTR)state, 0);
    if (winner) {
        destroy_srw_state(state);
        return winner;
    }
    return state;
}

static W2K_CV_STATE *get_cv_state(void **slot)
{
    W2K_CV_STATE *state;
    W2K_CV_STATE *winner;

    state = (W2K_CV_STATE *)*slot;
    if (state) return state;
    state = (W2K_CV_STATE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      sizeof(*state));
    if (!state) return NULL;
    InitializeCriticalSection(&state->gate);
    winner = (W2K_CV_STATE *)(LONG_PTR)InterlockedCompareExchange(
        (LONG volatile *)slot, (LONG)(LONG_PTR)state, 0);
    if (winner) {
        DeleteCriticalSection(&state->gate);
        HeapFree(GetProcessHeap(), 0, state);
        return winner;
    }
    return state;
}

static void acquire_exclusive_state(W2K_SRW_STATE *state)
{
    EnterCriticalSection(&state->gate);
    ++state->writers_waiting;
    ResetEvent(state->can_read);
    while (state->writer_active || state->readers != 0) {
        ResetEvent(state->can_write);
        LeaveCriticalSection(&state->gate);
        WaitForSingleObject(state->can_write, INFINITE);
        EnterCriticalSection(&state->gate);
    }
    --state->writers_waiting;
    state->writer_active = 1;
    ResetEvent(state->can_write);
    LeaveCriticalSection(&state->gate);
}

static BOOL try_acquire_exclusive_state(W2K_SRW_STATE *state)
{
    BOOL acquired = FALSE;
    EnterCriticalSection(&state->gate);
    if (!state->writer_active && state->readers == 0) {
        state->writer_active = 1;
        ResetEvent(state->can_write);
        ResetEvent(state->can_read);
        acquired = TRUE;
    }
    LeaveCriticalSection(&state->gate);
    return acquired;
}

static void release_exclusive_state(W2K_SRW_STATE *state)
{
    EnterCriticalSection(&state->gate);
    state->writer_active = 0;
    SetEvent(state->can_write);
    if (state->writers_waiting == 0) SetEvent(state->can_read);
    LeaveCriticalSection(&state->gate);
}

static void acquire_shared_state(W2K_SRW_STATE *state)
{
    EnterCriticalSection(&state->gate);
    while (state->writer_active || state->writers_waiting != 0) {
        ResetEvent(state->can_read);
        LeaveCriticalSection(&state->gate);
        WaitForSingleObject(state->can_read, INFINITE);
        EnterCriticalSection(&state->gate);
    }
    ++state->readers;
    if (state->readers == 1) ResetEvent(state->can_write);
    LeaveCriticalSection(&state->gate);
}

static BOOL try_acquire_shared_state(W2K_SRW_STATE *state)
{
    BOOL acquired = FALSE;
    EnterCriticalSection(&state->gate);
    if (!state->writer_active && state->writers_waiting == 0) {
        ++state->readers;
        if (state->readers == 1) ResetEvent(state->can_write);
        acquired = TRUE;
    }
    LeaveCriticalSection(&state->gate);
    return acquired;
}

static void release_shared_state(W2K_SRW_STATE *state)
{
    EnterCriticalSection(&state->gate);
    if (state->readers > 0) --state->readers;
    if (state->readers == 0) {
        SetEvent(state->can_write);
        if (state->writers_waiting == 0) SetEvent(state->can_read);
    }
    LeaveCriticalSection(&state->gate);
}

__declspec(dllexport) VOID WINAPI W2KInitializeSRWLock(void **slot)
{
    if (slot) *slot = NULL;
}

__declspec(dllexport) VOID WINAPI W2KAcquireSRWLockExclusive(void **slot)
{
    W2K_SRW_STATE *state = get_srw_state(slot);
    if (state) acquire_exclusive_state(state);
}

__declspec(dllexport) VOID WINAPI W2KAcquireSRWLockShared(void **slot)
{
    W2K_SRW_STATE *state = get_srw_state(slot);
    if (state) acquire_shared_state(state);
}

__declspec(dllexport) VOID WINAPI W2KReleaseSRWLockExclusive(void **slot)
{
    W2K_SRW_STATE *state = slot ? (W2K_SRW_STATE *)*slot : NULL;
    if (state) release_exclusive_state(state);
}

__declspec(dllexport) VOID WINAPI W2KReleaseSRWLockShared(void **slot)
{
    W2K_SRW_STATE *state = slot ? (W2K_SRW_STATE *)*slot : NULL;
    if (state) release_shared_state(state);
}

__declspec(dllexport) BOOLEAN WINAPI W2KTryAcquireSRWLockExclusive(void **slot)
{
    W2K_SRW_STATE *state = get_srw_state(slot);
    return state && try_acquire_exclusive_state(state) ? TRUE : FALSE;
}

__declspec(dllexport) BOOLEAN WINAPI W2KTryAcquireSRWLockShared(void **slot)
{
    W2K_SRW_STATE *state = get_srw_state(slot);
    return state && try_acquire_shared_state(state) ? TRUE : FALSE;
}

__declspec(dllexport) VOID WINAPI W2KInitializeConditionVariable(void **slot)
{
    if (slot) *slot = NULL;
}

static void remove_waiter(W2K_CV_STATE *state, W2K_CV_WAITER *waiter)
{
    W2K_CV_WAITER *current = state->head;
    W2K_CV_WAITER *previous = NULL;
    while (current) {
        if (current == waiter) {
            if (previous) previous->next = current->next;
            else state->head = current->next;
            if (state->tail == current) state->tail = previous;
            current->next = NULL;
            current->queued = FALSE;
            return;
        }
        previous = current;
        current = current->next;
    }
}

static BOOL wait_cv_common(W2K_CV_STATE *state,
                           W2K_SRW_STATE *srw,
                           PCRITICAL_SECTION critical_section,
                           DWORD milliseconds,
                           BOOL shared)
{
    W2K_CV_WAITER waiter;
    DWORD result;
    DWORD error = ERROR_SUCCESS;
    BOOL signaled;

    waiter.next = NULL;
    waiter.queued = TRUE;
    waiter.event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!waiter.event) return FALSE;

    EnterCriticalSection(&state->gate);
    if (state->tail) state->tail->next = &waiter;
    else state->head = &waiter;
    state->tail = &waiter;
    LeaveCriticalSection(&state->gate);

    if (critical_section) LeaveCriticalSection(critical_section);
    else if (shared) release_shared_state(srw);
    else release_exclusive_state(srw);

    result = WaitForSingleObject(waiter.event, milliseconds);

    EnterCriticalSection(&state->gate);
    if (waiter.queued) {
        remove_waiter(state, &waiter);
        signaled = result == WAIT_OBJECT_0;
    } else {
        /* A signaler removed this waiter while timeout raced with SetEvent. */
        signaled = TRUE;
    }
    LeaveCriticalSection(&state->gate);

    if (critical_section) EnterCriticalSection(critical_section);
    else if (shared) acquire_shared_state(srw);
    else acquire_exclusive_state(srw);

    if (!signaled) {
        error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    }
    CloseHandle(waiter.event);
    if (!signaled) SetLastError(error);
    return signaled;
}

__declspec(dllexport) BOOL WINAPI W2KSleepConditionVariableSRW(
    void **cv_slot, void **srw_slot, DWORD milliseconds, ULONG flags)
{
    W2K_CV_STATE *cv;
    W2K_SRW_STATE *srw;
    if (!cv_slot || !srw_slot ||
        (flags & ~W2K_CONDITION_VARIABLE_LOCKMODE_SHARED)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    cv = get_cv_state(cv_slot);
    srw = get_srw_state(srw_slot);
    if (!cv || !srw) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return wait_cv_common(cv, srw, NULL, milliseconds,
                          (flags & W2K_CONDITION_VARIABLE_LOCKMODE_SHARED) != 0);
}

__declspec(dllexport) BOOL WINAPI W2KSleepConditionVariableCS(
    void **cv_slot, PCRITICAL_SECTION critical_section, DWORD milliseconds)
{
    W2K_CV_STATE *cv;
    if (!cv_slot || !critical_section) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    cv = get_cv_state(cv_slot);
    if (!cv) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return wait_cv_common(cv, NULL, critical_section, milliseconds, FALSE);
}

__declspec(dllexport) VOID WINAPI W2KWakeConditionVariable(void **cv_slot)
{
    W2K_CV_STATE *state;
    W2K_CV_WAITER *waiter;
    if (!cv_slot) return;
    state = get_cv_state(cv_slot);
    if (!state) return;
    EnterCriticalSection(&state->gate);
    waiter = state->head;
    if (waiter) {
        remove_waiter(state, waiter);
        SetEvent(waiter->event);
    }
    LeaveCriticalSection(&state->gate);
}

__declspec(dllexport) VOID WINAPI W2KWakeAllConditionVariable(void **cv_slot)
{
    W2K_CV_STATE *state;
    W2K_CV_WAITER *waiter;
    W2K_CV_WAITER *next;
    if (!cv_slot) return;
    state = get_cv_state(cv_slot);
    if (!state) return;
    EnterCriticalSection(&state->gate);
    waiter = state->head;
    state->head = NULL;
    state->tail = NULL;
    while (waiter) {
        next = waiter->next;
        waiter->next = NULL;
        waiter->queued = FALSE;
        SetEvent(waiter->event);
        waiter = next;
    }
    LeaveCriticalSection(&state->gate);
}

static BOOL get_process_id_from_handle(HANDLE process, DWORD *process_id)
{
    HMODULE ntdll;
    W2K_NT_QUERY_INFORMATION_PROCESS query;
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
    ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    query = (W2K_NT_QUERY_INFORMATION_PROCESS)GetProcAddress(
        ntdll, "NtQueryInformationProcess");
    if (!query) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    ZeroMemory(&information, sizeof(information));
    status = query(process, 0, &information, sizeof(information), NULL);
    if (status < 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *process_id = (DWORD)information.unique_process_id;
    return TRUE;
}

/*
 * IsProcessInJob was added to kernel32 after Windows 2000, but Windows 2000
 * already supports querying a job's process-id list.  This preserves the
 * normal semantics for a supplied job and for the current process with a
 * NULL job handle (the path used by Chromium's sandbox launcher).
 */
__declspec(dllexport) BOOL WINAPI W2KIsProcessInJob(
    HANDLE process, HANDLE job, PBOOL result)
{
    DWORD process_id;
    DWORD capacity = 64;
    DWORD size;
    DWORD returned;
    DWORD error;
    JOBOBJECT_BASIC_PROCESS_ID_LIST *list;
    DWORD index;
    BOOL queried;
    HMODULE kernel32;
    W2K_QUERY_INFORMATION_JOB_OBJECT query_job;

    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *result = FALSE;
    if (!get_process_id_from_handle(process, &process_id)) return FALSE;
    kernel32 = GetModuleHandleA("kernel32.dll");
    query_job = kernel32 ? (W2K_QUERY_INFORMATION_JOB_OBJECT)GetProcAddress(
        kernel32, "QueryInformationJobObject") : NULL;
    if (!query_job) {
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
        queried = query_job(
            job, JobObjectBasicProcessIdList, list, size, &returned);
        if (queried) break;
        error = GetLastError();
        if (error == ERROR_MORE_DATA &&
            list->NumberOfAssignedProcesses > capacity) {
            capacity = list->NumberOfAssignedProcesses;
            HeapFree(GetProcessHeap(), 0, list);
            continue;
        }
        HeapFree(GetProcessHeap(), 0, list);
        if (!job && error == ERROR_INVALID_HANDLE) {
            /* A NULL query on a process outside every job has no job handle. */
            *result = FALSE;
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        SetLastError(error);
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
