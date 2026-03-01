#include "sphmirrSteppingAction.hh"
#include "sphmirrDetectorConstruction.hh"
#include "WorkerEventData.hh"
#include "SimConfig.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4VPhysicalVolume.hh"
#include "G4LogicalVolume.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4OpticalPhoton.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"
#include <algorithm>
#include <cmath>
#include <cstdio>

sphmirrSteppingAction::sphmirrSteppingAction(
        WorkerEventData* eventData,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fEventData(eventData), fConfig(config), fDetector(detector)
{
    InitializePixelCache();
    InitializeVolumeCache();
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

void sphmirrSteppingAction::InitializeVolumeCache() {
    fOpticalPhoton = G4OpticalPhoton::OpticalPhotonDefinition();
    auto* store = G4LogicalVolumeStore::GetInstance();
    fPmtLog       = store->GetVolume("PMT");
    fMirrorLog    = store->GetVolume("Mirror");
    fMosaicLog    = store->GetVolume("Mosaic");
    fCollectorLog = store->GetVolume("Collector");
    fHoodLog      = store->GetVolume("Hood");
    fHoodNLog     = store->GetVolume("Hood_n");
    fWorldLog     = store->GetVolume("World");
}

// Format one detection line into buf. Returns number of chars written.
static int FormatDetectionLine(char* buf, size_t bufsize,
    int cluster, int pix,
    double x, double y, double z, double t,
    double dx, double dy, double dz,
    int origin, int ii, int jj, int kk,
    double xx, double yy, double t0)
{
    return std::snprintf(buf, bufsize,
        "%5d%5d%14g%14g%14g%14g%14g%14g%14g%5d%5d%5d%5d%14g%14g%14g\n",
        cluster, pix, x, y, z, t, dx, dy, dz, origin, ii, jj, kk, xx, yy, t0);
}

void sphmirrSteppingAction::UserSteppingAction(const G4Step* aStep) {
    const G4Track* fTrack = aStep->GetTrack();
    if (fTrack->GetDefinition() != fOpticalPhoton) return;

    const G4StepPoint* thePostPoint = aStep->GetPostStepPoint();
    const G4VPhysicalVolume* thePostPV = thePostPoint->GetPhysicalVolume();

    // --- Diagnostic: track where optical photons die ---
    if (!thePostPV) {
        fEventData->diag_nLeftWorld++;
        return;
    }
    const G4LogicalVolume* postLog = thePostPV->GetLogicalVolume();
    if (fTrack->GetTrackStatus() == fStopAndKill) {
        if      (postLog == fMirrorLog)                      fEventData->diag_nKilledMirror++;
        else if (postLog == fMosaicLog)                      fEventData->diag_nKilledMosaic++;
        else if (postLog == fCollectorLog)                   fEventData->diag_nKilledBase++;
        else if (postLog == fPmtLog)                         fEventData->diag_nKilledPMT++;
        else if (postLog == fHoodLog || postLog == fHoodNLog) fEventData->diag_nKilledHood++;
        else if (postLog == fWorldLog)                       fEventData->diag_nKilledWorld++;
        else                                                  fEventData->diag_nKilledOther++;
    }
    // --- End diagnostic ---

    if (postLog != fPmtLog) return;

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
                fEventData->moshits.rdbuf()->pubsetbuf(fEventData->iobuf, sizeof(fEventData->iobuf));
                fEventData->moshits.open(fConfig->outputDir + "/moshits_" + fEventData->inputFileSuffix, std::ios::out);
                fEventData->moshits << fEventData->headerLine << '\n';
            }

            const G4double x_m = thePostPoint->GetPosition().x() / m;
            const G4double y_m = thePostPoint->GetPosition().y() / m;
            const G4double z_m = thePostPoint->GetPosition().z() / m;
            const G4double glt_ns = glt / ns;

            char line[256];
            fEventData->NEntry++;
            int n = FormatDetectionLine(line, sizeof(line),
                cluster_num, pix_num,
                x_m, y_m, z_m, glt_ns,
                dirx, diry, dirz,
                meta->origin, meta->ii, meta->jj, meta->kk,
                meta->xx, meta->yy, meta->t0);
            fEventData->moshits.write(line, n);

            // Crosstalk simulation: each of 6 neighbors fires independently with 7% probability
            constexpr G4double crosstalk_prob = 0.07;
            for (int nb = 1; nb <= 6; nb++) {
                if (G4UniformRand() < crosstalk_prob) {
                    const G4int neighbor_pix = (pix_num + nb) % 7;
                    fEventData->NEntry++;
                    n = FormatDetectionLine(line, sizeof(line),
                        cluster_num, neighbor_pix,
                        x_m, y_m, z_m, glt_ns,
                        dirx, diry, dirz,
                        meta->origin, meta->ii, meta->jj, meta->kk,
                        meta->xx, meta->yy, meta->t0);
                    fEventData->moshits.write(line, n);
                }
            }
        }
    }

    fEventData->TotPhot++;
}
