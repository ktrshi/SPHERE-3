#include <G4VisExtent.hh>
#include <G4Cons.hh>
#include <G4GenericPolycone.hh>
#include <memory>
#include "sphmirrDetectorConstruction.hh"
#include "G4Material.hh"
#include "G4Element.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4OpticalSurface.hh"
#include "G4Tubs.hh"
#include "G4ExtrudedSolid.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4Box.hh"
#include "G4Sphere.hh"
#include "G4IntersectionSolid.hh"
#include "G4UnionSolid.hh"
#include "G4NistManager.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4Transform3D.hh"
#include "G4RotationMatrix.hh"
#include "G4SDManager.hh"
#include "G4VisAttributes.hh"
#include "G4Colour.hh"
#include "G4SystemOfUnits.hh"

extern G4String AbsolutePath;
static std::ifstream pixel_data;
static G4double pix_phi[2653], pix_theta[2653]; // pixels' angles (file-scope, used only for placement)

std::vector<G4TwoVector> makeHexagonVertices(const G4double radius) {
    std::vector<G4TwoVector> vertices;
    constexpr G4int numSides = 6;
    for (G4int i = 0; i < numSides; i++) {
        const G4double angle = i * (2 * M_PI / numSides);
        G4double x = radius * std::cos(angle);
        G4double y = radius * std::sin(angle);
        vertices.emplace_back(x, y);
    }
    return vertices;
}


sphmirrDetectorConstruction::sphmirrDetectorConstruction() {
    pixel_data.open(AbsolutePath + "/configs/SPHERE3_pixel_data_A.dat");
    if (!pixel_data.is_open()) {
        std::cerr << "SPHERE3_pixel_data_A.dat not found" << std::endl;
        exit(1);
    }
    G4double x, y, z, phi, theta, tmp;
    G4int i = 0;
    while (pixel_data >> x >> y >> z >> tmp >> tmp >> tmp >> phi >> theta) {
        fPixX[i] = x * mm;
        fPixY[i] = y * mm;
        fPixZ[i] = z * mm;
        pix_phi[i] = phi * rad;
        pix_theta[i] = theta * rad;
        i++;
    }
    expHall_z = 4.0 * m;
    expHall_r = 2.0 * m;
    sphmirr_z = 1751.41 * mm;
    sphmos_r = 83.25 * cm;
    sphmos_R = 86.10 * cm;
    sphmos_z = 971.41 * mm;
    hood_r = 850 * mm;
    hood_R = 900 * mm;
    hood_hz = 0.2 * cm;
}
sphmirrDetectorConstruction::~sphmirrDetectorConstruction() = default;
G4VPhysicalVolume *sphmirrDetectorConstruction::Construct() {
    [[maybe_unused]] G4double a, z, density;
    [[maybe_unused]] G4int nelements;
    G4NistManager *nist = G4NistManager::Instance();
    G4Material *Air = nist->FindOrBuildMaterial("G4_AIR");
    G4Material *Al = nist->FindOrBuildMaterial("G4_Al");
    G4Material *Acrylyl = nist->FindOrBuildMaterial("G4_PLEXIGLASS");
    G4Material *C = nist->FindOrBuildMaterial("G4_C");
// The experimental Hall
    auto *expHall_sph = new G4Tubs("World", 0.0 * m, expHall_r, expHall_z, 0.0, 6.283185307179586 * rad);
    expHall_log
            = new G4LogicalVolume(expHall_sph, Air, "World");
    expHall_phys
            = new G4PVPlacement(nullptr, G4ThreeVector(), expHall_log, "World", nullptr, false, 0);
// The Mirror — analytical EVENASPH surface (Zemax Configuration A, R=1654mm)
    // EVENASPH: z(r) = c*r^2/(1+sqrt(1-(1+k)*c^2*r^2)) + sum(alpha_i * r^(2i))
    // k=0 for this prescription, so (1+k)=1
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
    constexpr G4double mirror_thickness = 10.0 * mm; // back surface offset (optically irrelevant)
    // Safety: c^2 * r_max^2 = 0.447 < 1.0, sqrt argument always positive
    constexpr int mirror_n_steps = 100;
    constexpr G4double mirror_dr = mirror_r_max / mirror_n_steps;

    auto mirrorSag = [&](G4double r) -> G4double {
        const G4double r2 = r * r;
        const G4double cr2 = mirror_curv * r2;
        G4double zz = cr2 / (1.0 + std::sqrt(1.0 - mirror_curv * mirror_curv * r2));
        G4double rp = r2;
        for (int i = 0; i < 8; ++i) {
            zz += mirror_alpha[i] * rp;
            rp *= r2;
        }
        return zz;
    };

    // Log max discretization error
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

    // Build closed (r, z) profile: back surface outward, then front surface inward
    const int n_profile = 2 * (mirror_n_steps + 1);
    std::vector<G4double> mirror_r(n_profile), mirror_z(n_profile);
    for (int i = 0; i <= mirror_n_steps; ++i) {
        G4double r = i * mirror_dr;
        mirror_r[i] = r;
        mirror_z[i] = mirrorSag(r) - mirror_thickness;
    }
    for (int i = 0; i <= mirror_n_steps; ++i) {
        G4double r = (mirror_n_steps - i) * mirror_dr;
        mirror_r[mirror_n_steps + 1 + i] = r;
        mirror_z[mirror_n_steps + 1 + i] = mirrorSag(r);
    }

    auto *SphMirr = new G4GenericPolycone("Mirror", 0.0, CLHEP::twopi,
                                          n_profile, mirror_r.data(), mirror_z.data());
    sphmirr_log = new G4LogicalVolume(SphMirr, Al, "Mirror");
    sphmirr_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, sphmirr_z),
                                     sphmirr_log, "Mirror", expHall_log, false, 0);
