#ifndef sphmirrDetectorConstruction_h
#define sphmirrDetectorConstruction_h 1
#include "globals.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4RotationMatrix.hh"
#include <vector>
#include <memory>

class G4LogicalVolume;
class G4VPhysicalVolume;

class sphmirrDetectorConstruction final : public G4VUserDetectorConstruction
{
  public:
    sphmirrDetectorConstruction();
   ~sphmirrDetectorConstruction() override;
    G4VPhysicalVolume* Construct() override;
  private:
    // Cache for rotation matrices to avoid repeated allocations
    std::vector<std::unique_ptr<G4RotationMatrix>> rotationMatrixCache;
    G4double expHall_z;
    G4double expHall_r;
    [[maybe_unused]] G4double sphmirr_r{};
    G4double sphmirr_z{};
    G4double sphmos_r;
    G4double sphmos_R;
    G4double sphmos_z{};
    G4double hood_r;
    G4double hood_R;
    G4double hood_hz;
    G4LogicalVolume* expHall_log{};
    G4LogicalVolume* sphmirr_log{};
    G4LogicalVolume* sphmos_log{};
    G4LogicalVolume* sphpmt_log{};
    G4LogicalVolume* hood_log{};
    G4LogicalVolume* cor_log{};
    G4VPhysicalVolume* expHall_phys{};
    [[maybe_unused]] G4VPhysicalVolume* sphmirr_phys{};
    [[maybe_unused]] G4VPhysicalVolume* sphmos_phys{};
    [[maybe_unused]] G4VPhysicalVolume* sphpmt_phys{};
    [[maybe_unused]] G4VPhysicalVolume* hood_phys{};
    [[maybe_unused]] G4VPhysicalVolume* cor_phys{};

// --- Public const accessors for pixel geometry (MT-safe: read-only after Construct) ---
public:
    const G4double* GetPixX() const { return fPixX; }
    const G4double* GetPixY() const { return fPixY; }
    const G4double* GetPixZ() const { return fPixZ; }
    G4double GetZstart() const { return fZstart; }
    static constexpr G4int kNPixels = 2653;

private:
    G4double fPixX[2653]{};
    G4double fPixY[2653]{};
    G4double fPixZ[2653]{};
    G4double fZstart{0.0};
};
#endif /*sphmirrDetectorConstruction_h*/
