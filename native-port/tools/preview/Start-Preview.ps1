param(
    [ValidateRange(1, 65535)][int]$Port = 19092
)
$ErrorActionPreference = 'Stop'
$binary = Join-Path $PSScriptRoot 'bin/opensim-native.exe'
$config = Join-Path $PSScriptRoot 'preview.ini'
$data = Join-Path $PSScriptRoot 'data'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path $data -Force | Out-Null
    if (-not (Test-Path -LiteralPath $config)) {
        $template = Get-Content (Join-Path $PSScriptRoot 'share/opensim-native/preview.ini.template') -Raw
        $template.Replace('%PORT%', [string]$Port).Replace('%BIND%', '127.0.0.1') |
            Set-Content -LiteralPath $config -Encoding utf8
    }
    $configuredPort = Select-String -LiteralPath $config -Pattern '^http_listener_port\s*=\s*(\d+)\s*$'
    if (-not $configuredPort -or [int]$configuredPort.Matches[0].Groups[1].Value -ne $Port) {
        throw 'Existing preview.ini has a different port; reuse that port to retain its settings'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $data 'users.db'))) {
        & $binary --config $config --init-db
        if ($LASTEXITCODE -ne 0) { throw 'Native database initialization failed' }
    }
    $password = if ($env:OPENSIM_PREVIEW_PASSWORD) { $env:OPENSIM_PREVIEW_PASSWORD } else { 'preview-password' }
    foreach ($last in @('First', 'Second')) {
        $marker = Join-Path $data "account-$($last.ToLower()).txt"
        if (Test-Path -LiteralPath $marker) { continue }
        $output = $password | & $binary --config $config --create-user Preview $last 2>&1
        if ($LASTEXITCODE -ne 0 -and ($output -join "`n") -notmatch 'User already exists') {
            throw "Account creation failed: $($output -join [Environment]::NewLine)"
        }
        $output | Set-Content -LiteralPath $marker -Encoding utf8
    }
    Write-Host "Preview grid: http://127.0.0.1:$Port/"
    Write-Host 'Accounts: Preview First / Preview Second. Initial test password: preview-password (or OPENSIM_PREVIEW_PASSWORD).'
    Write-Host 'Type quit in the server console to stop cleanly. Data and preview.ini remain here for the next launch.'
    & $binary --config $config --console
    if ($LASTEXITCODE -ne 0) { throw "Native preview exited with code $LASTEXITCODE" }
} finally {
    Pop-Location
}
