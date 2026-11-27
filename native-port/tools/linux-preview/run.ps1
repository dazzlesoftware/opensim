param(
    [ValidateSet('Check', 'Serve', 'Stop')]
    [string]$Mode = 'Check',
    [ValidateRange(1, 32)]
    [int]$Jobs = 8,
    [ValidateRange(1, 65535)]
    [int]$Port = 19091
)

$ErrorActionPreference = 'Stop'
$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
$dockerExecutable = if ($dockerCommand) { $dockerCommand.Source } else { $null }
if (-not $dockerExecutable) {
    foreach ($candidate in @(
        "$env:LOCALAPPDATA/Programs/DockerDesktop/resources/bin/docker.exe",
        "$env:ProgramFiles/Docker/Docker/resources/bin/docker.exe"
    )) {
        if (Test-Path -LiteralPath $candidate) {
            $dockerExecutable = $candidate
            break
        }
    }
}
if (-not $dockerExecutable) { throw 'Docker Desktop CLI was not found' }
# Also expose the installed credential helper to newly opened Codex sessions.
$env:PATH = "$(Split-Path -Parent $dockerExecutable);$env:PATH"

function Invoke-PreviewDocker {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$DockerArguments)
    & $dockerExecutable @DockerArguments
    if ($LASTEXITCODE -ne 0) { throw "Docker command failed: $($DockerArguments -join ' ')" }
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$outputDirectory = Join-Path $repository 'native-port/build-linux-preview'
$evidence = Join-Path $outputDirectory 'evidence'
$stateFile = Join-Path $outputDirectory 'docker-state.json'
$image = 'opensim-native-preview-build:ubuntu24.04'
$buildVolume = 'opensim-native-preview-build'
$containerName = 'opensim-native-linux-preview'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
Invoke-PreviewDocker info --format '{{.OSType}}'

if ($Mode -eq 'Check') {
    Invoke-PreviewDocker build -t $image -f (Join-Path $PSScriptRoot 'Dockerfile') $PSScriptRoot
    $sourceVolume = "opensim-native-preview-source-$([guid]::NewGuid().ToString('N').Substring(0, 10))"
    Invoke-PreviewDocker volume create $sourceVolume
    Invoke-PreviewDocker volume create $buildVolume
    Invoke-PreviewDocker run --rm --mount "type=bind,source=$repository,target=/host,readonly" `
        --mount "type=volume,source=$sourceVolume,target=/src" $image bash -lc `
        "set -eo pipefail; tar -C /host --exclude='./.git' --exclude='./native-port/build*' --exclude='./native-port/out' --exclude='./native-port/__pycache__' -cf - . | tar -C /src -xf -"
    $state = @{ SourceVolume = $sourceVolume; BuildVolume = $buildVolume; Image = $image; ChecksPassed = $false }
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
    Invoke-PreviewDocker run --rm --env "OPENSIM_BUILD_JOBS=$Jobs" `
        --mount "type=volume,source=$sourceVolume,target=/src,readonly" `
        --mount "type=volume,source=$buildVolume,target=/build" `
        --mount "type=bind,source=$evidence,target=/evidence" $image
    $state.ChecksPassed = $true
    $state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
    return
}

if ($Mode -eq 'Stop') {
    $owner = & $dockerExecutable inspect --format '{{index .Config.Labels "org.opensimulator.native-preview"}}' $containerName
    if ($LASTEXITCODE -ne 0 -or $owner -ne 'true') {
        throw 'The named container is not an owned native preview'
    }
    Invoke-PreviewDocker stop --timeout 30 $containerName
    return
}

if (-not (Test-Path -LiteralPath $stateFile)) { throw 'Run -Mode Check first' }
$state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
if (-not $state.ChecksPassed) { throw 'The Linux preview checks have not passed; run -Mode Check first' }
$existing = & $dockerExecutable ps -a --filter "name=^/$containerName`$" --format '{{.ID}}'
if ($existing) {
    $owner = & $dockerExecutable inspect --format '{{index .Config.Labels "org.opensimulator.native-preview"}}' $containerName
    if ($owner -ne 'true') { throw 'An unrelated container already uses the preview name' }
    $running = & $dockerExecutable inspect --format '{{.State.Running}}' $containerName
    if ($running -eq 'true') { throw 'The preview is already running; stop it before restarting' }
    Invoke-PreviewDocker rm $containerName
}
Invoke-PreviewDocker volume create opensim-native-preview-data
Invoke-PreviewDocker run --detach --init --interactive --name $containerName `
    --label 'org.opensimulator.native-preview=true' `
    --publish "127.0.0.1:${Port}:${Port}/tcp" --publish "127.0.0.1:${Port}:${Port}/udp" `
    --env "OPENSIM_PREVIEW_PORT=$Port" `
    --mount "type=volume,source=$($state.SourceVolume),target=/src,readonly" `
    --mount "type=volume,source=$($state.BuildVolume),target=/build,readonly" `
    --mount 'type=volume,source=opensim-native-preview-data,target=/data' `
    --mount "type=bind,source=$(Join-Path $PSScriptRoot 'serve.py'),target=/preview/serve.py,readonly" `
    $state.Image python3 /preview/serve.py
Write-Output "Local preview grid: http://127.0.0.1:$Port/"
Write-Output 'Test accounts: Preview First and Preview Second; password: preview-password'
