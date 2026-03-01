# Collector Geometry Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Заменить STL-коллектор и Shield на программную геометрию с раздельными оптическими свойствами: поглощающие боковые стенки (CollectorBase) и прозрачная сферическая линза (CollectorLens). PMT размещается внутри CollectorBase (без воздушного зазора).

**Architecture:**

```
World (Air)
├── CollectorBase (сплошная гекс-призма, Acrylyl, absorbing skin)
│   └── PMT (G4Box, Carbon, detecting skin) — дочерний объём
└── CollectorLens (сферическая шапка, Acrylyl, transparent skin)
```

Skin surface CollectorBase поглощает фотоны на всех гранях. Для «разрешённых» переходов (Lens↔Base, Base→PMT) добавляются G4LogicalBorderSurface, которые имеют приоритет над skin.

**Tech Stack:** Geant4 11.03, G4ExtrudedSolid, G4Sphere, G4IntersectionSolid

---

## Параметры геометрии

Из анализа STL:
- Радиус гексагона: 6 мм (описанная окружность, уменьшен с 7 мм для устранения overlap)
- Высота базы: 4 мм
- Радиус сферы: 17 мм
- Центр сферы: z = -11.6 мм относительно основания коллектора (PMT face)
- Вершина сферы: z = 5.4 мм
- Высота шапки линзы: 5.4 - 4.0 = 1.4 мм
- PMT: G4Box 3×3×0.1 мм (half-z = 0.1 мм)

## Локальная система координат коллектора

- z = 0: поверхность PMT (основание коллектора)
- z = 0..4 мм: CollectorBase (гекс-призма)
- z = 4..5.4 мм: CollectorLens (сферическая шапка)
- z = -11.6 мм: центр сферы линзы

В системе G4ExtrudedSolid (origin = геометрический центр):
- Base: z_local = -2..+2 мм (half-height = 2 мм)
- PMT внутри Base: z_local = -2.0 + 0.1 = -1.9 мм

## Приоритет оптических поверхностей (Geant4)

```
1. G4LogicalBorderSurface(preStepPV, postStepPV)  ← высший приоритет
2. G4LogicalSkinSurface(preStep LogicalVolume)
3. G4LogicalSkinSurface(postStep LogicalVolume)
4. Fresnel по материалам
```

Переходы и какая поверхность используется:

| Переход | Поверхность | Результат |
|---------|-------------|-----------|
| Air → Lens | Lens skin (transparent) | Френелевское преломление в акрил |
| Lens → Base | **Border** (transparent) | Акрил→акрил, проход без преломления |
| Base → PMT | **Border** (detecting) | Фотон детектируется |
| Base → World (бок. стенки) | Base skin (absorbing) | Поглощение |
| World → Base (вход сбоку) | Base skin (absorbing, post-step) | Поглощение |
| Base → Lens (отражённые) | **Border** (transparent) | Акрил→акрил, проход |

---

### Task 1: Удалить Shield и STL-коллектор

**Files:**
- Modify: `src/sphmirrDetectorConstruction.cc`

**Step 1: Удалить загрузку STL коллектора**

Удалить строки:
```cpp
auto sphpmtcol_mesh = CADMesh::TessellatedMesh::FromSTL(AbsolutePath + "/configs/collector_test.stl");
auto sphpmtcol_solid = sphpmtcol_mesh->GetSolid();
```

И удалить создание sphpmtcol_log:
```cpp
auto sphpmtcol_log = new G4LogicalVolume(sphpmtcol_solid, Acrylyl, "Collector");
sphpmtcol_log->SetVisAttributes(visAttrpmt);
```

**Step 2: Удалить создание Shield solid**

Удалить строки создания outer_hex, inner_hex, hollow_hex, shell_log (строки 117-145).

**Step 3: Удалить размещение Shield и Collector в цикле**

