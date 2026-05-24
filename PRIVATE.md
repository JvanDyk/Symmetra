# PRIVATE — DO NOT PUBLISH THIS DIRECTORY

This directory contains the full proprietary Symmetra engine source.
Publishing it would defeat all binary protections.

## Two-repo setup

### Private repo (this directory)
Keep locally or on a **private** GitHub repo.
Contains: engine source, compiler, runtime, all protection code.

### Public repo (the `public/` folder only)
The `public/` directory is a standalone self-contained repo.
It contains ONLY: website, SDK type definitions, examples, docs.
Push it to GitHub as a separate public repository.

```powershell
cd public
git init
git add .
git commit -m "Symmetra v1.0.0 — public release"
git remote add origin https://github.com/YOUR_USERNAME/symmetra.git
git push -u origin main
```

Then in GitHub repo settings → Pages → Deploy from branch `main` → folder `/` (root).
The site goes live at `https://YOUR_USERNAME.github.io/symmetra/`.

## What is safe to publish (public/)

| Path | Why it's safe |
|------|---------------|
| `public/index.html` | Website — no engine details |
| `public/style.css` | CSS only |
| `public/docs/` | User-facing docs, no internal names |
| `public/examples/` | TypeScript examples — no internal references |
| `public/packages/` | Type definitions only — no implementation |
| `public/README.md` | User-facing README |

## What must NEVER be public (engine/)

| Path | Why it's dangerous |
|------|-------------------|
| `engine/src/protect/AntiDebug.cpp` | Shows exact PEB offsets + timing thresholds |
| `engine/src/protect/Obfuscate.h` | Shows XOR key scheme |
| `engine/src/protect/PtxCrypt.h` | Shows AES key + IV |
| `engine/src/protect/KernelStore.cpp` | Shows decryption + load flow |
| `engine/src/compiler/Codegen.cpp` | Full proprietary compiler |
| `engine/src/runtime/GpuServer.cpp` | Full server implementation |
| `engine/src/kernels/genesis.cu` | Core CA algorithm |

## Binary distribution

The compiled `sym.exe` / `sym` binaries are uploaded as **GitHub Release assets**
on the public repo — they are NOT committed to the public repo's git history.

Users run the install script which downloads directly from the Release URL:
```
https://github.com/YOUR_USERNAME/symmetra/releases/download/v1.0.0/sym-windows-x64.exe
```

**Full publishing steps: see `RELEASE.md` in this directory.**
