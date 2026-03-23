#!/usr/bin/env python3
"""Batch orchestrator for SPHERE-3_G4 simulation.

Replaces run_all.sh + run_one.sh. Finds leaf directories with .phel.zst files,
runs SPHERE-3 for each, tracks progress in progress.tsv.
"""

import argparse
import fcntl
import logging
import os
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger(__name__)

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_SPHERE_BIN = SCRIPT_DIR.parent / "build" / "SPHERE-3"


def find_phel_dirs(root: Path, particle=None, energy=None, angle=None) -> list[Path]:
    """Find leaf directories containing .phel.zst files (depth 4: particle/energy/angle/height)."""
    dirs = []
    for p_dir in sorted(root.iterdir()):
        if not p_dir.is_dir():
            continue
        if particle and p_dir.name != particle:
            continue
        for e_dir in sorted(p_dir.iterdir()):
            if not e_dir.is_dir():
                continue
            if energy and e_dir.name != energy:
                continue
            for a_dir in sorted(e_dir.iterdir()):
                if not a_dir.is_dir():
                    continue
                if angle and a_dir.name != angle:
                    continue
                for h_dir in sorted(a_dir.iterdir()):
                    if not h_dir.is_dir():
                        continue
                    if any(h_dir.glob("*.phel.zst")):
                        dirs.append(h_dir)
    return dirs


def read_progress(tsv_path: Path) -> dict[str, str]:
    """Read progress.tsv → {rel_path: status}."""
    progress = {}
    if tsv_path.exists():
        for line in tsv_path.read_text().splitlines():
            parts = line.split("\t")
            if len(parts) >= 2:
                progress[parts[0]] = parts[1]
    return progress


def update_progress(tsv_path: Path, rel_path: str, status: str):
    """Thread-safe update of progress.tsv using flock."""
    lock_path = str(tsv_path) + ".lock"
    with open(lock_path, "w") as lock_fd:
        fcntl.flock(lock_fd, fcntl.LOCK_EX)
        progress = read_progress(tsv_path)
        progress[rel_path] = status
        lines = [f"{k}\t{v}" for k, v in sorted(progress.items())]
        tsv_path.write_text("\n".join(lines) + "\n")


def run_one(phels_dir: Path, moshits_root: Path, sphere_bin: Path,
            logs_dir: Path, progress_tsv: Path, phels_root: Path) -> tuple[str, bool]:
    """Process one phels directory. Returns (rel_path, success)."""
    rel_path = str(phels_dir.relative_to(phels_root))
    moshits_dir = moshits_root / rel_path
    moshits_dir.mkdir(parents=True, exist_ok=True)

    log_file = logs_dir / (rel_path.replace("/", "__") + ".log")
    log_file.parent.mkdir(parents=True, exist_ok=True)

    update_progress(progress_tsv, rel_path, "running")

    # SPHERE-3 expects currentPath as first positional arg for configs/
    current_path = str(sphere_bin.parent.parent)
    cmd = [
        str(sphere_bin), current_path,
        "--phels", str(phels_dir),
        "--moshits", str(moshits_dir),
        "--threads", "1",
    ]

    try:
        with open(log_file, "w") as lf:
            result = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT, timeout=7200)
        success = result.returncode == 0
        status = "completed" if success else f"failed:{result.returncode}"
    except subprocess.TimeoutExpired:
        success = False
        status = "failed:timeout"
    except Exception as e:
        success = False
        status = f"failed:{e}"

    update_progress(progress_tsv, rel_path, status)
    return rel_path, success


def main():
    parser = argparse.ArgumentParser(description="SPHERE-3_G4 batch processor")
    parser.add_argument("--phels-root", type=Path, default=Path.home() / "sphall" / "phels")
    parser.add_argument("--moshits-root", type=Path, default=Path.home() / "sphall" / "moshits")
    parser.add_argument("--sphere-bin", type=Path, default=DEFAULT_SPHERE_BIN)
    parser.add_argument("--max-jobs", type=int, default=30)
    parser.add_argument("--particle", help="Filter by particle (e.g. 0014)")
    parser.add_argument("--energy", help="Filter by energy (e.g. 10PeV)")
    parser.add_argument("--angle", help="Filter by zenith angle (e.g. 15)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    work_dir = args.moshits_root.parent
    logs_dir = work_dir / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    progress_tsv = work_dir / "progress.tsv"

    # Find directories
    all_dirs = find_phel_dirs(args.phels_root, args.particle, args.energy, args.angle)
    log.info("Found %d phel directories", len(all_dirs))

    # Filter completed
    progress = read_progress(progress_tsv)
    pending = []
    for d in all_dirs:
        rel = str(d.relative_to(args.phels_root))
        if progress.get(rel) == "completed":
            continue
        pending.append(d)

    log.info("Pending: %d (skipped %d completed)", len(pending), len(all_dirs) - len(pending))

    if args.dry_run:
        for d in pending:
            print(f"  {d.relative_to(args.phels_root)}")
        return

    if not args.sphere_bin.exists():
        log.error("SPHERE-3 binary not found: %s", args.sphere_bin)
        sys.exit(1)

    # Process
    completed = 0
    failed = 0
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.max_jobs) as pool:
        futures = {
            pool.submit(run_one, d, args.moshits_root, args.sphere_bin,
                        logs_dir, progress_tsv, args.phels_root): d
            for d in pending
        }
        for future in as_completed(futures):
            rel_path, success = future.result()
            if success:
                completed += 1
                log.info("OK  %s (%d/%d)", rel_path, completed + failed, len(pending))
            else:
                failed += 1
                log.warning("FAIL %s (%d/%d)", rel_path, completed + failed, len(pending))

    elapsed = time.time() - start
    log.info("Done: %d completed, %d failed, %.0f seconds", completed, failed, elapsed)


if __name__ == "__main__":
    main()
