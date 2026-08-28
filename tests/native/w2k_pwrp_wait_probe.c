#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef VOID (CALLBACK *WAIT_CALLBACK)(PVOID, BOOLEAN);
typedef BOOL (WINAPI *REGISTER_WAIT_FN)(PHANDLE, HANDLE, WAIT_CALLBACK,
                                        PVOID, ULONG, ULONG);
typedef BOOL (WINAPI *UNREGISTER_WAIT_EX_FN)(HANDLE, HANDLE);

typedef struct WAIT_TEST {
    volatile LONG callbacks;
    HANDLE callback_event;
} WAIT_TEST;

static void out_text(const char *text) {
    DWORD length = 0;
    DWORD written = 0;
    while (text[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, NULL);
}

static void out_number(DWORD value) {
    char digits[16];
    DWORD count = 0;
    if (!value) {
        out_text("0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) {
        char one[2] = {digits[--count], 0};
        out_text(one);
    }
}

static VOID CALLBACK wait_callback(PVOID context, BOOLEAN timed_out) {
    WAIT_TEST *test = (WAIT_TEST *)context;
    if (!timed_out) {
        InterlockedIncrement(&test->callbacks);
        SetEvent(test->callback_event);
    }
}

void __cdecl mainCRTStartup(void) {
    HMODULE module;
    REGISTER_WAIT_FN register_wait;
    UNREGISTER_WAIT_EX_FN unregister_wait_ex;
    WAIT_TEST test;
    HANDLE source_event = NULL;
    HANDLE wait_handle = NULL;
    DWORD first_wait;
    DWORD second_wait;
    BOOL registered;
    BOOL unregistered;

    module = LoadLibraryA("pwrp_k32.dll");
    if (!module) {
        out_text("FAIL LoadLibrary error=");
        out_number(GetLastError());
        out_text("\r\n");
        ExitProcess(10);
    }
    register_wait = (REGISTER_WAIT_FN)GetProcAddress(
        module, "RegisterWaitForSingleObject");
    unregister_wait_ex = (UNREGISTER_WAIT_EX_FN)GetProcAddress(
        module, "UnregisterWaitEx");
    if (!register_wait || !unregister_wait_ex) {
        out_text("FAIL GetProcAddress register=");
        out_number(register_wait != NULL);
        out_text(" unregister=");
        out_number(unregister_wait_ex != NULL);
        out_text(" error=");
        out_number(GetLastError());
        out_text("\r\n");
        ExitProcess(11);
    }

    test.callbacks = 0;
    test.callback_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    source_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!test.callback_event || !source_event) {
        out_text("FAIL CreateEvent error=");
        out_number(GetLastError());
        out_text("\r\n");
        ExitProcess(12);
    }

    registered = register_wait(&wait_handle, source_event, wait_callback,
                               &test, INFINITE, 0);
    if (!registered || !wait_handle) {
        out_text("FAIL RegisterWait result=");
        out_number(registered);
        out_text(" handle=");
        out_number(wait_handle != NULL);
        out_text(" error=");
        out_number(GetLastError());
        out_text("\r\n");
        ExitProcess(13);
    }

    SetEvent(source_event);
    first_wait = WaitForSingleObject(test.callback_event, 5000);
    SetEvent(source_event);
    second_wait = WaitForSingleObject(test.callback_event, 5000);
    unregistered = unregister_wait_ex(wait_handle, INVALID_HANDLE_VALUE);

    out_text("PWRP_WAIT registered=");
    out_number(registered);
    out_text(" first_wait=");
    out_number(first_wait);
    out_text(" second_wait=");
    out_number(second_wait);
    out_text(" callbacks=");
    out_number((DWORD)test.callbacks);
    out_text(" unregistered=");
    out_number(unregistered);
    out_text(" error=");
    out_number(GetLastError());
    out_text("\r\n");

    CloseHandle(source_event);
    CloseHandle(test.callback_event);
    FreeLibrary(module);
    if (first_wait == WAIT_OBJECT_0 && second_wait == WAIT_OBJECT_0 &&
        test.callbacks >= 2 && unregistered) {
        out_text("PASS recurring callback and blocking unregister\r\n");
        ExitProcess(0);
    }
    out_text("FAIL recurring callback or unregister\r\n");
    ExitProcess(20);
}
