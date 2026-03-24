# Analytical Mirror Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the STL tessellated mirror with an analytical G4GenericPolycone to eliminate the 55% CPU bottleneck in G4TessellatedSolid::SafetyFromInside.

**Architecture:** Compute the Zemax EVENASPH surface z(r) at ~1100 points, build a closed (r, z) profile with a constant-thickness back surface, and construct a G4GenericPolycone. All changes are confined to `sphmirrDetectorConstruction.cc`. Optical properties and all other geometry remain untouched.

**Tech Stack:** Geant4 11.03 (G4GenericPolycone), C++20, existing CMake build system.

**Spec:** `docs/superpowers/specs/2026-03-24-analytical-mirror-design.md`

---

### Task 0: Save baseline output for validation

**Files:**
- None modified — this is a data capture step

Capture the STL-version output for the 5 test events so we can compare after the change.

- [ ] **Step 1: Run baseline and save output**

```bash
cd /Users/vladimirivanov/Projects/SPHERE/SPHERE-3_G4/build
mkdir -p /tmp/sphere_baseline_moshits
./SPHERE-3 --phels /tmp/sphere_profile_phels --moshits /tmp/sphere_baseline_moshits --threads 1 2>&1 | tee /tmp/sphere_baseline_log.txt
```

Expected: 5 events, ~45s, output in `/tmp/sphere_baseline_moshits/`. Save the log showing TotPhot, NEntry, tmin, tmax per event.

- [ ] **Step 2: Record baseline numbers**

From the log, note per-event values:
- Event 0: TotPhot=8611, NEntry=8993
- Event 1: TotPhot=20655, NEntry=21361
- Event 2: TotPhot=19151, NEntry=20109
- Event 3: TotPhot=51791, NEntry=53536
- Event 4: TotPhot=65971, NEntry=67826

These are the validation targets.

---

### Task 1: Inspect STL bounding box to determine thickness and origin

**Files:**
- None modified — analysis only

We need two numbers from the STL: mirror thickness and whether the STL origin is at the vertex.

- [ ] **Step 1: Write a quick Python script to inspect the STL**

```bash
cd /Users/vladimirivanov/Projects/SPHERE/SPHERE-3_G4/build/configs
python3 -c "
import struct, sys

with open('mirror_test.stl', 'rb') as f:
    header = f.read(80)
    n_triangles = struct.unpack('<I', f.read(4))[0]
    print(f'Triangles: {n_triangles}')
    zmin, zmax, rmax = 1e9, -1e9, 0
    for _ in range(n_triangles):
        f.read(12)  # normal
        for _ in range(3):
            x, y, z = struct.unpack('<3f', f.read(12))
            r = (x*x + y*y)**0.5
            zmin = min(zmin, z)
            zmax = max(zmax, z)
            rmax = max(rmax, r)
        f.read(2)  # attribute
    print(f'z range: [{zmin:.3f}, {zmax:.3f}] mm')
    print(f'thickness (z extent): {zmax - zmin:.3f} mm')
    print(f'max radius: {rmax:.3f} mm')
    print(f'z at vertex (r~0): check manually or take zmax')
"
```

Expected output: z range, thickness, max radius. Record these values — they determine the back-surface offset and whether placement z needs adjustment.

- [ ] **Step 2: Compute EVENASPH z(r) at key points for cross-check**

```bash
python3 -c "
import math

c = -6.045949214026601900e-4
alpha = [1.02196065e-5, 1.15244950e-11, -2.00781727e-18, -8.77201898e-24,
         1.79536598e-29, -1.40758850e-35, 5.28735920e-42, -7.79178450e-49]

def sag(r):
    cr2 = c * r * r
    z = cr2 / (1.0 + math.sqrt(1.0 - c * c * r * r))
    for i, a in enumerate(alpha):
        z += a * r ** (2 * (i + 1))
    return z

print(f'z(0)     = {sag(0):.6f} mm')
print(f'z(500)   = {sag(500):.6f} mm')
print(f'z(1000)  = {sag(1000):.6f} mm')
print(f'z(1106)  = {sag(1106):.6f} mm  (mirror edge)')
print()
print('For hood validation:')
print(f'sphmirr_z + z(1106) = {1751.41 + sag(1106):.3f} mm')
print(f'Expected mirror_rim_z from code: 1353.4 mm')
print(f'Difference: {1751.41 + sag(1106) - 1353.4:.3f} mm')
"
```

