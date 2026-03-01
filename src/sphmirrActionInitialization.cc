#include "sphmirrActionInitialization.hh"
#include "sphmirrPrimaryGeneratorAction.hh"
#include "sphmirrRunAction.hh"
#include "sphmirrEventAction.hh"
#include "sphmirrSteppingAction.hh"
#include "sphmirrStackingAction.hh"
#include "FileQueue.hh"
#include "SimConfig.hh"
#include "WorkerEventData.hh"
#include "sphmirrDetectorConstruction.hh"

sphmirrActionInitialization::sphmirrActionInitialization(
        FileQueue* fileQueue,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fFileQueue(fileQueue), fConfig(config), fDetector(detector) {}

void sphmirrActionInitialization::Build() const {
    // Each worker thread gets its own WorkerEventData and action instances
    auto* eventData = new WorkerEventData();

    SetUserAction(new sphmirrPrimaryGeneratorAction(fFileQueue, eventData, fConfig, fDetector));
    SetUserAction(new RunAction(fConfig));
    SetUserAction(new EventAction(eventData, fConfig));
    SetUserAction(new sphmirrSteppingAction(eventData, fConfig, fDetector));
    SetUserAction(new sphmirrStackingAction());
}

void sphmirrActionInitialization::BuildForMaster() const {
    SetUserAction(new RunAction(fConfig));
}
