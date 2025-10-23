#ifndef sphmirrPhysicsList_h
#define sphmirrPhysicsList_h 1
#include <G4Decay.hh>


#include "G4OpWLS.hh"
#include "G4VUserPhysicsList.hh"
#include "globals.hh"

class G4OpBoundaryProcess;
class sphmirrPhysicsListMessenger;
class sphmirrPhysicsList final: public G4VUserPhysicsList
{
    public:
        sphmirrPhysicsList();
        ~sphmirrPhysicsList() override; void ConstructParticle() override;
        void ConstructProcess() override;   void SetCuts() override;    //these methods Construct particles
        static void ConstructBosons();
        static void ConstructLeptons();
        static void ConstructMesons();
        static void ConstructBaryons(); //these methods Construct physics processes and register them
        void ConstructGeneral() const;
        void ConstructEM() const;
        void ConstructOp();
        void SetVerbose(G4int) const;
    
    private:
        G4Decay* theDecayProcess;
        G4OpBoundaryProcess* theBoundaryProcess;
        sphmirrPhysicsListMessenger* pMessenger;
};
#endif /* sphmirrPhysicsList_h */
