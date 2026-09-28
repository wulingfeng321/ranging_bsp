param([string]$VcVars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path ([IO.Path]::GetTempPath()) 'ranging-capture-tests'
[void][IO.Directory]::CreateDirectory($out)
$commands = @('@echo off', "call `"$VcVars`" >nul", "cd /d `"$root`"")
$commands += "cl /nologo /utf-8 /O2 /Itools/tests/sd_stubs /ICore/Inc /Fo`"$out/`" tools/tests/test_capture_sd.c /Fe`"$out/capture-sd.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commands += "`"$out/capture-sd.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
foreach ($role in 1,2) {
    $commands += "cl /nologo /utf-8 /O2 /wd4312 /DAPP_BOARD_ROLE=$role /Itools/tests/capture_stubs /Itools/tests/net_stubs /ICore/Inc /Fo`"$out/`" tools/tests/test_capture.c /Fe`"$out/capture$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/capture$role.exe`" `"$out`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
}
$commandFile = Join-Path $out 'capture-tests.cmd'
$commands | Set-Content -LiteralPath $commandFile -Encoding ASCII
& cmd /d /c $commandFile
if ($LASTEXITCODE -ne 0) { throw 'Capture host tests failed' }
& python (Join-Path $root 'tools/decode_capture.py') $out
if ($LASTEXITCODE -ne 0) { throw 'Capture decoder failed' }
