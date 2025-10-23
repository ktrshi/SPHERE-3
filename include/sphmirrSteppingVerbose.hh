#ifndef sphmirrSteppingVerbose_h
#define sphmirrSteppingVerbose_h 1
#include "G4SteppingVerbose.hh"

class sphmirrSteppingVerbose;
class sphmirrSteppingVerbose final : public G4SteppingVerbose
{
    public:
       sphmirrSteppingVerbose();
       ~sphmirrSteppingVerbose() override;
       void StepInfo() override;
       void TrackingStarted() override;
};
#endif