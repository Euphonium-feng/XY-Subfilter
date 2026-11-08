#include "diagnostic_log.h"

#include <windows.h>

namespace
{
CStringW BuildDiagnosticPath()
{
    wchar_t temp_path[MAX_PATH] = { 0 };
    if (!GetTempPathW(_countof(temp_path), temp_path)) {
        return CStringW();
    }
    CStringW path(temp_path);
    path += L"xy_hdr_diag.txt";
    return path;
}
}

void AppendXyDiagnostic(const CStringW& text)
{
    const CStringW path = BuildDiagnosticPath();
    if (path.IsEmpty()) {
        return;
    }

    // Each process starts a fresh diagnostic file. The file is not an
    // accumulating history across runs: one launch, one log.
    static volatile LONG s_started = 0;
    const bool first_write = InterlockedCompareExchange(&s_started, 1, 0) == 0;

    const DWORD max_file_size = 512UL * 1024UL;
    bool truncate = false;
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (GetFileAttributesExW(
            path.GetString(), GetFileExInfoStandard, &attributes)
        && attributes.nFileSizeHigh == 0
        && attributes.nFileSizeLow > max_file_size) {
        truncate = true;
    }

    FILE* file = NULL;
    if (_wfopen_s(
            &file,
            path.GetString(),
            (first_write || truncate) ? L"w, ccs=UTF-8" : L"a, ccs=UTF-8") == 0
        && file) {
        SYSTEMTIME local_time = {};
        GetLocalTime(&local_time);
        fwprintf(
            file,
            L"\r\n[%04u-%02u-%02u %02u:%02u:%02u.%03u]\r\n",
            local_time.wYear,
            local_time.wMonth,
            local_time.wDay,
            local_time.wHour,
            local_time.wMinute,
            local_time.wSecond,
            local_time.wMilliseconds);
        fwprintf(file, L"%ls", text.GetString());
        fclose(file);
    }
}
