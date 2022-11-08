/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_PROGRAM
#define SKSL_PROGRAM

#include <vector>
#include <memory>

#include "include/private/SkSLDefines.h"
#include "include/private/SkSLModifiers.h"
#include "include/private/SkSLProgramElement.h"
#include "include/private/SkTHash.h"
#include "src/sksl/SkSLAnalysis.h"
#include "src/sksl/SkSLProgramSettings.h"
#include "src/sksl/analysis/SkSLProgramUsage.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLLiteral.h"
#include "src/sksl/ir/SkSLSymbolTable.h"

// name of the uniform used to handle features that are sensitive to whether Y is flipped.
#define SKSL_RTFLIP_NAME "u_skRTFlip"

namespace SkSL {

class Context;
class Pool;

/**
 * Represents a fully-digested program, ready for code generation.
 */
struct Program {
    struct Inputs {
        bool fUseFlipRTUniform = false;
        bool operator==(const Inputs& that) const {
            return fUseFlipRTUniform == that.fUseFlipRTUniform;
        }
        bool operator!=(const Inputs& that) const { return !(*this == that); }
    };

    Program(std::unique_ptr<std::string> source,
            std::unique_ptr<ProgramConfig> config,
            std::shared_ptr<Context> context,
            std::vector<std::unique_ptr<ProgramElement>> elements,
            std::vector<const ProgramElement*> sharedElements,
            std::unique_ptr<ModifiersPool> modifiers,
            std::shared_ptr<SymbolTable> symbols,
            std::unique_ptr<Pool> pool,
            Inputs inputs)
    : fSource(std::move(source))
    , fConfig(std::move(config))
    , fContext(context)
    , fModifiers(std::move(modifiers))
    , fSymbols(symbols)
    , fPool(std::move(pool))
    , fOwnedElements(std::move(elements))
    , fSharedElements(std::move(sharedElements))
    , fInputs(inputs) {
        fUsage = Analysis::GetUsage(*this);
    }

    ~Program() {
        // Some or all of the program elements are in the pool. To free them safely, we must attach
        // the pool before destroying any program elements. (Otherwise, we may accidentally call
        // delete on a pooled node.)
        AutoAttachPoolToThread attach(fPool.get());

        fOwnedElements.clear();
        fContext.reset();
        fSymbols.reset();
        fModifiers.reset();
    }

    class ElementsCollection {
    public:
        class iterator {
        public:
            const ProgramElement* operator*() {
                if (fShared != fSharedEnd) {
                    return *fShared;
                } else {
                    return fOwned->get();
                }
            }

            iterator& operator++() {
                if (fShared != fSharedEnd) {
                    ++fShared;
                } else {
                    ++fOwned;
                }
                return *this;
            }

            bool operator==(const iterator& other) const {
                return fOwned == other.fOwned && fShared == other.fShared;
            }

            bool operator!=(const iterator& other) const {
                return !(*this == other);
            }

        private:
            using Owned  = std::vector<std::unique_ptr<ProgramElement>>::const_iterator;
            using Shared = std::vector<const ProgramElement*>::const_iterator;
            friend class ElementsCollection;

            iterator(Owned owned, Owned ownedEnd, Shared shared, Shared sharedEnd)
                    : fOwned(owned), fOwnedEnd(ownedEnd), fShared(shared), fSharedEnd(sharedEnd) {}

            Owned  fOwned;
            Owned  fOwnedEnd;
            Shared fShared;
            Shared fSharedEnd;
        };

        iterator begin() const {
            return iterator(fProgram.fOwnedElements.begin(), fProgram.fOwnedElements.end(),
                            fProgram.fSharedElements.begin(), fProgram.fSharedElements.end());
        }

        iterator end() const {
            return iterator(fProgram.fOwnedElements.end(), fProgram.fOwnedElements.end(),
                            fProgram.fSharedElements.end(), fProgram.fSharedElements.end());
        }

    private:
        friend struct Program;

        ElementsCollection(const Program& program) : fProgram(program) {}
        const Program& fProgram;
    };

    // Can be used to iterate over *all* elements in this Program, both owned and shared (builtin).
    // The iterator's value type is 'const ProgramElement*', so it's clear that you *must not*
    // modify anything (as you might be mutating shared data).
    ElementsCollection elements() const { return ElementsCollection(*this); }

    std::string description() const {
        std::string result;
        result += fConfig->versionDescription();
        for (const ProgramElement* e : this->elements()) {
            result += e->description();
        }
        return result;
    }

    const ProgramUsage* usage() const { return fUsage.get(); }

    std::unique_ptr<std::string> fSource;
    std::unique_ptr<ProgramConfig> fConfig;
    std::shared_ptr<Context> fContext;
    std::unique_ptr<ProgramUsage> fUsage;
    std::unique_ptr<ModifiersPool> fModifiers;
    // it's important to keep fOwnedElements defined after (and thus destroyed before) fSymbols,
    // because destroying elements can modify reference counts in symbols
    std::shared_ptr<SymbolTable> fSymbols;
    std::unique_ptr<Pool> fPool;
    // Contains *only* elements owned exclusively by this program.
    std::vector<std::unique_ptr<ProgramElement>> fOwnedElements;
    // Contains *only* elements owned by a built-in module that are included in this program.
    // Use elements() to iterate over the combined set of owned + shared elements.
    std::vector<const ProgramElement*> fSharedElements;
    Inputs fInputs;
};

}  // namespace SkSL

#endif
