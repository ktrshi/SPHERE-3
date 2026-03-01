# Ring Analysis: Snow → Mosaic Transport Evaluation

## Goal

Evaluate the SPHERE-3 detector by tracing photons from `phels_to_trace` through G4 optics and analyzing how rings on snow map to hits on the mosaic. Understand per-ring efficiency and spatial transport.

## Input Files

### phels_to_trace (snow photons)

- **Header (line 1):** `event_num zz ysh xsh num_rings`
  - Example: `1 -5.000000e+02 0.000000e+00 0.000000e+00 21`
- **Data (7 columns):** `ring_id jj kk photon_idx x_snow y_snow t0`
  - `ring_id`: ring number (0..52)
  - `jj`, `kk`: always 0 in this dataset
  - `photon_idx`: photon index within position (0,1,2,...)
  - `x_snow`, `y_snow`: coordinates on snow (meters)
  - `t0`: time delay
- **Terminator (last line):** `-1 -1 -1 -1 0 0 0`

### moshits (mosaic hits after G4)

- **Header (line 1):** same as phels_to_trace header (copied through)
- **Data (16 columns):**
  `cluster pix x_mos y_mos z_mos glt dirx diry dirz origin ring_id phl_jj phl_kk phl_xx phl_yy phl_t0`
  - `cluster`: PMT cluster number (0..378)
  - `pix`: pixel within cluster (0..6)
  - `x_mos, y_mos, z_mos`: photon position at detection (meters)
  - `glt`: global time (ns)
  - `dirx, diry, dirz`: momentum direction at detection
  - `origin`: 1=Cherenkov, 2=background
  - `ring_id`: ring number from input (preserved through tracing)
  - `phl_xx, phl_yy`: original snow coordinates
  - `phl_t0`: original time

## Script: `analyze_rings.py`

### Usage

```bash
# Interactive viewer (default)
uv run python analyze_rings.py -i phels_to_trace_file -m moshits_file

# Summary mode: table + static plots saved to PNG
uv run python analyze_rings.py -i phels_to_trace_file -m moshits_file --summary
```

### Dependencies

- numpy, pandas, matplotlib

### Data Flow

1. Parse phels_to_trace → DataFrame: `ring_id, photon_idx, x_snow, y_snow, t0`
2. Parse moshits → DataFrame: `cluster, pix, x_mos, y_mos, z_mos, glt, origin, ring_id, phl_xx, phl_yy`
3. Group by `ring_id`, compute per-ring: `n_emitted`, `n_detected`, `efficiency`

### Interactive Viewer Layout

```
┌─────────────────────┬─────────────────────┐
│   Snow (x, y)       │   Mosaic (x, y)     │
│   scatter of photon │   scatter of hits   │
│   positions         │   for same ring     │
│   gray = all rings  │   gray = all hits   │
│   color = ring N    │   color = ring N    │
├─────────────────────┴─────────────────────┤
│   Efficiency vs Ring Number (bar chart)    │
│   selected ring highlighted               │
├───────────────────────────────────────────┤
│   [ ═══════●══════════════ ] Ring: 17     │
│   Emitted: 29059  Detected: 847  Eff: 2.9%│
└───────────────────────────────────────────┘
```

- **Top row:** two scatter plots. Gray points = all photons/hits (context). Colored = selected ring.
- **Middle row:** bar chart of efficiency by ring. Selected ring highlighted.
- **Slider** at bottom switches rings (0..max_ring). Text line with N_emitted, N_detected, efficiency.
- Slider position −1 or "All" button shows all rings colored by ring_id (colormap).

### Summary Mode (`--summary`)

1. **Efficiency vs Ring** bar chart → saved as PNG
2. **Full snow map** — all rings, colormap by ring_id → PNG
3. **Full mosaic map** — all hits, colormap by ring_id → PNG
4. **Table to stdout:**

```
Ring | Emitted | Detected | Eff(%) | <r_snow> (m)
   0 |    2976 |       85 |   2.86 |   12.3
   1 |    4563 |      132 |   2.89 |   24.7
 ...
 ALL | 1465254 |    42150 |   2.88 |     —
```

PNGs saved next to moshits file as `*_summary.png`.
