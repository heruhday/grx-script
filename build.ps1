<#
.SYNOPSIS
    Builds qjs ObjectARX / GRX plugin for AutoCAD and GstarCAD.

.DESCRIPTION
    Automates CMake configuration and compilation.
    By default, builds for AutoCAD 2021.
    Use -All to build all supported AutoCAD versions (2017-2024) and GstarCAD 2026.
    Use -Zip to archive the current source without building or deleting files.

.PARAMETER Version
    Specific CAD version to build:
    - AutoCAD: '2017', '2018', '2019', '2020', '2021', '2022', '2023', '2024'
    - GstarCAD: '2026', 'gstarcad', 'gcad'

.PARAMETER GstarCAD
    Target GstarCAD 2026 only.

.PARAMETER All
    Builds binaries for ALL supported targets:
    AutoCAD 2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, and GstarCAD 2026.

.PARAMETER Clean
    Cleans build_* and dist directories before compiling.

.PARAMETER Zip
    Creates <project-folder>-source.zip in OutDir (default: dist).
    Includes current source and bundled dependencies; excludes build outputs,
    caches, version-control data, local databases, and private environment files.
    Runs without CMake, CAD SDKs, Node, Rust, or Git.

.PARAMETER Config
    Build configuration (Release or Debug). Default is Release.

.PARAMETER OutDir
    Output folder, relative to this script or absolute. Default is dist.

.EXAMPLE
    .\build.ps1                    # Builds default: AutoCAD 2021
    .\build.ps1 -Clean             # Cleans old builds, then builds default targets
    .\build.ps1 -All               # Builds all AutoCAD (2017..2024) & GstarCAD 2026
    .\build.ps1 -Version 2018      # Builds AutoCAD 2018
    .\build.ps1 -GstarCAD          # Builds GstarCAD 2026
    .\build.ps1 -Zip               # Creates dist/<project-folder>-source.zip
    .\build.ps1 -Zip -OutDir zips  # Creates the source ZIP in zips/
#>

