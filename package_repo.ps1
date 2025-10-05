# package_repo.ps1
# Collect all important text-based files into a single text file for review.
# runs with: powershell -ExecutionPolicy Bypass -File .\package_repo.ps1

$output = "repo_summary.txt"
if (Test-Path $output) { Remove-Item $output }

Write-Host "Collecting important files into $output ..."

# Define what to include and exclude
$include = @("*.cpp", "*.c", "*.h", "*.hpp", "*.rc", "*.manifest", "*.md", ".gitignore", "Makefile", "CMakeLists.txt")
$exclude = @("*.exe", "*.dll", "*.o", "*.obj", "*.lib", "*.so", "*.bin")

# Get matching files recursively
$files = Get-ChildItem -Recurse -File | Where-Object {
    $includeMatch = $false
    foreach ($pattern in $include) {
        if ($_ -like $pattern) { $includeMatch = $true; break }
    }
    $excludeMatch = $false
    foreach ($pattern in $exclude) {
        if ($_ -like $pattern) { $excludeMatch = $true; break }
    }
    $includeMatch -and -not $excludeMatch
}

# Combine contents
foreach ($file in $files) {
    Add-Content $output "`n`n======================================"
    Add-Content $output "FILE: $($file.FullName)"
    Add-Content $output "======================================"
    Get-Content $file | Add-Content $output
}

Write-Host "✅ Done! All relevant project files have been merged into $output."
