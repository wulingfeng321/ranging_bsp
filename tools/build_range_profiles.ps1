param(
    [string]$IarBuild = 'C:/Program Files (x86)/IAR Systems/Embedded Workbench 8.2/common/bin/IarBuild.exe',
    [ValidateSet(16000,48000)][int]$SampleRate = 48000,
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
        $defines = $project.SelectSingleNode("//option[name='CCDefines']")
        $jointNumber = if ($JointPeaks) { 1 } else { 0 }
        foreach ($definition in @("APP_AUDIO_SAMPLE_RATE=$SampleRate", "APP_BOARD_ROLE=$role", "APP_RANGE_AUDIO_PROFILE=$profileNumber", "APP_RANGE_JOINT_PEAKS=$jointNumber")) {
            $node = $project.CreateElement('state')
            $node.InnerText = $definition
            [void]$defines.AppendChild($node)
        }
        $project.SelectSingleNode("//option[name='ObjPath']/state").InnerText = "$name/Obj"
        $project.SelectSingleNode("//option[name='ListPath']/state").InnerText = "$name/List"
        $project.SelectSingleNode("//option[name='IlinkOutputFile']/state").InnerText = "$artifactName.out"
        $project.SelectSingleNode("//option[name='OOCOutputFile']/state").InnerText = "$artifactName.hex"
        try {
            $project.Save($temporaryProject)
            # Absolute project path is required by this IAR version's linker.
            & $IarBuild $temporaryProject -make ranging_bsp -log warnings -parallel 4
            if ($LASTEXITCODE -ne 0) { throw "Build failed: $name" }
        } finally {
            if (Test-Path -LiteralPath $temporaryProject) { Remove-Item -LiteralPath $temporaryProject }
        }
    }
}