This verifies whether `mirror_rim_z = 1353.4 mm` is consistent with the analytical formula. If there's a significant discrepancy, `mirror_rim_z` must be updated.

- [ ] **Step 3: Document findings**

Record: STL thickness, STL z-origin, analytical z(1106), whether mirror_rim_z needs updating. These feed into Task 2.

---

### Task 2: Replace mirror construction in DetectorConstruction

**Files:**
- Modify: `src/sphmirrDetectorConstruction.cc:1` (add include)
- Modify: `src/sphmirrDetectorConstruction.cc:85-90` (replace mirror construction)
- Modify: `src/sphmirrDetectorConstruction.cc:170-171` (update constants if needed)

- [ ] **Step 1: Add G4GenericPolycone include**

In `src/sphmirrDetectorConstruction.cc`, add after line 2 (`#include <G4Cons.hh>`):

```cpp
#include <G4GenericPolycone.hh>
```

- [ ] **Step 2: Replace mirror construction (lines 85-90)**

Replace:
```cpp
// The Mirror
    auto mesh = CADMesh::TessellatedMesh::FromSTL(AbsolutePath + "/configs/mirror_test.stl");
    auto SphMirr = mesh->GetSolid();
    sphmirr_log = new G4LogicalVolume(SphMirr, Al, "Mirror");
    sphmirr_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, sphmirr_z),
                                     sphmirr_log, "Mirror", expHall_log, false, 0);
```

With (adjust `mirror_thickness` based on Task 1 findings):

```cpp
// The Mirror — analytical EVENASPH surface (Zemax Configuration A)
    constexpr G4double mirror_curv = -6.045949214026601900e-4; // mm^-1, R=1654mm
    constexpr G4double mirror_alpha[8] = {
        +1.02196065e-5,   // r^2
        +1.15244950e-11,  // r^4
        -2.00781727e-18,  // r^6
        -8.77201898e-24,  // r^8
        +1.79536598e-29,  // r^10
        -1.40758850e-35,  // r^12
        +5.28735920e-42,  // r^14
        -7.79178450e-49   // r^16
    };
    constexpr G4double mirror_r_max = 1106.0 * mm;
    constexpr G4double mirror_thickness = 5.0 * mm; // from STL bounding box — adjust per Task 1
    // Safety: c^2 * r_max^2 = 0.447 < 1.0, so sqrt argument is always positive
    constexpr int mirror_n_steps = 1106;
    constexpr G4double mirror_dr = mirror_r_max / mirror_n_steps;

    // EVENASPH sag function: z(r) = c*r^2/(1+sqrt(1-c^2*r^2)) + sum(alpha_i * r^(2i))
    auto mirrorSag = [&](G4double r) -> G4double {
        const G4double r2 = r * r;
        const G4double cr2 = mirror_curv * r2;
        G4double z = cr2 / (1.0 + std::sqrt(1.0 - mirror_curv * mirror_curv * r2));
        G4double rp = r2; // r^(2i), starts at r^2
        for (int i = 0; i < 8; ++i) {
            z += mirror_alpha[i] * rp;
            rp *= r2;
        }
        return z;
    };

    // Build closed (r, z) profile: back surface outward, then front surface inward
    const int n_profile = 2 * (mirror_n_steps + 1);
    std::vector<G4double> mirror_r(n_profile), mirror_z(n_profile);
    // Back surface: r=0 to r_max
    for (int i = 0; i <= mirror_n_steps; ++i) {
        G4double r = i * mirror_dr;
        mirror_r[i] = r;
        mirror_z[i] = mirrorSag(r) - mirror_thickness;
    }
    // Front surface: r_max back to r=0
    for (int i = 0; i <= mirror_n_steps; ++i) {
        G4double r = (mirror_n_steps - i) * mirror_dr;
        mirror_r[mirror_n_steps + 1 + i] = r;
        mirror_z[mirror_n_steps + 1 + i] = mirrorSag(r);
    }

    // Log max discretization error for documentation
    {
        G4double maxErr = 0.0;
        for (int i = 0; i < mirror_n_steps; ++i) {
            G4double r_mid = (i + 0.5) * mirror_dr;
            G4double z_true = mirrorSag(r_mid);
            G4double z_linear = 0.5 * (mirrorSag(i * mirror_dr) + mirrorSag((i + 1) * mirror_dr));
            maxErr = std::max(maxErr, std::abs(z_true - z_linear));
        }
        G4cout << "Mirror polycone max discretization error: " << maxErr / um << " um" << G4endl;
    }

    auto *SphMirr = new G4GenericPolycone("Mirror", 0.0, CLHEP::twopi,
                                          n_profile, mirror_r.data(), mirror_z.data());
    sphmirr_log = new G4LogicalVolume(SphMirr, Al, "Mirror");
    sphmirr_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, sphmirr_z),
                                     sphmirr_log, "Mirror", expHall_log, false, 0);
```

