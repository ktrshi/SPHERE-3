#include "sphmirrSteppingAction.hh"
#include "sphmirrDetectorConstruction.hh"
#include "WorkerEventData.hh"
#include "SimConfig.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4VPhysicalVolume.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <filesystem>

sphmirrSteppingAction::sphmirrSteppingAction(
        WorkerEventData* eventData,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fEventData(eventData), fConfig(config), fDetector(detector)
{
    InitializePixelCache();
}

sphmirrSteppingAction::~sphmirrSteppingAction() = default;

void sphmirrSteppingAction::InitializePixelCache() {
    const G4double* px = fDetector->GetPixX();
    const G4double* py = fDetector->GetPixY();
    const G4double* pz = fDetector->GetPixZ();
    pixelCache.assign(sphmirrDetectorConstruction::kNPixels, {});
    for (G4int i = 0; i < sphmirrDetectorConstruction::kNPixels; i++) {
        const G4double r = std::sqrt(px[i]*px[i] + py[i]*py[i] + pz[i]*pz[i]);
        const G4double inv_r = 1.0 / r;
        pixelCache[i] = {inv_r, px[i]*inv_r, py[i]*inv_r, pz[i]*inv_r};
    }
}

void sphmirrSteppingAction::UserSteppingAction(const G4Step* aStep) {
    const G4Track* fTrack = aStep->GetTrack();
    if (fTrack->GetDefinition()->GetParticleName() != "opticalphoton") return;

    const G4StepPoint* thePostPoint = aStep->GetPostStepPoint();
    const G4VPhysicalVolume* thePostPV = thePostPoint->GetPhysicalVolume();

    // --- Diagnostic: track where optical photons die ---
    if (!thePostPV) {
        fEventData->diag_nLeftWorld++;
        return;
    }
    const G4String& thePostPVname = thePostPV->GetName();
    if (fTrack->GetTrackStatus() == fStopAndKill) {
        if      (thePostPVname == "Mirror")        fEventData->diag_nKilledMirror++;
        else if (thePostPVname == "Mosaic")        fEventData->diag_nKilledMosaic++;
        else if (thePostPVname == "Collector" ||
                 thePostPVname == "CollectorBase") fEventData->diag_nKilledBase++;
        else if (thePostPVname == "CollectorLens") fEventData->diag_nKilledLens++;
        else if (thePostPVname == "PMT")           fEventData->diag_nKilledPMT++;
        else if (thePostPVname == "Hood" || thePostPVname == "Hood_n") fEventData->diag_nKilledHood++;
        else if (thePostPVname == "World")         fEventData->diag_nKilledWorld++;
        else                                        fEventData->diag_nKilledOther++;
    }
    // --- End diagnostic ---

    if (thePostPVname != "PMT") return;

    const G4TouchableHandle theTouchable = thePostPoint->GetTouchableHandle();
    const G4int copyNo = theTouchable->GetCopyNumber(1);
    const auto& cache = pixelCache[copyNo];

    const G4double dirx = thePostPoint->GetMomentumDirection().x();
    const G4double diry = thePostPoint->GetMomentumDirection().y();
    const G4double dirz = thePostPoint->GetMomentumDirection().z();
    const G4double dot = cache.u * dirx + cache.v * diry + cache.w * dirz;

    if (dot < 0.0) {
        const G4double glt = fTrack->GetGlobalTime();
        if (fEventData->tmin > glt) fEventData->tmin = glt;
        if (fEventData->tmax < glt) fEventData->tmax = glt;

        const G4int cluster_num = copyNo / 7;
        const G4int pix_num = copyNo - 7 * cluster_num;
        if (pix_num >= 7 || cluster_num >= 379) return;

        const G4double pp = std::clamp(-dot, 0.0, 1.0);
        if (pp > 0.0 && G4UniformRand() < std::pow(pp, fConfig->p1)) {
            // Look up per-photon metadata by trackID
            const G4int trackID = fTrack->GetTrackID();
            const PhotonMeta* meta = nullptr;
            if (trackID >= 1 && trackID <= static_cast<G4int>(fEventData->photonMeta.size())) {
                meta = &fEventData->photonMeta[trackID - 1];
            }
            // Fallback for secondary optical photons: use parent
            if (!meta) {
                const G4int parentID = fTrack->GetParentID();
                if (parentID >= 1 && parentID <= static_cast<G4int>(fEventData->photonMeta.size())) {
                    meta = &fEventData->photonMeta[parentID - 1];
                }
            }
            if (!meta) return;  // shouldn't happen, but guard

            // Lazy file opening: open moshits on first detection
            if (!fEventData->moshits.is_open() && !fEventData->inputFileSuffix.empty()) {
                const std::string outputDir = fConfig->outputDir;
                if (!std::filesystem::exists(outputDir))
                    std::filesystem::create_directories(outputDir);
                fEventData->moshits.open(outputDir + "/moshits_" + fEventData->inputFileSuffix, std::ios::out);
                fEventData->moshits << fEventData->headerLine << '\n';
            }

            const G4double x = thePostPoint->GetPosition().x();
            const G4double y = thePostPoint->GetPosition().y();
            const G4double z = thePostPoint->GetPosition().z();

            fEventData->NEntry++;
            fEventData->moshits << std::setw(5) << cluster_num << std::setw(5) << pix_num
                    << std::setw(14) << x / m << std::setw(14) << y / m
                    << std::setw(14) << z / m << std::setw(14) << glt / ns
                    << std::setw(14) << dirx << std::setw(14) << diry
                    << std::setw(14) << dirz << std::setw(5) << meta->origin
                    << std::setw(5) << meta->ii << std::setw(5) << meta->jj
                    << std::setw(5) << meta->kk
                    << std::setw(14) << meta->xx << std::setw(14) << meta->yy
                    << std::setw(14) << meta->t0
                    << '\n';

            // Crosstalk simulation: each of 6 neighbors fires independently with 7% probability
            constexpr G4double crosstalk_prob = 0.07;
            for (int n = 1; n <= 6; n++) {
                if (G4UniformRand() < crosstalk_prob) {
                    const G4int neighbor_pix = (pix_num + n) % 7;
                    fEventData->NEntry++;
                    fEventData->moshits << std::setw(5) << cluster_num << std::setw(5) << neighbor_pix
                            << std::setw(14) << x / m << std::setw(14) << y / m
                            << std::setw(14) << z / m << std::setw(14) << glt / ns
                            << std::setw(14) << dirx << std::setw(14) << diry
                            << std::setw(14) << dirz << std::setw(5) << meta->origin
                            << std::setw(5) << meta->ii << std::setw(5) << meta->jj
                            << std::setw(5) << meta->kk
                            << std::setw(14) << meta->xx << std::setw(14) << meta->yy
                            << std::setw(14) << meta->t0
                            << '\n';
                }
            }
        }
    }

    fEventData->TotPhot++;
}
