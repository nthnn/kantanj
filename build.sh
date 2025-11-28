#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_FILE="$SCRIPT_DIR/kantanj.c"
OUTPUT_BINARY="$SCRIPT_DIR/kantanj"

if [[ ! -f "$SOURCE_FILE" ]]; then
    echo "Error: kantanj.c not found in $SCRIPT_DIR"
    exit 1
fi

echo "Building kantanj..."
gcc -O0 -o "$OUTPUT_BINARY" "$SOURCE_FILE"

if [[ ! -f "$OUTPUT_BINARY" ]]; then
    echo "Build failed: kantanj was not created."
    exit 1
fi

echo "Build successful."
echo "Installing kantanj into /usr/local/bin..."

sudo install -m 0755 "$OUTPUT_BINARY" /usr/local/bin/kantanj

echo "Installation complete!"
echo "You can now run:"
echo "    kantanj --help  (or any command you implemented)"
