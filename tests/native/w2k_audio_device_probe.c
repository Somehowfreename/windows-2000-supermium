#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

static void out_text(const char *text) {
    DWORD length = 0;
    DWORD written = 0;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    while (text[length]) {
        ++length;
    }
    WriteFile(output, text, length, &written, NULL);
}

static void out_number(DWORD value) {
    char digits[16];
    DWORD count = 0;
    if (!value) {
        out_text("0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count) {
        char one[2];
        one[0] = digits[--count];
        one[1] = 0;
        out_text(one);
    }
}

static void report_error(const char *stage, MMRESULT result) {
    out_text("FAIL stage=");
    out_text(stage);
    out_text(" mmresult=");
    out_number(result);
    out_text("\r\n");
}

void __cdecl mainCRTStartup(void) {
    const DWORD sample_rate = 22050;
    const DWORD sample_count = 22050;
    const DWORD byte_count = sample_count * sizeof(short);
    DWORD i;
    UINT device_count;
    WAVEOUTCAPSA caps;
    WAVEFORMATEX format;
    WAVEHDR header;
    HWAVEOUT output = NULL;
    HANDLE done_event = NULL;
    short *samples = NULL;
    MMRESULT mm;
    DWORD wait_result;
    MMTIME position;
    DWORD exit_code = 1;

    device_count = waveOutGetNumDevs();
    out_text("W2K_AUDIO devices=");
    out_number(device_count);
    out_text("\r\n");
    if (!device_count) {
        out_text("FAIL stage=waveOutGetNumDevs no-device\r\n");
        ExitProcess(10);
    }

    mm = waveOutGetDevCapsA(WAVE_MAPPER, &caps, sizeof(caps));
    if (mm != MMSYSERR_NOERROR) {
        report_error("waveOutGetDevCapsA", mm);
        ExitProcess(11);
    }
    out_text("W2K_AUDIO device_name=");
    out_text(caps.szPname);
    out_text("\r\n");

    done_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!done_event) {
        out_text("FAIL stage=CreateEvent\r\n");
        ExitProcess(12);
    }
    samples = (short *)HeapAlloc(GetProcessHeap(), 0, byte_count);
    if (!samples) {
        out_text("FAIL stage=HeapAlloc\r\n");
        CloseHandle(done_event);
        ExitProcess(13);
    }

    /* A one-second, approximately 441 Hz low-amplitude square wave. */
    for (i = 0; i < sample_count; ++i) {
        samples[i] = (short)(((i % 50) < 25) ? 4096 : -4096);
    }

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sample_rate;
    format.nAvgBytesPerSec = sample_rate * sizeof(short);
    format.nBlockAlign = sizeof(short);
    format.wBitsPerSample = 16;
    format.cbSize = 0;

    header.lpData = (LPSTR)samples;
    header.dwBufferLength = byte_count;
    header.dwBytesRecorded = 0;
    header.dwUser = 0;
    header.dwFlags = 0;
    header.dwLoops = 0;
    header.lpNext = NULL;
    header.reserved = 0;

    mm = waveOutOpen(&output, WAVE_MAPPER, &format,
                     (DWORD_PTR)done_event, 0, CALLBACK_EVENT);
    if (mm != MMSYSERR_NOERROR) {
        report_error("waveOutOpen", mm);
        goto cleanup;
    }
    mm = waveOutPrepareHeader(output, &header, sizeof(header));
    if (mm != MMSYSERR_NOERROR) {
        report_error("waveOutPrepareHeader", mm);
        goto cleanup;
    }
    /* CALLBACK_EVENT may be signaled once for WOM_OPEN.  Discard that state
       so the following wait proves WOM_DONE for the submitted buffer. */
    ResetEvent(done_event);
    mm = waveOutWrite(output, &header, sizeof(header));
    if (mm != MMSYSERR_NOERROR) {
        report_error("waveOutWrite", mm);
        goto cleanup;
    }

    wait_result = WaitForSingleObject(done_event, 10000);
    if (wait_result != WAIT_OBJECT_0 || !(header.dwFlags & WHDR_DONE)) {
        out_text("FAIL stage=completion wait_result=");
        out_number(wait_result);
        out_text(" flags=");
        out_number(header.dwFlags);
        out_text("\r\n");
        goto cleanup;
    }

    position.wType = TIME_BYTES;
    mm = waveOutGetPosition(output, &position, sizeof(position));
    if (mm != MMSYSERR_NOERROR) {
        report_error("waveOutGetPosition", mm);
        goto cleanup;
    }
    out_text("PASS waveOutOpen=1 waveOutWrite=1 completed=1 bytes=");
    out_number(position.u.cb);
    out_text(" expected=");
    out_number(byte_count);
    out_text("\r\n");
    exit_code = 0;

cleanup:
    if (output) {
        waveOutReset(output);
        if (header.dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(output, &header, sizeof(header));
        }
        waveOutClose(output);
    }
    if (samples) {
        HeapFree(GetProcessHeap(), 0, samples);
    }
    if (done_event) {
        CloseHandle(done_event);
    }
    ExitProcess(exit_code);
}
