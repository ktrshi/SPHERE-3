#!/usr/bin/env bash
set -euo pipefail

# run_one.sh — process a single phels directory with SPHERE-3
# Usage: run_one.sh <phels_dir> <moshits_root> <sphere_bin> <logs_dir> <progress_tsv>

PHELS_DIR="$1"
MOSHITS_ROOT="$2"
SPHERE_BIN="$3"
LOGS_DIR="$4"
PROGRESS_TSV="$5"

# Derive relative path (e.g. 0014/5PeV/05/500)
# PHELS_ROOT is parent 4 levels up from leaf dir
PHELS_ROOT="$(cd "$PHELS_DIR/../../../.." && pwd)"
REL_PATH="${PHELS_DIR#$PHELS_ROOT/}"

# Output directory mirrors input hierarchy
MOSHITS_DIR="${MOSHITS_ROOT}/${REL_PATH}"

# Log file uses __ as separator
LOG_FILE="${LOGS_DIR}/${REL_PATH//\//__}.log"

# Lockfile for thread-safe TSV updates
LOCK_FILE="${PROGRESS_TSV}.lock"

update_progress() {
    local status="$1"
    local end_time="${2:-}"
    local exit_code="${3:-}"
    (
        flock 200
        # Remove existing entry for this directory
        if [ -f "$PROGRESS_TSV" ]; then
            awk -F'\t' -v dir="$REL_PATH" '$1 != dir' "$PROGRESS_TSV" > "${PROGRESS_TSV}.tmp" || true
            mv "${PROGRESS_TSV}.tmp" "$PROGRESS_TSV"
        fi
        # Append new entry
        printf '%s\t%s\t%s\t%s\t%s\n' \
            "$REL_PATH" "$status" "$START_TIME" "$end_time" "$exit_code" \
            >> "$PROGRESS_TSV"
    ) 200>"$LOCK_FILE"
}

# Record start
START_TIME="$(date -Iseconds)"
update_progress "running"

# Create output directory
mkdir -p "$MOSHITS_DIR"
mkdir -p "$(dirname "$LOG_FILE")"

# Run SPHERE-3
EXIT_CODE=0
"$SPHERE_BIN" --phels "$PHELS_DIR" --moshits "$MOSHITS_DIR" --threads 1 \
    > "$LOG_FILE" 2>&1 || EXIT_CODE=$?

# Record completion
END_TIME="$(date -Iseconds)"
if [ "$EXIT_CODE" -eq 0 ]; then
    update_progress "completed" "$END_TIME" "0"
else
    update_progress "failed" "$END_TIME" "$EXIT_CODE"
fi

exit "$EXIT_CODE"
