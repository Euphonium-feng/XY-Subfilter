[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Video,

    [string]$Player = 'D:\PotPlayer\PotPlayer64.exe',
    [string]$StartAt = '00:01:00',
    [int]$Trials = 10,
    [int]$SeeksPerTrial = 400,
    [int]$SeekDelayMs = 20,
    [int]$LaunchTimeoutSeconds = 25,
    [string[]]$ExpectedModules = @('madVR64.ax', 'XySubFilter.dll'),
    [string]$BugReportPath = '',
    [string]$CrashDumpDirectory = '',
    [string]$OutputPath = '',
    [switch]$PlanOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class SeekDriver
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hWnd);
}
'@

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CrashDumpDirectory)) {
    $CrashDumpDirectory = Join-Path $env:LOCALAPPDATA 'CrashDumps'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot 'artifacts\seek-stability\results.json'
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $repoRoot $OutputPath
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

function Send-Key {
    param([IntPtr]$Window, [int]$VirtualKey)
    [SeekDriver]::PostMessage($Window, 0x0100, [IntPtr]$VirtualKey, [IntPtr]1) | Out-Null
    [SeekDriver]::PostMessage($Window, 0x0101, [IntPtr]$VirtualKey, [IntPtr]0xC0000001) | Out-Null
}

function Get-NewCrashEvidence {
    param([datetime]$SinceUtc, $BugReportBeforeUtc)

    $eventLog = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'
        StartTime = $SinceUtc
    } -ErrorAction SilentlyContinue | Where-Object {
        $_.ProviderName -match 'Application Error|Windows Error Reporting|Application Hang' -and
        $_.Message -match 'PotPlayer|madVR|madHcNet'
    })

    $bugReportChanged = $false
    if ($BugReportPath -and (Test-Path -LiteralPath $BugReportPath)) {
        $bugReportChanged = (Get-Item -LiteralPath $BugReportPath).LastWriteTimeUtc -gt $BugReportBeforeUtc
    }

    $dumps = @(Get-ChildItem -LiteralPath $CrashDumpDirectory -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -match 'PotPlayer|madVR' -and $_.LastWriteTimeUtc -gt $SinceUtc
        })

    [pscustomobject]@{
        EventLog = $eventLog
        BugReportChanged = [bool]$bugReportChanged
        Dumps = $dumps
    }
}

if (-not (Test-Path -LiteralPath $Video)) {
    throw "Video not found: $Video"
}
if (-not (Test-Path -LiteralPath $Player)) {
    throw "Player not found: $Player"
}
if ($Trials -lt 1 -or $SeeksPerTrial -lt 1 -or $SeekDelayMs -lt 0) {
    throw 'Trials, SeeksPerTrial, and SeekDelayMs must be valid positive values.'
}

$bugReportBefore = $null
if ($BugReportPath -and (Test-Path -LiteralPath $BugReportPath)) {
    $bugReportBefore = (Get-Item -LiteralPath $BugReportPath).LastWriteTimeUtc
}

if ($PlanOnly) {
    [pscustomobject]@{
        Video = $Video
        Player = $Player
        StartAt = $StartAt
        Trials = $Trials
        SeeksPerTrial = $SeeksPerTrial
        SeekDelayMs = $SeekDelayMs
        ExpectedModules = $ExpectedModules
        BugReportPath = $BugReportPath
        CrashDumpDirectory = $CrashDumpDirectory
        OutputPath = $OutputPath
    } | ConvertTo-Json -Depth 4
    exit 0
}

$results = @()
for ($trial = 1; $trial -le $Trials; $trial++) {
    $startedUtc = [datetime]::UtcNow
    $process = Start-Process -FilePath $Player -ArgumentList @(
        "`"$Video`"",
        '/new',
        "/seek=$StartAt"
    ) -WindowStyle Maximized -PassThru

    $deadline = (Get-Date).AddSeconds($LaunchTimeoutSeconds)
    $ready = $false
    do {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) {
            break
        }
        $moduleNames = @($process.Modules | Select-Object -ExpandProperty ModuleName)
        $ready = @($ExpectedModules | Where-Object { $moduleNames -notcontains $_ }).Count -eq 0
    } while (-not $ready -and (Get-Date) -lt $deadline)

    if ($process.HasExited) {
        $evidence = Get-NewCrashEvidence -SinceUtc $startedUtc -BugReportBeforeUtc $bugReportBefore
        $results += [pscustomobject]@{
            Trial = $trial
            Verdict = 'launch-exit'
            Sends = 0
            ExitCode = $process.ExitCode
            EventCount = @($evidence.EventLog).Count
            BugReportChanged = $evidence.BugReportChanged
            DumpCount = @($evidence.Dumps).Count
        }
        continue
    }
    if (-not $ready) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $results += [pscustomobject]@{
            Trial = $trial
            Verdict = 'not-ready'
            Sends = 0
            ExitCode = $null
            EventCount = 0
            BugReportChanged = $false
            DumpCount = 0
        }
        continue
    }

    $window = $process.MainWindowHandle
    $sends = 0
    for ($i = 0; $i -lt $SeeksPerTrial; $i++) {
        $process.Refresh()
        if ($process.HasExited -or -not [SeekDriver]::IsWindow($window)) {
            break
        }
        Send-Key -Window $window -VirtualKey 0x27
        $sends++
        Start-Sleep -Milliseconds $SeekDelayMs
        if ($i % 16 -eq 15) {
            for ($j = 0; $j -lt 18; $j++) {
                Send-Key -Window $window -VirtualKey 0x25
                $sends++
            }
            Start-Sleep -Milliseconds 30
        }
    }

    Start-Sleep -Seconds 2
    $process.Refresh()
    $evidence = Get-NewCrashEvidence -SinceUtc $startedUtc -BugReportBeforeUtc $bugReportBefore
    $results += [pscustomobject]@{
        Trial = $trial
        Verdict = if ($process.HasExited) { 'process-exit' } else { 'alive' }
        Sends = $sends
        ExitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
        EventCount = @($evidence.EventLog).Count
        BugReportChanged = $evidence.BugReportChanged
        DumpCount = @($evidence.Dumps).Count
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$results | Format-Table -AutoSize

$failures = @($results | Where-Object {
    $_.Verdict -ne 'alive' -or $_.EventCount -gt 0 -or
    $_.BugReportChanged -or $_.DumpCount -gt 0
})
if ($failures.Count -gt 0) {
    'FAIL: seek stress produced process exit or crash evidence.'
    exit 1
}

'PASS: all seek stress trials stayed alive with no crash evidence.'
exit 0
