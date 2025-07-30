#!/bin/bash
set -e

echo "Building TON validator-engine for Ubuntu 22.04..."

# Build the Docker image
docker build -f Dockerfile.ubuntu-ipc -t ton-ipc-ubuntu:latest .

# Extract the binary from the container
docker create --name temp-container ton-ipc-ubuntu:latest
docker cp temp-container:/usr/local/bin/validator-engine ./validator-engine-ubuntu
docker rm temp-container

echo "Build complete! Binary saved as: validator-engine-ubuntu"
echo "File info:"
file ./validator-engine-ubuntu
ls -lah ./validator-engine-ubuntu