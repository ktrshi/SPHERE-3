#include "sphmirrPrimaryGeneratorAction.hh"
#include "FileQueue.hh"
#include "SimConfig.hh"
#include "WorkerEventData.hh"
#include "sphmirrDetectorConstruction.hh"
#include "G4Event.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4RunManager.hh"
#include "Randomize.hh"
#include <fstream>
#include <sstream>
#include <cmath>
#include <optional>
#include <cstdlib>
#include <cstdio>
#include <cctype>

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

bool sphmirrPrimaryGeneratorAction::ParseHeader(const std::string& headerLine) {
    auto safeParse = [](const std::string& s) -> std::optional<double> {
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0') return std::nullopt;
        return v;
    };

    std::istringstream iss(headerLine);
    std::vector<std::string> values;
    std::string value;
    while (iss >> value) values.push_back(value);

    if (values.size() >= 4) {
        if (auto vx = safeParse(values[3])) fEventData->xsh = *vx * m;
        if (auto vy = safeParse(values[2])) fEventData->ysh = *vy * m;
        if (auto vz = safeParse(values[1])) fEventData->zz = *vz * m;
    }
    if (values.size() >= 2) {
        if (auto vh = safeParse(values[1])) {
            const int h_int = static_cast<int>(std::lround(std::fabs(*vh)));
            fEventData->height = std::to_string(h_int);
        }
    }
    return true;
}

std::string sphmirrPrimaryGeneratorAction::BuildSuffix(const std::string& inputFilename) {
    size_t pos = inputFilename.find("Q");
    if (pos == std::string::npos) return {};
    std::string params = inputFilename.substr(pos);
    std::erase(params, ' ');

    // Match "_cNNN" (exactly 3 digits) at end of string — replaces std::regex
    if (params.size() >= 5) {
        const size_t cpos = params.size() - 5;
        if (params[cpos] == '_' && params[cpos + 1] == 'c' &&
            std::isdigit(static_cast<unsigned char>(params[cpos + 2])) &&
            std::isdigit(static_cast<unsigned char>(params[cpos + 3])) &&
            std::isdigit(static_cast<unsigned char>(params[cpos + 4]))) {
            return params.substr(0, cpos) + "_" + fEventData->height + "m" + params.substr(cpos);
        }
    }
    return params;
}

void sphmirrPrimaryGeneratorAction::GeneratePrimaries(G4Event* anEvent) {
    // Reset per-event data HERE (not in BeginOfEventAction) because
    // Geant4 calls GeneratePrimaries BEFORE BeginOfEventAction.
    fEventData->TotPhot = 0;
    fEventData->NEntry = 0;
    fEventData->tmin = std::numeric_limits<G4double>::max();
    fEventData->tmax = 0.0;
    fEventData->inputFileSuffix.clear();
    fEventData->photonMeta.clear();
    // Reset diagnostics
    fEventData->diag_nKilledMirror = 0;
    fEventData->diag_nKilledMosaic = 0;
    fEventData->diag_nKilledBase = 0;
    fEventData->diag_nKilledLens = 0;
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

    // Open input file (local variable -- no shared state)
    std::ifstream inpho(fConfig->phelsDir + "/" + filename);
    if (!inpho.is_open()) {
        G4cout << "WARNING: Cannot open " << filename << ", skipping." << G4endl;
        return;
    }

    // Read and parse header line
    std::string headerLine;
    if (!std::getline(inpho, headerLine)) {
        G4cout << "WARNING: Empty file " << filename << ", skipping." << G4endl;
        return;
    }
    fEventData->headerLine = headerLine;
    ParseHeader(headerLine);

    // Build output suffix
    fEventData->inputFileSuffix = BuildSuffix(filename);
    if (fEventData->inputFileSuffix.empty()) {
        G4cout << "WARNING: Invalid filename format: " << filename << G4endl;
        return;
    }

    // Read all photon lines and create primary vertices
    fEventData->photonMeta.clear();
    fEventData->photonMeta.reserve(100000);
    const G4double zstart = fDetector->GetZstart();
    const G4double zz = fEventData->zz;

    std::string line;
    line.reserve(128);
    int lineCount = 0;
    while (std::getline(inpho, line)) {
        int ii, jj, kk, mmm;
        double xx, yy, t0;
        if (std::sscanf(line.c_str(), "%d %d %d %d %lf %lf %lf",
                         &ii, &jj, &kk, &mmm, &xx, &yy, &t0) != 7) {
            G4cout << "WARNING: Skipping malformed line: " << line << G4endl;
            continue;
        }

        // Store per-photon metadata for SteppingAction lookup
        PhotonMeta meta;
        meta.ii = ii; meta.jj = jj; meta.kk = kk; meta.mmm = mmm;
        meta.xx = xx; meta.yy = yy; meta.t0 = t0;
        meta.origin = (mmm < 100) ? 1 : 2;
        fEventData->photonMeta.push_back(meta);

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
        G4double xgr = xx * m;
        G4double ygr = yy * m;
        G4double diag = std::sqrt((xi - xgr) * (xi - xgr) + (yi - ygr) * (yi - ygr) + (zz - zi) * (zz - zi));
        G4double pxb = (xi - xgr) / diag;
        G4double pyb = (yi - ygr) / diag;
        G4double pzb = (zi - zz) / diag;

        fParticleGun->SetParticlePosition(G4ThreeVector(xi, yi, zi));
        fParticleGun->SetParticleMomentumDirection(G4ThreeVector(pxb, pyb, pzb));
        fParticleGun->SetParticleTime(t0 * ns);
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
