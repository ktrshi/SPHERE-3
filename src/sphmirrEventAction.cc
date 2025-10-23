#include "sphmirrEventAction.hh"
#include "sphmirrRunAction.hh"
#include "G4Event.hh"
#include "G4VVisManager.hh"
EventAction::EventAction(RunAction *run)
        : runAct(run), drawFlag("all") {
}
EventAction::~EventAction() = default;
void EventAction::BeginOfEventAction(const G4Event *evt) {
    [[maybe_unused]] G4int evtNb = evt->GetEventID();
}
void EventAction::EndOfEventAction(const G4Event *evt) {
}