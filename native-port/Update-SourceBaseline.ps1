# Rebuild the source baseline used to track the C++ migration.
# This inventory is coverage input, not proof of behavioral feature parity.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $revision = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision' }
    $tracked = @(git ls-files)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate tracked source' }
    $sources = @($tracked | Where-Object { $_.EndsWith('.cs') -and -not $_.StartsWith('native-port/') })
    $portMap = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'port-map.json') -Raw | ConvertFrom-Json
    $mapped = @{}
    foreach ($entry in $portMap.entries) {
        if ($mapped.ContainsKey($entry.source)) { throw "Duplicate port mapping: $($entry.source)" }
        if ($sources -notcontains $entry.source) { throw "Unknown legacy source: $($entry.source)" }
        foreach ($native in $entry.native) {
            if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $native) -PathType Leaf)) {
                throw "Missing native source: $native"
            }
        }
        $mapped[$entry.source] = $entry
    }
    $records = @(foreach ($source in $sources) {
        $fullPath = Join-Path $root $source
        [ordered]@{
            path = $source
            lines = @([System.IO.File]::ReadLines($fullPath)).Length
            sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
            testDirectory = $source -match '(^|/)Tests/'
            portStatus = $(if ($mapped.ContainsKey($source)) { $mapped[$source].status } else { 'not_started' })
        }
    })
    [xml]$build = Get-Content -LiteralPath (Join-Path $root 'prebuild.xml') -Raw
    $projects = @(foreach ($project in $build.SelectNodes("//*[local-name()='Project']")) {
        [ordered]@{
            name = $project.GetAttribute('name')
            path = $project.GetAttribute('path')
            type = $project.GetAttribute('type')
            references = @($project.SelectNodes("./*[local-name()='Reference']") | ForEach-Object { $_.GetAttribute('name') } | Sort-Object -Unique)
            portStatus = 'not_started'
        }
    })
    $manifest = [ordered]@{
        schemaVersion = 1
        sourceRevision = "$revision"
        targetPlatforms = @('Windows', 'Linux', 'macOS')
        requiredRuntime = 'native C++; no .NET runtime'
        parityVerified = $false
        limitations = @(
            'Tracked-file inventory only; ignored and external modules need separate discovery.'
            'Project declarations can contain conditional build settings; references are not a resolved build graph.'
            'File coverage and existing tests do not establish complete behavioral parity.'
        )
        summary = [ordered]@{
            csharpFiles = $records.Length
            csharpLines = ($records | ForEach-Object { $_.lines } | Measure-Object -Sum).Sum
            declaredProjects = $projects.Length
            mappedSourceScopes = $mapped.Count
            sourcesWithoutPortMapping = $records.Length - $mapped.Count
        }
        projects = $projects
        sources = $records
    }
    $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'source-baseline.json') -Encoding utf8
    $manifest.summary | ConvertTo-Json
}
finally { Pop-Location }
