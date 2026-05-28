# Upload Release Assets to GitHub

Your release `v1.0.0` has been created but needs binary assets uploaded.

---

## 📦 Asset Ready

**Location:** `D:\GITHUB\SYMMETRA\release-packages\`

- `symmetra-1.0.0-windows-x64.zip` (0.25 MB)
  - SHA256: `B6DE242F77BD75ACA8A9496E971AF7900FCEA2F57E8A8100D278CA6FB627EAB9`

---

## 🚀 Upload to GitHub Release

### Option 1: Via GitHub Web UI (Easiest)

1. Go to: https://github.com/JvanDyk/Symmetra/releases/tag/v1.0.0
2. Click "Edit" button
3. Scroll to "Attach binaries" section
4. Drag and drop: `symmetra-1.0.0-windows-x64.zip`
5. Click "Update release"

**Done!** The asset will be uploadedand available for download.

---

### Option 2: Via GitHub CLI (gh command)

```powershell
cd D:\GITHUB\SYMMETRA\release-packages

# Upload asset
gh release upload v1.0.0 `
  symmetra-1.0.0-windows-x64.zip `
  --repo JvanDyk/Symmetra
```

---

### Option 3: Via PowerShell Script

```powershell
# Set variables
$Owner = "JvanDyk"
$Repo = "Symmetra"
$Tag = "v1.0.0"
$AssetPath = "D:\GITHUB\SYMMETRA\release-packages\symmetra-1.0.0-windows-x64.zip"
$Token = "YOUR_GITHUB_TOKEN"  # Create at github.com/settings/tokens

# Upload
$Headers = @{
    "Authorization" = "Bearer $Token"
    "Accept" = "application/vnd.github.v3+json"
}

$ReleaseUrl = "https://api.github.com/repos/$Owner/$Repo/releases/tags/$Tag"
$Release = Invoke-RestMethod -Uri $ReleaseUrl -Headers $Headers

$UploadUrl = $Release.upload_url -replace '\{.*'
$UploadUrl = "$UploadUrl?name=$(Split-Path $AssetPath -Leaf)"

$FileContent = [IO.File]::ReadAllBytes($AssetPath)

Invoke-RestMethod -Uri $UploadUrl `
  -Method POST `
  -Headers @{
      "Authorization" = "Bearer $Token"
      "Content-Type" = "application/zip"
  } `
  -Body $FileContent
```

---

## 📝 After Upload

Once assets are uploaded to the release:

1. ✅ Users can download from: https://github.com/JvanDyk/Symmetra/releases/tag/v1.0.0
2. ✅ Installation scripts will work: `iwr https://symmetra.dev/install.ps1 | iex`
3. ✅ Release is complete!

---

## Future Releases

For next releases (v1.0.1, v1.1.0, etc.):

1. **Build locally** in private repo
2. **Create packages** (ZIP, MSI, etc.)
3. **Tag version** in public repo
4. **Upload assets** manually

The simplified workflow will automatically create the release page. You just upload the binaries.

---

**Choose Option 1 (Web UI) for simplicity!**
