param(
    [string]$Captures = '',
    [string]$VcVars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path ([IO.Path]::GetTempPath()) 'ranging-early-tests'
[void][IO.Directory]::CreateDirectory($out)
$commands = @('@echo off', "call `"$VcVars`" >nul", "cd /d `"$root`"")
$commands += "cl /nologo /utf-8 /O2 /LD /DAPP_RANGE_EARLY=1 /DRANGE_DSP_PROFILE /DAPP_AUDIO_SAMPLE_RATE=48000 /DAPP_RANGE_AUDIO_PROFILE=1 /DAPP_RANGE_JOINT_PEAKS=1 /ICore/Inc /Fo`"$out/`" Core/Src/range_dsp.c tools/tests/peak_pair_host.c /link /OUT:`"$out/early.dll`" /IMPLIB:`"$out/early.lib`" /EXPORT:RangeDsp_Find /EXPORT:RangeDsp_Candidates /EXPORT:RangeDsp_Reset /EXPORT:rangeDspDiagnostics /EXPORT:Test_MacCount"
$commands += 'if errorlevel 1 exit /b 1'
foreach ($role in 1,2) {
    $commands += "cl /nologo /utf-8 /O2 /wd4312 /DRANGE_DSP_PROFILE /DAPP_RANGE_EARLY=1 /DAPP_AUDIO_SAMPLE_RATE=48000 /DAPP_BOARD_ROLE=$role /DAPP_RANGE_JOINT_PEAKS=1 /Itools/tests/net_stubs /ICore/Inc /Fo`"$out/`" tools/tests/test_joint_fsm.c Core/Src/range_dsp.c Core/Src/range_sync.c /Fe`"$out/fsm$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/fsm$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
}
$commands += 'exit /b %errorlevel%'
$commandFile = Join-Path $out 'early.cmd'
$commands | Set-Content -LiteralPath $commandFile -Encoding ASCII
& cmd /d /c $commandFile
if ($LASTEXITCODE -ne 0) { throw 'Early detector host build failed' }
$arguments = @((Join-Path $PSScriptRoot 'test_early_c.py'), (Join-Path $out 'early.dll'))
if ($Captures) { $arguments += $Captures }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Early detector tests failed' }