- [ ] **Step 3: Update `mirror_rim_z` if needed (lines 170-171)**

Based on Task 1 Step 2 findings, if the analytical `sphmirr_z + z(1106)` differs from `1353.4 mm`, update:

```cpp
constexpr G4double mirror_edge_R = 1106.0 * mm;
constexpr G4double mirror_rim_z = /* computed value */ * mm;
```

If values match (within ~1 mm), no change needed.

- [ ] **Step 4: Build and verify compilation**

```bash
cd /Users/vladimirivanov/Projects/SPHERE/SPHERE-3_G4/build
cmake --build . 2>&1 | tail -20
```

Expected: clean build, no errors or warnings related to Mirror/polycone.

- [ ] **Step 5: Commit**

```bash
git add src/sphmirrDetectorConstruction.cc
git commit -m "perf: replace STL mirror mesh with analytical G4GenericPolycone

Compute Zemax EVENASPH surface (R=1654mm, 8 polynomial coefficients)
at 1106 points and build G4GenericPolycone. Eliminates 55% CPU spent
in G4TessellatedSolid::SafetyFromInside voxel sorting."
```

---

### Task 3: Validate physics output

**Files:**
- None modified — validation only

- [ ] **Step 1: Run the same 5 test events**

```bash
cd /Users/vladimirivanov/Projects/SPHERE/SPHERE-3_G4/build
mkdir -p /tmp/sphere_polycone_moshits
./SPHERE-3 --phels /tmp/sphere_profile_phels --moshits /tmp/sphere_polycone_moshits --threads 1 2>&1 | tee /tmp/sphere_polycone_log.txt
```

- [ ] **Step 2: Compare TotPhot and NEntry per event**

Compare against baseline (Task 0 Step 2). Values must match exactly or within <0.1% (the RNG seed is fixed at 3453544, so with identical geometry the results should be deterministic — small differences indicate geometry mismatch).

If TotPhot differs by >1%: stop, investigate. The mirror surface shape likely has an error.

- [ ] **Step 3: Compare timing**

Expected: ~18-25s vs baseline ~45s. Note the actual speedup factor.

- [ ] **Step 4: Profile to confirm bottleneck is gone**

```bash
./SPHERE-3 --phels /tmp/sphere_profile_phels --moshits /tmp/sphere_polycone_moshits --threads 1 &
PID=$!; sleep 3; sample $PID 10 -f /tmp/sphere_polycone_profile.txt; wait $PID
```

Verify: `G4TessellatedSolid` should NOT appear in the hot path. `G4GenericPolycone` operations should be negligible (<5% CPU).

- [ ] **Step 5: Record results and commit validation log**

If validation passes, no code changes needed. The implementation is complete.

If validation fails (TotPhot mismatch), debug by:
1. Check mirror placement z — print `mirrorSag(0)` and compare to STL z at vertex
2. Check bounding box of new polycone vs old tessellated solid
3. Run with overlap checking temporarily enabled

---

### Task 4: Cleanup (optional)

- [ ] **Step 1: Remove `mirror_test.stl` from configs if no longer needed**

Only if the STL is not used by any other tool (visualization, validation scripts). If unsure, keep it.

- [ ] **Step 2: Update comment on `mirror_rim_z`**

If the constant was updated in Task 2, change the comment from `// from STL bounding box` to `// from EVENASPH z(1106)`.
