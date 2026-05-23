#!/bin/bash
set -e



echo "stop old"
pkill -f "./Server" 2>/dev/null || true
sudo fuser -k 8888/tcp 2>/dev/null || true
sleep 1

# Пересборка
echo "Building"
cd ~/Messenger/Server
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)

echo "Starting server (Ctrl+C to stop)..."
./Server
