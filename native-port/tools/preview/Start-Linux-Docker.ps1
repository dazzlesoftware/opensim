param(
    [ValidateSet('Start', 'Stop')][string]$Mode = 'Start',
    [ValidatePattern('^[a-z0-9][a-z0-9_.-]*$')][string]$Release = 'local-preview',
    [ValidatePattern('^[a-z0-9][a-z0-9_.-]*$')][string]$Instance = $Release,
    [ValidateRange(1, 65535)][int]$Port = 19093
)
$ErrorActionPreference = 'Stop'
$docker = Get-Command docker -ErrorAction SilentlyContinue
$dockerExe = if ($docker) { $docker.Source } else { "$env:LOCALAPPDATA/Programs/DockerDesktop/resources/bin/docker.exe" }
if (-not (Test-Path -LiteralPath $dockerExe)) { throw 'Docker Desktop CLI was not found' }
function Invoke-Docker {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & $dockerExe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($Arguments -join ' ')" }
}
$name = "opensim-native-packaged-preview-$Instance"
$volume = "$name-data"
$image = "opensim-native-preview:$Release"
$existing = & $dockerExe ps -a --filter "name=^/$name`$" --format '{{.ID}}'
if ($existing) {
    $owner = & $dockerExe inspect --format '{{index .Config.Labels "org.opensimulator.packaged-preview"}}' $name
    if ($LASTEXITCODE -ne 0 -or $owner -ne 'true') { throw 'An unrelated container uses the preview name' }
    if ($Mode -eq 'Stop') { Invoke-Docker stop --timeout 30 $name; return }
    $configuredImage = & $dockerExe inspect --format '{{.Config.Image}}' $name
    if ($LASTEXITCODE -ne 0 -or $configuredImage -ne $image) {
        throw 'This instance belongs to a different release; choose a separate -Instance to preserve its data'
    }
    $expectedImage = & $dockerExe image inspect --format '{{.Id}}' $image
    if ($LASTEXITCODE -ne 0) { throw 'The configured release image is unavailable' }
    $actualImage = & $dockerExe inspect --format '{{.Image}}' $name
    if ($LASTEXITCODE -ne 0 -or $actualImage -ne $expectedImage) {
        throw 'This instance uses an older image; choose a separate -Instance to preserve its data'
    }
    $configuredEnvironment = & $dockerExe inspect --format '{{range .Config.Env}}{{println .}}{{end}}' $name
    if ($LASTEXITCODE -ne 0 -or $configuredEnvironment -notcontains "OPENSIM_PREVIEW_PORT=$Port") {
        throw 'This instance has a different port; reuse its original -Port to retain the existing configuration'
    }
    $running = & $dockerExe inspect --format '{{.State.Running}}' $name
    if ($running -ne 'true') { Invoke-Docker start $name }
} else {
    if ($Mode -eq 'Stop') { throw 'The packaged preview container does not exist' }
    & $dockerExe image inspect $image *> $null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Docker load -i (Join-Path $PSScriptRoot 'opensim-native-preview-linux-amd64.image.tar.gz')
    }
    Invoke-Docker volume create $volume
    Invoke-Docker run --detach --init --interactive --name $name `
        --label 'org.opensimulator.packaged-preview=true' `
        --env "OPENSIM_PREVIEW_PORT=$Port" `
        --publish "127.0.0.1:${Port}:${Port}/tcp" --publish "127.0.0.1:${Port}:${Port}/udp" `
        --mount "type=volume,source=$volume,target=/data" $image
}
$deadline = [DateTime]::UtcNow.AddSeconds(90)
while ($true) {
    $running = & $dockerExe inspect --format '{{.State.Running}}' $name
    if ($LASTEXITCODE -ne 0 -or $running -ne 'true') {
        & $dockerExe logs --tail 30 $name
        throw 'Linux preview stopped during startup'
    }
    try {
        $response = Invoke-WebRequest "http://127.0.0.1:$Port/get_grid_info" -UseBasicParsing -TimeoutSec 2
        if ($response.StatusCode -eq 200) { break }
    } catch {
        if ([DateTime]::UtcNow -ge $deadline) {
            throw 'Linux preview did not become ready; inspect Docker logs before retrying'
        }
    }
    Start-Sleep -Milliseconds 500
}
Write-Output "Linux preview ready: http://127.0.0.1:$Port/"
Write-Output 'Accounts: Preview First / Preview Second; initial test password: preview-password'
Write-Output 'Use -Mode Stop for a clean stop. Database and configuration files remain in the named data volume.'
