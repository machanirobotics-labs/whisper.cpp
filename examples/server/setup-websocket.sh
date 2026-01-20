#!/bin/bash

# Setup script for WebSocket streaming server
# This downloads uWebSockets and uSockets if not present

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
DEPS_DIR="$SCRIPT_DIR/deps"

echo "Setting up WebSocket dependencies..."

# Create deps directory
mkdir -p "$DEPS_DIR"
cd "$DEPS_DIR"

# Clone uWebSockets if not present
if [ ! -d "uWebSockets" ]; then
    echo "Downloading uWebSockets..."
    git clone https://github.com/uNetworking/uWebSockets.git
    echo "✓ uWebSockets downloaded"
else
    echo "✓ uWebSockets already present"
fi

# Clone uSockets if not present
if [ ! -d "uSockets" ]; then
    echo "Downloading uSockets..."
    git clone https://github.com/uNetworking/uSockets.git
    echo "✓ uSockets downloaded"
else
    echo "✓ uSockets already present"
fi

echo ""
echo "=================================================="
echo "Setup complete!"
echo "=================================================="
echo ""
echo "Now you can build with:"
echo "  cd ../../build"
echo "  cmake .."
echo "  make whisper-websocket-server"
echo ""
