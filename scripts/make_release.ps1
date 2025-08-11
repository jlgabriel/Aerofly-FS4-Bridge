param(
  [Parameter(Mandatory=$true)] [string]$Version,
  [Parameter(Mandatory=$false)] [string]$NotesPath
)

$ErrorActionPreference = 'Stop'

Write-Host "=== Aerofly Bridge Release Helper ===" -ForegroundColor Cyan
Write-Host "Version: $Version"

# 1) Build
Write-Host "Building DLL..." -ForegroundColor Yellow
& "$PSScriptRoot\compile.bat"

$dll = Join-Path $PSScriptRoot "..\dist\AeroflyBridge.dll"
if (!(Test-Path $dll)) {
  Write-Error "Build failed or DLL not found at $dll"
}

# 2) Prepare notes
$notes = ""
if ($NotesPath -and (Test-Path $NotesPath)) {
  $notes = Get-Content -Raw -Path $NotesPath
} else {
  $notes = "Release $Version"
}

# 3) Create release with GitHub CLI
Write-Host "Creating GitHub Release..." -ForegroundColor Yellow

# Ensure gh is available
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  Write-Error "GitHub CLI (gh) is not installed. Install from https://cli.github.com/"
}

# Create release (will fail if tag exists)
& gh release create $Version $dll -t $Version -n $notes

Write-Host "Release created successfully." -ForegroundColor Green
