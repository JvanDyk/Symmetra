#!/bin/bash
set -e

echo "=== Installing cmake and build tools ==="
sudo apt-get update -qq
sudo apt-get install -y -q cmake build-essential
echo "cmake: $(cmake --version | head -1)"
echo "g++: $(g++ --version | head -1)"

echo ""
echo "=== Adding CUDA 12.4 repo ==="
wget -q https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update -qq

echo ""
echo "=== Installing CUDA nvcc + nvrtc (build only, not full toolkit) ==="
sudo apt-get install -y -q cuda-nvcc-12-4 cuda-cudart-dev-12-4 cuda-nvrtc-dev-12-4

echo ""
echo "=== CUDA installed ==="
/usr/local/cuda-12.4/bin/nvcc --version || echo "nvcc not found at expected path"
ls /usr/local/cuda-12.4/bin/ | grep nvcc

echo ""
echo "=== Build complete ==="