// The Mosaic
    [[maybe_unused]] G4bool checkOverlaps = true;
    auto *SphMos = new G4Sphere("Mosaic", sphmos_r, sphmos_R, 0.0 * degree, 360.0 * degree,
                                0.0 * degree, 23.0 * degree);
    auto extent = SphMos->GetExtent();
    sphmos_log = new G4LogicalVolume(SphMos, Al, "Mosaic");
    constexpr G4double pixel_sphere_R = 86.25 * cm;  // radius of best-fit sphere through pixel positions
    auto p = G4ThreeVector(0.0, 0.0, sphmos_z - pixel_sphere_R);
    sphmos_phys = new G4PVPlacement(nullptr, p, sphmos_log, "Mosaic", expHall_log, false, 0);
// Collector geometry parameters
    constexpr G4double collector_hex_radius = 6.0 * mm;
    constexpr G4double collector_base_height = 4.0 * mm;
    constexpr G4double collector_sphere_radius = 17.0 * mm;
    constexpr G4double collector_sphere_center_z = -11.6 * mm;  // relative to PMT face
    constexpr G4double pmt_half_z = 0.1 * mm;

// CollectorBase: solid hexagonal prism (lower part of collector)
    auto collector_base_solid = new G4ExtrudedSolid(
        "CollectorBase",
        makeHexagonVertices(collector_hex_radius),
        collector_base_height / 2.0,
        G4TwoVector(0, 0), 1.0,
        G4TwoVector(0, 0), 1.0
    );

// CollectorLens: spherical cap (upper part)
    constexpr G4double cap_height = collector_sphere_radius + collector_sphere_center_z
                                    - collector_base_height;  // 17 + (-11.6) - 4 = 1.4mm
    constexpr G4double cap_half_height = cap_height / 2.0;    // 0.7mm
    constexpr G4double cap_center_in_sphere = -collector_sphere_center_z
                                              + collector_base_height
                                              + cap_half_height;  // 11.6 + 4 + 0.7 = 16.3mm

    auto collector_sphere = new G4Sphere(
        "CollectorSphere",
        0.0, collector_sphere_radius,
        0.0, 360.0 * degree,
        0.0, 90.0 * degree
    );
    auto collector_hex_slab = new G4ExtrudedSolid(
        "CollectorHexSlab",
        makeHexagonVertices(collector_hex_radius),
        cap_half_height,
        G4TwoVector(0, 0), 1.0,
        G4TwoVector(0, 0), 1.0
    );
    auto collector_lens_solid = new G4IntersectionSolid(
        "CollectorLens",
        collector_sphere,
        collector_hex_slab,
        nullptr,
        G4ThreeVector(0, 0, cap_center_in_sphere)
    );

