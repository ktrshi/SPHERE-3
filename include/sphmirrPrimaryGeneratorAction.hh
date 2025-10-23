#ifndef sphmirrPrimaryGeneratorAction_h
#define sphmirrPrimaryGeneratorAction_h 1
#include "G4VUserPrimaryGeneratorAction.hh"
#include <fstream>
#include <vector>
#include "globals.hh"
class G4ParticleGun;
class G4Event;
class sphmirrPrimaryGeneratorMessenger;
class sphmirrPrimaryGeneratorAction final: public G4VUserPrimaryGeneratorAction
{
    public:
        sphmirrPrimaryGeneratorAction();
        ~sphmirrPrimaryGeneratorAction() override;
        void GeneratePrimaries(G4Event*) override;void SetOptPhotonPolar() const;
        void SetOptPhotonPolar(G4double) const;
        void SetOptPhotonDirTheta(G4double);
        void SetOptPhotonDirPhi(G4double);
        void SetOptPhotonDirGrp(G4int);
        [[nodiscard]] G4int GetOptPhotonDirGrp() const {return Grp;};
        void SetOptPhotonBeamRad(G4double);
        void SetOptPhotonBeamX0(G4double);
        void SetOptPhotonBeamY0(G4double);
        G4double Theta{};
        G4double Phi{};
        G4double Rad{};
        G4double X0{};
        G4double Y0{};
        G4int    Grp{};
    private:
        G4ParticleGun* particleGun;
        sphmirrPrimaryGeneratorMessenger* gunMessenger;
        std::vector<std::string>::iterator currentFile;
        bool isFirstLine;

        // Buffered I/O optimization
        static constexpr size_t BUFFER_SIZE = 65536; // 64KB buffer
        std::vector<std::string> lineBuffer;
        size_t bufferIndex{0};

        void LoadNextBuffer();
};
#endif /*sphmirrPrimaryGeneratorAction_h*/
