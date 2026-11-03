#requires -Version 5.1
<#
BrickTrace environment check
Usage:
  powershell -ExecutionPolicy Bypass -File .\scripts\check-env.ps1
#>

$ErrorActionPreference = "Continue"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$script:Failures = 0
$script:Warnings = 0

function Write-Result {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Ok,
        [string]$Detail = "",
        [switch]$WarnOnly
    )
    if ($Ok) {
        Write-Host "[PASS] $Name" -ForegroundColor Green
    }
    elseif ($WarnOnly) {
        $script:Warnings++
        Write-Host "[WARN] $Name" -ForegroundColor Yellow
    }
    else {
        $script:Failures++
        Write-Host "[FAIL] $Name" -ForegroundColor Red
    }
    if ($Detail) {
        Write-Host "       $Detail" -ForegroundColor DarkGray
    }
}

function Get-CommandPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

Write-Host "=============================================="
Write-Host " BrickTrace environment check"
Write-Host "=============================================="
Write-Host ""

# OS
$os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
if ($os) {
    $ok = $os.Caption -match "Windows 1[01]"
    Write-Result "Windows 10/11" $ok ("$($os.Caption) $($os.Version)")
}
else {
    Write-Result "Windows" $false "Unable to query OS"
}

# RAM
$cs = Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue
if ($cs) {
    $ramGB = [math]::Round($cs.TotalPhysicalMemory / 1GB, 1)
    Write-Result "RAM >= 16GB" ($ramGB -ge 16) "$ramGB GB"
}

# GPU list
$gpus = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue
if ($gpus) {
    foreach ($g in $gpus) {
        Write-Host "       GPU: $($g.Name) / driver $($g.DriverVersion)"
    }
}

# NVIDIA
$smi = Get-CommandPath "nvidia-smi"
if ($smi) {
    $smiOut = (& $smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>&1 | Out-String).Trim()
    Write-Host "       nvidia-smi: $smiOut"
    Write-Result "RTX 5060" ($smiOut -match "5060") $smiOut
    if ($smiOut -notmatch "RTX") {
        Write-Result "RT-capable NVIDIA GPU" $false "GTX may expose RT extensions but has no RT cores; use RTX for validation" -WarnOnly
    }
}
else {
    Write-Result "nvidia-smi" $false "NVIDIA driver not installed or not in PATH"
}

# Vulkan
$vulkaninfo = $null
if ($env:VULKAN_SDK) {
    $candidate = Join-Path $env:VULKAN_SDK "Bin\vulkaninfo.exe"
    if (Test-Path $candidate) { $vulkaninfo = $candidate }
}
if (-not $vulkaninfo) { $vulkaninfo = Get-CommandPath "vulkaninfo" }

if ($vulkaninfo) {
    Write-Result "vulkaninfo" $true $vulkaninfo
    $null = & $vulkaninfo --summary 2>&1 | Out-String
    Write-Result "Vulkan runtime" ($LASTEXITCODE -eq 0) "vulkaninfo --summary exit=$LASTEXITCODE"

    $extOutput = (& $vulkaninfo 2>&1 | Out-String)
    foreach ($ext in @("VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline")) {
        Write-Result $ext ($extOutput -match [regex]::Escape($ext)) ""
    }
    Write-Result "VK_KHR_ray_query (recommended)" ($extOutput -match "VK_KHR_ray_query") "" -WarnOnly
}
else {
    Write-Result "Vulkan SDK" $false "Install LunarG Vulkan SDK and reopen terminal"
}

# Java
$java = Get-CommandPath "java"
if ($java) {
    $jv = (cmd /c "java -version 2>&1" | Out-String).Trim()
    $major = 0
    if ($jv -match 'version "(\d+)') { $major = [int]$Matches[1] }
    Write-Result "Java >= 25" ($major -ge 25) ($jv -replace "`r?`n", " | ")
}
else {
    Write-Result "Java" $false "JDK 25 not installed or not in PATH"
}

# Git
$git = Get-CommandPath "git"
if ($git) {
    $gv = (& git --version 2>&1 | Out-String).Trim()
    Write-Result "git" $true $gv
}
else {
    Write-Result "git" $false "Install Git"
}

# CMake
$cmake = Get-CommandPath "cmake"
if ($cmake) {
    $cv = ((& cmake --version 2>&1 | Out-String).Trim() -split "`n")[0]
    Write-Result "cmake" $true $cv
}
else {
    Write-Result "cmake" $false "Install CMake 3.28+"
}

# MSVC / VS Build Tools
$cl = Get-CommandPath "cl"
$vsFound = $false
$vsDetail = ""
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = (& $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>&1 | Out-String).Trim()
    if (-not $vsPath) {
        $vsPath = (& $vswhere -latest -prerelease -products * -property installationPath 2>&1 | Out-String).Trim()
    }
    if ($vsPath) {
        $vsFound = $true
        $vsDetail = $vsPath
    }
}
if ($cl) {
    Write-Result "MSVC (cl in PATH)" $true $cl
}
elseif ($vsFound) {
    Write-Result "MSVC (VS Build Tools)" $true $vsDetail
}
else {
    Write-Result "MSVC" $false "Install VS 2022 Build Tools with C++ workload"
}

# Vulkan SDK env
Write-Result "VULKAN_SDK" ([bool]$env:VULKAN_SDK) ($(if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { "not set" }))

# Disk
$driveLetter = (Get-Location).Path.Substring(0, 1)
$drive = Get-PSDrive -Name $driveLetter -ErrorAction SilentlyContinue
if ($drive) {
    $freeGB = [math]::Round($drive.Free / 1GB, 1)
    Write-Result "Disk free >= 20GB" ($freeGB -ge 20) "$freeGB GB free on $driveLetter`:"
}

# Minecraft 26.3
$mcPaths = @(
    (Join-Path $env:APPDATA ".minecraft\versions\26.3"),
    (Join-Path $env:USERPROFILE ".minecraft\versions\26.3")
)
$foundMc = $mcPaths | Where-Object { Test-Path $_ }
if ($foundMc) {
    Write-Result "Minecraft 26.3" $true ($foundMc -join "; ")
}
else {
    Write-Result "Minecraft 26.3" $false "Install 26.3 and launch once" -WarnOnly
}

# Lumen baseline: look in the repo-relative mods/ dir (this script lives in scripts/)
$repoRoot = Split-Path -Parent $PSScriptRoot
$lumenCandidates = @(
    (Join-Path $repoRoot "mods\Lumen-0.1.3-alpha.jar"),
    "E:\mc\mods\Lumen-0.1.3-alpha.jar"
)
$lumen = $lumenCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($lumen) {
    $len = (Get-Item $lumen).Length
    $expected = 56382798
    Write-Result "Lumen baseline jar" ($len -eq $expected) "$lumen ($len bytes; expected $expected)"
}
else {
    Write-Result "Lumen baseline jar" $false "Copy mods/Lumen-0.1.3-alpha.jar from the old machine" -WarnOnly
}

Write-Host ""
Write-Host "=============================================="
Write-Host (" Failures: {0}   Warnings: {1}" -f $script:Failures, $script:Warnings)
Write-Host "=============================================="
if ($script:Failures -gt 0) { exit 1 } else { exit 0 }
