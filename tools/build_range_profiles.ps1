param(
    [string]$IarBuild = 'C:/Program Files (x86)/IAR Systems/Embedded Workbench 8.2/common/bin/IarBuild.exe',
    [ValidateSet(48000)][int]$SampleRate = 48000,
    [ValidateSet('legacy','wide')][string[]]$Profiles = @('legacy','wide'),
    [switch]$JointPeaks
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$projectDirectory = Join-Path $workspace 'EWARM'
foreach ($profile in $Profiles) {
    $profileNumber = if ($profile -eq 'legacy') { 1 } else { 2 }
    foreach ($role in 1,2) {
        $board = if ($role -eq 1) { 'A' } else { 'B' }
        $name = "ranging_${profile}_${board}"
        if ($JointPeaks) { $name = "ranging_${profile}_joint_${board}" }
        $artifactName = $name
        if ($JointPeaks) { $artifactName = "ranging_${profile}_joint2_${board}" }
        $name = "${name}_${SampleRate}"
        $artifactName = "${artifactName}_${SampleRate}Hz"
        $temporaryProject = Join-Path $projectDirectory "$name.ewp"
        if (Test-Path -LiteralPath $temporaryProject) { throw "Already exists: $temporaryProject" }
        [xml]$project = Get-Content -Raw -Encoding UTF8 (Join-Path $projectDirectory 'ranging_bsp.ewp')
        # File/group compiler overrides replace the project macros in IAR.
        # Keep the board and audio settings consistent in every definition list.
        $defineLists = $project.SelectNodes("//settings[name='ICCARM']/data/option[name='CCDefines']")
        if ($defineLists.Count -eq 0) { throw 'No IAR compiler definitions found' }
        $jointNumber = if ($JointPeaks) { 1 } else { 0 }
        $variantDefinitions = @("APP_AUDIO_SAMPLE_RATE=$SampleRate", "APP_BOARD_ROLE=$role", "APP_RANGE_AUDIO_PROFILE=$profileNumber", "APP_RANGE_JOINT_PEAKS=$jointNumber")
        foreach ($defines in $defineLists) {
            foreach ($state in @($defines.SelectNodes('state'))) {
                if ($state.InnerText -match '^\s*APP_(AUDIO_SAMPLE_RATE|BOARD_ROLE|RANGE_AUDIO_PROFILE|RANGE_JOINT_PEAKS)(\s|=|$)') {
                    [void]$defines.RemoveChild($state)
                }
            }
            foreach ($definition in $variantDefinitions) {
                $node = $project.CreateElement('state')
                $node.InnerText = $definition
                [void]$defines.AppendChild($node)
            }
        }
        $project.SelectSingleNode("//option[name='ObjPath']/state").InnerText = "$name/Obj"
        $project.SelectSingleNode("//option[name='ListPath']/state").InnerText = "$name/List"
        $project.SelectSingleNode("//option[name='IlinkOutputFile']/state").InnerText = "$artifactName.out"
        $project.SelectSingleNode("//option[name='OOCOutputFile']/state").InnerText = "$artifactName.hex"
        try {
            $project.Save($temporaryProject)
            # Absolute project path is required by this IAR version's linker.
            # Rebuild every module; record and check the actual compiler commands.
            $buildOutput = @(& $IarBuild $temporaryProject -build ranging_bsp -log all -parallel 4 2>&1)
            $buildExitCode = $LASTEXITCODE
            $logDirectory = Join-Path $projectDirectory "$name/List"
            [void][IO.Directory]::CreateDirectory($logDirectory)
            $buildLog = Join-Path $logDirectory 'build.log'
            $buildOutput | Set-Content -LiteralPath $buildLog -Encoding UTF8
            $buildOutput | Select-String -Pattern 'Total number of errors', 'Total number of warnings', '(Error|Warning)\[.*?\]:' | ForEach-Object { $_.Line }
            if ($buildExitCode -ne 0) {
                $buildOutput | Select-Object -Last 40
                throw "Build failed: $name; see $buildLog"
            }
            $compilerCommands = @($buildOutput | ForEach-Object { $_.ToString() } | Where-Object { $_ -match '^iccarm(?:\.exe)?\s+' })
            if ($compilerCommands.Count -eq 0) { throw "No compiler commands to verify: $buildLog" }
            foreach ($command in $compilerCommands) {
                foreach ($definition in $variantDefinitions) {
                    if ($command -notmatch ('(?:^|\s)-D\s+' + [regex]::Escape($definition) + '(?:\s|$)')) {
                        throw "Inconsistent compiler definitions for $definition; see $buildLog"
                    }
                }
            }
            Write-Output "$artifactName : verified $($compilerCommands.Count) compiler commands; log: $buildLog"
        } finally {
            if (Test-Path -LiteralPath $temporaryProject) { Remove-Item -LiteralPath $temporaryProject }
        }
    }
}
