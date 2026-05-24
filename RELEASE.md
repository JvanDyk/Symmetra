# How to publish a new Symmetra release

## Architecture

```
PRIVATE repo (this directory)          PUBLIC repo (public/ pushed to GitHub)
────────────────────────────           ────────────────────────────────────────
engine source code                     website, SDK types, examples, docs
.github/workflows/release.yml    →     Releases (sym-windows-x64.exe, sym-linux-x64)
                                        ↑
                                 GitHub Actions uploads binaries here
```

The private repo is **never** made public. It has its own GitHub Actions runner
that builds the binaries and uploads them to the **separate** public repo's Releases.

---

## One-time setup (do this once)

### 1. Create the public GitHub repo

Go to github.com → New repository → name it `symmetra` → set it **Public**.
Do NOT add a README (we will push from the `public/` folder).

### 2. Push the public folder

```powershell
cd public
git init
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/symmetra.git
git add .
git commit -m "Symmetra v1.0.0 — public release"
git push -u origin main
```

### 3. Enable GitHub Pages on the public repo

GitHub → `symmetra` repo → Settings → Pages → Deploy from branch `main` → folder `/`.
Site goes live at `https://YOUR_USERNAME.github.io/symmetra/`.

### 4. Update install scripts with your username

In `public/install.ps1` line 5 — change `YOUR_USERNAME` to your real GitHub username.
In `public/install.sh` line 5  — change `YOUR_USERNAME` to your real GitHub username.

```powershell
cd public
git add install.ps1 install.sh
git commit -m "Set release URLs"
git push
```

### 5. Create a Personal Access Token (PAT) for cross-repo releases

GitHub → Settings → Developer settings → Personal access tokens → Fine-grained tokens:
- Resource owner: your account
- Repository access: **Only the public `symmetra` repo**
- Permissions → Repository → **Contents: Read and write**

Copy the token.

### 6. Add secrets to the PRIVATE repo

GitHub → your private repo → Settings → Secrets and variables → Actions → New secret:

| Secret name       | Value                              |
|-------------------|------------------------------------|
| `PUBLIC_REPO`     | `YOUR_USERNAME/symmetra`           |
| `PUBLIC_REPO_PAT` | the PAT you just created           |

---

## Publishing a new release (every time)

### Option A — Automated (recommended)

Push a version tag to the **private** repo. GitHub Actions does everything else:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

The workflow (`.github/workflows/release.yml`) will:
1. Build `sym.exe` on Windows (CUDA 12.6)
2. Build `sym` on Linux (CUDA 12.6)
3. Create a Release on the PUBLIC `symmetra` repo
4. Attach both binaries as release assets

Watch progress at: `https://github.com/YOUR_PRIVATE_REPO/actions`

### Option B — Manual

Build locally, then use GitHub CLI to upload to the PUBLIC repo:

```powershell
# Build
cd engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Install GitHub CLI if needed
winget install GitHub.cli
gh auth login

# Upload to the PUBLIC repo's releases
gh release create v1.0.0 `
  --repo YOUR_USERNAME/symmetra `
  --title "Symmetra v1.0.0" `
  --notes "See https://symmetra.dev/docs/getting-started" `
  engine\build\Release\sym.exe#sym-windows-x64.exe
  # Add Linux binary if you also built on Linux:
  # engine\build\sym#sym-linux-x64
```

---

## After the release

User-facing download URLs (auto-assigned by GitHub):
```
https://github.com/YOUR_USERNAME/symmetra/releases/download/v1.0.0/sym-windows-x64.exe
https://github.com/YOUR_USERNAME/symmetra/releases/download/v1.0.0/sym-linux-x64
```

The install scripts already point to these URLs (after you set your username in step 4).
