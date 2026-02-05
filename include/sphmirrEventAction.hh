#ifndef EventAction_h
#define EventAction_h 1

#include "G4UserEventAction.hh"
#include "globals.hh"

struct WorkerEventData;
struct SimConfig;

class EventAction final : public G4UserEventAction {
public:
    EventAction(WorkerEventData* eventData, const SimConfig* config);
    ~EventAction() override;
    void BeginOfEventAction(const G4Event*) override;
    void EndOfEventAction(const G4Event*) override;

private:
    WorkerEventData* fEventData;
    const SimConfig* fConfig;
};

#endif
