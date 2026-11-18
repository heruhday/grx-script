<#
.SYNOPSIS
    Exercises build.ps1 -Zip with an isolated source fixture; no build SDKs required.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-ZipTest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Write-FixtureFile([string]$RelativePath, [string]$Text) {
    $path = Join-Path $fixtureRoot $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
    [System.IO.File]::WriteAllText($path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Read-FixtureArchive([string]$Path) {
    Assert-ZipTest (Test-Path -LiteralPath $Path -PathType Leaf) "Archive not created: $Path"
    $contents = @{}
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        foreach ($entry in $archive.Entries) {
            Assert-ZipTest ($entry.FullName.StartsWith("$fixtureName/", [System.StringComparison]::Ordinal)) "Missing single source root: $($entry.FullName)"
            Assert-ZipTest (-not $entry.FullName.Contains('\')) "Non-portable ZIP separator: $($entry.FullName)"
            if ($entry.FullName.EndsWith('/')) { continue }
            $relative = $entry.FullName.Substring($fixtureName.Length + 1)
            Assert-ZipTest (-not $contents.ContainsKey($relative)) "Duplicate archive entry: $relative"
            $reader = [System.IO.StreamReader]::new($entry.Open(), [System.Text.Encoding]::UTF8)
            try { $contents[$relative] = $reader.ReadToEnd() } finally { $reader.Dispose() }
        }
    } finally { $archive.Dispose() }
    return $contents
}

function Invoke-FixtureZip([string[]]$ExtraArguments = @()) {
    # Use an absolute shell path and no dependency-tool search path. The source
    # fixture deliberately has no Git repository, node_modules, or SDK setup.
    $previousPath = $env:PATH
    try {
        $env:PATH = $emptyToolPath
        $output = & $shellExecutable -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixtureRoot 'build.ps1') -Zip @ExtraArguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally { $env:PATH = $previousPath }
    Assert-ZipTest ($exitCode -eq 0) "Source zip failed ($exitCode): $($output -join [Environment]::NewLine)"
    Assert-ZipTest (-not (($output -join [Environment]::NewLine) -match 'Configuring and Building|Building for Targets:|Using CMake:')) 'Zip mode unexpectedly entered the CAD build workflow'
}

$sourceScript = Join-Path (Split-Path $PSScriptRoot -Parent) 'build.ps1'
$shellExecutable = (Get-Process -Id $PID).Path
$tempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\', '/')
$taskTempRoot = Join-Path $tempParent ('source-zip-tests-' + [guid]::NewGuid().ToString('N'))
$fixtureName = 'source fixture'
$fixtureRoot = Join-Path $taskTempRoot $fixtureName
$emptyToolPath = Join-Path $taskTempRoot 'no-dependency-tools'
$junctionPath = Join-Path $fixtureRoot 'linked-outside-source'
$junctionCreated = $false

try {
    New-Item -ItemType Directory -Path $fixtureRoot, $emptyToolPath -Force | Out-Null
    Copy-Item -LiteralPath $sourceScript -Destination (Join-Path $fixtureRoot 'build.ps1')
    $scriptHash = (Get-FileHash -LiteralPath (Join-Path $fixtureRoot 'build.ps1') -Algorithm SHA256).Hash

    $included = @{
        'README.md' = 'Read me from the current working tree'
        'CMakeLists.txt' = 'project(source_zip_fixture)'
        'src/current.cpp' = 'source version one'
        'src/new untracked.cpp' = 'untracked working source'
        'include/plugin.hpp' = 'source header'
        'qto-property-box/src/App.jsx' = 'current UI source'
        'qto-property-box/src-tauri/Cargo.lock' = 'locked dependency versions'
        'third_party/quickjs/source.c' = 'vendored dependency source'
        'third_party/quickjs/quickjs-amalgam.zip' = 'vendored archive bytes'
        '.gitignore' = 'build/'
        '.env.example' = 'EXAMPLE_ONLY=1'
        '.env.sample' = 'SAMPLE_ONLY=1'
        '.env.template' = 'TEMPLATE_ONLY=1'
    }
    $unicodePath = 'src/space folder/r' + [char]0xE9 + 'sum' + [char]0xE9 + ' ' + [char]0x540D + '.js'
    $included[$unicodePath] = 'Unicode file content ' + [char]0x540D
    foreach ($relative in $included.Keys) { Write-FixtureFile $relative $included[$relative] }

    $excluded = @(
        '.git/config', '.vs/cache.txt', '.vscode/settings.json', '.idea/workspace.xml',
        'build/cache.txt', 'build_2021/do-not-delete.txt', 'build-preview/cache.txt',
        'dist/old-source.zip', 'scratch/private.txt', 'node_modules/package/index.js',
        'qto-property-box/node_modules/dependency/index.js', 'qto-property-box/dist/index.html',
        'qto-property-box/src-tauri/target/release/cache.txt', 'target/cache.txt', '__pycache__/cache.pyc',
        '.env', '.env.local', '.env.production', 'project.db', 'project.db-wal', 'project.db-shm',
        'project.db-journal', 'project.sqlite', 'project.sqlite-wal', 'project.sqlite3',
        'project.sqlite3-shm', 'project.sqlite3-journal', 'local.log', 'local.tmp',
        'compiled.exe', 'compiled.dll', 'compiled.arx', 'compiled.grx', 'compiled.pdb',
        'compiled.obj', 'compiled.o', 'compiled.lib', 'compiled.exp', 'compiled.ilk', 'compiled.dmp'
    )
    foreach ($relative in $excluded) { Write-FixtureFile $relative "keep original: $relative" }
    $outsideTarget = Join-Path $taskTempRoot 'outside-fixture'
    New-Item -ItemType Directory -Path $outsideTarget | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $outsideTarget 'outside-private.txt'), 'Do not include this linked file')
    try {
        New-Item -ItemType Junction -Path $junctionPath -Target $outsideTarget -ErrorAction Stop | Out-Null
        $junctionCreated = $true
    } catch { Write-Host 'Junction creation unavailable; remaining archive checks still run.' }

    # Invocation from outside the source root also checks PSScriptRoot handling.
    Push-Location $taskTempRoot
    try {
        Invoke-FixtureZip
        $defaultArchive = Join-Path $fixtureRoot "dist/$fixtureName-source.zip"
        $contents = Read-FixtureArchive $defaultArchive
        Assert-ZipTest ($contents.Count -eq $included.Count + 1) "Unexpected archive entries: $($contents.Keys -join ', ')"
        foreach ($relative in $included.Keys) {
            Assert-ZipTest ($contents.ContainsKey($relative)) "Source omitted: $relative"
            Assert-ZipTest ($contents[$relative] -ceq $included[$relative]) "Source bytes changed: $relative"
        }
        Assert-ZipTest ($contents.ContainsKey('build.ps1')) 'The build script is missing from the source archive'
        foreach ($relative in $excluded) {
            Assert-ZipTest (-not $contents.ContainsKey($relative)) "Local/generated file included: $relative"
            Assert-ZipTest ([System.IO.File]::ReadAllText((Join-Path $fixtureRoot $relative)) -ceq "keep original: $relative") "Zip mode modified/deleted a local file: $relative"
        }
        if ($junctionCreated) {
            Assert-ZipTest (-not $contents.ContainsKey('linked-outside-source/outside-private.txt')) 'Archive followed a directory junction'
        }

        # Re-running must replace the archive using the latest source bytes.
        Write-FixtureFile 'src/current.cpp' 'source version two'
        Invoke-FixtureZip
        $contents = Read-FixtureArchive $defaultArchive
        Assert-ZipTest ($contents['src/current.cpp'] -ceq 'source version two') 'Repeat zip reused an old working-tree version'

        Write-FixtureFile 'packages/keep-output-sentinel.txt' 'custom output directory sentinel'
        Invoke-FixtureZip -ExtraArguments @('-OutDir', 'packages')
        $customContents = Read-FixtureArchive (Join-Path $fixtureRoot "packages/$fixtureName-source.zip")
        Assert-ZipTest ($customContents.Count -eq $included.Count + 1) "Custom output folder was archived: $($customContents.Keys -join ', ')"
        Assert-ZipTest ($customContents['src/current.cpp'] -ceq 'source version two') 'Custom archive omitted the current source version'
        Assert-ZipTest ([System.IO.File]::ReadAllText((Join-Path $fixtureRoot 'packages/keep-output-sentinel.txt')) -ceq 'custom output directory sentinel') 'Custom output folder contents were removed'
        Assert-ZipTest ((Get-FileHash -LiteralPath (Join-Path $fixtureRoot 'build.ps1') -Algorithm SHA256).Hash -eq $scriptHash) 'Zip mode modified the build script'
        Assert-ZipTest ([System.IO.File]::ReadAllText((Join-Path $outsideTarget 'outside-private.txt')) -ceq 'Do not include this linked file') 'Zip mode modified the junction target'
    } finally { Pop-Location }

    Write-Host 'Source ZIP checks passed: current source, exclusions, Unicode paths, repeat/custom output, link safety, and source preservation.'
} finally {
    # Only remove the uniquely named test-owned folder after validating its full
    # path; even the optional junction target is contained within this folder.
    $cleanupRoot = [System.IO.Path]::GetFullPath($taskTempRoot).TrimEnd('\', '/')
    $requiredPrefix = $tempParent + [System.IO.Path]::DirectorySeparatorChar
    if (-not $cleanupRoot.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $cleanupRoot -Leaf) -notlike 'source-zip-tests-*') {
        throw "Refusing test cleanup outside its temporary root: $cleanupRoot"
    }
    if (Test-Path -LiteralPath $cleanupRoot) {
        Remove-Item -LiteralPath $cleanupRoot -Recurse -Force
    }
}
