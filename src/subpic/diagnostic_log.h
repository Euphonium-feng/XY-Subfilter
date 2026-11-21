#pragma once

#include <atlstr.h>

// The one implementation of the diagnostic file location. Callers that need
// to show or open the log must use this instead of rebuilding the path.
CStringW BuildXyDiagnosticPath();

// Appends one diagnostic block to %TEMP%\xy_hdr_diag.txt. The first write in
// a process truncates the file, so one launch produces one log.
void AppendXyDiagnostic(const wchar_t* text);
