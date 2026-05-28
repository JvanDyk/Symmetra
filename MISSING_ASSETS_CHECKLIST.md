# Missing Assets Checklist

## What's Missing from Public Repository

Your public GitHub repo is missing critical files needed for the release workflow and installation.

---

## ✅ CHECKLIST: What You Need to Add

### 1. GitHub Actions Release Workflow (CRITICAL)
**File:** `.github/workflows/release.yml`  
**Status:** ❌ **MISSING** — Must be added to public repo  
**Purpose:** Automates building binaries and publishing releases

**Location in private repo:** `D:\GITHUB\SYMMETRA\.github\workflows\release.yml`

**What to do:**
```bash
# Copy from private repo to public repo
Copy-Item "D:\GITHUB\SYMMETRA\.github\workflows\release.yml" `
          "D:\GitHub\Symmetra-Public\.github\workflows\release.yml" -Force
```

---

### 2. Installation Scripts (IMPORTANT)
**Files needed:**
- `scripts/install.ps1` (Windows)
- `scripts/install.sh` (Linux)

**Status:** ❌ **MISSING from public repo** — Need to copy

**Location in private repo:**
- `D:\GITHUB\SYMMETRA\public\install.ps1`
- `D:\GITHUB\SYMMETRA\public\install.sh`

**What to do:**
```bash
# Create scripts directory and copy files
New-Item "D:\GitHub\Symmetra-Public\scripts" -ItemType Directory -Force

Copy-Item "D:\GITHUB\SYMMETRA\public\install.ps1" `
          "D:\GitHub\Symmetra-Public\scripts\install.ps1" -Force

Copy-Item "D:\GITHUB\SYMMETRA\public\install.sh" `
          "D:\GitHub\Symmetra-Public\scripts\install.sh" -Force
```

---

### 3. Compiled Binaries (sym.exe and sym)
**Status:** ❌ **NOT IN REPOSITORY** — Created by CI/CD pipeline  
**Purpose:** Distributed via GitHub Releases, NOT stored in git repo code

**How they get created:**
1. You push a version tag: `git tag v1.0.0`
2. CI/CD pipeline (release.yml) automatically:
   - Builds `sym.exe` on Windows
   - Builds `sym` on Linux
   - Creates packages (ZIP, MSI, deb, rpm, snap)
3. Publishes to GitHub Releases

**For now:** You need the compiled binaries from `D:\GITHUB\SYMMETRA\engine\build\Release\`

---

## 📋 CLEAR STEP-BY-STEP INSTRUCTIONS

### Step 1: Copy the Release Workflow

```powershell
# Navigate to public repo
cd D:\GitHub\Symmetra-Public

# Create .github/workflows directory
New-Item -Path ".\.github\workflows" -ItemType Directory -Force

# Copy the release workflow
Copy-Item "D:\GITHUB\SYMMETRA\.github\workflows\release.yml" `
          "D:\GitHub\Symmetra-Public\.github\workflows\release.yml" -Force
```

**Verify:**
```powershell
Get-Content "D:\GitHub\Symmetra-Public\.github\workflows\release.yml" | Select-Object -First 10
```

---

### Step 2: Copy Installation Scripts

```powershell
# Create scripts directory
New-Item -Path "D:\GitHub\Symmetra-Public\scripts" -ItemType Directory -Force

# Copy PowerShell installer
Copy-Item "D:\GITHUB\SYMMETRA\public\install.ps1" `
          "D:\GitHub\Symmetra-Public\scripts\install.ps1" -Force

# Copy Bash installer
Copy-Item "D:\GITHUB\SYMMETRA\public\install.sh" `
          "D:\GitHub\Symmetra-Public\scripts\install.sh" -Force
```

**Verify:**
```powershell
Get-ChildItem "D:\GitHub\Symmetra-Public\scripts"
```

---

### Step 3: Commit and Push to GitHub

