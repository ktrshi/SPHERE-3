#ifndef sphmirrRunAction_h
#define sphmirrRunAction_h 1
#include "globals.hh"
#include "G4UserRunAction.hh"
class G4Timer;
class G4Run;
class RunAction final: public G4UserRunAction
{
    public:
        RunAction();
        ~RunAction() override;
        void BeginOfRunAction(const G4Run* aRun) override;
        void EndOfRunAction(const G4Run* aRun) override;
    private:
        G4Timer* timer;
};
#endif /*sphmirrRunAction_h*/
