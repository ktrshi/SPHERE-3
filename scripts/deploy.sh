#!/bin/bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <user@host> [--build-only]"
    exit 1
fi

SERVER="$1"
BUILD_ONLY="${2:-}"
REMOTE_DIR="/home/ivanov/SPHERE-3_G4"

if [ "$BUILD_ONLY" != "--build-only" ]; then
    echo "==> Syncing code to $SERVER:$REMOTE_DIR ..."
    rsync -avz --exclude build/ --exclude '.git/' --exclude '*.o' \
        --exclude '.venv/' --exclude '.cache/' --exclude '.idea/' \
        --exclude '.serena/' --exclude '.vscode/' --exclude '.zed/' \
        --exclude '.claude/' --exclude 'docs/' --exclude '.DS_Store' \
        --exclude 'moshits_*.zip' --exclude '__pycache__/' \
        "$(dirname "$0")/../" "$SERVER:$REMOTE_DIR/"
fi

echo "==> Building on $SERVER ..."
ssh "$SERVER" "cd $REMOTE_DIR && mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j\$(nproc)"

echo "==> Verifying binary ..."
ssh "$SERVER" "$REMOTE_DIR/build/SPHERE-3 --help 2>&1 || true"

echo "==> Done. To run:"
echo "   ssh $SERVER"
echo "   tmux new -s sphere"
echo "   python3 $REMOTE_DIR/scripts/run_batch.py --phels-root ~/sphall/phels --moshits-root ~/sphall/moshits --sphere-bin $REMOTE_DIR/build/SPHERE-3 --max-jobs 30"
