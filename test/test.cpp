#include "test.h"

namespace skia {

#define MAX_REC_COUNT   1000

struct Rec {
    Test::Factory   fFact;
    void*           fData;
};
static Rec gRecs[MAX_REC_COUNT];
static int gRecCount;

void Test::Register(Factory fact, void* data) {
    SkASSERT(gRecCount < MAX_REC_COUNT);

    gRecs[gRecCount].fFact = fact;
    gRecs[gRecCount].fData = data;
    gRecCount++;
}

Test::Registrar::Registrar(Factory fact, void* data) {
    Test::Register(fact, data);
}
    
Test::Init::Init(void (*proc)()) {
    proc();
}

Test* Test::Iter::next() {
    if (fIndex < gRecCount) {
        Test* test = gRecs[fIndex].fFact(gRecs[fIndex].fData);
        fIndex++;
        return test;
    }
    return NULL;
}
    
int Test::Iter::Count() { return gRecCount; }

}   // namespace