// Collector = UnionSolid(Base + Lens): single Acrylyl volume, no internal boundary
    // Lens sphere center in base-local frame:
    //   base top = +collector_base_height/2 = +2mm
    //   lens cap bottom = cap_center_in_sphere - cap_half_height = 15.6mm from sphere center
    //   sphere center z = base_top - lens_cap_bottom = 2.0 - 15.6 = -13.6mm
    constexpr G4double sphere_center_z_in_base =
        collector_base_height / 2.0 - (cap_center_in_sphere - cap_half_height);
    auto collector_solid = new G4UnionSolid(
        "Collector",
        collector_base_solid,
        collector_lens_solid,
        nullptr,
        G4ThreeVector(0, 0, sphere_center_z_in_base)
    );

    auto collector_log = new G4LogicalVolume(collector_solid, Acrylyl, "Collector");

// PMT
    auto sphpmt_solid = new G4Box("PMT", 3 * mm, 3 * mm, pmt_half_z);
    sphpmt_log = new G4LogicalVolume(sphpmt_solid, C, "PMT");

// hood
    auto *cam_hood = new G4Tubs("Hood", hood_r, hood_R, hood_hz,
                                0.0, 6.283185307179586 * rad);
    hood_log = new G4LogicalVolume(cam_hood, Al, "Hood");
    constexpr G4double mirror_edge_R = 1106.0 * mm;
    constexpr G4double mirror_rim_z = 1353.4 * mm;    // EVENASPH z(1106)+sphmirr_z = 1353.446mm, delta=0.046mm
    G4double hood_n_hz = mirror_rim_z / 2.0;
    auto cam_hood_n = new G4Cons("hood_n",
        hood_R - 0.01*mm, hood_R,
        mirror_edge_R - 0.01*mm, mirror_edge_R,
        hood_n_hz, 0.0, 2 * M_PI * rad);
    auto hood_n_log = new G4LogicalVolume(cam_hood_n, Al, "Hood_n");
    [[maybe_unused]] auto hood_n_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, hood_n_hz), hood_n_log, "Hood_n", expHall_log, false, 0);
// Corrector — analytical EVENASPH surface (Zemax Configuration A, SURF 3)
    // EVENASPH with CURV=0: z(r) = sum(alpha_i * r^(2i)), pure polynomial
    constexpr G4double cor_alpha[8] = {
        -9.45444322e-5,   // r^2
        +5.61720565e-11,  // r^4
        +3.66970589e-17,  // r^6
        -7.87699703e-23,  // r^8
        +2.77181769e-28,  // r^10
        -4.02397883e-34,  // r^12
        +3.18966763e-40,  // r^14
        -8.49480030e-47   // r^16
    };
    constexpr G4double cor_r_max = 850.0 * mm;
    constexpr G4double cor_thickness = 30.0 * mm; // center thickness (Zemax DISZ)
    constexpr int cor_n_steps = 100;
    constexpr G4double cor_dr = cor_r_max / cor_n_steps;

    auto correctorSag = [&](G4double r) -> G4double {
        const G4double r2 = r * r;
        G4double z = 0.0;
        G4double rp = r2;
        for (int i = 0; i < 8; ++i) {
            z += cor_alpha[i] * rp;
            rp *= r2;
        }
        return z;
    };

    // Log max discretization error
    {
        G4double maxErr = 0.0;
        for (int i = 0; i < cor_n_steps; ++i) {
            G4double r_mid = (i + 0.5) * cor_dr;
            G4double z_true = correctorSag(r_mid);
            G4double z_linear = 0.5 * (correctorSag(i * cor_dr) + correctorSag((i + 1) * cor_dr));
            maxErr = std::max(maxErr, std::abs(z_true - z_linear));
        }
        G4cout << "Corrector polycone max discretization error: " << maxErr / um << " um" << G4endl;
    }

    // Build closed (r, z) profile: flat bottom outward, then shaped top inward
    // Bottom surface: flat at z=0
    // Top surface: z = cor_thickness + correctorSag(r)
    const int cor_n_profile = 2 * (cor_n_steps + 1);
    std::vector<G4double> cor_r(cor_n_profile), cor_z(cor_n_profile);
    for (int i = 0; i <= cor_n_steps; ++i) {
        G4double r = i * cor_dr;
        cor_r[i] = r;
        cor_z[i] = 0.0; // flat bottom
    }
    for (int i = 0; i <= cor_n_steps; ++i) {
        G4double r = (cor_n_steps - i) * cor_dr;
        cor_r[cor_n_steps + 1 + i] = r;
        cor_z[cor_n_steps + 1 + i] = cor_thickness + correctorSag(r);
    }

    auto *CorrSolid = new G4GenericPolycone("Corrector", 0.0, CLHEP::twopi,
                                             cor_n_profile, cor_r.data(), cor_z.data());
    cor_log = new G4LogicalVolume(CorrSolid, Acrylyl, "Corrector");
    cor_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, 0),
                                 cor_log, "Corrector", expHall_log, false, 0);
    auto pos1 = G4ThreeVector(0.0 * cm, 0.0 * cm, 0);
    hood_phys = new G4PVPlacement(nullptr, pos1,
                                  hood_log, "Hood", expHall_log, false, 0);
    fZstart = -0.1 * cm;   // z-coordinate for photon start point