В цикле for(i=0..2653) удалить:
```cpp
[[maybe_unused]] auto sphpmtcol_phys = new G4PVPlacement(rotm_ptr, pos_col, sphpmtcol_log, "Collector", expHall_log, false, i, true);
[[maybe_unused]] auto sphpmt_tube_phys = new G4PVPlacement(rotm_alt_ptr, pos_shield, shell_log, "Shield", expHall_log, false, i, true);
```

Удалить `pos_col`, `pos_shield`, создание `rotm_alt` и `rotm_alt_ptr`.

**Step 4: Удалить skin surface старого коллектора**

Удалить строку:
```cpp
[[maybe_unused]] auto *sphpmtcolSkinSurface = new G4LogicalSkinSurface("sphcorSkin", sphpmtcol_log, OpsphcorSurface);
```

**Step 5: Уменьшить кэш матриц**

Изменить:
```cpp
rotationMatrixCache.reserve(2653 * 2);
```
На:
```cpp
rotationMatrixCache.reserve(2653);
```

**Step 6: Удалить размещение PMT в World**

В цикле удалить:
```cpp
sphpmt_phys = new G4PVPlacement(rotm_ptr, pos_pmt, sphpmt_log, "PMT", expHall_log, false, i, true);
```

(PMT будет размещён как дочерний объём CollectorBase в Task 4.)

**Step 7: Собрать и проверить компиляцию**

Run: `cd build && make -j4 2>&1 | tail -10`
Expected: `[100%] Built target SPHERE-3`

**Step 8: Commit**

```bash
git add src/sphmirrDetectorConstruction.cc
git commit -m "refactor: remove Shield, STL collector, and old PMT placement"
```

---

### Task 2: Создать геометрию CollectorBase (гексагональная призма)

**Files:**
- Modify: `src/sphmirrDetectorConstruction.cc`

**Step 1: Определить константы геометрии коллектора**

После строки `sphmos_phys = new G4PVPlacement(...)` добавить:

```cpp
// Collector geometry parameters
constexpr G4double collector_hex_radius = 7.0 * mm;    // circumscribed radius
constexpr G4double collector_base_height = 4.0 * mm;
constexpr G4double collector_sphere_radius = 17.0 * mm;
constexpr G4double collector_sphere_center_z = -11.6 * mm;  // relative to PMT face (base bottom)
constexpr G4double pmt_half_z = 0.1 * mm;                   // PMT box half-height
```

**Step 2: Создать гексагональную призму для базы**

```cpp
// CollectorBase: solid hexagonal prism (absorbing sides, transparent top/bottom via border surfaces)
auto collector_base_solid = new G4ExtrudedSolid(
    "CollectorBase",
    makeHexagonVertices(collector_hex_radius),
    collector_base_height / 2.0,  // half-height = 2mm
    G4TwoVector(0, 0), 1.0,
    G4TwoVector(0, 0), 1.0
);
auto collector_base_log = new G4LogicalVolume(collector_base_solid, Acrylyl, "CollectorBase");
collector_base_log->SetVisAttributes(visAttrpmt);
```

**Step 3: Собрать и проверить**

Run: `cd build && make -j4 2>&1 | tail -10`
Expected: `[100%] Built target SPHERE-3`

**Step 4: Commit**

```bash
git add src/sphmirrDetectorConstruction.cc
git commit -m "feat: add CollectorBase hexagonal prism geometry"
```

---

### Task 3: Создать геометрию CollectorLens (сферическая шапка)

**Files:**
- Modify: `src/sphmirrDetectorConstruction.cc`

**Ключевая идея:** линза — это только шапка сферы выше z=4мм (верха базы). Для этого пересекаем сферу с тонким гекс-слэбом, позиционированным в системе координат сферы так, чтобы покрыть только область шапки.

В системе сферы (origin = центр сферы):
- Основание коллектора (z=0 collector) → z = 11.6 мм в сфере
- Верх базы (z=4мм collector) → z = 15.6 мм в сфере
- Вершина сферы → z = 17.0 мм в сфере
- Шапка: z = 15.6..17.0 мм в сфере, высота = 1.4 мм
- Гекс-слэб: half-height = 0.7 мм, центр z = 16.3 мм в сфере

