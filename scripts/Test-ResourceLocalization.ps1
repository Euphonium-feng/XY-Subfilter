[CmdletBinding()]
param(
    [string]$RepoRoot = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRootPath = [System.IO.Path]::GetFullPath($RepoRoot)
$rcPath = Join-Path $repoRootPath 'src\filters\transform\vsfilter\VSFilter.rc'
$pagePath = Join-Path $repoRootPath 'src\filters\transform\vsfilter\DirectVobSubPropPage.cpp'
$encoding = [System.Text.Encoding]::GetEncoding(936)
$text = [System.IO.File]::ReadAllText($rcPath, $encoding)

$englishMarker = '#if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_ENU)'
$chineseMarker = '#if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_CHS)'
$englishStart = $text.IndexOf($englishMarker)
$chineseStart = $text.IndexOf($chineseMarker)
if ($englishStart -lt 0 -or $chineseStart -lt 0 -or $chineseStart -le $englishStart) {
    throw 'Could not locate the English and Simplified Chinese resource blocks.'
}

$englishBlock = $text.Substring($englishStart, $chineseStart - $englishStart)
$chineseBlock = $text.Substring($chineseStart)

function Get-StringIds {
    param([string]$Block)
    $matches = [regex]::Matches($Block, '(?m)^\s*(IDS_[A-Za-z0-9_]+)\s+"')
    return @($matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
}

function Get-DialogControlIds {
    param([string]$Block)
    $result = @{}
    $matches = [regex]::Matches(
        $Block,
        '(?ms)^\s*(IDD_[A-Za-z0-9_]+)\s+DIALOGEX\b.*?^\s*END\s*$')
    foreach ($match in $matches) {
        $dialog = $match.Groups[1].Value
        $controls = [regex]::Matches($match.Value, '\bIDC_[A-Za-z0-9_]+\b') |
            ForEach-Object { $_.Value } |
            Where-Object { $_ -ne 'IDC_STATIC' } |
            Sort-Object -Unique
        $result[$dialog] = @($controls)
    }
    return $result
}

$englishStrings = Get-StringIds $englishBlock
$chineseStrings = Get-StringIds $chineseBlock
$missingChinese = @($englishStrings | Where-Object { $_ -notin $chineseStrings })
$extraChinese = @($chineseStrings | Where-Object { $_ -notin $englishStrings })
if ($missingChinese.Count -or $extraChinese.Count) {
    throw "Resource string IDs differ. Missing: $($missingChinese -join ', '); extra: $($extraChinese -join ', ')"
}

$englishDialogs = Get-DialogControlIds $englishBlock
$chineseDialogs = Get-DialogControlIds $chineseBlock
$englishDialogIds = @($englishDialogs.Keys | Sort-Object)
$chineseDialogIds = @($chineseDialogs.Keys | Sort-Object)
if (($englishDialogIds -join '|') -ne ($chineseDialogIds -join '|')) {
    throw 'Dialog resources differ between languages.'
}
foreach ($dialog in $englishDialogIds) {
    if (($englishDialogs[$dialog] -join '|') -ne (($chineseDialogs[$dialog]) -join '|')) {
        throw "Dialog '$dialog' has different controls between languages."
    }
}

$hardcoded = @(
    Get-Content -LiteralPath $pagePath | Where-Object {
        $_ -match 'AddString\s*\(\s*(CString\s*\()?(_T|L)\s*\(\s*"' -and
        $_ -notmatch 'l10n-exempt:'
    }
)
if ($hardcoded.Count) {
    throw "Hard-coded property-page strings found:`n$($hardcoded -join "`n")"
}

$localizedIds = @($englishStrings | Where-Object { $_ -like 'IDS_XY_UI_*' })
if ($localizedIds.Count -eq 0) {
    throw 'No IDS_XY_UI_* resource strings were found.'
}

Write-Host "Resource localization checks passed: $($localizedIds.Count) UI strings, $($englishDialogIds.Count) dialogs."
