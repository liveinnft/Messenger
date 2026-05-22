#!/bin/bash
set -e

echo "================================================"
echo "  Messenger Server (foreground mode)"
echo "================================================"

# Остановка старого сервера
echo "Cleaning up old processes..."
pkill -f "./Server" 2>/dev/null || true
sudo fuser -k 8888/tcp 2>/dev/null || true
sleep 1

# Пересборка
echo "Building server..."
cd ~/Messenger/Server
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)

echo "Starting server (Ctrl+C to stop)..."
echo "================================================"
./Server