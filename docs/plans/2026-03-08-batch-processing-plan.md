# Batch Processing Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable parallel batch processing of 108 photon directories on a remote 32-thread server with progress tracking and restart capability.

**Architecture:** Minimal C++ CLI changes (add `--phels`, `--moshits`, `--threads` args) + bash orchestrator using GNU parallel to run up to 30 single-threaded SPHERE-3 instances. Progress tracked in TSV journal.

**Tech Stack:** C++ (Geant4), Bash, GNU parallel, fd

---

### Task 1: Add CLI argument parsing to SPHERE-3.cpp

**Files:**
- Modify: `SPHERE-3.cpp:20-55`

**Step 1: Add CLI argument parsing**

Replace the current argument parsing block (lines 22-30 and config setup 50-56) with a version that supports `--phels`, `--moshits`, `--threads`, while preserving `--vis` and the positional path argument.

Current parsing (lines 22-30):
```cpp
bool visMode = false;
std::string currentPath;
for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--vis") {
        visMode = true;
    } else if (currentPath.empty()) {
        currentPath = argv[i];
    }
}
```

New parsing:
```cpp
bool visMode = false;
std::string currentPath;
std::string cliPhelsDir;
std::string cliMoshitsDir;
std::string cliThreads;
for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--vis") {
        visMode = true;
    } else if (arg == "--phels" && i + 1 < argc) {
        cliPhelsDir = argv[++i];
    } else if (arg == "--moshits" && i + 1 < argc) {
        cliMoshitsDir = argv[++i];
    } else if (arg == "--threads" && i + 1 < argc) {
        cliThreads = argv[++i];
    } else if (currentPath.empty()) {
        currentPath = argv[i];
    }
}
```

Current config setup (lines 50-56):
```cpp
auto* config = new SimConfig();
config->phi = 0.0 * deg;
config->the = 0.0 * deg;
config->p1 = 1.093;
config->currentPath = currentPath;
config->phelsDir = currentPath + "/phels";
config->outputDir = currentPath + "/moshits";
```

New config setup:
```cpp
auto* config = new SimConfig();
config->phi = 0.0 * deg;
config->the = 0.0 * deg;
config->p1 = 1.093;
config->currentPath = currentPath;
config->phelsDir = cliPhelsDir.empty() ? currentPath + "/phels" : cliPhelsDir;
config->outputDir = cliMoshitsDir.empty() ? currentPath + "/moshits" : cliMoshitsDir;
```

Current thread config (lines 75-81) reads from ini only:
```cpp
if (!threadsStr.empty()) {
    const G4int nThreads = std::stoi(threadsStr);
```

Replace with CLI override:
```cpp
const std::string finalThreads = cliThreads.empty() ? threadsStr : cliThreads;
if (!finalThreads.empty()) {
    const G4int nThreads = std::stoi(finalThreads);
```

**Step 2: Build and verify**

Run:
```bash
cd build && cmake --build .
```
Expected: compiles without errors.

**Step 3: Test backward compatibility**

Run:
```bash
./SPHERE-3 --vis
```
Expected: opens visualization mode (same as before).

**Step 4: Test new arguments**

Run:
```bash
./SPHERE-3 --phels /tmp/test_phels --moshits /tmp/test_moshits --threads 1
```
Expected: prints "Found 0 input files" (empty dir) or error about missing dir — confirms args are parsed.

**Step 5: Commit**

```bash
git add SPHERE-3.cpp
git commit -m "feat: add --phels, --moshits, --threads CLI arguments"
```

---

### Task 2: Create worker script (`scripts/run_one.sh`)

**Files:**
- Create: `scripts/run_one.sh`

**Step 1: Write the script**

```bash
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
            grep -v "^${REL_PATH}	" "$PROGRESS_TSV" > "${PROGRESS_TSV}.tmp" || true
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
```

**Step 2: Make executable**

```bash
chmod +x scripts/run_one.sh
```

**Step 3: Commit**

```bash
git add scripts/run_one.sh
git commit -m "feat: add run_one.sh worker script for single directory processing"
```

---

### Task 3: Create orchestrator script (`scripts/run_all.sh`)

**Files:**
- Create: `scripts/run_all.sh`

**Step 1: Write the script**

