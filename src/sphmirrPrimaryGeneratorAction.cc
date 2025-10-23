#include "sphmirrPrimaryGeneratorAction.hh"
#include "sphmirrPrimaryGeneratorMessenger.hh"
#include <G4Types.hh>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <regex>
#include "Randomize.hh"
#include "G4Event.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4ParticleDefinition.hh"
#include "G4SystemOfUnits.hh"
#include "G4RunManager.hh"

[[maybe_unused]] extern G4int DirGrp;
extern G4double zstart;
extern G4double xgr, ygr;    // coordinates of the starting point of a photon in meters
extern G4double xx, yy, t0;   // starting coordinates and time of a photon at snow level
extern G4double zz;    // snow level z; in case of inclined detector -- snow level z under the detector
extern G4int NbOfEvents;
[[maybe_unused]] extern G4double costh;
extern G4double phi;
extern G4double the;
extern std::ifstream inpho;
extern std::ofstream moshits;
[[maybe_unused]] extern G4double tbig;          // delay term due to EAS fly to the ground, ns
extern G4int input_file_open;  //  key shows the number of open moshits,  0 means clone#1 is to be opened
std::string parameters;
std::string outputDir;
std::string Inpho;
std::string Moshits;
extern std::string Height;
extern G4String NameOut; // moshits prefix
extern G4String CurrentPath;
extern std::vector<std::string> fileList;
extern std::string phelsDir;
extern int phl_CloneNum, phl_ii, phl_jj, phl_kk, phl_mmm;
extern double phl_xx, phl_yy, phl_t0;
extern G4double xsh;     // EAS axis x-shift at snow level, m
extern G4double ysh;     // EAS axis y-shift at snow level, m
extern G4int CorAtmMod;  // CORSIKA atmosphere model number
extern G4int CloneNum;   // event clone number
extern G4int origin;   // phel origin tag: 1 - CL, 2 - BG
[[maybe_unused]] G4int n100, n10, n1, n;

sphmirrPrimaryGeneratorAction::sphmirrPrimaryGeneratorAction() {
    constexpr G4int n_particle = 1;
    particleGun = new G4ParticleGun(n_particle);
    currentFile = fileList.begin();
    isFirstLine = true;
    gunMessenger = new sphmirrPrimaryGeneratorMessenger(this);
    G4ParticleTable *particleTable = G4ParticleTable::GetParticleTable();
    G4ParticleDefinition *particle = particleTable->FindParticle("opticalphoton");
    particleGun->SetParticleDefinition(particle);
    particleGun->SetParticleTime(0.0 * ns);
    particleGun->SetParticlePosition(G4ThreeVector(30.0 * cm, 50.0 * cm, 500.0 * cm));
    particleGun->SetParticleMomentumDirection(G4ThreeVector(0., 0., -1.));
    particleGun->SetParticleEnergy(2.0 * eV);
}
sphmirrPrimaryGeneratorAction::~sphmirrPrimaryGeneratorAction() {
    delete particleGun;
    delete gunMessenger;
}

