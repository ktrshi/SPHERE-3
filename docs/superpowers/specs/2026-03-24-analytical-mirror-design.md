# Replace STL Tessellated Mirror with Analytical G4GenericPolycone

**Date:** 2026-03-24
**Status:** Approved
**Branch:** feat/binary-format-migration (or new branch)

## Problem

CPU profiling (`sample`, 10s sampling on 5 events / 652K photons / 45s total) reveals that **55% of CPU time** is spent in `G4TessellatedSolid::SafetyFromInside` for the STL mirror mesh. Inside this function, ~45% of total CPU goes to `introsort` in `MinDistanceFacet` — Geant4 sorts voxel distances on every step of every photon track. This is O(N log N) per step where N = number of mesh facets.

All other geometry (2653 boolean-solid collectors, corrector STL, mosaic sphere) collectively account for <1% of CPU.

## Solution

Replace the STL mesh mirror (`mirror_test.stl` loaded via CADMesh → `G4TessellatedSolid`) with a `G4GenericPolycone` constructed analytically from the Zemax EVENASPH surface prescription.

`G4GenericPolycone` navigation uses O(log N) binary search on z-slices for `SafetyFromInside` and analytical ray-surface intersection for `DistanceToOut`, eliminating the voxel sorting bottleneck entirely.

## Mirror Surface Formula

Zemax **EVENASPH** (Configuration A, from `SPHERE-3.2_corr.ZMX` SURF 5):

```
z(r) = c·r² / (1 + √(1 - (1+k)·c²·r²)) + α₁·r² + α₂·r⁴ + α₃·r⁶ + α₄·r⁸ + α₅·r¹⁰ + α₆·r¹² + α₇·r¹⁴ + α₈·r¹⁶
```

Note: with k=0 (as in this prescription), `(1+k) = 1`, so the denominator simplifies to `√(1 - c²·r²)`.

Parameters:
- `c = -6.045949214026601900e-4 mm⁻¹` (base curvature, R = 1654.0 mm)
- `k = 0` (conic constant, not specified → zero)
- `α₁ = +1.02196065e-5` (r² term)
- `α₂ = +1.15244950e-11` (r⁴ term)
- `α₃ = -2.00781727e-18` (r⁶ term)
- `α₄ = -8.77201898e-24` (r⁸ term)
- `α₅ = +1.79536598e-29` (r¹⁰ term)
- `α₆ = -1.40758850e-35` (r¹² term)
- `α₇ = +5.28735920e-42` (r¹⁴ term)
- `α₈ = -7.79178450e-49` (r¹⁶ term)

Radial range: r = 0 to 1106 mm (`mirror_edge_R` from existing code).

## Construction

1. **Discretize** the reflective surface z_front(r) at ~1100 points (step ≈ 1 mm), yielding sub-micron accuracy.
2. **Back surface**: z_back(r) = z_front(r) - thickness. Determine thickness from STL bounding box (expected ~5-10 mm). Exact value is non-critical — photons reflect off the front surface and never penetrate.
3. **Build closed profile** for `G4GenericPolycone`:
   - Trace back surface outward: (r=0, z_back(0)), ..., (r_max, z_back(r_max))
   - Trace front surface inward: (r_max, z_front(r_max)), ..., (r=0, z_front(0))
4. Construct: `G4GenericPolycone("Mirror", 0, 2π, N, r_vec, z_vec)`
5. Position at z = 1751.41 mm. **Important**: Verify that the STL internal origin corresponds to the EVENASPH vertex (z(0)=0). Inspect STL bounding box z_min/z_max to confirm alignment. If the STL origin differs from the vertex, adjust placement z accordingly.
6. **Validate `mirror_rim_z`**: Compute z(1106) from the EVENASPH formula and verify it matches the hardcoded `mirror_rim_z = 1353.4 mm` (relative to world origin). This constant is used by the hood cone geometry (`hood_n`). If it differs, update `mirror_rim_z` to prevent overlap/gap with the hood.

## Optical Properties

No changes. The existing `G4LogicalSkinSurface` with:
- `unified` model, `polished` finish, `dielectric_metal` type
- Reflectivity: 0.88 @ 1.5eV → 0.85 @ 2.0eV (wavelength-dependent)
- Efficiency: 0.0

...is attached to `mirror_log` and works identically regardless of solid type.

## What Does NOT Change

- `sphmirrSteppingAction.cc` — hot path unchanged
- `sphmirrPrimaryGeneratorAction.cc` — unchanged
- `sphmirrPhysicsList.cc` — unchanged
- All other volumes: mosaic, collectors, PMTs, hood, corrector — unchanged
- Input/output format — unchanged
- CADMesh dependency stays (corrector is still STL)

## Construction Notes

- Use constant thickness offset for back surface, since only the front (reflective) surface is optically active. Photons reflect off the front and never penetrate.
- At r=0 (axis of revolution), the profile is a closed 2D contour in (r, z) space. Back and front surfaces meet at different z values, connected by the polycone closure.
- During implementation, compute max deviation between the piecewise-linear polycone profile and the true polynomial to document actual accuracy.

## Files Modified

- `src/sphmirrDetectorConstruction.cc` — replace mirror construction section (~30 lines → ~60 lines); add `#include "G4GenericPolycone.hh"`
- CADMesh include and dependency remain (corrector is still STL)

## Expected Performance

- **Before**: 45s for 5 events (652K photons), 55% CPU in TessellatedSolid
- **After**: ~18-25s for same workload (**1.8–2.5x speedup**)
- Navigation for mirror becomes O(log N) instead of O(N log N) voxel sort

## Validation Plan

1. Run identical 5 test events on STL version → save output `.moshit.zst` files
2. Run same events on Polycone version → save output
3. Compare:
   - TotPhot, NEntry per event (must match within statistical noise, <0.1%)
   - Per-pixel hit distributions
   - tmin/tmax values
4. Profile again with `sample` to confirm TessellatedSolid is gone from hot path

## Risks

1. **Surface accuracy**: Discretization at 1mm step → max error ~0.3 μm for this polynomial. Negligible impact on photon reflection angles.
2. **Mirror thickness**: Wrong thickness could cause overlaps with hood geometry. Mitigated by checking STL bounding box before implementation.
3. **Edge effects**: Polycone edge at r_max may differ slightly from STL mesh boundary. Use same `mirror_edge_R = 1106 mm` constant.
4. **STL origin alignment**: The STL mesh internal origin may not be at the EVENASPH vertex. Must verify by inspecting STL bounding box before setting placement z.
5. **Hood cone geometry**: The `mirror_rim_z` constant (1353.4 mm) derived from STL must be validated against z(1106) from the analytical formula. Mismatch causes overlap or gap with `hood_n`.
