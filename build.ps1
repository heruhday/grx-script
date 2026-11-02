<#
.SYNOPSIS
    Builds qjs ObjectARX / GRX plugin for AutoCAD and GstarCAD.

.DESCRIPTION
    Automates CMake configuration and compilation.
    By default, builds for AutoCAD 2018 and GstarCAD 2026.
    Use -All to build all supported AutoCAD versions (2017-2024) and GstarCAD 2026.

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

.PARAMETER Config
    Build configuration (Release or Debug). Default is Release.

.EXAMPLE
    .\build.ps1                    # Builds default: AutoCAD 2018 & GstarCAD 2026
    .\build.ps1 -Clean             # Cleans old builds, then builds default targets
    .\build.ps1 -All               # Builds all AutoCAD (2017..2024) & GstarCAD 2026
    .\build.ps1 -Version 2018      # Builds AutoCAD 2018
    .\build.ps1 -GstarCAD          # Builds GstarCAD 2026
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

    [switch]$Clean,
    [string]$Config = "Release",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

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

$scriptRoot = $PSScriptRoot
if (-not $scriptRoot) { $scriptRoot = Get-Location }

if ($Clean) {
    Write-Host "[Clean] Removing build and $OutDir directories..." -ForegroundColor Yellow
    Get-ChildItem -Path $scriptRoot -Directory -Filter "build*" | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path (Join-Path $scriptRoot $OutDir)) {
        Remove-Item -Path (Join-Path $scriptRoot $OutDir) -Recurse -Force -ErrorAction SilentlyContinue
    }
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
    # Default: AutoCAD 2018, AutoCAD 2021, and GstarCAD 2026
    $targets.Add([PSCustomObject]@{ Platform="AUTOCAD"; Version="2018"; Key="2018" })
    $targets.Add([PSCustomObject]@{ Platform="AUTOCAD"; Version="2021"; Key="2021" })
    $targets.Add([PSCustomObject]@{ Platform="GSTARCAD"; Version="2026"; Key="gstarcad_2026" })
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
        Write-Error "Failed to configure CMake for $plat $ver."
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Failed (Configure)"; Binary="-" })
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
        Write-Error "Failed to build for $plat $ver."
        $results.Add([PSCustomObject]@{ Platform=$plat; Version=$ver; Status="Failed (Build)"; Binary="-" })
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

Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host " Build Summary" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
$results | Format-Table -AutoSize

Write-Host "Distribution outputs are in: $distPath" -ForegroundColor Green
