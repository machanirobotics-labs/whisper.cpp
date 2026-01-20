# Building WebSocket Server on Linux

## Prerequisites

Install required dependencies:

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    pkg-config

# Fedora/RHEL/CentOS
sudo dnf install -y \
    gcc \
    gcc-c++ \
    cmake \
    git \
    openssl-devel

# Arch Linux
sudo pacman -S \
    base-devel \
    cmake \
    git \
    openssl
```

## Build Steps

```bash
# 1. Download WebSocket dependencies
cd examples/server
./setup-websocket.sh

# 2. Build
cd ../../
mkdir -p build
cd build
cmake ..
make whisper-websocket-server -j$(nproc)

# 3. Run
./bin/whisper-websocket-server --model ../models/ggml-base.en.bin --port 8081
```

## GPU Support on Linux

### CUDA (NVIDIA)

```bash
# Build with CUDA support
cd build
cmake .. -DGGML_CUDA=ON
make whisper-websocket-server -j$(nproc)
```

### ROCm (AMD)

```bash
# Build with ROCm support
cd build
cmake .. -DGGML_HIPBLAS=ON
make whisper-websocket-server -j$(nproc)
```

### Vulkan

```bash
# Install Vulkan SDK first
sudo apt-get install vulkan-tools libvulkan-dev

# Build with Vulkan
cd build
cmake .. -DGGML_VULKAN=ON
make whisper-websocket-server -j$(nproc)
```

## Troubleshooting

### OpenSSL not found

```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# Fedora/RHEL
sudo dnf install openssl-devel
```

### Permission denied on setup-websocket.sh

```bash
chmod +x examples/server/setup-websocket.sh
```

### Missing pthread or dl libraries

These should be automatically linked on Linux. If you get errors, ensure you have:
```bash
sudo apt-get install build-essential
```

## Running as a Service (systemd)

Create `/etc/systemd/system/whisper-websocket.service`:

```ini
[Unit]
Description=Whisper WebSocket Streaming Server
After=network.target

[Service]
Type=simple
User=whisper
WorkingDirectory=/path/to/whisper.cpp/build
ExecStart=/path/to/whisper.cpp/build/bin/whisper-websocket-server \
    --model /path/to/models/ggml-base.en.bin \
    --host 0.0.0.0 \
    --port 8081
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable whisper-websocket
sudo systemctl start whisper-websocket
sudo systemctl status whisper-websocket
```

## Firewall Configuration

Allow WebSocket port:
```bash
# UFW (Ubuntu)
sudo ufw allow 8081/tcp

# firewalld (Fedora/RHEL)
sudo firewall-cmd --permanent --add-port=8081/tcp
sudo firewall-cmd --reload

# iptables
sudo iptables -A INPUT -p tcp --dport 8081 -j ACCEPT
```

## Performance Tuning

For production deployments:

```bash
# Increase file descriptor limits
ulimit -n 65536

# Run with nice priority
nice -n -10 ./bin/whisper-websocket-server --model models/ggml-base.en.bin
```
