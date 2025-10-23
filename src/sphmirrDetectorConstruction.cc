#include <G4VisExtent.hh>
#include <G4Cons.hh>
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
#include "G4SubtractionSolid.hh"
#include "G4NistManager.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4Transform3D.hh"
#include "G4RotationMatrix.hh"
#include "G4SDManager.hh"
#include "G4VisAttributes.hh"
#include "G4Colour.hh"
#include "G4SystemOfUnits.hh"
#include "CADMesh.hh"
#include "G4Polyhedra.hh"

[[maybe_unused]] extern G4double zdetshift, zmoscent, Rmos, zstart;
[[maybe_unused]] extern G4double xpmt[379], ypmt[379], zpmt[379];   // PMT centers' coordinates, mm
extern G4String AbsolutePath;
std::ifstream pixel_data;
extern G4double rs[379], angles[379];     // segments
extern G4double rc[7], anglec[7];
extern G4double pix_x[2653], pix_y[2653], pix_z[2653]; // pixels' coordinates
extern G4double pix_phi[2653], pix_theta[2653]; // pixels' angles// pixels in a segment

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
    while (pixel_data >> x >> y >> z >> tmp >> tmp >> tmp >> phi >> theta) {
        static G4int i = 0;
        pix_x[i] = x * mm;
        pix_y[i] = y * mm;
        pix_z[i] = z * mm;
        pix_phi[i] = phi * rad;
        pix_theta[i] = theta * rad;
        i++;
    }
    expHall_z = 4.0 * m;
    expHall_r = 2.0 * m;
    sphmirr_z = 1751.41 * mm;
    sphmos_r = 86.25 * cm;
    sphmos_R = 110.0 * cm;
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
// The Mirror
    auto *visAttr = new G4VisAttributes(G4Colour(0.5, 0.5, 0.5));
    auto mesh = CADMesh::TessellatedMesh::FromSTL(AbsolutePath + "/configs/mirror_test.stl");
    auto SphMirr = mesh->GetSolid();
    sphmirr_log = new G4LogicalVolume(SphMirr, Al, "Mirror");
    sphmirr_log->SetVisAttributes(visAttr);
    sphmirr_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, sphmirr_z),
                                     sphmirr_log, "Mirror", expHall_log, false, 0);
// The Mosaic
    [[maybe_unused]] G4bool checkOverlaps = true;
    auto *SphMos = new G4Sphere("Mosaic", sphmos_r, sphmos_R, 0.0 * degree, 360.0 * degree,
                                0.0 * degree, 21.0 * degree);
    auto extent = SphMos->GetExtent();
    sphmos_log = new G4LogicalVolume(SphMos, Al, "Mosaic");
    auto p = G4ThreeVector(0.0, 0.0, sphmos_z - sphmos_R - 1.5*cm);
    sphmos_phys = new G4PVPlacement(nullptr, p, sphmos_log, "Mosaic", expHall_log, false, 0);