//	------------- Surfaces --------------
    constexpr G4int num = 2;
    G4double Ephoton[num] = {1.500 * eV, 2.00 * eV};
    G4double RefrIndAir[num] = {1.00029, 1.00029};
//OpticalsphmirrSurface
    auto *AirProperties = new G4MaterialPropertiesTable();
    AirProperties->AddProperty("RINDEX", Ephoton, RefrIndAir, num);
    Air->SetMaterialPropertiesTable(AirProperties);
    G4double Refl[num] = {0.88, 0.85};
    G4double Effi[num] = {0.0, 0.0};
    auto *sphmirrSurfProperties =
            new G4MaterialPropertiesTable();
    sphmirrSurfProperties->AddProperty("REFLECTIVITY", Ephoton, Refl, num);
    sphmirrSurfProperties->AddProperty("EFFICIENCY", Ephoton, Effi, num);
    auto *OpsphmirrSurface =
            new G4OpticalSurface("sphmirrSurface", unified, polished, dielectric_metal);
    OpsphmirrSurface->SetMaterialPropertiesTable(sphmirrSurfProperties);
    OpsphmirrSurface->SetPolish(1.0);
    [[maybe_unused]] auto *sphmirrSkinSurface =
            new G4LogicalSkinSurface("sphmirrSkin", sphmirr_log, OpsphmirrSurface);
//OpticalsphmosSurface
    G4double Reflm[num] = {0.0, 0.0};
    G4double Effim[num] = {1.0, 1.0};
    auto *sphmosSurfProperties =
            new G4MaterialPropertiesTable();
    sphmosSurfProperties->AddProperty("REFLECTIVITY", Ephoton, Reflm, num);
    sphmosSurfProperties->AddProperty("EFFICIENCY", Ephoton, Effim, num);
    auto *OpsphmosSurface =
            new G4OpticalSurface("sphmosSurface", unified, polished, dielectric_metal);
    OpsphmosSurface->
            SetMaterialPropertiesTable(sphmosSurfProperties);
    [[maybe_unused]] auto *sphmosSkinSurface =
            new G4LogicalSkinSurface("sphmosSkin", sphmos_log, OpsphmosSurface);
//OpticalsphhoodSurface
    G4double Reflh[num] = {0.0, 0.0};
    G4double Effih[num] = {1.0, 1.0};
    auto *sphhoodSurfProperties =
            new G4MaterialPropertiesTable();
    sphhoodSurfProperties->AddProperty("REFLECTIVITY", Ephoton, Reflh, num);
    sphhoodSurfProperties->AddProperty("EFFICIENCY", Ephoton, Effih, num);
    auto *OpsphhoodSurface =
            new G4OpticalSurface("sphhoodSurface", unified, polished, dielectric_metal);
    OpsphhoodSurface->
            SetMaterialPropertiesTable(sphhoodSurfProperties);
    [[maybe_unused]] auto *sphhoodSkinSurface =
            new G4LogicalSkinSurface("sphhoodSkin", hood_log, OpsphhoodSurface);
