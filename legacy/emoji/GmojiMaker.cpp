/*
 * Copyright 2009, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <iostream>
#include <string>
#include <vector>

static const char gSite[] = "http://www.corp.google.com/eng/doc/emoji/dev.html";

using namespace std;

static int hexchar_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    return -1;  // unrecognized char for nex
}

/*  Tool to build gmoji_pua table, listing all of the pua values for gmoji
 */
int main (int argc, char * const argv[]) {
    
    char buffer[10000];    
    FILE* file = fopen(argv[1], "r");
    if (NULL == file) {
        std::cerr << "Can't open " << argv[1] << " for input. Aborting\n";
        std::cout << "\n";
        return -1;
    }
    
    vector<int> unichars;
    int lineNo = 0;
    for (;;) {
        if (fgets(buffer, sizeof(buffer), file) == 0) {
            break;
        }
        
        int prevPua = 0;
        int pua = 0;
        // we just want to eat the first 5 chars
        for (int i = 0; i < 5; i++) {
            int value = hexchar_to_int(buffer[i]);
            if (value < 0) {    // bad char for hex
                std::cerr << "Expected hex char on line " << lineNo
                          << " col " << i << "\n";
                return -1;
            }
            pua = (pua << 4) | value;
        }
        if (pua < 0xFE000 || pua > 0xFEFFF) {
            std::cerr << "PUA not in expected range " << pua << " line "
                      << lineNo << "\n";
            return -1;
        }
        if (pua <= prevPua) {
            std::cerr << "PUA value not in ascending order line "
                      << lineNo << "\n";
            return -1;
        }
        unichars.push_back(pua);
        prevPua = pua;
        lineNo++;
    }
    
    // Now output our resulting array to look like a C array
    const int perLine = 8;
    const int base = unichars[0];
    printf("\n");
    printf("// Compressed gmoji table, sorted\n");
    printf("// Originally scraped from %s\n", gSite);
    printf("// Input text file \"%s\"\n", argv[1]);
    printf("\n");
    printf("static const uint16_t gGmojiPUA[] = {\n");
    for (int i = 0; i < unichars.size(); i++) {
        if ((i % perLine) == 0) {   // first one
            printf("    ");
        }
        printf("0x%03X", unichars[i] - base);
        if (i == unichars.size() - 1) { // last one entirely
            printf("\n");
        }
        else if ((i % perLine) == (perLine - 1)) {   // last one on line
            printf(",\n");
        } else {
            printf(", ");
        }
    }
    printf("};\n");
    printf("\n");
    printf("#define GMOJI_PUA_MIN   0x%X\n", unichars[0]);
    printf("#define GMOJI_PUA_MAX   0x%X\n", unichars[unichars.size()-1]);
    printf("#define GMOJI_PUA_COUNT (sizeof(gGmojiPUA) / sizeof(gGmojiPUA[0]))\n");
    printf("// GMOJI_PUA_COUNT should be %d\n", unichars.size());
    printf("\n");
    
    fclose(file);
    return 0;
}
