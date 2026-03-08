#!/usr/bin/env bash
set -euo pipefail

# run_all.sh — orchestrate parallel SPHERE-3 processing of all phels directories
# Usage: run_all.sh [--dry-run]
#
# Configuration: edit variables below or override via environment:
#   PHELS_ROOT=/path MOSHITS_ROOT=/path SPHERE_BIN=/path MAX_JOBS=30 ./run_all.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Configuration (override via environment) ---
PHELS_ROOT="${PHELS_ROOT:-/home/ivanov/sphall/phels}"
MOSHITS_ROOT="${MOSHITS_ROOT:-/home/ivanov/sphall/moshits}"
SPHERE_BIN="$(realpath "${SPHERE_BIN:-${SCRIPT_DIR}/../build/SPHERE-3}")"
MAX_JOBS="${MAX_JOBS:-30}"
WORK_DIR="${WORK_DIR:-/home/ivanov/sphall}"
PROGRESS_TSV="${WORK_DIR}/progress.tsv"
LOGS_DIR="${WORK_DIR}/logs"

DRY_RUN=false
if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN=true
fi

# --- Validation ---
if [ ! -x "$SPHERE_BIN" ]; then
    echo "ERROR: SPHERE-3 binary not found or not executable: $SPHERE_BIN" >&2
    exit 1
fi

if [ ! -d "$PHELS_ROOT" ]; then
    echo "ERROR: phels root directory not found: $PHELS_ROOT" >&2
    exit 1
fi

if ! command -v parallel &>/dev/null; then
    echo "ERROR: GNU parallel not found. Install with: sudo apt install parallel" >&2
    exit 1
fi

# --- Setup ---
mkdir -p "$MOSHITS_ROOT" "$LOGS_DIR"

# Initialize progress file with header if it doesn't exist
if [ ! -f "$PROGRESS_TSV" ]; then
    printf 'directory\tstatus\tstart_time\tend_time\texit_code\n' > "$PROGRESS_TSV"
fi

# --- Find directories to process ---
# Find all leaf directories at depth 4 (particle/energy/zenith/altitude)
ALL_DIRS=$(find "$PHELS_ROOT" -mindepth 4 -maxdepth 4 -type d | sort)
TOTAL=$(echo "$ALL_DIRS" | grep -c . || echo 0)

# Filter out completed directories
COMPLETED=$(grep "	completed	" "$PROGRESS_TSV" 2>/dev/null | cut -f1 || true)

DIRS_TO_PROCESS=""
SKIPPED=0
while IFS= read -r dir; do
    REL_PATH="${dir#$PHELS_ROOT/}"
    if echo "$COMPLETED" | grep -qx "$REL_PATH"; then
        ((SKIPPED++)) || true
    else
        DIRS_TO_PROCESS="${DIRS_TO_PROCESS}${dir}"$'\n'
    fi
done <<< "$ALL_DIRS"

# Remove trailing newline
DIRS_TO_PROCESS=$(echo "$DIRS_TO_PROCESS" | sed '/^$/d')
TO_RUN=$(echo "$DIRS_TO_PROCESS" | grep -c . || echo 0)

echo "=== SPHERE-3 Batch Processing ==="
echo "Total directories:     $TOTAL"
echo "Already completed:     $SKIPPED"
echo "To process:            $TO_RUN"
echo "Parallel jobs:         $MAX_JOBS"
echo "Binary:                $SPHERE_BIN"
echo "================================="

if [ "$TO_RUN" -eq 0 ]; then
    echo "Nothing to process. All directories completed."
    exit 0
fi

if [ "$DRY_RUN" = true ]; then
    echo ""
    echo "Directories to process:"
    echo "$DIRS_TO_PROCESS"
    echo ""
    echo "(dry run — no jobs started)"
    exit 0
fi

# --- Run ---
echo ""
echo "Starting processing at $(date -Iseconds)..."
PARALLEL_EXIT=0
echo "$DIRS_TO_PROCESS" | parallel -j "$MAX_JOBS" --halt soon,fail=5 \
    "$SCRIPT_DIR/run_one.sh" {} "$MOSHITS_ROOT" "$SPHERE_BIN" "$LOGS_DIR" "$PROGRESS_TSV" \
    || PARALLEL_EXIT=$?

# --- Summary ---
echo ""
echo "=== Completed at $(date -Iseconds) ==="
DONE=$(grep -c "	completed	" "$PROGRESS_TSV" 2>/dev/null || echo 0)
FAILED=$(grep -c "	failed	" "$PROGRESS_TSV" 2>/dev/null || echo 0)
echo "Completed: $DONE"
echo "Failed:    $FAILED"
if [ "$FAILED" -gt 0 ]; then
    echo ""
    echo "Failed directories:"
    grep "	failed	" "$PROGRESS_TSV" | cut -f1
    echo ""
    echo "Re-run this script to retry failed directories."
fi

exit "$PARALLEL_EXIT"
