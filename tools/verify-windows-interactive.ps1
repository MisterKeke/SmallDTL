param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '../out/interactive-verification.json')
)
$ErrorActionPreference = 'Stop'
# This is deliberately interactive: offscreen Qt cannot establish physical
# shortcut delivery, Explorer drop interoperability, or actual display changes.
$cases = @(
    'Copy text and images from another application; verify history capture, pause across restart, exclusions, and a password manager sensitive marker.',
    'Search history, clear without pins, then clear with pins; verify the count and preservation of gallery/original files.',
    'Drag Unicode-named shelf files into Explorer and a second application; verify both receive actual files.',
    'Move a disposable folder to a different volume; cancel a second move mid-copy and verify remaining source data.',
    'Copy disposable files to a writable network share, then repeat with the share disconnected or read-only; verify errors and responsiveness.',
    'Press physical history/shelf/profile shortcuts while another application is focused; verify dispatch and conflict reporting.',
    'Arrange disposable test windows across monitors with different DPI, including a negative-origin monitor; verify taskbar bounds and restore.',
    'Disconnect a monitor used by a saved layout; preview/apply and verify fallback. Check an elevated target separately.',
    'Verify the storage panel, retry a locked history payload after releasing its lock, compact storage, and clear thumbnails.',
    'Start a large disposable shelf copy, request cancellation, then exit; verify staged data is cleaned and the next launch works.'
)
$results = foreach ($case in $cases) {
    Write-Host "`n$case"
    $result = Read-Host 'Result (pass/fail/not-run)'
    while ($result -notin @('pass', 'fail', 'not-run')) { $result = Read-Host 'Enter pass, fail, or not-run' }
    $notes = Read-Host 'Evidence or notes'
    [ordered]@{ check = $case; result = $result; notes = $notes }
}
$absoluteOutput = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($absoluteOutput)) -Force | Out-Null
[ordered]@{ recordedAt = (Get-Date -Format o); computer = $env:COMPUTERNAME; checks = @($results) } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $absoluteOutput -Encoding utf8
Write-Output "Recorded verification: $absoluteOutput"