// The PMTs
    auto sphpmtcol_mesh = CADMesh::TessellatedMesh::FromSTL(AbsolutePath + "/configs/collector_test.stl");
    auto sphpmtcol_solid = sphpmtcol_mesh->GetSolid();
    auto visAttrpmt = new G4VisAttributes(G4Colour(0.4, 0.4, 0.4));
    auto visAttrpmm = new G4VisAttributes(G4Colour(0.4, 0.4, 0.4));
    visAttr->SetVisibility(false);
    auto sphpmtcol_log = new G4LogicalVolume(sphpmtcol_solid, Acrylyl, "Collector");
    sphpmtcol_log->SetVisAttributes(visAttrpmt);
    auto sphpmt_solid = new G4Box("PMT", 3 * mm, 3 * mm, 0.1 * mm);
    sphpmt_log = new G4LogicalVolume(sphpmt_solid, C, "PMT");
    sphpmt_log -> SetVisAttributes(visAttrpmm);
    G4double outer_radius = 7.11*mm;
    G4double inner_radius = 7.10*mm;
    G4double height = 2.5*mm;
    auto outer_hex = new G4ExtrudedSolid(
    "OuterHex",
    makeHexagonVertices(outer_radius),
    height,
    G4TwoVector(0,0), 1.0,
    G4TwoVector(0,0), 1.0
    );
    auto inner_hex = new G4ExtrudedSolid(
        "InnerHex",
        makeHexagonVertices(inner_radius),
        height,
        G4TwoVector(0,0), 1.0,
        G4TwoVector(0,0), 1.0
    );
    // Create the hollow shell by subtracting the inner from outer
    auto hollow_hex = new G4SubtractionSolid(
        "HollowHex",
        outer_hex,    // Larger hexagon
        inner_hex     // Smaller hexagon to subtract
    );
    auto shell_log = new G4LogicalVolume(
    hollow_hex,
    Al,
    "CollectorShell"
    );
    shell_log->SetVisAttributes(visAttrpmt);

    // Pre-allocate rotation matrices to avoid repeated allocations
    rotationMatrixCache.reserve(2653 * 2);

    for (G4int i = 0; i < 2653; i++) {
    // for (i = 0; i < 7*7; i++) {
        // Precompute trigonometric values
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

        // Build rotation matrix rows with precomputed values
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

        // Create and cache rotation matrices
        auto rotm = std::make_unique<G4RotationMatrix>();
        rotm->setRows(firstRow, secondRow, thirdRow);
        auto rotm_ptr = rotm.get();
        rotationMatrixCache.push_back(std::move(rotm));

        auto rotm_alt = std::make_unique<G4RotationMatrix>();
        rotm_alt->setRows(firstRow, secondRow, thirdRow);
        rotm_alt->rotateZ(30*degree);
        auto rotm_alt_ptr = rotm_alt.get();
        rotationMatrixCache.push_back(std::move(rotm_alt));

        // Create positions
        const auto pos_pmt = G4ThreeVector(pix_x[i], pix_y[i], pix_z[i]);
        const auto pos_col = G4ThreeVector(pix_x[i], pix_y[i], pix_z[i] + 0.1 * mm);
        const auto pos_shield = G4ThreeVector(pix_x[i], pix_y[i], pix_z[i] + 2.0 * mm);

        // Place volumes using cached rotation matrices
        sphpmt_phys = new G4PVPlacement(rotm_ptr, pos_pmt, sphpmt_log, "PMT", expHall_log, false, i);
        [[maybe_unused]] auto sphpmtcol_phys = new G4PVPlacement(rotm_ptr, pos_col, sphpmtcol_log, "Collector", expHall_log, false, i);
        [[maybe_unused]] auto sphpmt_tube_phys = new G4PVPlacement(rotm_alt_ptr, pos_shield, shell_log, "Shield", expHall_log, false, i);
    }
// hood
    auto *cam_hood = new G4Tubs("Hood", hood_r, hood_R, hood_hz,
                                0.0, 6.283185307179586 * rad);
    hood_log = new G4LogicalVolume(cam_hood, Al, "Hood");
    auto cam_hood_n = new G4Cons("hood_n", hood_R-0.01*mm, hood_R, sphmos_R-0.01*mm, sphmos_R, (1354.0 / 2)*mm, 0.0, 6.283185307179586 * rad);
    auto hood_n_log = new G4LogicalVolume(cam_hood_n, Al, "Hood_n");
    auto rotm_h = new G4RotationMatrix();
    rotm_h->rotateZ(180*degree);
    [[maybe_unused]] auto hood_n_phys = new G4PVPlacement(rotm_h, G4ThreeVector(0.0, 0.0, (1354.0 / 2)*mm), hood_n_log, "Hood_n", expHall_log, false, 0);
// Corrector
    visAttr = new G4VisAttributes(G4Colour(0.5, 0.5, 0.9));
    auto mesh_ = CADMesh::TessellatedMesh::FromSTL(AbsolutePath + "/configs/corrector_A-.stl");
    auto corout = mesh_->GetSolid();
    cor_log = new G4LogicalVolume(corout, Acrylyl, "Сorrector");
    cor_log->SetVisAttributes(visAttr);
    // cor_phys = new G4PVPlacement(nullptr, G4ThreeVector(0.0, 0.0, 0),
    //                              cor_log, "Corrector", expHall_log, false, 0);
    auto pos1 = G4ThreeVector(0.0 * cm, 0.0 * cm, 0);
    hood_phys = new G4PVPlacement(nullptr, pos1,
                                  hood_log, "Hood", expHall_log, false, 0);
    zstart = -0.1 * cm;   // z-coordinate for photon start point
//	------------- Surfaces --------------
    constexpr G4int num = 2;
    G4double Ephoton[num] = {1.500 * eV, 2.00 * eV};
    G4double RefrIndAir[num] = {1.00029, 1.00029};
//OpticalsphmirrSurface
    auto *AirProperties = new G4MaterialPropertiesTable();
    AirProperties->AddProperty("RINDEX", Ephoton, RefrIndAir, num);
    Air->SetMaterialPropertiesTable(AirProperties);
    G4double Refl[num] = {1.0, 1.0};
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
    [[maybe_unused]] auto *sphpmtcolSkinSurface = new G4LogicalSkinSurface("sphcorSkin", sphpmtcol_log, OpsphcorSurface);
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
    [[maybe_unused]] G4SDManager *SDman = G4SDManager::GetSDMpointer();
    return expHall_phys;
}