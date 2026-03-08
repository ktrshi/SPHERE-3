# Batch Processing Design: Remote Server Hierarchical Data

## Problem

108 directories × 10,000 photon files on server `ivanov@213.131.1.51`.
Directory hierarchy: `/home/ivanov/sphall/phels/{particle}/{energy}/{zenith}/{altitude}/`
Example: `/home/ivanov/sphall/phels/0014/5PeV/05/500`

Need to run SPHERE-3 simulation on all directories with progress tracking and restart capability.

## Server

- Host: `ivanov@213.131.1.51`
- 32 threads available
- Geant4 installed, SPHERE-3 needs to be built

## Design

### 1. C++ Changes (SPHERE-3.cpp)

Add CLI arguments `--phels`, `--moshits`, `--threads`:

```
./SPHERE-3 --phels /path/to/phels/dir --moshits /path/to/output/dir --threads 1
```

If not specified — fallback to current behavior (`{currentPath}/phels`, `{currentPath}/moshits`, `input.ini` Threads).

Files changed: `SPHERE-3.cpp` only. `SimConfig.hh` already has `phelsDir` and `outputDir` fields.

### 2. Orchestrator Script (`scripts/run_all.sh`)

Configuration:
- `PHELS_ROOT=/home/ivanov/sphall/phels`
- `MOSHITS_ROOT=/home/ivanov/sphall/moshits`
- `MAX_JOBS=30` (2 cores reserved for system)
- `THREADS_PER_JOB=1`

Logic:
1. `fd -t d -d 4 . $PHELS_ROOT` finds all 108 leaf directories
2. Filter out directories with `completed` status in `progress.tsv`
3. Pipe to `GNU parallel -j $MAX_JOBS`
4. Each job calls `run_one.sh`

Restart: re-run `./run_all.sh` — skips `completed`, retries `failed` and unprocessed.

### 3. Worker Script (`scripts/run_one.sh`)

Per-directory wrapper:
1. Update `progress.tsv` → status `running`
2. Create mirror output directory under `MOSHITS_ROOT`
3. Run `SPHERE-3 --phels $DIR --moshits $OUTDIR --threads 1`
4. Capture stdout/stderr to `logs/{relative_path}.log`
5. Update `progress.tsv` → `completed` or `failed` with exit code

### 4. Progress Journal (`progress.tsv`)

```
directory	status	start_time	end_time	exit_code
0014/5PeV/05/500	completed	2026-03-08T10:00:00	2026-03-08T10:15:32	0
0014/5PeV/05/700	failed	2026-03-08T10:00:00	2026-03-08T10:03:11	1
```

### 5. File Layout on Server

```
/home/ivanov/sphall/
├── phels/                    # input (existing)
│   └── 0014/5PeV/05/500/
├── moshits/                  # output (created by scripts)
│   └── 0014/5PeV/05/500/
├── logs/                     # per-directory logs
│   └── 0014__5PeV__05__500.log
└── progress.tsv              # progress journal
```

## Parallelization Strategy

30 single-threaded SPHERE-3 processes via GNU parallel.
Each process handles one directory (10,000 files) sequentially.
30 of 32 cores utilized; 2 reserved for OS and monitoring.
