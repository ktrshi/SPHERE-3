#ifndef sphmirrPhysicsList_h
#define sphmirrPhysicsList_h 1

#include "G4VUserPhysicsList.hh"
#include "globals.hh"

class G4OpBoundaryProcess;
class sphmirrPhysicsListMessenger;

class sphmirrPhysicsList final: public G4VUserPhysicsList
{
    public:
        sphmirrPhysicsList();
        ~sphmirrPhysicsList() override;
        void ConstructParticle() override;
        void ConstructProcess() override;
        void SetCuts() override;
        static void ConstructBosons();
        void ConstructOp();
        void SetVerbose(G4int) const;

    private:
        G4OpBoundaryProcess* theBoundaryProcess;
        sphmirrPhysicsListMessenger* pMessenger;
};
#endif /* sphmirrPhysicsList_h */
