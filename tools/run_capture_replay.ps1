param(
    [Parameter(Mandatory=$true)][string]$CaptureRoot,
    [Parameter(Mandatory=$true)][string]$Output,
    [int[]]$Rounds = @(1,2,3,4,5,6),
    [switch]$Ablations
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build=Join-Path ([IO.Path]::GetTempPath()) 'ranging-capture-replay'
[void][IO.Directory]::CreateDirectory($build)
$vc='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$commands=@('@echo off',"call `"$vc`" >nul", "cd /d `"$repo`"",
    "cl /nologo /utf-8 /O2 /LD /DAPP_RANGE_EARLY=0 /DAPP_AUDIO_SAMPLE_RATE=48000 /DAPP_RANGE_AUDIO_PROFILE=1 /DAPP_RANGE_JOINT_PEAKS=1 /Itools/tests/net_stubs /ICore/Inc /Fo`"$build/`" Core/Src/range_dsp.c tools/tests/peak_pair_host.c /link /OUT:`"$build/baseline.dll`" /IMPLIB:`"$build/baseline.lib`" /EXPORT:RangeDsp_Find /EXPORT:RangeDsp_Candidates /EXPORT:rangeDspDiagnostics",
    'if errorlevel 1 exit /b 1')
if($Ablations){
    $source=[IO.File]::ReadAllText((Join-Path $repo 'Core/Src/range_dsp.c'))
    foreach($variant in @('no_gap','half_coarse','both')){
        $content=$source
        if($variant -ne 'half_coarse'){
            $content=$content.Replace('if(gap1>v*APP_RANGE_MAX_GAP_ENERGY_RATIO || gap2>v*APP_RANGE_MAX_GAP_ENERGY_RATIO) {','if(0) { /* offline diagnostic ablation only */')
        }
        if($variant -ne 'no_gap'){
            $content=$content.Replace('if (v < APP_RANGE_COARSE_SCORE) continue;','if (v < APP_RANGE_COARSE_SCORE*0.5f) continue;')
        }
        $variantFile=Join-Path $build ($variant+'.c')
        [IO.File]::WriteAllText($variantFile,$content)
        $commands+="cl /nologo /utf-8 /O2 /LD /DAPP_RANGE_EARLY=0 /DAPP_AUDIO_SAMPLE_RATE=48000 /DAPP_RANGE_AUDIO_PROFILE=1 /DAPP_RANGE_JOINT_PEAKS=1 /ICore/Inc /Fo`"$build/`" `"$variantFile`" /link /OUT:`"$build/$variant.dll`" /IMPLIB:`"$build/$variant.lib`" /EXPORT:RangeDsp_Find /EXPORT:RangeDsp_Candidates /EXPORT:rangeDspDiagnostics"
        $commands+='if errorlevel 1 exit /b 1'
    }
}
$commands+='exit /b 0'
$cmdFile=Join-Path $build 'build.cmd'
$commands | Set-Content -LiteralPath $cmdFile -Encoding ascii
& cmd /d /c $cmdFile
if($LASTEXITCODE -ne 0){throw 'Baseline DLL build failed'}
$extra=@()
if($Ablations){$extra=@('--ablation-dir',$build)}
& python (Join-Path $PSScriptRoot 'replay_capture.py') $CaptureRoot --dll (Join-Path $build 'baseline.dll') --output $Output --rounds @Rounds @extra
if($LASTEXITCODE -ne 0){throw 'Replay failed'}