**Step 1: Создать полусферу**

```cpp
// Full upper hemisphere
auto collector_sphere = new G4Sphere(
    "CollectorSphere",
    0.0,                        // inner radius
    collector_sphere_radius,    // outer radius = 17mm
    0.0, 360.0 * degree,       // full phi
    0.0, 90.0 * degree          // upper hemisphere only
);
```

**Step 2: Создать тонкий гекс-слэб для вырезки шапки**

```cpp
// Thin hex slab positioned to capture only the cap region
constexpr G4double cap_height = collector_sphere_radius + collector_sphere_center_z
                                - collector_base_height;  // = 17 + (-11.6) - 4 = 1.4mm
constexpr G4double cap_half_height = cap_height / 2.0;    // = 0.7mm
constexpr G4double cap_center_in_sphere = -collector_sphere_center_z
                                          + collector_base_height
                                          + cap_half_height;  // = 11.6 + 4 + 0.7 = 16.3mm

auto collector_hex_slab = new G4ExtrudedSolid(
    "CollectorHexSlab",
    makeHexagonVertices(collector_hex_radius),
    cap_half_height,  // half-height = 0.7mm
    G4TwoVector(0, 0), 1.0,
    G4TwoVector(0, 0), 1.0
);
```

**Step 3: Пересечение сферы с гекс-слэбом → шапка**

```cpp
// Intersection: sphere cap clipped by hexagonal boundary
auto collector_lens_solid = new G4IntersectionSolid(
    "CollectorLens",
    collector_sphere,
    collector_hex_slab,
    nullptr,
    G4ThreeVector(0, 0, cap_center_in_sphere)  // slab at z=16.3mm in sphere frame
);
auto collector_lens_log = new G4LogicalVolume(collector_lens_solid, Acrylyl, "CollectorLens");
collector_lens_log->SetVisAttributes(visAttrpmt);
```

**Step 4: Собрать и проверить**

Run: `cd build && make -j4 2>&1 | tail -10`
Expected: `[100%] Built target SPHERE-3`

**Step 5: Commit**

```bash
git add src/sphmirrDetectorConstruction.cc
git commit -m "feat: add CollectorLens spherical cap geometry"
```

---

### Task 4: Назначить оптические поверхности и border surfaces

**Files:**
- Modify: `src/sphmirrDetectorConstruction.cc`

**Step 1: Absorbing skin surface для CollectorBase**

В секции Surfaces добавить:

```cpp
// Absorbing skin surface for CollectorBase (all faces by default)
G4double ReflBase[num] = {0.0, 0.0};
G4double EffiBase[num] = {0.0, 0.0};  // no detection, just absorb
auto collectorBaseSurfProperties = new G4MaterialPropertiesTable();
collectorBaseSurfProperties->AddProperty("REFLECTIVITY", Ephoton, ReflBase, num);
collectorBaseSurfProperties->AddProperty("EFFICIENCY", Ephoton, EffiBase, num);
auto OpCollectorBaseSurface = new G4OpticalSurface(
    "CollectorBaseSurface", unified, polished, dielectric_metal);
OpCollectorBaseSurface->SetMaterialPropertiesTable(collectorBaseSurfProperties);
[[maybe_unused]] auto collectorBaseSkinSurface =
    new G4LogicalSkinSurface("CollectorBaseSkin", collector_base_log, OpCollectorBaseSurface);
```

**Step 2: Transparent skin surface для CollectorLens**

```cpp
// Transparent skin surface for CollectorLens (Fresnel refraction at air-acrylic boundary)
[[maybe_unused]] auto collectorLensSkinSurface =
    new G4LogicalSkinSurface("CollectorLensSkin", collector_lens_log, OpsphcorSurface);
```

**Step 3: Подготовить transparent border surface (для Lens↔Base переходов)**