void sphmirrPrimaryGeneratorAction::LoadNextBuffer() {
    lineBuffer.clear();
    lineBuffer.reserve(1000); // Reserve space for ~1000 lines
    bufferIndex = 0;

    std::string line;
    size_t bytesRead = 0;
    while (bytesRead < BUFFER_SIZE && std::getline(inpho, line)) {
        lineBuffer.push_back(std::move(line));
        bytesRead += lineBuffer.back().size() + 1; // +1 for newline
    }
}
void sphmirrPrimaryGeneratorAction::GeneratePrimaries(G4Event *anEvent) {
    if (currentFile == fileList.end()) {
        G4RunManager::GetRunManager()->AbortRun();
        G4cout << "No more files to process." << G4endl;
        return;
    }
    if (!inpho.is_open()) {
        Inpho = CurrentPath + "/phels/" + *currentFile;
        size_t pos = currentFile->find("Q");
        if (pos == std::string::npos) {
            G4cout << "Invalid file name format: " << *currentFile << G4endl;
            ++currentFile;
            GeneratePrimaries(anEvent);  // Попробовать следующий файл
            return;
        }
        parameters = currentFile->substr(pos);
        std::erase(parameters, ' ');
        std::regex pattern(R"((_c\d{3})$)");
        std::string insert = "_" + Height + "m$1";
        parameters = std::regex_replace(parameters, pattern, insert);
        outputDir = CurrentPath + "/moshits";
        if (!std::filesystem::exists(outputDir)) {
            std::filesystem::create_directory(outputDir);
        }
        Moshits = outputDir + "/moshits_" + parameters;
        // Open input file with larger buffer
        inpho.rdbuf()->pubsetbuf(nullptr, BUFFER_SIZE);
        inpho.open(Inpho);
        if (inpho.fail()) {
            G4RunManager::GetRunManager()->AbortRun();
            G4cout << "File " << Inpho << " doesn't exist!" << G4endl;
            return;
        }
        // Open output file with larger buffer
        moshits.rdbuf()->pubsetbuf(nullptr, BUFFER_SIZE);
        moshits.open(Moshits, std::ios::out);
        if (moshits.fail()) {
            G4RunManager::GetRunManager()->AbortRun();
            G4cout << "Cannot open file " << Moshits << " for writing!" << G4endl;
            return;
        }
        isFirstLine = true;
        lineBuffer.clear();
        bufferIndex = 0;
    }
    std::string line;
    if (isFirstLine) {
        if (std::getline(inpho, line)) {
            moshits << line << G4endl;
            isFirstLine = false;
            std::istringstream iss(line);
            std::vector<std::string> values;
            std::string value;
            while (iss >> value) {
                values.push_back(value);
            }
            if (!values.empty()) {
                xsh = std::stod(values[3])*m;
                ysh = std::stod(values[2])*m;
                zz = std::stod(values[1])*m;
            }
        }
    }
    if (std::getline(inpho, line)) {
        G4double pzb{0.0};
        G4double pyb{0.0};
        G4double pxb{0.0};
        G4double diag{0.0};
        G4double dzeta{0.0};
        G4double zi0{0.0};
        G4double yi0{0.0};
        G4double xi0{0.0};
        static int lineCounter = 0;
        lineCounter++;
        if (lineCounter % 1000000 == 0) {
            G4cout << "Processed " << lineCounter << " lines." << G4endl;
        }
        int ii, jj, kk, mmm;
        std::istringstream iss(line);
        // Parse all values in one go - remove duplicate read
        if (!(iss >> ii >> jj >> kk >> mmm >> xx >> yy >> t0)) {
            G4cout << "Error parsing line: " << line << G4endl;
            return;
        }
        phl_CloneNum = CloneNum;
        phl_ii = ii;
        phl_jj = jj;
        phl_kk = kk;
        phl_mmm = mmm;
        phl_xx = xx;
        phl_yy = yy;
        phl_t0 = t0;
        if (mmm < 100) origin = 1;   //  CL phel
        if (mmm > 100) origin = 2;   //  BG phel
        G4double r = 85.0 * cm * sqrt(G4UniformRand());
        dzeta = 360.0 * deg * G4UniformRand();
        xi0 = r * cos(dzeta);    // x of the entry point of a photon in the hood opening
        yi0 = r * sin(dzeta);    // y of the entry point of a photon in the hood opening
        zi0 = 0.0 * cm;
        G4double xi{0.0}, yi{0.0}, zi{0.0}, cthe{0.0}, sthe{0.0}, cphi{0.0}, sphi{0.0};
        cthe = cos(-the);
        sthe = sin(-the);
        cphi = cos(phi);
        sphi = sin(phi);
        xi = cthe * cphi * xi0 - sphi * yi0 - sthe * cphi * zi0;
        yi = cthe * sphi * xi0 + cphi * yi0 - sthe * sphi * zi0;
        zi = sthe * xi0 + cthe * zi0 + zstart;
        xgr = xx * m;
        ygr = yy * m;
        diag = sqrt((xi - xgr) * (xi - xgr) + (yi - ygr) * (yi - ygr) + (zz - zi) * (zz - zi));
        pxb = (xi - xgr) / diag;
        pyb = (yi - ygr) / diag;
        pzb = (zi - zz) / diag;
        costh = pzb;
        particleGun->SetParticlePosition(G4ThreeVector(xi, yi, zi));
        particleGun->SetParticleMomentumDirection(G4ThreeVector(pxb, pyb, pzb));
        particleGun->SetParticleTime(t0 * ns);
        SetOptPhotonPolar();
        particleGun->GeneratePrimaryVertex(anEvent);
    } else {
        inpho.close();
        moshits.close();
        ++currentFile;
        GeneratePrimaries(anEvent);
    }
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonDirTheta(const G4double Th) {
    Theta = Th;
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonDirPhi(const G4double Ph) {
    Phi = Ph;
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonDirGrp(const G4int Gr) {
    Grp = Gr;
    DirGrp = Gr;
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonBeamRad(const G4double radius) {
    Rad = radius;
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonBeamY0(const G4double y0) {
    Y0 = y0;
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonBeamX0(const G4double x0) {
    X0 = x0;
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonPolar() const {
    const G4double angle = G4UniformRand() * 360.0 * deg;
    SetOptPhotonPolar(angle);
}
void sphmirrPrimaryGeneratorAction::SetOptPhotonPolar(const G4double angle) const {
    if (particleGun->GetParticleDefinition()->GetParticleName() != "opticalphoton") {
        G4cout << "--> warning from PrimaryGeneratorAction::SetOptPhotonPolar() :"
                  "the particleGun is not an opticalphoton" << G4endl;
        return;
    }
    const G4ThreeVector normal(1., 0., 0.);
    const G4ThreeVector kphoton = particleGun->GetParticleMomentumDirection();
    const G4ThreeVector product = normal.cross(kphoton);
    const G4double modul2 = product * product;
    G4ThreeVector e_perpend(0., 0., 1.);
    if (modul2 > 0.) e_perpend = (1. / std::sqrt(modul2)) * product;
    const G4ThreeVector e_paralle = e_perpend.cross(kphoton);
    const G4ThreeVector polar = std::cos(angle) * e_paralle + std::sin(angle) * e_perpend;
    particleGun->SetParticlePolarization(polar);
}