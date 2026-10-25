param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$CompilerBin,
    [string]$CMakePath = 'cmake',
    [string]$BuildDirectory,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build-release' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot ('out/SmallDTL-portable-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists. Choose a new output directory; existing files are preserved.' }
$deployTool = Join-Path $QtRoot 'bin/windeployqt.exe'
$sqlitePlugin = Join-Path $QtRoot 'plugins/sqldrivers/qsqlite.dll'
$compiler = Join-Path $CompilerBin 'g++.exe'
# Generated CMake compiler files quote these paths as CMake strings. Native
# backslashes such as the CI checkout's \a would become invalid escapes.
$cmakeQtRoot = $QtRoot.Replace('\', '/')
$cmakeCompiler = $compiler.Replace('\', '/')
$cmakeResourceCompiler = (Join-Path $CompilerBin 'windres.exe').Replace('\', '/')
$cmakeMakeProgram = (Join-Path $CompilerBin 'mingw32-make.exe').Replace('\', '/')
if (!(Test-Path -LiteralPath $deployTool) -or !(Test-Path -LiteralPath $sqlitePlugin) -or !(Test-Path -LiteralPath $compiler)) { throw 'Provide a Qt MinGW installation with the QSQLITE driver and its compatible compiler directory.' }
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM
$previousBackend = $env:QT_QUICK_BACKEND
$previousPluginPath = $env:QT_PLUGIN_PATH
$previousImports = $env:QML_IMPORT_PATH
$previousFontDirectory = $env:QT_QPA_FONTDIR
try {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $CompilerBin + ';' + $previousPath
    & $CMakePath -S $projectRoot -B $BuildDirectory -G 'MinGW Makefiles' '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH:PATH=$cmakeQtRoot" "-DCMAKE_CXX_COMPILER:FILEPATH=$cmakeCompiler" "-DCMAKE_RC_COMPILER:FILEPATH=$cmakeResourceCompiler" "-DCMAKE_MAKE_PROGRAM:FILEPATH=$cmakeMakeProgram"
    if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }
    & $CMakePath --build $BuildDirectory --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    $ctest = Join-Path ([IO.Path]::GetDirectoryName((Get-Command $CMakePath).Source)) 'ctest.exe'
    $qtTestLog = Join-Path $BuildDirectory 'qt-test-results.txt'
    if (Test-Path -LiteralPath $qtTestLog) { Remove-Item -LiteralPath $qtTestLog -Force }
    & $ctest --test-dir $BuildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path -LiteralPath $qtTestLog) { Get-Content -LiteralPath $qtTestLog }
        throw 'Release tests failed. See qt-test-results.txt and Testing/Temporary/LastTest.log in the build directory.'
    }
    if (Test-Path -LiteralPath $qtTestLog) {
        Select-String -LiteralPath $qtTestLog -Pattern '^Totals:' | ForEach-Object { Write-Output $_.Line }
    }
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    $binary = Join-Path $OutputDirectory 'SmallDTL.exe'
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'SmallDTL.exe') -Destination $binary
    & $deployTool --release --compiler-runtime --no-translations --include-plugins qoffscreen --qmldir (Join-Path $projectRoot 'qml') $binary
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    $sqlDriverDirectory = Join-Path $OutputDirectory 'sqldrivers'
    New-Item -ItemType Directory -Path $sqlDriverDirectory -Force | Out-Null
    Copy-Item -LiteralPath $sqlitePlugin -Destination (Join-Path $sqlDriverDirectory 'qsqlite.dll')
    # Verify the bundle without the development Qt DLL/plugin/import search paths.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $env:QML_IMPORT_PATH = $null
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_BACKEND = 'software'
    $env:QT_QPA_FONTDIR = Join-Path $env:SystemRoot 'Fonts'
    $process = Start-Process -FilePath $binary -ArgumentList '--smoke-test' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Packaged-runtime smoke test failed ($($process.ExitCode))." }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $OutputDirectory
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs') -Destination $OutputDirectory -Recurse
    $licenseDirectory = Join-Path $OutputDirectory 'licenses'
    New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
    $zxingLicense = Join-Path $BuildDirectory 'third-party-licenses/ZXing-C++-Apache-2.0.txt'
    if (!(Test-Path -LiteralPath $zxingLicense)) { throw 'Portable package is missing the ZXing-C++ Apache-2.0 license.' }
    Copy-Item -LiteralPath $zxingLicense -Destination $licenseDirectory
    if (Get-ChildItem -LiteralPath $OutputDirectory -Filter '*ZXing*.dll' -File -Recurse) { throw 'Portable QR build unexpectedly requires a ZXing DLL.' }
    if (!(Test-Path -LiteralPath (Join-Path $sqlDriverDirectory 'qsqlite.dll'))) { throw 'Portable package is missing the SQLite driver.' }
    $manifest = [ordered]@{ application='SmallDTL'; configuration='Release'; qtRoot=$QtRoot; builtAt=(Get-Date -Format o); tests='passed'; packagedRuntimeSmokeTest='passed'; sqliteDriver='sqldrivers/qsqlite.dll'; qrDependency='ZXing-C++ v3.1.1 (static, Apache-2.0)'; qrLicense='licenses/ZXing-C++-Apache-2.0.txt' }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'build-manifest.json') -Encoding utf8
    Compress-Archive -LiteralPath $OutputDirectory -DestinationPath ($OutputDirectory + '.zip')
    Write-Output "Portable application: $OutputDirectory"
    Write-Output "Archive: $OutputDirectory.zip"
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM = $previousPlatform
    $env:QT_QUICK_BACKEND = $previousBackend
    $env:QT_PLUGIN_PATH = $previousPluginPath
    $env:QML_IMPORT_PATH = $previousImports
    $env:QT_QPA_FONTDIR = $previousFontDirectory
}
