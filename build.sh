#!/usr/bin/env bash
set -euo pipefail

# Build & install kantanj (Bash version)
REPO_URL="https://github.com/nthnn/kantanj"
WORKDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLONE_DIR="$WORKDIR/"
SOURCE_FILE_NAME="kantanj.c"
OUTPUT_BINARY_NAME="kantanj"
DEST="/usr/local/bin/$OUTPUT_BINARY_NAME"

echo "Using workdir: $WORKDIR"
echo "Cloning (or updating) $REPO_URL -> $CLONE_DIR"

if [[ -d "$CLONE_DIR/.git" ]]; then
  printf "Repo exists, updating...\n"
  (cd "$CLONE_DIR" && git fetch --depth=1 origin && git reset --hard origin/HEAD)
else
  git clone --depth 1 "$REPO_URL" "$CLONE_DIR"
fi

SRC_PATH="$CLONE_DIR/$SOURCE_FILE_NAME"
BUILD_PATH="$WORKDIR/$OUTPUT_BINARY_NAME"

if [[ ! -f "$SRC_PATH" ]]; then
  echo "Error: $SOURCE_FILE_NAME not found in $CLONE_DIR"
  exit 1
fi

echo "Building kantanj from $SRC_PATH..."
gcc -O2 -std=c11 -Wall -Wextra -o "$BUILD_PATH" "$SRC_PATH"

if [[ ! -f "$BUILD_PATH" ]]; then
  echo "Build failed: $BUILD_PATH not created."
  exit 1
fi

echo "Build successful. Installing to $DEST (requires sudo)..."
sudo install -m 0755 "$BUILD_PATH" "$DEST"

echo "Installation complete: $DEST"
echo "Run: $DEST --help  (or just 'kantanj' if /usr/local/bin is in PATH)"
