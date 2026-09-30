param(
    [string]$VcVars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path ([IO.Path]::GetTempPath()) 'ranging-host-tests'
[void][IO.Directory]::CreateDirectory($out)
if (!(Test-Path -LiteralPath $VcVars)) { throw "Pass -VcVars with your MSVC vcvars64.bat path" }
& python (Join-Path $PSScriptRoot 'test_audio_tools.py')
if ($LASTEXITCODE -ne 0) { throw 'Audio generator tests failed' }
& python (Join-Path $root 'tools/audio/generate.py') all --regression
if ($LASTEXITCODE -ne 0) { throw 'Audio fixture generation failed' }
$commands = @('@echo off', "call `"$VcVars`" >nul", "cd /d `"$root`"")
$common = "/nologo /utf-8 /O2 /Itools/tests/net_stubs /ICore/Inc /Fo`"$out/`""
foreach ($rate in 16000,48000) {
    $commands += "cl $common /LD /DAPP_AUDIO_SAMPLE_RATE=$rate /DAPP_RANGE_AUDIO_PROFILE=1 /DAPP_RANGE_JOINT_PEAKS=1 /DRANGE_DSP_PROFILE Core/Src/range_dsp.c tools/tests/peak_pair_host.c /link /OUT:`"$out/joint$rate.dll`" /IMPLIB:`"$out/joint$rate.lib`" /EXPORT:RangeDsp_Find /EXPORT:RangeDsp_Candidates /EXPORT:Test_Pair /EXPORT:Test_Pack /EXPORT:Test_Unpack /EXPORT:Test_SampleRate /EXPORT:Test_MacCount"
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    foreach ($role in 1,2) {
        $commands += "cl $common /DAPP_AUDIO_SAMPLE_RATE=$rate /DAPP_BOARD_ROLE=$role /DAPP_RANGE_JOINT_PEAKS=1 tools/tests/test_joint_fsm.c Core/Src/range_dsp.c Core/Src/range_sync.c /Fe`"$out/fsm${rate}_${role}.exe`""
        $commands += 'if not "%errorlevel%"=="0" exit /b 1'
        $commands += "`"$out/fsm${rate}_${role}.exe`""
        $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    }
    $commands += "cl $common /DAPP_AUDIO_SAMPLE_RATE=$rate tools/tests/test_range_audio_time.c /Fe`"$out/time${rate}.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/time${rate}.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
}
$commands += "cl $common /LD /DAPP_RANGE_AUDIO_PROFILE=2 /DAPP_RANGE_JOINT_PEAKS=0 Core/Src/range_dsp.c /link /OUT:`"$out/wide48.dll`" /IMPLIB:`"$out/wide48.lib`" /EXPORT:RangeDsp_Find"
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commandFile = Join-Path $out 'build.cmd'
$commands | Set-Content -LiteralPath $commandFile -Encoding ASCII
Push-Location $root
try {
    & cmd /d /c $commandFile
    if ($LASTEXITCODE -ne 0) { throw 'C host build/test failed' }
    foreach ($test in @(
        @('test_joint_peaks.py', 'joint16000.dll'),
        @('test_48k.py', 'joint48000.dll'),
        @('test_wide_48k.py', 'wide48.dll')
    )) {
        & python (Join-Path $PSScriptRoot $test[0]) (Join-Path $out $test[1])
        if ($LASTEXITCODE -ne 0) { throw "Failed: $($test[0])" }
    }
    & python (Join-Path $PSScriptRoot 'compare_dsp_load.py') $out
    if ($LASTEXITCODE -ne 0) { throw 'Load characterization failed' }
} finally { Pop-Location }