```cpp
// Transparent border surface for Lens<->Base transitions (overrides absorbing skin)
auto OpTransparentSurface = new G4OpticalSurface(
    "TransparentBorder", unified, polished, dielectric_dielectric);
OpTransparentSurface->SetMaterialPropertiesTable(mpt);  // acrylic RINDEX + ABSLENGTH
```

**Step 4: Собрать и проверить**

Run: `cd build && make -j4 2>&1 | tail -10`
Expected: `[100%] Built target SPHERE-3`

**Step 5: Commit**

```bash
git add src/sphmirrDetectorConstruction.cc
git commit -m "feat: add optical surfaces for collector components"
```

---

### Task 5: Разместить PMT и коллекторы

**Files:**
- Modify: `src/sphmirrDetectorConstruction.cc`
- Modify: `src/sphmirrSteppingAction.cc`

**Ключевые идеи:**
1. PMT размещается ОДИН РАЗ как дочерний объём collector_base_log (ВНЕ цикла). Каждый физический экземпляр CollectorBase автоматически содержит PMT.
2. Позиции смещаются вдоль нормали к мозаике (локальная ось z после поворота), а не вдоль глобальной z. Нормаль извлекается из третьего столбца R^T.
3. Border surfaces создаются В цикле для каждого пикселя (ссылаются на конкретные physical volume).

**Step 1: Разместить PMT как daughter ВНЕ цикла**

После создания collector_base_log и sphpmt_log, ДО цикла:

```cpp
// Place PMT once as daughter of CollectorBase (all instances inherit it)
// Local position: bottom of base + half PMT height
auto pmt_phys = new G4PVPlacement(
    nullptr,
    G4ThreeVector(0, 0, -collector_base_height / 2.0 + pmt_half_z),
    sphpmt_log, "PMT", collector_base_log, false, 0, true);
```

**Step 2: Вычислить нормаль и позиции (внутри цикла)**

После создания rotm и rotm_ptr:

```cpp
// Outward normal direction in global frame = R^T * (0,0,1) = 3rd column of R^T
const G4ThreeVector outward_normal(
    firstRow.z(),    // sin_theta * ay
    secondRow.z(),   // -sin_theta * ax
    thirdRow.z()     // cos_theta
);

// CollectorBase: center offset from PMT position along normal
// PMT sits at local z = -base_half + pmt_half_z = -1.9mm in base frame
// So base center is 1.9mm outward from PMT position
constexpr G4double base_center_offset = collector_base_height / 2.0 - pmt_half_z;  // 1.9mm
const auto pos_base = G4ThreeVector(pix_x[i], pix_y[i], pix_z[i])
                      + base_center_offset * outward_normal;

// CollectorLens: sphere center is at -11.6mm from PMT face (inward)
const auto pos_lens = G4ThreeVector(pix_x[i], pix_y[i], pix_z[i])
                      + collector_sphere_center_z * outward_normal;
```

**Step 3: Разместить CollectorBase и CollectorLens (внутри цикла)**

```cpp
// Place CollectorBase in World
auto collector_base_phys = new G4PVPlacement(
    rotm_ptr, pos_base, collector_base_log, "CollectorBase",
    expHall_log, false, i, true);

// Place CollectorLens in World (origin = sphere center)
auto collector_lens_phys = new G4PVPlacement(
    rotm_ptr, pos_lens, collector_lens_log, "CollectorLens",
    expHall_log, false, i, true);
```

**Step 4: Добавить border surfaces (внутри цикла)**

```cpp
// Border surfaces override absorbing skin for allowed transitions
new G4LogicalBorderSurface("Lens2Base_" + std::to_string(i),
    collector_lens_phys, collector_base_phys, OpTransparentSurface);
new G4LogicalBorderSurface("Base2Lens_" + std::to_string(i),
    collector_base_phys, collector_lens_phys, OpTransparentSurface);
new G4LogicalBorderSurface("Base2PMT_" + std::to_string(i),
    collector_base_phys, pmt_phys, OpsphPMTSurface);
```