//OpticalsphcorSurface
    G4double energy[] = {1.0 * eV, 2.0 * eV, 3.0 * eV, 4.0 * eV, 5.0 * eV};
    G4double rIndex[] = {1.5122, 1.5122, 1.5122, 1.5122, 1.5122};
    G4double absorption[] = {4.0 * m, 4.0 * m, 4.0 * m, 4.0 * m, 4.0 * m};
    auto *mpt = new G4MaterialPropertiesTable();
    mpt->AddProperty("RINDEX", energy, rIndex, 5);
    mpt->AddProperty("ABSLENGTH", energy, absorption, 5);
    mpt->AddConstProperty("RESOLUTIONSCALE", 1.0);
    Acrylyl->SetMaterialPropertiesTable(mpt);
    auto *OpsphcorSurface = new G4OpticalSurface("sphcorSurfac", unified, polished, dielectric_dielectric);
    OpsphcorSurface->SetMaterialPropertiesTable(mpt);
    [[maybe_unused]] auto *sphcorSkinSurface = new G4LogicalSkinSurface("sphcorSkin", cor_log, OpsphcorSurface);
// NOTE: No SkinSurface on collector_log.  PMT SkinSurface (dielectric_metal)
// is picked up as PostStep skin when photon enters PMT from Collector.
// Air↔Collector boundaries use Fresnel from material RINDEX (identical
// to the former polished dielectric_dielectric skin).
//OpticalsphPMTSurface
    G4double Reflp[num] = {0.0, 0.0};
    G4double Effip[num] = {1.0, 1.0};
    auto *sphPMTSurfProperties =
            new G4MaterialPropertiesTable();
    sphPMTSurfProperties->AddProperty("REFLECTIVITY", Ephoton, Reflp, num);
    sphPMTSurfProperties->AddProperty("EFFICIENCY", Ephoton, Effip, num);
    auto *OpsphPMTSurface =
            new G4OpticalSurface("sphPMTSurface", unified, polished, dielectric_metal);
    OpsphPMTSurface->
            SetMaterialPropertiesTable(sphPMTSurfProperties);
    [[maybe_unused]] auto *sphPMTSkinSurface =
            new G4LogicalSkinSurface("sphPMTSkin", sphpmt_log, OpsphPMTSurface);

// Place PMT once as daughter of Collector (all instances inherit it)
    auto pmt_phys = new G4PVPlacement(
        nullptr,
        G4ThreeVector(0, 0, -collector_base_height / 2.0 + pmt_half_z),
        sphpmt_log, "PMT", collector_log, false, 0, false);

// Place Collector for each pixel, with detecting border surface to PMT
    rotationMatrixCache.reserve(2653);

    for (G4int i = 0; i < 2653; i++) {
        const G4double theta = -pix_theta[i];
        const G4double phi = pix_phi[i];
        const G4double cos_theta = cos(theta);
        const G4double sin_theta = sin(theta);
        const G4double cos_phi = cos(phi);
        const G4double sin_phi = sin(phi);
        const G4double ax = -cos_phi;
        const G4double ay = sin_phi;
        const G4double one_minus_cos_theta = 1.0 - cos_theta;
        const G4double ax_sq = ax * ax;
        const G4double ay_sq = ay * ay;
        const G4double ax_ay = ax * ay;

        const auto firstRow = G4ThreeVector(
            cos_theta + one_minus_cos_theta * ax_sq,
            one_minus_cos_theta * ax_ay,
            sin_theta * ay
        );
        const auto secondRow = G4ThreeVector(
            one_minus_cos_theta * ax_ay,
            cos_theta + one_minus_cos_theta * ay_sq,
            -sin_theta * ax
        );
        const auto thirdRow = G4ThreeVector(
            -sin_theta * ay,
            sin_theta * ax,
            cos_theta
        );

        auto rotm = std::make_unique<G4RotationMatrix>();
        rotm->setRows(firstRow, secondRow, thirdRow);
        auto rotm_ptr = rotm.get();
        rotationMatrixCache.push_back(std::move(rotm));

        // Outward normal direction in global frame = R^T * (0,0,1)
        const G4ThreeVector outward_normal(
            firstRow.z(),
            secondRow.z(),
            thirdRow.z()
        );

        // Collector center = base center (base bottom at pixel position)
        constexpr G4double base_center_offset = collector_base_height / 2.0;
        const auto pos_collector = G4ThreeVector(fPixX[i], fPixY[i], fPixZ[i])
                                   + base_center_offset * outward_normal;

        auto collector_phys = new G4PVPlacement(
            rotm_ptr, pos_collector, collector_log, "Collector",
            expHall_log, false, i, false);

        // PMT detection handled by SkinSurface on sphpmt_log (PostStep priority)
    }

    [[maybe_unused]] G4SDManager *SDman = G4SDManager::GetSDMpointer();
    return expHall_phys;
}
