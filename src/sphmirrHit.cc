#include "sphmirrHit.hh"
#include "G4UnitsTable.hh"
#include "G4VVisManager.hh"
#include "G4Circle.hh"
#include "G4Colour.hh"
#include "G4VisAttributes.hh"
G4Allocator<sphmirrHit> sphmirrHitAllocator;
sphmirrHit::sphmirrHit()
        : fpos(G4ThreeVector()) {}
sphmirrHit::~sphmirrHit() = default;
sphmirrHit::sphmirrHit(const sphmirrHit &right)
        : G4VHit() {
    fpos = right.fpos;
}
sphmirrHit& sphmirrHit::operator=(const sphmirrHit &right) {
    fpos = right.fpos;
    return *this;
}
G4int sphmirrHit::operator==(const sphmirrHit &right) const {
    return (this == &right) ? 1 : 0;
}
void sphmirrHit::Draw() {
    if (G4VVisManager *pVVisManager = G4VVisManager::GetConcreteInstance()) {
        G4Circle circle(fpos);
        circle.SetScreenSize(8.);
        circle.SetFillStyle(G4Circle::filled);
        const G4Colour colour(1., 0., 0.);
        const G4VisAttributes attribs(colour);
        circle.SetVisAttributes(attribs);
        pVVisManager->Draw(circle);
    }
}
void sphmirrHit::Print() {
    G4cout << "  position: " << G4BestUnit(fpos, "Length") << G4endl;
}