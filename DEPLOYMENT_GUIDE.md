# Symmetra Production Deployment Guide

**Version**: 1.0  
**Last Updated**: 2026-05-25  
**Status**: Ready for Production

Complete step-by-step guide for deploying Symmetra to production environments.

---

## Table of Contents

1. [Pre-Deployment Checklist](#pre-deployment-checklist)
2. [Environment Setup](#environment-setup)
3. [Installation](#installation)
4. [Configuration](#configuration)
5. [Testing](#testing)
6. [Deployment](#deployment)
7. [Verification](#verification)
8. [Monitoring](#monitoring)
9. [Scaling](#scaling)
10. [Troubleshooting](#troubleshooting)

---

## Pre-Deployment Checklist

### Hardware Requirements

- [ ] **GPU**: NVIDIA RTX series or better (sm_75+)
  - RTX 2070, 2080, 3080, 4080, 4090, etc.
  - Check: `nvidia-smi`

- [ ] **VRAM**: Minimum 8GB (16GB+ recommended)
  - For 3M RPS: ~100MB needed
  - For scaling: Plan 100MB per instance

- [ ] **CPU**: 8+ cores (16+ cores recommended)
  - 1 core per CUDA stream
  - Default 32 streams = 32 CPU threads

- [ ] **Memory**: 16GB+ RAM
  - Pinned buffers: ~64MB
  - Working space: ~256MB
  - Headroom: 50%

- [ ] **Network**: 10Gbps+ recommended
  - For 1M RPS: ~8Gbps sustained
  - For 3M RPS: ~24Gbps sustained

### Software Requirements

```bash
# Windows
- [ ] CUDA Toolkit 13.1+  (check: nvcc --version)
- [ ] NVIDIA Driver 13.1+  (check: nvidia-smi)
- [ ] Visual C++ 2022+     (check: cl.exe)
- [ ] CMake 3.20+          (check: cmake --version)

# Linux
- [ ] CUDA Toolkit 13.1+
- [ ] NVIDIA Driver 13.1+
- [ ] GCC 11+ or Clang 14+
- [ ] CMake 3.20+
```

### Pre-Deployment Validation

```bash
# Verify CUDA installation
nvcc --version
# Expected: Cuda compilation tools, release 13.1 or higher

# Verify GPU availability
nvidia-smi
# Expected: GPU 0: <your GPU model> with memory

# Verify CUDA is accessible to your compiler
cmake --system-information | grep CUDA
# Expected: CUDA_TOOLKIT_ROOT_DIR found
```

---

## Environment Setup

### Step 1: Create Deployment User

```bash
# Linux
sudo useradd -m -s /bin/bash symmetra
sudo usermod -aG docker symmetra  # if using Docker

# Windows (PowerShell, Admin)
$User = "symmetra"
New-LocalUser -Name $User -Password (ConvertTo-SecureString "YourPassword" -AsPlainText) -FullName "Symmetra Service"
Add-LocalGroupMember -Group "Administrators" -Member $User
```

### Step 2: Create Application Directory

```bash
# Linux/Windows both
mkdir -p /opt/symmetra/bin
mkdir -p /opt/symmetra/config
mkdir -p /opt/symmetra/logs
mkdir -p /opt/symmetra/data

# Set permissions (Linux)
sudo chown -R symmetra:symmetra /opt/symmetra
sudo chmod -R 755 /opt/symmetra
```

### Step 3: Install CUDA (if not already installed)

```bash
# Ubuntu 22.04
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-ubuntu2204.pin
sudo mv cuda-ubuntu2204.pin /etc/apt/preferences.d/cuda-repository-pin-600
wget https://developer.download.nvidia.com/compute/cuda/13.2.0/local_installers/cuda-repo-ubuntu2204-13-2-local_13.2.0-535.104.05-1_amd64.deb
sudo dpkg -i cuda-repo-ubuntu2204-13-2-local_13.2.0-535.104.05-1_amd64.deb
sudo apt-get update
sudo apt-get install cuda-toolkit-13-2

# Windows (Download and run installer)
# https://developer.nvidia.com/cuda-downloads
```

### Step 4: Set Environment Variables

```bash
# Linux (~/.bashrc or /etc/profile.d/cuda.sh)
export PATH=/usr/local/cuda-13.2/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda-13.2/lib64:$LD_LIBRARY_PATH
export CUDA_HOME=/usr/local/cuda-13.2

# Windows (System Environment Variables)
CUDA_PATH = C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2
PATH += C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\bin
```

---

## Installation

### Step 1: Download Binary

```bash
# Option A: Download pre-built binary
wget https://releases.symmetra.dev/symmetra-1.0-linux-x64.tar.gz
tar -xzf symmetra-1.0-linux-x64.tar.gz -C /opt/symmetra/bin

# Option B: Build from source
git clone https://github.com/yourorg/symmetra.git
cd symmetra/engine
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release --parallel 8
cp Release/sym /opt/symmetra/bin/
```

### Step 2: Verify Installation

```bash
/opt/symmetra/bin/sym --version
# Expected: Symmetra 1.0.0  ·  CUDA 13.2  ·  Runtime ready

/opt/symmetra/bin/sym doctor
# Expected: All checks passing
```

### Step 3: Copy Configuration Template

```bash
cp EXAMPLES.md /opt/symmetra/
cp QUICK_START.md /opt/symmetra/
```

---

## Configuration

### Configuration File: `/opt/symmetra/config/server.conf`

```yaml
# Symmetra Server Configuration

# Port Configuration
server:
  port: 8080
  
  # CUDA Configuration
  cuda:
    device: 0                    # GPU device ID (0-indexed)
    streams: 32                  # CUDA stream pool size
    batch_mode: true            # Enable warp-aligned batching
  
  # Thread Configuration
  io_threads: 32                # HTTP thread pool (match streams)
  io_thread_affinity: true      # Pin threads to cores
  
  # Performance
  verbose: false                # Disable logging in production
  use_batch: true               # Critical for throughput
  
  # TLS Configuration (optional)
  tls:
    enabled: false
    psk: ""                     # Pre-shared key (hex, 32+ bytes)
    # Example: "0123456789ABCDEF0123456789ABCDEF"

# Logging Configuration
logging:
  level: "INFO"                 # DEBUG, INFO, WARNING, ERROR
  file: "/opt/symmetra/logs/server.log"
  max_size: 100MB
  max_backups: 10
  max_age_days: 30
  compress: true

# Monitoring Configuration
monitoring:
  metrics_enabled: true
  metrics_port: 9090
  health_check_path: "/health"
  health_check_interval_ms: 5000

# Rate Limiting
rate_limit:
  enabled: true
  tokens_per_second: 100        # Per IP
  bucket_capacity: 1000         # Max accumulated tokens

# Resource Limits
resources:
  max_request_size: 65536       # 64KB max request body
  max_concurrent_requests: 256  # Per instance
  request_timeout_ms: 30000     # 30 second timeout

# Deployment
deployment:
  environment: "production"
  graceful_shutdown_timeout_ms: 30000
  enable_profiling: false
```

### Environment Variables

```bash
# Create /opt/symmetra/config/env.sh
export SYMMETRA_HOME=/opt/symmetra
export SYMMETRA_CONFIG=$SYMMETRA_HOME/config/server.conf
export SYMMETRA_LOG_DIR=$SYMMETRA_HOME/logs
export CUDA_DEVICE=0
export CUDA_VISIBLE_DEVICES=0
export LD_LIBRARY_PATH=/usr/local/cuda-13.2/lib64:$LD_LIBRARY_PATH
export SYMMETRA_VERBOSE=false
```

### Systemd Service (Linux)

```ini
# /etc/systemd/system/symmetra.service
[Unit]
Description=Symmetra GPU Compute Engine
After=network.target
Wants=network-online.target

[Service]
Type=simple
User=symmetra
WorkingDirectory=/opt/symmetra
EnvironmentFile=/opt/symmetra/config/env.sh
ExecStart=/opt/symmetra/bin/sym --config /opt/symmetra/config/server.conf
Restart=on-failure
RestartSec=5

# Resource Limits
LimitNOFILE=65536
LimitNPROC=65536

# GPU Access
DeviceAccess=/dev/nvidia*
DeviceAccess=/dev/nvidiactl

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=symmetra

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable symmetra
sudo systemctl start symmetra
sudo systemctl status symmetra
```

---

## Testing

### Step 1: Local Functionality Test

```bash
# Start server
/opt/symmetra/bin/sym --config /opt/symmetra/config/server.conf

# In another terminal
# Test GET
curl -X GET http://localhost:8080/health
# Expected: 200 OK

# Test POST
curl -X POST http://localhost:8080/api/test \
  -H "Content-Type: application/json" \
  -d '{"data": [1, 2, 3]}'
# Expected: 200 OK with response
```

### Step 2: Load Test

```bash
# Using Apache Bench
ab -n 10000 -c 32 http://localhost:8080/endpoint
# Expected: 
#   Requests per second: 85K+
#   Failed requests: 0
#   Latency: <50ms avg

# Using wrk (better for concurrent load)
wrk -t12 -c400 -d30s http://localhost:8080/endpoint
# Expected:
#   Requests/sec: 100K+
#   Latency p99: <100μs
#   Errors: 0%
```

### Step 3: Performance Validation

```bash
# Run built-in performance test
/opt/symmetra/bin/performance_test.exe

# Expected output:
# Max Concurrency:    87K+ RPS
# Sustained Load:     3M+ RPS
# Memory Stress:      90K+ RPS
# Latency Tail:       <65μs P99
# Burst Traffic:      12M+ RPS
```

---

## Deployment

### Single Instance Deployment

```bash
# 1. Create deployment package
cd /opt/symmetra
tar -czf symmetra-deployment-$(date +%Y%m%d).tar.gz \
  bin/ config/ logs/

# 2. Copy to server
scp symmetra-deployment-*.tar.gz symmetra@prod-server:/opt/

# 3. Extract and verify
ssh symmetra@prod-server
tar -xzf /opt/symmetra-deployment-*.tar.gz -C /opt/symmetra
/opt/symmetra/bin/sym --version

# 4. Start service
sudo systemctl start symmetra
sudo systemctl status symmetra
```

### Multi-Instance Deployment (Kubernetes)

```yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: symmetra
  namespace: production
spec:
  serviceName: symmetra
  replicas: 4
  selector:
    matchLabels:
      app: symmetra
  template:
    metadata:
      labels:
        app: symmetra
    spec:
      # GPU scheduling
      nodeSelector:
        accelerator: nvidia-gpu
      
      containers:
      - name: symmetra
        image: myregistry/symmetra:1.0
        imagePullPolicy: IfNotPresent
        
        ports:
        - containerPort: 8080
          name: http
        - containerPort: 9090
          name: metrics
        
        env:
        - name: CUDA_VISIBLE_DEVICES
          value: "0"
        - name: SYMMETRA_VERBOSE
          value: "false"
        
        resources:
          requests:
            nvidia.com/gpu: 1
            memory: "2Gi"
            cpu: "8"
          limits:
            nvidia.com/gpu: 1
            memory: "4Gi"
            cpu: "16"
        
        # Health checks
        livenessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 10
          periodSeconds: 5
          failureThreshold: 3
        
        readinessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 5
          periodSeconds: 5
        
        # Graceful shutdown
        lifecycle:
          preStop:
            exec:
              command: ["/bin/sh", "-c", "sleep 15"]
        
        volumeMounts:
        - name: config
          mountPath: /opt/symmetra/config
        - name: logs
          mountPath: /opt/symmetra/logs
      
      volumes:
      - name: config
        configMap:
          name: symmetra-config
      - name: logs
        emptyDir: {}

---
apiVersion: v1
kind: Service
metadata:
  name: symmetra-service
  namespace: production
spec:
  type: LoadBalancer
  selector:
    app: symmetra
  ports:
  - port: 8080
    targetPort: 8080
    protocol: TCP
    name: http
  - port: 9090
    targetPort: 9090
    protocol: TCP
    name: metrics
```

Deploy:
```bash
kubectl apply -f symmetra-deployment.yaml
kubectl get pods -n production
kubectl describe pod symmetra-0 -n production
```

---

## Verification

### Post-Deployment Health Check

```bash
#!/bin/bash
# /opt/symmetra/scripts/health-check.sh

echo "Checking Symmetra deployment..."

# 1. Service running
systemctl is-active symmetra && echo "✓ Service running" || echo "✗ Service stopped"

# 2. GPU accessible
nvidia-smi -L | grep -q "GPU 0" && echo "✓ GPU accessible" || echo "✗ GPU not found"

# 3. HTTP endpoint
curl -s http://localhost:8080/health | grep -q "200" && echo "✓ HTTP endpoint alive" || echo "✗ HTTP endpoint down"

# 4. Performance baseline
echo "Running performance baseline..."
/opt/symmetra/bin/performance_test.exe | tail -5

echo "✓ Deployment verification complete"
```

Run verification:
```bash
chmod +x /opt/symmetra/scripts/health-check.sh
/opt/symmetra/scripts/health-check.sh
```

---

## Monitoring

### Metrics to Collect

```yaml
Key Metrics:
  - Throughput (RPS)
    Target: >1M RPS
    Alert: <500K RPS
  
  - Latency P99
    Target: <100μs
    Alert: >500μs
  
  - Error Rate
    Target: <0.1%
    Alert: >1%
  
  - GPU Memory
    Target: <80% utilized
    Alert: >95% utilized
  
  - CPU Usage
    Target: <50%
    Alert: >80%
```

### Prometheus Integration

```yaml
# /opt/symmetra/config/prometheus.yml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'symmetra'
    static_configs:
      - targets: ['localhost:9090']
```

### Alerting Rules

```yaml
# /opt/symmetra/config/alerts.yml
groups:
  - name: symmetra
    rules:
      - alert: HighLatency
        expr: symmetra_latency_p99_us > 500
        for: 5m
        annotations:
          summary: "High P99 latency detected"
      
      - alert: HighErrorRate
        expr: symmetra_error_rate > 0.01
        for: 5m
        annotations:
          summary: "Error rate exceeds 1%"
      
      - alert: LowThroughput
        expr: symmetra_throughput_rps < 500000
        for: 5m
        annotations:
          summary: "Throughput below target"
```

---

## Scaling

### Horizontal Scaling

```bash
# When single instance approaching capacity:
# 1. Launch another instance on different GPU
# 2. Add to load balancer
# 3. Monitor latency and throughput

# Scaling formula:
# - 1 GPU instance = up to 3M RPS
# - 2 GPU instances = up to 6M RPS (with load balancing)
# - N instances = N × 3M RPS (with proper load distribution)
```

### Vertical Scaling

```bash
# Increase per-instance capacity:
# 1. Increase CUDA streams: opts.streams = 64 (instead of 32)
# 2. Increase IO threads: opts.ioThreads = 64
# 3. Allocate more VRAM: larger GPU (RTX 4090 > RTX 4080)

# Performance formula:
# - 32 streams = 87K RPS single-stream, 3M RPS batch
# - 64 streams = 150K+ RPS single-stream, 6M+ RPS batch
```

---

## Troubleshooting

### Common Issues

| Problem | Symptom | Solution |
|---------|---------|----------|
| **GPU not detected** | "CUDA: invalid device" | Check `nvidia-smi`, verify CUDA path |
| **High latency variance** | P99 >> P50 | Reduce stream count (32 → 8) |
| **Memory exhaustion** | "CUDA malloc failed" | Reduce concurrent requests, scale out |
| **Low throughput** | <500K RPS | Enable batch mode, increase streams |
| **High error rate** | Error rate >1% | Check logs, verify input validation |

### Debug Mode

```bash
# Enable verbose logging
export SYMMETRA_VERBOSE=true
/opt/symmetra/bin/sym --config /opt/symmetra/config/server.conf

# Collect logs
tail -f /opt/symmetra/logs/server.log
```

---

## Rollback Procedure

```bash
# If new version fails

# 1. Stop current version
sudo systemctl stop symmetra

# 2. Revert to previous binary
cp /opt/symmetra/bin/sym.1.0.1 /opt/symmetra/bin/sym

# 3. Start previous version
sudo systemctl start symmetra

# 4. Verify
curl http://localhost:8080/health

# 5. Investigate failure
grep -i error /opt/symmetra/logs/server.log
```

---

## Support

- **Documentation**: `/opt/symmetra/QUICK_START.md`
- **Examples**: `/opt/symmetra/EXAMPLES.md`
- **Logs**: `/opt/symmetra/logs/server.log`
- **Health Check**: `curl http://localhost:8080/health`

---

**Version**: 1.0  
**Status**: Production Ready  
**Last Updated**: 2026-05-25
