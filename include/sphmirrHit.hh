#ifndef sphmirrHit_h
#define sphmirrHit_h 1
#include "G4VHit.hh"
#include "G4THitsCollection.hh"
#include "G4Allocator.hh"
#include "G4ThreeVector.hh"
class sphmirrHit final: public G4VHit
{
    public:
        sphmirrHit();
        ~sphmirrHit() override;
        sphmirrHit(const sphmirrHit&);
        sphmirrHit& operator=(const sphmirrHit&);
        G4int operator==(const sphmirrHit&) const;
        inline void* operator new(size_t);
        inline void  operator delete(void*);
        void Draw() override;
        void Print() override;
        void SetPos      (const G4ThreeVector& xyz){ fpos = xyz; };
        [[maybe_unused]] G4ThreeVector GetPos(){ return fpos; };
    private:
        G4ThreeVector fpos;
};
typedef G4THitsCollection<sphmirrHit> sphmirrHitsCollection;
extern G4Allocator<sphmirrHit> sphmirrHitAllocator;
inline void* sphmirrHit::operator new(size_t)
{
    const auto aHit = static_cast<void *>(sphmirrHitAllocator.MallocSingle());
    return aHit;
}
inline void sphmirrHit::operator delete(void *aHit)
{
  sphmirrHitAllocator.FreeSingle(static_cast<sphmirrHit*>(aHit));
}
#endif