```bash
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
SPHERE_BIN="${SPHERE_BIN:-${SCRIPT_DIR}/../build/SPHERE-3}"
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
TOTAL=$(echo "$ALL_DIRS" | wc -l | tr -d ' ')

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
echo "$DIRS_TO_PROCESS" | parallel -j "$MAX_JOBS" --halt soon,fail=20% \
    "$SCRIPT_DIR/run_one.sh" {} "$MOSHITS_ROOT" "$SPHERE_BIN" "$LOGS_DIR" "$PROGRESS_TSV"

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
```

**Step 2: Make executable**

```bash
chmod +x scripts/run_all.sh
```

**Step 3: Test with --dry-run locally**

```bash
# Create a fake hierarchy to test
mkdir -p /tmp/test_phels/0014/5PeV/05/{500,700}
PHELS_ROOT=/tmp/test_phels WORK_DIR=/tmp/test_work scripts/run_all.sh --dry-run
```

Expected output:
```
=== SPHERE-3 Batch Processing ===
Total directories:     2
Already completed:     0
To process:            2
...
(dry run — no jobs started)
```

**Step 4: Commit**

```bash
git add scripts/run_all.sh
git commit -m "feat: add run_all.sh orchestrator with GNU parallel and progress tracking"
```

---

### Task 4: End-to-end local test

**Files:** none (testing only)

**Step 1: Create test fixture**

```bash
mkdir -p /tmp/sphere_test/phels/0014/5PeV/05/500
# Copy a few real phels files or create minimal test files
cp /path/to/some/phels/files /tmp/sphere_test/phels/0014/5PeV/05/500/
```

**Step 2: Run single directory**

```bash
scripts/run_one.sh \
    /tmp/sphere_test/phels/0014/5PeV/05/500 \
    /tmp/sphere_test/moshits \
    ./build/SPHERE-3 \
    /tmp/sphere_test/logs \
    /tmp/sphere_test/progress.tsv
```

Expected: `progress.tsv` has one `completed` entry, output files in `moshits/0014/5PeV/05/500/`.

**Step 3: Run orchestrator on test data**

```bash
PHELS_ROOT=/tmp/sphere_test/phels \
MOSHITS_ROOT=/tmp/sphere_test/moshits \
SPHERE_BIN=./build/SPHERE-3 \
WORK_DIR=/tmp/sphere_test \
MAX_JOBS=2 \
scripts/run_all.sh
```

Expected: processes test directory, progress.tsv updated.

**Step 4: Test restart (re-run)**

```bash
# Same command again — should skip completed
PHELS_ROOT=/tmp/sphere_test/phels \
MOSHITS_ROOT=/tmp/sphere_test/moshits \
SPHERE_BIN=./build/SPHERE-3 \
WORK_DIR=/tmp/sphere_test \
MAX_JOBS=2 \
scripts/run_all.sh
```

Expected: "Nothing to process. All directories completed."

---

### Task 5: Deploy to server

**Step 1: Push code to server**

```bash
rsync -avz --exclude build/ ./ ivanov@213.131.1.51:/home/ivanov/SPHERE-3_G4/
```

**Step 2: Build on server**

```bash
ssh ivanov@213.131.1.51
cd /home/ivanov/SPHERE-3_G4
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
```

**Step 3: Dry run on server**

```bash
cd /home/ivanov/sphall
SPHERE_BIN=/home/ivanov/SPHERE-3_G4/build/SPHERE-3 \
/home/ivanov/SPHERE-3_G4/scripts/run_all.sh --dry-run
```

Expected: lists all 108 directories, no jobs started.

**Step 4: Launch full processing**

```bash
# Run in tmux/screen to survive disconnection
tmux new -s sphere
cd /home/ivanov/sphall
SPHERE_BIN=/home/ivanov/SPHERE-3_G4/build/SPHERE-3 \
/home/ivanov/SPHERE-3_G4/scripts/run_all.sh
```

**Step 5: Monitor progress (in another terminal)**

```bash
ssh ivanov@213.131.1.51
# Count completed
grep -c "completed" /home/ivanov/sphall/progress.tsv
# Watch live
watch -n 10 'grep -c completed /home/ivanov/sphall/progress.tsv; echo "---"; tail -3 /home/ivanov/sphall/progress.tsv'
```
