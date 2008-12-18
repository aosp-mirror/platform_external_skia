#ifndef SkiaTest_DEFINED
#define SkiaTest_DEFINED

#include "SkCanvas.h"
#include "SkPoint.h"
#include "SkString.h"

namespace skia {

    class Test {
    public:
        virtual ~Test() {}
        
        enum StringType {
            kTitle,
            kDescription
        };
        
        virtual void getSize(SkIPoint* size) = 0;
        virtual void draw(SkCanvas*) = 0;
        virtual bool getString(StringType, SkString*) = 0;
        
        ///////////////////////////////////////////////////////////////////////
        
        class Iter {
        public:
            Iter() : fIndex(0) {}
            
            void reset() { fIndex = 0; }
            Test* next();
            
            static int Count();
        private:
            int fIndex;
        };
        
        ///////////////////////////////////////////////////////////////////////
        
        typedef Test* (*Factory)(void*);
        static void Register(Factory, void*);

        class Registrar {
        public:
            Registrar(Factory, void*);
        };
        
        class Init {
        public:
            Init(void (*)());
        };
    };

}

#endif

