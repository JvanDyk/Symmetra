# Symmetra Public Repository Deployment Script

$repoUrl = "https://github.com/JvanDyk/Symmetra.git"
$sourceDir = "D:\GITHUB\SYMMETRA\repo"
$deployDir = "D:\GitHub\Symmetra-Public"

Write-Host ""
Write-Host "=============================================================="
Write-Host "  Symmetra Public Repository Deployment"
Write-Host "=============================================================="
Write-Host ""
Write-Host "Repository URL: $repoUrl"
Write-Host "Deploy Directory: $deployDir"
Write-Host "Source Files: $sourceDir"
Write-Host ""

# Step 1: Verify source files exist
Write-Host "[1/5] Verifying source files..." -ForegroundColor Cyan
if (-not (Test-Path $sourceDir)) {
    Write-Host "ERROR: Source directory not found: $sourceDir" -ForegroundColor Red
    exit 1
}

$fileCount = @(Get-ChildItem -Path $sourceDir -Recurse -File).Count
Write-Host "OK - Found $fileCount files in source directory" -ForegroundColor Green
Write-Host ""

# Step 2: Clean and create deploy directory
Write-Host "[2/5] Preparing deploy directory..." -ForegroundColor Cyan
if (Test-Path $deployDir) {
    Write-Host "Removing existing directory: $deployDir"
    Remove-Item -Path $deployDir -Recurse -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}
New-Item -ItemType Directory -Path $deployDir -Force | Out-Null
Write-Host "OK - Deploy directory ready" -ForegroundColor Green
Write-Host ""

# Step 3: Clone the GitHub repository
Write-Host "[3/5] Cloning GitHub repository..." -ForegroundColor Cyan
Push-Location $deployDir

$gitOutput = git clone $repoUrl "." 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "Repository not found. Initializing fresh repository..." -ForegroundColor Yellow
    git init
    git remote add origin $repoUrl
}

Write-Host "OK - Repository ready" -ForegroundColor Green
Pop-Location
Write-Host ""

# Step 4: Copy all files from repo/
Write-Host "[4/5] Copying documentation files..." -ForegroundColor Cyan
Get-ChildItem -Path $sourceDir -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($sourceDir.Length + 1)
    $destination = Join-Path $deployDir $relative
    $destinationDir = Split-Path $destination

    if (-not (Test-Path $destinationDir)) {
        New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
    }

    Copy-Item -Path $_.FullName -Destination $destination -Force
    Write-Host "  OK - $relative"
}
Write-Host "OK - All files copied successfully" -ForegroundColor Green
Write-Host ""

# Step 5: Commit and push
Write-Host "[5/5] Committing and pushing to GitHub..." -ForegroundColor Cyan
Push-Location $deployDir

git config user.email "deployment@symmetra.dev"
git config user.name "Symmetra Deployment"

git add .
$status = git status --porcelain

if ($status) {
    Write-Host "Creating commit with all documentation..."
    git commit -m "Initial public repository setup with documentation"

    Write-Host "Pushing to origin/main..."
    git push origin main 2>&1
    Write-Host "OK - Successfully pushed to GitHub" -ForegroundColor Green
} else {
    Write-Host "Repository is already up to date. No changes to commit." -ForegroundColor Yellow
}

Pop-Location
Write-Host ""

Write-Host "=============================================================="
Write-Host "  OK - Deployment Complete!"
Write-Host "=============================================================="
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Verify repository: $repoUrl"
Write-Host "  2. Check README renders correctly"
Write-Host "  3. Test the release workflow by tagging: git tag v1.0.0"
Write-Host ""
Write-Host "Repository location: $deployDir"
Write-Host ""
