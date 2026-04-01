#include "sphmirrPrimaryGeneratorAction.hh"
#include "FileQueue.hh"
#include "FastBackgroundSampler.hh"
#include "SimConfig.hh"
#include "WorkerEventData.hh"
#include "sphmirrDetectorConstruction.hh"
#include "G4Event.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4RunManager.hh"
#include "Randomize.hh"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <limits>

sphmirrPrimaryGeneratorAction::sphmirrPrimaryGeneratorAction(
        FileQueue* fileQueue,
        WorkerEventData* eventData,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fParticleGun(new G4ParticleGun(1))
    , fFileQueue(fileQueue)
    , fEventData(eventData)
    , fConfig(config)
    , fDetector(detector)
{
    G4ParticleTable* particleTable = G4ParticleTable::GetParticleTable();
    G4ParticleDefinition* particle = particleTable->FindParticle("opticalphoton");
    fParticleGun->SetParticleDefinition(particle);
    fParticleGun->SetParticleEnergy(2.0 * eV);

    // Cache detector orientation trigonometry (read-only after init)
    fCosTheta = std::cos(-fConfig->the);
    fSinTheta = std::sin(-fConfig->the);
    fCosPhi = std::cos(fConfig->phi);
    fSinPhi = std::sin(fConfig->phi);
}

sphmirrPrimaryGeneratorAction::~sphmirrPrimaryGeneratorAction() {
    delete fParticleGun;
}

std::string sphmirrPrimaryGeneratorAction::BuildSuffix(
    const std::string& filename, const std::string& height)
{
    // Strip .phel.zst extension
    std::string name = filename;
    const std::string ext = ".phel.zst";
    if (name.size() > ext.size() &&
        name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
        name = name.substr(0, name.size() - ext.size());
    }

    // Find "Q" marker
    auto qpos = name.find('Q');
    if (qpos == std::string::npos) return name;
    std::string suffix = name.substr(qpos);

    // Remove spaces
    suffix.erase(std::remove(suffix.begin(), suffix.end(), ' '), suffix.end());

    // Insert _{height}m before _cNNN at end
    if (suffix.size() >= 5 && suffix[suffix.size()-5] == '_' && suffix[suffix.size()-4] == 'c') {
        bool allDigits = true;
        for (int i = 3; i >= 1; --i) {
            if (!std::isdigit(suffix[suffix.size()-i])) allDigits = false;
        }
        if (allDigits) {
            suffix.insert(suffix.size()-5, "_" + height + "m");
        }
    }
    return suffix;
}

void sphmirrPrimaryGeneratorAction::GeneratePrimaries(G4Event* anEvent) {
    // Reset per-event data HERE (not in BeginOfEventAction) because
    // Geant4 calls GeneratePrimaries BEFORE BeginOfEventAction.
    fEventData->TotPhot = 0;
    fEventData->NEntry = 0;
    fEventData->tmin = std::numeric_limits<double>::max();
    fEventData->tmax = -std::numeric_limits<double>::max();
    fEventData->inputFileSuffix.clear();
    fEventData->photonData = nullptr;
    // Reset diagnostics
    fEventData->diag_nKilledMirror = 0;
    fEventData->diag_nKilledMosaic = 0;
    fEventData->diag_nKilledBase = 0;
    fEventData->diag_nKilledHood = 0;
    fEventData->diag_nKilledPMT = 0;
    fEventData->diag_nKilledWorld = 0;
    fEventData->diag_nKilledOther = 0;
    fEventData->diag_nLeftWorld = 0;

    // Pop next input file from the thread-safe queue
    std::string filename;
    if (!fFileQueue->Pop(filename)) {
        // No more files -- generate empty event (Geant4 handles gracefully)
        return;
    }

    // Read binary .phel.zst file
    std::string filepath = fConfig->phelsDir + "/" + filename;
    try {
        fCurrentEvent = PhelReader::Read(filepath);
    } catch (const std::runtime_error& e) {
        G4cerr << "PhelReader error: " << e.what() << G4endl;
        return;
    }

    if (fConfig->fastBackgroundEnabled && fCurrentEvent.has_background) {
        G4Exception("sphmirrPrimaryGeneratorAction",
                    "FastBackgroundDoubleInject",
                    FatalException,
                    "Input PHEL already contains background photons; do not combine it with --background-operator");
    }
    if (fConfig->fastBackgroundEnabled && fConfig->backgroundOperator) {
        const std::string compatibility_error = BackgroundOperatorCompatibilityError(
            *fConfig->backgroundOperator,
            fCurrentEvent.catm,
            fCurrentEvent.zz,
            static_cast<float>(fConfig->phi / deg),
            static_cast<float>(fConfig->the / deg));
        if (!compatibility_error.empty()) {
            const std::string message =
                compatibility_error +
                "; operator(catm=" + std::to_string(fConfig->backgroundOperator->header.catm) +
                ", zz=" + std::to_string(fConfig->backgroundOperator->header.zz) +
                ", phi=" + std::to_string(fConfig->backgroundOperator->header.phi) +
                ", the=" + std::to_string(fConfig->backgroundOperator->header.the) +
                "), event/run(catm=" + std::to_string(fCurrentEvent.catm) +
                ", zz=" + std::to_string(fCurrentEvent.zz) +
                ", phi=" + std::to_string(static_cast<float>(fConfig->phi / deg)) +
                ", the=" + std::to_string(static_cast<float>(fConfig->the / deg)) + ")";
            G4Exception("sphmirrPrimaryGeneratorAction",
                        "FastBackgroundOperatorMismatch",
                        FatalException,
                        message.c_str());
        }
    }

    // Extract header data
    fEventData->zz  = fCurrentEvent.zz;
    fEventData->xsh = fCurrentEvent.xsh;
    fEventData->ysh = fCurrentEvent.ysh;
    int heightVal = static_cast<int>(std::abs(fCurrentEvent.zz));
    fEventData->height = std::to_string(heightVal);
    fEventData->inputFileSuffix = BuildSuffix(filename, fEventData->height);
    fEventData->photonData = &fCurrentEvent.photons;
    fEventData->moshitWriter.Begin(fCurrentEvent.zz, fCurrentEvent.xsh, fCurrentEvent.ysh);

    if (fEventData->inputFileSuffix.empty()) {
        G4cout << "WARNING: Invalid filename format: " << filename << G4endl;
        return;
    }

    // Generate primary vertices from photon data
    const G4double zstart = fDetector->GetZstart();
    // Convert zz from meters (raw header) to Geant4 internal units (mm)
    const G4double zz = fCurrentEvent.zz * m;

    int lineCount = 0;
    for (const auto& ph : fCurrentEvent.photons) {
        // Compute entry point in hood opening
        G4double r = 85.0 * cm * std::sqrt(G4UniformRand());
        G4double dzeta = 360.0 * deg * G4UniformRand();
        G4double xi0 = r * std::cos(dzeta);
        G4double yi0 = r * std::sin(dzeta);
        G4double zi0 = 0.0;

        // Rotate to detector frame
        G4double xi = fCosTheta * fCosPhi * xi0 - fSinPhi * yi0 - fSinTheta * fCosPhi * zi0;
        G4double yi = fCosTheta * fSinPhi * xi0 + fCosPhi * yi0 - fSinTheta * fSinPhi * zi0;
        G4double zi = fSinTheta * xi0 + fCosTheta * zi0 + zstart;

        // Direction from snow-level source to entry point
        G4double xgr = ph.x * m;
        G4double ygr = ph.y * m;
        G4double diag = std::sqrt((xi - xgr) * (xi - xgr) + (yi - ygr) * (yi - ygr) + (zz - zi) * (zz - zi));
        G4double pxb = (xi - xgr) / diag;
        G4double pyb = (yi - ygr) / diag;
        G4double pzb = (zi - zz) / diag;

        fParticleGun->SetParticlePosition(G4ThreeVector(xi, yi, zi));
        fParticleGun->SetParticleMomentumDirection(G4ThreeVector(pxb, pyb, pzb));
        fParticleGun->SetParticleTime(ph.t * ns);
        SetOptPhotonPolar();
        fParticleGun->GeneratePrimaryVertex(anEvent);

        lineCount++;
        if (lineCount % 1000000 == 0) {
            G4cout << "[" << filename << "] Loaded " << lineCount << " photons." << G4endl;
        }
    }

    G4cout << "Event " << anEvent->GetEventID()
           << ": loaded " << lineCount << " photons from " << filename << G4endl;
}

void sphmirrPrimaryGeneratorAction::SetOptPhotonPolar() const {
    const G4double angle = G4UniformRand() * 360.0 * deg;
    SetOptPhotonPolar(angle);
}

void sphmirrPrimaryGeneratorAction::SetOptPhotonPolar(const G4double angle) const {
    const G4ThreeVector normal(1., 0., 0.);
    const G4ThreeVector kphoton = fParticleGun->GetParticleMomentumDirection();
    const G4ThreeVector product = normal.cross(kphoton);
    const G4double modul2 = product * product;
    G4ThreeVector e_perpend(0., 0., 1.);
    if (modul2 > 0.) e_perpend = (1. / std::sqrt(modul2)) * product;
    const G4ThreeVector e_paralle = e_perpend.cross(kphoton);
    const G4ThreeVector polar = std::cos(angle) * e_paralle + std::sin(angle) * e_perpend;
    fParticleGun->SetParticlePolarization(polar);
}
