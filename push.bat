@echo off
cd /d "%~dp0"
git config user.email "deployment@symmetra.dev"
git config user.name "Symmetra Deployment"
git add .
git commit -m "Add GitHub Actions release workflow and installation scripts"
git push origin main
echo Push complete!
