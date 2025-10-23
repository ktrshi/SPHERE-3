#ifndef EventAction_h
#define EventAction_h 1
#include <utility>
#include "G4UserEventAction.hh"
#include "globals.hh"
class RunAction;
class EventAction final : public G4UserEventAction
{
public:
    explicit EventAction(RunAction*);
    ~EventAction() override;
    void  BeginOfEventAction(const G4Event*) override;
    void    EndOfEventAction(const G4Event*) override;
    [[maybe_unused]] void SetDrawFlag(G4String val)  { drawFlag = std::move(val); };
private:
    [[maybe_unused]] RunAction*  runAct;
    G4String drawFlag;
};
#endif

    
