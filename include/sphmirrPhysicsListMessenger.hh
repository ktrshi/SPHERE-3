#ifndef sphmirrPhysicsListMessenger_h
#define sphmirrPhysicsListMessenger_h 1
#include "globals.hh"
#include "G4UImessenger.hh"
class sphmirrPhysicsList;
class G4UIdirectory;
class G4UIcmdWithAnInteger;
class sphmirrPhysicsListMessenger final : public G4UImessenger
{
    public:
        explicit sphmirrPhysicsListMessenger(sphmirrPhysicsList* );
        ~sphmirrPhysicsListMessenger() override;
        void SetNewValue(G4UIcommand*, G4String) override;

    private:
        sphmirrPhysicsList* pPhysicsList;
        G4UIdirectory* sphmirrDir;
        G4UIdirectory* physDir;
        G4UIcmdWithAnInteger* verboseCmd;
};
#endif