**Step 5: Удалить неиспользуемые переменные**

Удалить `pos_pmt`, `pos_col`, `pos_shield` и всё, связанное с `rotm_alt`.

**Step 6: Обновить SteppingAction — получение copyNo через touchable hierarchy**

В `src/sphmirrSteppingAction.cc`, строка 90, заменить:

```cpp
const G4int copyNo = theTouchable->GetCopyNumber();
```

На:

```cpp
// PMT is daughter (depth 0) inside CollectorBase (depth 1)
// Use depth 1 to get the CollectorBase copy number = pixel index
const G4int copyNo = theTouchable->GetCopyNumber(1);
```

**Step 7: Собрать и проверить**

Run: `cd build && make -j4 2>&1 | tail -10`
Expected: `[100%] Built target SPHERE-3`

**Step 8: Commit**

```bash
git add src/sphmirrDetectorConstruction.cc src/sphmirrSteppingAction.cc
git commit -m "feat: place CollectorBase (with PMT daughter), CollectorLens, and border surfaces"
```

---

### Task 6: Запустить и проверить overlap

**Files:**
- None (runtime test)

**Step 1: Запустить симуляцию**

Run: `cd build && ./SPHERE-3 2>&1 | head -100`

**Step 2: Проверить отсутствие overlap**

Expected: Все `Checking overlaps for volume ... OK!`

Возможные проблемы:
- Overlap Base↔Lens: проверить, что гекс-слэб позиционирован точно на z=15.6..17.0 мм в frame сферы
- Overlap между соседними коллекторами: если radius 7мм слишком большой для расстояния между пикселями, уменьшить до 6мм
- PMT не помещается внутри Base: PMT 3×3мм, Base radius 7мм — должно быть ОК

**Step 3: Commit финальный**

```bash
git add -A
git commit -m "feat: complete collector geometry refactor - Base+Lens+PMT hierarchy"
```

---

### Task 7: Очистка

**Files:**
- Modify: `include/sphmirrDetectorConstruction.hh` (удалить неиспользуемые члены, если есть)
- Delete: `configs/collector_test.stl` (если больше не нужен)

**Step 1: Проверить, что collector_test.stl не используется**

Run: `rg "collector_test" src/ include/`
Expected: No matches

**Step 2: Проверить, нужен ли ещё CADMesh**

Run: `rg "CADMesh" src/ include/`

Если CADMesh используется только для mirror_test.stl — оставить. Если collector_test.stl был единственным — можно удалить include.

**Step 3: Удалить STL файл**

```bash
rm configs/collector_test.stl
git add configs/collector_test.stl
git commit -m "chore: remove unused collector STL file"
```

---

## Примечания

1. **PMT как daughter volume:** PMT размещается ОДИН РАЗ (вне цикла) в ЛОКАЛЬНЫХ координатах CollectorBase (без rotation). Поворот наследуется от родителя. Каждый физический экземпляр CollectorBase автоматически содержит PMT. copyNo PMT = 0, copyNo CollectorBase = i (pixel index).

2. **SteppingAction:** `theTouchable->GetCopyNumber(1)` вместо `GetCopyNumber()` — depth 1 даёт copyNo родительского CollectorBase, а не дочернего PMT (который всегда 0).

3. **Border surfaces:** 3 × 2653 = 7959 объектов G4LogicalBorderSurface. Каждый хранит 2 указателя + ссылку на surface. Не влияет на производительность.

3. **Нормаль vs. глобальная z:** Cмещения позиций вдоль `outward_normal`, а не вдоль глобальной z. Для углов до 21° ошибка при использовании глобальной z была бы до ~0.7мм (10% от радиуса гексагона).

4. **Overlap между соседними коллекторами:** Радиус гексагона 7мм при расстоянии между пикселями ~12мм. Если overlap — уменьшить до 6мм.

5. **Визуализация:** После реализации проверить геометрию визуально в Geant4 viewer.
