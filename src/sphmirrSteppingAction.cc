#include "sphmirrSteppingAction.hh"
#include "sphmirrCounters.hh"
#include "sphmirrDetectorConstruction.hh"
#include "sphmirrEventAction.hh"
#include "G4SteppingManager.hh"
#include "G4Track.hh"
#include "G4TrackVector.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4VPhysicalVolume.hh"
#include "G4ThreeVector.hh"
#include "G4ProcessManager.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4RunManager.hh"
#include "G4UImanager.hh"
#include "G4SystemOfUnits.hh"
#include <algorithm>

extern std::ofstream moshits;
[[maybe_unused]] extern G4int DirGrp;
extern G4double xgr, ygr;   // coordinates of the starting point of a photon in meters
[[maybe_unused]] extern G4double xax, yax;   // coordinates of the shower axis position
extern G4double xx, yy, t0;   // starting coordinates and time of a photon at snow level
[[maybe_unused]] extern G4double costh;
extern G4int TotPhot;             // total number of tracked photons that entered PMTs
extern G4int NEntry;       // number of tracked photons that entered PMTs within the time window
extern G4double tmin;      // minimum delay at mosaic, ns
extern G4double tmax;      // maximum delay at mosaic, ns
[[maybe_unused]] extern G4double tbig;      // delay term due to EAS fly to the ground, ns
extern G4double p1;        // sensitivity(xi) = cos(xi)^p1
extern G4int origin;      // phel origin tag: 1 - CL, 2 - BG
extern int phl_CloneNum, phl_ii, phl_jj, phl_kk, phl_mmm;
extern double phl_xx, phl_yy, phl_t0;
extern G4double pix_x[2653], pix_y[2653], pix_z[2653]; // pixels' coordinates

static const auto ZHat = G4ThreeVector(0.0, 0.0, 1.0);

sphmirrSteppingAction::sphmirrSteppingAction(sphmirrDetectorConstruction *det,
                                             EventAction *evt)
        : detector(det), eventaction(evt) {
    InitializePixelCache();
}

sphmirrSteppingAction::~sphmirrSteppingAction() = default;

void sphmirrSteppingAction::InitializePixelCache() {
    // Precompute normalized vectors for all 2653 pixels to avoid sqrt in hot path
    pixelCache.assign(2653, {});
    for (G4int cluster = 0; cluster < 379; cluster++) {
        for (G4int pixel = 0; pixel < 7; pixel++) {
            const G4int copyNo = cluster * 7 + pixel;
            if (copyNo < 2653) {
                const G4double x = pix_x[copyNo];
                const G4double y = pix_y[copyNo];
                const G4double z = pix_z[copyNo];
                const G4double r = sqrt(x * x + y * y + z * z);
                const G4double inv_r = 1.0 / r;

                PixelNormalCache cache;
                cache.inv_r = inv_r;
                cache.u = x * inv_r;
                cache.v = y * inv_r;
                cache.w = z * inv_r;
                pixelCache[copyNo] = cache;
            }
        }
    }
}
void sphmirrSteppingAction::UserSteppingAction(const G4Step *aStep) {
    const G4Track *fTrack = aStep->GetTrack();
    const G4StepPoint *thePrePoint = aStep->GetPreStepPoint();
    const G4StepPoint *thePostPoint = aStep->GetPostStepPoint();
    const G4VPhysicalVolume *thePrePV = thePrePoint->GetPhysicalVolume();
    const G4VPhysicalVolume *thePostPV = thePostPoint->GetPhysicalVolume();
    G4String thePrePVname = " ";
    G4String thePostPVname = " ";
    if (thePostPV) {
        thePrePVname = thePrePV->GetName();
        thePostPVname = thePostPV->GetName();
    }
    const G4double dirx = thePostPoint->GetMomentumDirection().x();
    const G4double diry = thePostPoint->GetMomentumDirection().y();
    const G4double dirz = thePostPoint->GetMomentumDirection().z();
    const G4double z = thePostPoint->GetPosition().z();
    const G4double x = thePostPoint->GetPosition().x();
    const G4double y = thePostPoint->GetPosition().y();
    if (const G4String PartName = aStep->GetTrack()->GetDefinition()->GetParticleName(); PartName == "opticalphoton") {
        if (thePostPVname == "PMT") {
            const G4TouchableHandle theTouchable = thePostPoint->GetTouchableHandle();
            const G4int copyNo = theTouchable->GetCopyNumber();
            const auto& cache = pixelCache[copyNo];
            // Check photon travels toward PMT face using local pixel normal
            const G4double dot = cache.u * dirx + cache.v * diry + cache.w * dirz;
            if (dot < 0.0) {
            const G4double glt = fTrack->GetGlobalTime();
            if (tmin > glt) tmin = glt;
            if (tmax < glt) tmax = glt;
            if (tminAll > glt) tminAll = glt;
            if (tmaxAll < glt) tmaxAll = glt;
            const G4int cluster_num = copyNo / 7;
            if (const G4int pix_num = copyNo - 7 * cluster_num; (pix_num < 7) && (cluster_num < 379)) {
                // Clamp projection to [0,1] for sensitivity calculation
                const G4double pp = std::clamp(-dot, 0.0, 1.0);
                if (pp > 0.0 && G4UniformRand() < pow(pp, p1)) {     //  photon is detected (phel produced)
                    // Initial detection
                    NEntry++;
                    NEntryTotal++;
                    moshits << std::setw(5) << cluster_num << std::setw(5) << pix_num
                            << std::setw(14) << x / m << std::setw(14) << y / m
                            << std::setw(14) << z / m << std::setw(14) << glt / ns
                            << std::setw(14) << dirx << std::setw(14) << diry
                            << std::setw(14) << dirz << std::setw(5) << origin
                            << std::setw(5) << phl_ii << std::setw(5) << phl_jj << std::setw(5) << phl_kk
                            << std::setw(14) << phl_xx << std::setw(14) << phl_yy << std::setw(14) << phl_t0
                            << '\n';

                    // Crosstalk simulation: each of 6 neighbors fires independently with 7% probability
                    // Electronic crosstalk — no angular sensitivity filter applied
                    constexpr G4double crosstalk_prob = 0.07;
                    for (int n = 1; n <= 6; n++) {
                        if (G4UniformRand() < crosstalk_prob) {
                            const G4int neighbor_pix = (pix_num + n) % 7;
                            NEntry++;
                            NEntryTotal++;
                            moshits << std::setw(5) << cluster_num << std::setw(5) << neighbor_pix
                                    << std::setw(14) << x / m << std::setw(14) << y / m
                                    << std::setw(14) << z / m << std::setw(14) << glt / ns
                                    << std::setw(14) << dirx << std::setw(14) << diry
                                    << std::setw(14) << dirz << std::setw(5) << origin
                                    << std::setw(5) << phl_ii << std::setw(5) << phl_jj << std::setw(5) << phl_kk
                                    << std::setw(14) << phl_xx << std::setw(14) << phl_yy << std::setw(14) << phl_t0
                                    << '\n';
                        }
                    }
                }
            } else {
            }
            TotPhot++;
            TotPhotTotal++;
            }
        }
    }
}
