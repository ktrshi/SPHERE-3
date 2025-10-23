#include "G4Timer.hh"
#include "sphmirrRunAction.hh"
#include "G4Run.hh"
#include <iomanip>
#include <fstream>
#include "G4SystemOfUnits.hh"
extern std::ofstream moshits;
extern G4int TotPhot;             // total number of tracked photons that entered PMTs
extern G4int NEntry;       // number of tracked photons that entered PMTs within the time window
extern G4double tmin;      // minimum delay at mosaic, ns
extern G4double tmax;      // maximum delay at mosaic, ns
RunAction::RunAction()
{
    timer = new G4Timer;
}
RunAction::~RunAction()
{
    delete timer;
}
void RunAction::BeginOfRunAction(const G4Run* aRun)
{
    G4cout << "### Run " << aRun->GetRunID() << " start." << G4endl;
    timer->Start();
}
void RunAction::EndOfRunAction(const G4Run* aRun)
{   
    timer->Stop();
    G4cout << "number of event = " << aRun->GetNumberOfEvent()
           << " " << *timer << G4endl;
    moshits.close();
    G4cout << "    TotPhot = " <<  TotPhot << G4endl;
    G4cout << "    NEntry = " <<  NEntry << G4endl;
    G4cout << "    tmin=" <<  tmin/ns << "ns,  tmax=" <<  tmax/ns << "ns" << G4endl;
}
