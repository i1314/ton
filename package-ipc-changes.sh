#!/bin/bash
set -e

echo "Packaging IPC changes..."

# Create a patch file with all IPC-related changes
git diff master..HEAD > ipc-changes.patch

# Create a tarball with the new files and patches
tar czf ton-ipc-package.tar.gz \
    ipc-changes.patch \
    validator/ipc-publisher.cpp \
    validator/ipc-publisher.hpp \
    validator/IPC-README.md \
    validator/ipc-client-example.py \
    CMakeLists.txt

echo "Package created: ton-ipc-package.tar.gz"
echo ""
echo "To apply on Ubuntu:"
echo "1. Copy ton-ipc-package.tar.gz to your Ubuntu machine"
echo "2. Extract: tar xzf ton-ipc-package.tar.gz"
echo "3. Apply patch: git apply ipc-changes.patch"
echo "4. Build with: cmake .. -DTON_IPC_ENABLED=ON"