[CmdletBinding(DefaultParameterSetName = "Default")]
param(
    [Parameter(ParameterSetName = "SpecificVersion", Position = 0)]
    [ValidateSet("2017", "2018", "2019", "2020", "2021", "2022", "2023", "2024", "2026", "gstarcad", "gcad")]
    [string]$Version,

    [Parameter(ParameterSetName = "GstarCAD")]
    [switch]$GstarCAD,

    [Parameter(ParameterSetName = "All")]
    [switch]$All,

    [Parameter(ParameterSetName = "Zip", Mandatory = $true)]
    [switch]$Zip,

    [Parameter(ParameterSetName = "Default")]
    [Parameter(ParameterSetName = "SpecificVersion")]
    [Parameter(ParameterSetName = "GstarCAD")]
    [Parameter(ParameterSetName = "All")]
    [switch]$Clean,

    [Parameter(ParameterSetName = "Default")]
    [Parameter(ParameterSetName = "SpecificVersion")]
    [Parameter(ParameterSetName = "GstarCAD")]
    [Parameter(ParameterSetName = "All")]
    [switch]$BuildUI,

    [Parameter(ParameterSetName = "Default")]
    [Parameter(ParameterSetName = "SpecificVersion")]
    [Parameter(ParameterSetName = "GstarCAD")]
    [Parameter(ParameterSetName = "All")]
    [string]$Config = "Release",

    [ValidateNotNullOrEmpty()]
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

$scriptRoot = $PSScriptRoot
if (-not $scriptRoot) { $scriptRoot = (Get-Location).Path }

function New-SourceArchive([string]$SourceRoot, [string]$OutputDirectory) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $source = Get-Item -LiteralPath $SourceRoot
    $sourcePath = $source.FullName.TrimEnd('\', '/')
    $projectName = $source.Name
    $outputPath = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
        [System.IO.Path]::GetFullPath($OutputDirectory)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $sourcePath $OutputDirectory))
    }
    if ($outputPath.Length -gt [System.IO.Path]::GetPathRoot($outputPath).Length) {
        $outputPath = $outputPath.TrimEnd('\', '/')
    }
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    $archiveName = "$projectName-source.zip"
    $archivePath = Join-Path $outputPath $archiveName
    $temporaryPath = "$archivePath.$([guid]::NewGuid().ToString('N')).tmp"
    $excludedDirectories = @(
        '.git', '.hg', '.svn', '.vs', '.vscode', '.idea', '.codex', '.agents',
        '.aws', '.azure', '.ssh', '.nuget', '.cache', '.vite', '.parcel-cache',
        '.turbo', '.next', '.nuxt', '.svelte-kit', '__pycache__', '.pytest_cache',
        '.mypy_cache', '.ruff_cache', 'node_modules', 'target', 'dist', 'bin',
        'obj', 'x64', 'ARM64', 'Debug', 'Release', 'scratch', 'coverage', 'htmlcov',
        'TestResults', 'test-results', 'playwright-report', 'CMakeFiles', '_CPack_Packages'
    )
    $count = 0
    try {
        $archive = [System.IO.Compression.ZipFile]::Open($temporaryPath, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            $pending = [System.Collections.Generic.Stack[System.IO.DirectoryInfo]]::new()
            $pending.Push($source)
            while ($pending.Count -gt 0) {
                foreach ($item in (Get-ChildItem -LiteralPath $pending.Pop().FullName -Force | Sort-Object Name)) {
                    # OneDrive placeholders are also reparse points; only skip actual links/junctions.
                    if ($item.LinkType) { continue }
                    $relativePath = $item.FullName.Substring($sourcePath.Length + 1).Replace('\', '/')
                    if ($item.PSIsContainer) {
                        if ($item.Name -in $excludedDirectories -or $item.Name -match '^(build($|[_-])|cmake-build-)' -or
                            $relativePath -eq 'qto-property-box/src-tauri/gen' -or
                            $item.FullName.TrimEnd('\', '/') -eq $outputPath) { continue }
                        $pending.Push($item)
                        continue
                    }
                    if ($item.Name -eq $archiveName -or $item.FullName -eq $temporaryPath -or
                        $item.Name -in @('.git', '.DS_Store', 'desktop.ini', 'CMakeCache.txt', 'compile_commands.json') -or
                        ($item.Name -like '.env*' -and $item.Name -notin @('.env.example', '.env.sample', '.env.template')) -or
                        $item.Name -match '\.(db|db3|sqlite|sqlite3)(-(wal|shm|journal))?$' -or
                        $item.Name -match '\.(exe|dll|arx|grx|pdb|obj|o|lib|exp|ilk|dmp|pch|idb|tlog|user|suo|log|tmp|temp|bak|pyc|pyo|tsbuildinfo|swp|swo)$' -or
                        $item.Name -like '*~') { continue }
                    $entryName = "$projectName/$relativePath"
                    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                        $archive, $item.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal
                    ) | Out-Null
                    $count++
                }
            }
        } finally {
            $archive.Dispose()
        }
        Move-Item -LiteralPath $temporaryPath -Destination $archivePath -Force
        Write-Host "[Zip] Archived $count source files: $archivePath" -ForegroundColor Green
        Write-Host '[Zip] Existing source and build folders were left in place.'
    } finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

# Source packaging is independent of the native/UI build toolchains.
if ($Zip) {
    New-SourceArchive $scriptRoot $OutDir
    return
}

# Find CMake executable
$cmakeExe = $null
$cmakeCandidates = @(
    "cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\CMake\bin\cmake.exe"
)

foreach ($c in $cmakeCandidates) {
    if (Get-Command $c -ErrorAction SilentlyContinue) {
        $cmakeExe = (Get-Command $c).Source
        break
    } elseif (Test-Path $c) {
        $cmakeExe = $c
        break
    }
}

if (-not $cmakeExe) {
    Write-Error "CMake executable could not be found. Please ensure CMake or Visual Studio 2022 is installed."
    exit 1
}

if ($Clean) {
    Write-Host "[Clean] Removing build and $OutDir directories..." -ForegroundColor Yellow
    Get-ChildItem -Path $scriptRoot -Directory -Filter "build*" | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path (Join-Path $scriptRoot $OutDir)) {
        Remove-Item -Path (Join-Path $scriptRoot $OutDir) -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Known SDK locations for AutoCAD and GstarCAD
$knownSdkPaths = @{
    "AUTOCAD:2017" = "C:\Autodesk\Autodesk_ObjectARX_2017_Win_64_and_32_Bit\inc\AcRxDefault.def"
    "AUTOCAD:2018" = "C:\Autodesk\Autodesk_ObjectARX_2018_Win_64_and_32_Bit\inc\AcRxDefault.def"
    "AUTOCAD:2019" = "C:\Autodesk\Autodesk_ObjectARX_2019_Win_64_and_32_Bit\inc\AcRxDefault.def"
    "AUTOCAD:2020" = "C:\Autodesk\ObjectARX_for_AutoCAD_2020_Win_64_bit\inc\AcRxDefault.def"
    "AUTOCAD:2021" = "C:\Autodesk\ObjectARX_for_AutoCAD_2021_Win_64bit_dlm\inc\AcRxDefault.def"
    "AUTOCAD:2022" = "C:\Autodesk\ObjectARX_for_AutoCAD_2022_Win_64bit_dlm\inc\AcRxDefault.def"
    "AUTOCAD:2023" = "C:\Autodesk\ObjectARX_for_AutoCAD_2023_Win_64bit_dlm\inc\AcRxDefault.def"
    "AUTOCAD:2024" = "C:\Autodesk\ObjectARX_for_AutoCAD_2024_Win_64bit_dlm\inc\AcRxDefault.def"
    "GSTARCAD:2026" = "C:\GstarCAD\grxsdk\arx\inc\AcRxDefault.def"
}

function Test-SdkInstalled($plat, $ver) {
    $k = "$plat`:$ver"
    if ($knownSdkPaths.ContainsKey($k)) {
        return (Test-Path $knownSdkPaths[$k])
    }
    return $true
}

# Determine target list
$targets = [System.Collections.Generic.List[PSObject]]::new()

if ($All) {
    foreach ($v in @("2017", "2018", "2019", "2020", "2021", "2022", "2023", "2024")) {
        $targets.Add([PSCustomObject]@{ Platform="AUTOCAD"; Version=$v; Key=$v })
    }
    $targets.Add([PSCustomObject]@{ Platform="GSTARCAD"; Version="2026"; Key="gstarcad_2026" })
} elseif ($GstarCAD -or ($PSCmdlet.ParameterSetName -eq "SpecificVersion" -and ($Version -in @("2026", "gstarcad", "gcad")))) {
    $targets.Add([PSCustomObject]@{ Platform="GSTARCAD"; Version="2026"; Key="gstarcad_2026" })
} elseif ($PSCmdlet.ParameterSetName -eq "SpecificVersion" -and $Version) {
    $targets.Add([PSCustomObject]@{ Platform="AUTOCAD"; Version=$Version; Key=$Version })
} else {
    $targets.Add([PSCustomObject]@{ Platform="AUTOCAD"; Version="2021"; Key="2021" })
}

$distPath = Join-Path $scriptRoot $OutDir
$distBin = Join-Path $distPath "bin"
$bundleContents = Join-Path $distPath "arx-script.bundle\Contents"

New-Item -ItemType Directory -Force -Path $distBin | Out-Null
New-Item -ItemType Directory -Force -Path $bundleContents | Out-Null

$results = [System.Collections.Generic.List[PSObject]]::new()

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " Building for Targets: $(($targets | ForEach-Object { "$($_.Platform) $($_.Version)" }) -join ', ')" -ForegroundColor Cyan
Write-Host " Using CMake: $cmakeExe" -ForegroundColor Cyan
Write-Host " Config: $Config" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

foreach ($tgt in $targets) {
    $plat = $tgt.Platform
    $ver = $tgt.Version
    $key = $tgt.Key

    Write-Host "`n>>> [$plat $ver] Configuring and Building..." -ForegroundColor Green
    $buildDir = Join-Path $scriptRoot "build_$key"
    $targetDir = Join-Path $distPath $key
    $bundleVerDir = Join-Path $bundleContents $key
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    New-Item -ItemType Directory -Force -Path $bundleVerDir | Out-Null

    # Check if SDK is available
    if (-not (Test-SdkInstalled $plat $ver)) {
        Write-Warning "SDK for $plat $ver was not found. Skipping build."
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Skipped (Missing SDK)"; Size="-"; Binary="-" })
        continue
    }

    # 1. CMake Configure
    $configureArgs = @(
        "-S", $scriptRoot,
        "-B", $buildDir,
        "-DCAD_PLATFORM=$plat"
    )
    if ($plat -eq "AUTOCAD") {
        $configureArgs += "-DAUTOCAD_VERSION=$ver"
    } else {
        $configureArgs += "-DGSTARCAD_VERSION=$ver"
    }

    & $cmakeExe @configureArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to configure CMake for $plat $ver."
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Failed (Configure)"; Size="-"; Binary="-" })
        continue
    }

    # 2. CMake Build
    $buildArgs = @(
        "--build", $buildDir,
        "--config", $Config,
        "--target", "arx01"
    )
    & $cmakeExe @buildArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to build for $plat $ver."
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Failed (Build)"; Size="-"; Binary="-" })
        continue
    }

    # 3. Locate built binaries
    # AutoCAD output is qjs_<version>.arx
    # GstarCAD output is qjs_<version>.grx
    $primaryName = if ($plat -eq "GSTARCAD") { "qjs_$ver.grx" } else { "qjs_$ver.arx" }
    $builtBin = Join-Path $buildDir "bin\$Config\$primaryName"
    $builtDll = Join-Path $buildDir "bin\$Config\WebView2Loader.dll"

    if (Test-Path $builtBin) {
        function Safe-CopyFile($src, $dst) {
            try {
                Copy-Item -Path $src -Destination $dst -Force
            } catch {
                Write-Warning "Could not overwrite $dst (process may be holding file lock)"
            }
        }

        # Copy primary binary to dist/<key>/, central dist/bin/, and bundle
        Safe-CopyFile $builtBin (Join-Path $targetDir $primaryName)
        Safe-CopyFile $builtBin (Join-Path $distBin $primaryName)
        Safe-CopyFile $builtBin (Join-Path $bundleVerDir $primaryName)

        if (Test-Path $builtDll) {
            Safe-CopyFile $builtDll (Join-Path $targetDir "WebView2Loader.dll")
            Safe-CopyFile $builtDll (Join-Path $distBin "WebView2Loader.dll")
            Safe-CopyFile $builtDll (Join-Path $bundleVerDir "WebView2Loader.dll")
        }

        $size = "{0:N2} MB" -f ((Get-Item $builtBin).Length / 1MB)
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Success"; Size=$size; Binary="dist/$key/$primaryName" })
    } else {
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Missing Output"; Binary="-" })
    }
}


# Copy PackageContents.xml if present
$pkgXml = Join-Path $scriptRoot "PackageContents.xml"
if (Test-Path $pkgXml) {
    Copy-Item -Path $pkgXml -Destination (Join-Path $distPath "arx-script.bundle\PackageContents.xml") -Force
}

# Copy runjs.lsp to dist
$runjsLsp = Join-Path $scriptRoot "lisp\runjs.lsp"
if (Test-Path $runjsLsp) {
    Copy-Item -Path $runjsLsp -Destination (Join-Path $distPath "runjs.lsp") -Force
    Copy-Item -Path $runjsLsp -Destination (Join-Path $distPath "arx-script.bundle\Contents\runjs.lsp") -Force
}

# Build QTO Property Box (Tauri) if requested
$tauriDir = Join-Path $scriptRoot "qto-property-box"
if ($BuildUI -and (Test-Path $tauriDir)) {
    Write-Host "`n>>> [QTO Property Box] Building Tauri application..." -ForegroundColor Green
    Push-Location $tauriDir
    try {
        if (-not (Test-Path "node_modules")) {
            Write-Host "Installing npm dependencies..." -ForegroundColor Cyan
            npm install
        }
        Write-Host "Building web frontend..." -ForegroundColor Cyan
        npm run build
        Write-Host "Compiling Tauri Rust binary..." -ForegroundColor Cyan
        cargo build --release --manifest-path src-tauri\Cargo.toml
    } finally {
        Pop-Location
    }
}

# Copy QTO Property Box (Tauri executable) to dist/bin and bundle dirs if built
$tauriExeRelease = Join-Path $scriptRoot "qto-property-box\src-tauri\target\release\qto-property-box.exe"
$tauriExeDebug = Join-Path $scriptRoot "qto-property-box\src-tauri\target\debug\qto-property-box.exe"
$tauriExe = if (Test-Path $tauriExeRelease) { $tauriExeRelease } elseif (Test-Path $tauriExeDebug) { $tauriExeDebug } else { $null }

if ($tauriExe) {
    function Safe-CopyTauri($dst) {
        if (Test-Path (Split-Path $dst -Parent)) {
            try {
                Copy-Item -Path $tauriExe -Destination $dst -Force -ErrorAction SilentlyContinue
            } catch {}
        }
    }

    Safe-CopyTauri (Join-Path $distBin "qto-property-box.exe")
    foreach ($tgt in $targets) {
        Safe-CopyTauri (Join-Path $distPath "$($tgt.Key)\qto-property-box.exe")
        Safe-CopyTauri (Join-Path $bundleContents "$($tgt.Key)\qto-property-box.exe")
    }
}

Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host " Build Summary" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
$results | Format-Table -AutoSize

Write-Host "Distribution outputs are in: $distPath" -ForegroundColor Green
