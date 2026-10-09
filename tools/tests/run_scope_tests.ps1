param(
    [string]$VcVars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path ([IO.Path]::GetTempPath()) 'ranging-host-tests'
[void][IO.Directory]::CreateDirectory($out)
$commands = @('@echo off', "call `"$VcVars`" >nul", "cd /d `"$root`"")
$commands += "cl /nologo /utf-8 /O2 /DAPP_AUDIO_SAMPLE_RATE=48000 /DAPP_RANGE_AUDIO_PROFILE=1 /DAPP_RANGE_JOINT_PEAKS=1 /ICore/Inc /Fo`"$out/`" tools/tests/test_dsp_diagnostics.c /Fe`"$out/dsp-diagnostics.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commands += "`"$out/dsp-diagnostics.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
foreach ($test in @('board_audio','wire','app_net')) {
    $commands += "cl /nologo /utf-8 /O2 /wd4312 /Itools/tests/scope_stubs /Itools/tests/net_stubs /ICore/Inc /Fo`"$out/`" tools/tests/test_$test.c /Fe`"$out/$test.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/$test.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
}
foreach ($role in 1,2) {
    $commands += "cl /nologo /utf-8 /O2 /DAPP_BOARD_ROLE=$role /Itools/tests/temperature_stubs /ICore/Inc /Fo`"$out/`" tools/tests/test_temperature.c Core/Src/dht11.c /Fe`"$out/temperature$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/temperature$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
}
$commands += 'python tools/tests/test_memory_layout.py'
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commands += "cl /nologo /utf-8 /O2 /ICore/Inc /Fo`"$out/`" tools/tests/test_wave_module.c Core/Src/app_wave.c /Fe`"$out/wave-module.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commands += "`"$out/wave-module.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commands += "cl /nologo /utf-8 /O2 /ICore/Inc /Fo`"$out/`" tools/tests/test_range_protocol.c Core/Src/app_range_protocol.c /Fe`"$out/range-protocol.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
$commands += "`"$out/range-protocol.exe`""
$commands += 'if not "%errorlevel%"=="0" exit /b 1'
foreach ($role in 1,2) {
    $common = "/nologo /utf-8 /O2 /DAPP_BOARD_ROLE=$role /DAPP_AUDIO_SAMPLE_RATE=48000 /Fo`"$out/`""
    $commands += "cl $common /ICore/Inc tools/tests/test_clap_module.c Core/Src/app_clap.c /Fe`"$out/clap-module$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/clap-module$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /ICore/Inc tools/tests/test_position_module.c Core/Src/app_position.c /Fe`"$out/position-module$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/position-module$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_range_transport.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c Core/Src/app_position.c /Fe`"$out/range-transport$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/range-transport$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    # Cortex-M's fixed 32-bit SDRAM addresses are not dereferenced by this host test.
    $commands += "cl $common /wd4312 /Itools/tests/scope_stubs /ICore/Inc tools/tests/test_scope.c /Fe`"$out/scope$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/scope$role.exe`" `"$out/scope$role`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_clap.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c Core/Src/app_position.c /Fe`"$out/clap$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/clap$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_position.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c /Fe`"$out/position$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/position$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_wave.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c Core/Src/app_position.c /Fe`"$out/wave$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/wave$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_ui_control.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c Core/Src/app_position.c /Fe`"$out/ui$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/ui$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_remote_temperature.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c Core/Src/app_position.c /Fe`"$out/remote-temperature$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/remote-temperature$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "cl $common /Itools/tests/net_stubs /ICore/Inc tools/tests/test_master_time.c Core/Src/range_dsp.c Core/Src/range_sync.c Core/Src/app_range_protocol.c Core/Src/app_wave.c Core/Src/app_clap.c Core/Src/app_position.c /Fe`"$out/master$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
    $commands += "`"$out/master$role.exe`""
    $commands += 'if not "%errorlevel%"=="0" exit /b 1'
}
$commandFile = Join-Path $out 'scope-tests.cmd'
$commands | Set-Content -LiteralPath $commandFile -Encoding ASCII
& cmd /d /c $commandFile
if ($LASTEXITCODE -ne 0) { throw 'Scope host tests failed' }
