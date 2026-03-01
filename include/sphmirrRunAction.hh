#ifndef sphmirrRunAction_h
#define sphmirrRunAction_h 1

#include "G4UserRunAction.hh"
#include "G4Accumulable.hh"
#include "G4MergeMode.hh"
#include "globals.hh"
#include <limits>

class G4Timer;
class G4Run;
struct SimConfig;

class RunAction final : public G4UserRunAction {
public:
    explicit RunAction(const SimConfig* config = nullptr);
    ~RunAction() override;
    void BeginOfRunAction(const G4Run* aRun) override;
    void EndOfRunAction(const G4Run* aRun) override;

private:
    G4Timer* fTimer;
    const SimConfig* fConfig;
    G4Accumulable<G4int> fTotPhotTotal{"TotPhotTotal", 0};
    G4Accumulable<G4int> fNEntryTotal{"NEntryTotal", 0};
    G4Accumulable<G4double> fTminAll{"TminAll", std::numeric_limits<G4double>::max(), G4MergeMode::kMinimum};
    G4Accumulable<G4double> fTmaxAll{"TmaxAll", 0.0, G4MergeMode::kMaximum};
};

#endif /*sphmirrRunAction_h*/
