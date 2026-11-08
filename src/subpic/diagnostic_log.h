#pragma once

#include <atlstr.h>

// Appends one diagnostic block to %TEMP%\xy_hdr_diag.txt. The first write in
// a process truncates the file, so one launch produces one log.
void AppendXyDiagnostic(const CStringW& text);
