#!/usr/bin/env bash
set -euo pipefail

REPO_URL="https://github.com/nthnn/kantanj"
INSTALL_DIR="/usr/local/bin"
TEMP_DIR="$(mktemp -d)"
OUTPUT_BINARY="kantanj"

echo "==> Temporary directory: $TEMP_DIR"
echo "==> Cloning repository..."
git clone --depth 1 "$REPO_URL" "$TEMP_DIR/repo"

SRC_FILE="$TEMP_DIR/repo/kantanj.c"
BUILD_PATH="$TEMP_DIR/$OUTPUT_BINARY"

if [[ ! -f "$SRC_FILE" ]]; then
    echo "ERROR: kantanj.c not found in repository!"
    exit 1
fi

echo "==> Building kantanj..."
gcc -O0 -std=c11 -Wall -Wextra -o "$BUILD_PATH" "$SRC_FILE" >/dev/null 2>&1

if [[ ! -f "$BUILD_PATH" ]]; then
    echo "ERROR: Build failed — binary not created."
    exit 1
fi

echo "==> Installing to $INSTALL_DIR (requires sudo)..."
sudo install -m 0755 "$BUILD_PATH" "$INSTALL_DIR/$OUTPUT_BINARY"

echo "==> Cleaning up..."
rm -rf "$TEMP_DIR"

echo "==> Installation complete!"
echo ""
echo "Run: kantanj --help"
