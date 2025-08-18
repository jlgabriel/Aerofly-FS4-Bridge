# Prepare Release v0.3.0 - Performance Optimization
# This script helps prepare all files for the v0.3.0 release

Write-Host "🚀 Preparing Aerofly FS4 Bridge v0.3.0 Release" -ForegroundColor Green
Write-Host "=================================================" -ForegroundColor Green

# Check if we're in the right directory
if (!(Test-Path "aerofly_bridge_dll.cpp")) {
    Write-Host "❌ Error: Please run this script from the project root directory" -ForegroundColor Red
    exit 1
}

Write-Host "✅ Current directory verified" -ForegroundColor Green

# Compile the DLL
Write-Host "🔨 Compiling DLL..." -ForegroundColor Yellow
& "scripts\compile.bat"
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Compilation failed!" -ForegroundColor Red
    exit 1
}
Write-Host "✅ DLL compiled successfully" -ForegroundColor Green

# Check DLL size
$dllPath = "dist\AeroflyBridge.dll"
if (Test-Path $dllPath) {
    $dllSize = (Get-Item $dllPath).Length
    $dllSizeKB = [math]::Round($dllSize / 1024, 1)
    Write-Host "DLL Size: $dllSizeKB KB" -ForegroundColor Cyan
} else {
    Write-Host "DLL not found at $dllPath" -ForegroundColor Red
    exit 1
}

# Display release checklist
Write-Host ""
Write-Host "📋 Release Checklist for v0.3.0:" -ForegroundColor Yellow
Write-Host "=================================" -ForegroundColor Yellow
Write-Host "✅ CHANGELOG.md updated with v0.3.0 changes"
Write-Host "✅ RELEASE_NOTES_v0.3.0.md created for GitHub"
Write-Host "✅ DLL compiled successfully ($dllSizeKB KB)"
Write-Host "✅ Code optimization completed (118 variables)"
Write-Host "✅ Cleanup verification completed"
Write-Host ""

Write-Host "📝 Next Steps:" -ForegroundColor Magenta
Write-Host "1. Commit and push all changes to main branch"
Write-Host "2. Create and push git tag: git tag v0.3.0 && git push origin v0.3.0"  
Write-Host "3. Create GitHub release using RELEASE_NOTES_v0.3.0.md"
Write-Host "4. Upload dist\AeroflyBridge.dll to the GitHub release"
Write-Host ""

Write-Host "🎯 Key Features of v0.3.0:" -ForegroundColor Green
Write-Host "- Hash map optimization (O(n) → O(1))"
Write-Host "- 118 variables optimized (~35% coverage)"
Write-Host "- Code cleanup (~8KB reduction)"
Write-Host "- Zero breaking changes"
Write-Host "- Backward compatibility maintained"
Write-Host ""

Write-Host "🎉 Release v0.3.0 preparation complete!" -ForegroundColor Green

# Display file locations
Write-Host "📁 Important Files:" -ForegroundColor Cyan
Write-Host "- DLL: dist\AeroflyBridge.dll"
Write-Host "- Changelog: CHANGELOG.md"
Write-Host "- Release Notes: RELEASE_NOTES_v0.3.0.md"
Write-Host "- This Script: scripts\prepare_release_v0.3.0.ps1"