```bash
cd D:\GitHub\Symmetra-Public
git config user.email "deployment@symmetra.dev"
git config user.name "Symmetra Deployment"
git add .
git commit -m "Add release workflow and installation scripts"
git push origin main
```

**Or via batch file:**
```batch
@echo off
cd /d "D:\GitHub\Symmetra-Public"
git config user.email "deployment@symmetra.dev"
git config user.name "Symmetra Deployment"
git add .
git commit -m "Add release workflow and installation scripts"
git push origin main
pause
```

---

### Step 4: Prepare Compiled Binaries (If Not Already Built)

You need `sym.exe` and `sym` binary files. These should be in:
- Windows: `D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe`
- Linux: `D:\GITHUB\SYMMETRA\engine\build\Release\sym`

**If they don't exist, build them:**

```bash
cd D:\GITHUB\SYMMETRA\engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 8
```

**Find the binaries:**
```powershell
Get-ChildItem "D:\GITHUB\SYMMETRA\engine\build\Release" -Filter "sym*"
```

---

### Step 5: Test the Release Workflow

Once release.yml is in the public repo:

```bash
cd D:\GITHUB\SYMMETRA  # Private repo
git tag v1.0.0
git push origin v1.0.0
```

**Monitor the workflow:**
1. Go to: https://github.com/JvanDyk/Symmetra/actions
2. Watch the workflow run
3. It will create binaries and publish GitHub Release automatically

---

## 📊 Summary of What's Missing

| Item | Location | Status | Action |
|------|----------|--------|--------|
| **release.yml** | `.github/workflows/` | ❌ Missing | Copy from private repo |
| **install.ps1** | `scripts/` | ❌ Missing | Copy from private repo |
| **install.sh** | `scripts/` | ❌ Missing | Copy from private repo |
| **sym.exe** | Build artifact | ❌ Not in repo* | Build or wait for CI/CD |
| **sym** | Build artifact | ❌ Not in repo* | Build or wait for CI/CD |
| **Documentation** | Root files | ✅ Present | Done |
| **Issue templates** | `.github/ISSUE_TEMPLATE/` | ✅ Present | Done |

*Binaries should NOT be in repository code — they're published to GitHub Releases via CI/CD

---

## ⚠️ Important Notes

1. **Binaries in Releases, not in Repo**
   - Do NOT commit `.exe` or binary files to git
   - They're auto-created by CI/CD when you tag a version
   - Published to GitHub Releases automatically

2. **release.yml is Critical**
   - Without this, CI/CD won't run
   - Without CI/CD, binaries won't be built/published
   - This is the missing piece!

3. **Installation Scripts in Repository**
   - These scripts download binaries from GitHub Releases
   - They must be in the repo for users to find them
   - Currently missing from public repo

---

## 🚀 Quick Command to Fix Everything

```powershell
# 1. Create directories
New-Item "D:\GitHub\Symmetra-Public\.github\workflows" -ItemType Directory -Force
New-Item "D:\GitHub\Symmetra-Public\scripts" -ItemType Directory -Force

# 2. Copy files
Copy-Item "D:\GITHUB\SYMMETRA\.github\workflows\release.yml" `
          "D:\GitHub\Symmetra-Public\.github\workflows\release.yml" -Force
Copy-Item "D:\GITHUB\SYMMETRA\public\install.ps1" `
          "D:\GitHub\Symmetra-Public\scripts\install.ps1" -Force
Copy-Item "D:\GITHUB\SYMMETRA\public\install.sh" `
          "D:\GitHub\Symmetra-Public\scripts\install.sh" -Force

# 3. Verify
Get-ChildItem "D:\GitHub\Symmetra-Public\.github\workflows"
Get-ChildItem "D:\GitHub\Symmetra-Public\scripts"

# 4. Commit and push
cd "D:\GitHub\Symmetra-Public"
git add .
git commit -m "Add release workflow and installation scripts"
git push origin main
```

---

**Next Action:** Execute the quick command above to add the missing files.

Then test by pushing version tag: `git tag v1.0.0` in the private repo.
