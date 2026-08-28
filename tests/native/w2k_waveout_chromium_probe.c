#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

typedef VOID (CALLBACK *WAIT_CALLBACK)(PVOID, BOOLEAN);
typedef BOOL (WINAPI *REGISTER_WAIT_FN)(PHANDLE, HANDLE, WAIT_CALLBACK,
                                        PVOID, ULONG, ULONG);
typedef BOOL (WINAPI *UNREGISTER_WAIT_EX_FN)(HANDLE, HANDLE);

#define BUFFER_COUNT 3
#define FRAMES_PER_BUFFER 2048
#define CHANNELS 2
#define SAMPLE_RATE 48000
#define BITS_PER_SAMPLE 16
#define BUFFER_BYTES (FRAMES_PER_BUFFER * CHANNELS * (BITS_PER_SAMPLE / 8))
#define TARGET_COMPLETIONS 30

typedef struct WAVE_TEST {
    HWAVEOUT output;
    WAVEHDR headers[BUFFER_COUNT];
    HANDLE all_done;
    volatile LONG completed;
    LONG submitted;
    CRITICAL_SECTION lock;
} WAVE_TEST;

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

static void zero_bytes(void *destination, DWORD size) {
    volatile BYTE *bytes = (volatile BYTE *)destination;
    while (size) bytes[--size] = 0;
}

static VOID CALLBACK buffer_callback(PVOID context, BOOLEAN timed_out) {
    WAVE_TEST *test = (WAVE_TEST *)context;
    int index;
    if (timed_out) return;
    EnterCriticalSection(&test->lock);
    for (index = 0; index < BUFFER_COUNT; ++index) {
        if (test->headers[index].dwFlags & WHDR_DONE) {
            ++test->completed;
            if (test->submitted < TARGET_COMPLETIONS) {
                if (waveOutWrite(test->output, &test->headers[index],
                                 sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                    ++test->submitted;
                }
            }
        }
    }
    if (test->completed >= TARGET_COMPLETIONS) SetEvent(test->all_done);
    LeaveCriticalSection(&test->lock);
}

void __cdecl mainCRTStartup(void) {
    static const GUID pcm_subformat = {
        0x00000001, 0x0000, 0x0010,
        {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}
    };
    HMODULE pwrp;
    REGISTER_WAIT_FN register_wait;
    UNREGISTER_WAIT_EX_FN unregister_wait_ex;
    WAVEFORMATEXTENSIBLE format;
    WAVE_TEST test;
    HANDLE buffer_event;
    HANDLE wait_handle = NULL;
    MMRESULT opened;
    MMRESULT paused;
    MMRESULT restarted;
    MMRESULT written = MMSYSERR_NOERROR;
    DWORD wait_result;
    BOOL registered;
    BOOL unregistered;
    int index;

    zero_bytes(&test, sizeof(test));
    zero_bytes(&format, sizeof(format));
    InitializeCriticalSection(&test.lock);
    pwrp = LoadLibraryA("pwrp_k32.dll");
    if (!pwrp) {
        out_text("FAIL LoadLibrary pwrp error=");
        out_number(GetLastError());
        out_text("\r\n");
        ExitProcess(10);
    }
    register_wait = (REGISTER_WAIT_FN)GetProcAddress(
        pwrp, "RegisterWaitForSingleObject");
    unregister_wait_ex = (UNREGISTER_WAIT_EX_FN)GetProcAddress(
        pwrp, "UnregisterWaitEx");
    if (!register_wait || !unregister_wait_ex) ExitProcess(11);

    buffer_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    test.all_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!buffer_event || !test.all_done) ExitProcess(12);

    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = CHANNELS;
    format.Format.nSamplesPerSec = SAMPLE_RATE;
    format.Format.wBitsPerSample = BITS_PER_SAMPLE;
    format.Format.nBlockAlign = CHANNELS * (BITS_PER_SAMPLE / 8);
    format.Format.nAvgBytesPerSec = SAMPLE_RATE * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = BITS_PER_SAMPLE;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = pcm_subformat;

    opened = waveOutOpen(&test.output, WAVE_MAPPER,
                         (LPCWAVEFORMATEX)&format,
                         (DWORD_PTR)buffer_event, 0, CALLBACK_EVENT);
    if (opened != MMSYSERR_NOERROR) {
        out_text("FAIL waveOutOpen extensible mmresult=");
        out_number(opened);
        out_text("\r\n");
        ExitProcess(13);
    }

    for (index = 0; index < BUFFER_COUNT; ++index) {
        WAVEHDR *header = &test.headers[index];
        header->lpData = (LPSTR)HeapAlloc(GetProcessHeap(),
                                          HEAP_ZERO_MEMORY, BUFFER_BYTES);
        header->dwBufferLength = BUFFER_BYTES;
        if (!header->lpData || waveOutPrepareHeader(
                test.output, header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            out_text("FAIL prepare header index=");
            out_number(index);
            out_text("\r\n");
            ExitProcess(14);
        }
    }

    ResetEvent(buffer_event);
    registered = register_wait(&wait_handle, buffer_event, buffer_callback,
                               &test, INFINITE, 0);
    paused = waveOutPause(test.output);
    for (index = 0; index < BUFFER_COUNT; ++index) {
        written = waveOutWrite(test.output, &test.headers[index],
                               sizeof(WAVEHDR));
        if (written != MMSYSERR_NOERROR) break;
        ++test.submitted;
    }
    restarted = waveOutRestart(test.output);
    wait_result = WaitForSingleObject(test.all_done, 10000);
    unregistered = unregister_wait_ex(wait_handle, INVALID_HANDLE_VALUE);

    out_text("CHROMIUM_WAVEOUT opened=");
    out_number(opened);
    out_text(" registered=");
    out_number(registered);
    out_text(" paused=");
    out_number(paused);
    out_text(" write=");
    out_number(written);
    out_text(" restarted=");
    out_number(restarted);
    out_text(" wait=");
    out_number(wait_result);
    out_text(" completed=");
    out_number((DWORD)test.completed);
    out_text(" unregistered=");
    out_number(unregistered);
    out_text("\r\n");

    waveOutReset(test.output);
    for (index = 0; index < BUFFER_COUNT; ++index) {
        waveOutUnprepareHeader(test.output, &test.headers[index],
                               sizeof(WAVEHDR));
        HeapFree(GetProcessHeap(), 0, test.headers[index].lpData);
    }
    waveOutClose(test.output);
    CloseHandle(buffer_event);
    CloseHandle(test.all_done);
    DeleteCriticalSection(&test.lock);
    FreeLibrary(pwrp);

    if (opened == MMSYSERR_NOERROR && registered &&
        paused == MMSYSERR_NOERROR && written == MMSYSERR_NOERROR &&
        restarted == MMSYSERR_NOERROR && wait_result == WAIT_OBJECT_0 &&
        test.completed >= TARGET_COMPLETIONS && unregistered) {
        out_text("PASS Chromium WaveOut format and recurring callback sequence\r\n");
        ExitProcess(0);
    }
    out_text("FAIL Chromium WaveOut format or callback sequence\r\n");
    ExitProcess(20);
}
