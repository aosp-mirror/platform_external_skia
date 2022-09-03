/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_PipelineDataCache_DEFINED
#define skgpu_graphite_PipelineDataCache_DEFINED

#include "include/private/SkTHash.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkPipelineData.h"


namespace skgpu::graphite {

// Add a block of data to the cache and return a stable pointer to the contents (assuming that a
// resettable gatherer had accumulated the input data pointer).
//
// If an identical block of data is already in the cache, that existing pointer is returned, making
// pointer comparison suitable when comparing data blocks retreived from the cache.
//
// T must define a hash() function, an operator==, and a static Make(const T&, SkArenaAlloc*)
// factory that's used to copy the data into an arena allocation owned by the PipelineDataCache.
template<typename T>
class PipelineDataCache {
public:
    PipelineDataCache() = default;

    const T* insert(const T& dataBlock) {
        DataRef data{&dataBlock}; // will not be persisted, since pointer isn't from the arena.
        const DataRef* existing = fDataPointers.find(data);
        if (existing) {
            return existing->fPointer;
        } else {
            // Need to make a copy of dataBlock into the arena
            T* copy = T::Make(dataBlock, &fArena);
            fDataPointers.add(DataRef{copy});
            return copy;
        }
    }

    // The number of unique T objects in the cache
    int count() const {
        return fDataPointers.count();
    }

private:
    struct DataRef {
        const T* fPointer;

        bool operator==(const DataRef& o) const {
            if (!fPointer || !o.fPointer) {
                return !fPointer && !o.fPointer;
            } else {
                return *fPointer == *o.fPointer;
            }
        }
    };
    struct Hash {
        // This hash operator de-references and hashes the data contents
        size_t operator()(const DataRef& dataBlock) const {
            return dataBlock.fPointer ? dataBlock.fPointer->hash() : 0;
        }
    };

    SkTHashSet<DataRef, Hash> fDataPointers;
    // Holds the data that is pointed to by fDataPointers
    SkArenaAlloc fArena{0};
};

// A UniformDataCache lives for the entire duration of a Recorder.
using UniformDataCache = PipelineDataCache<SkUniformDataBlock>;

// A TextureDataCache only lives for a single Recording. When a Recording is snapped it is pulled
// off of the Recorder and goes with the Recording as a record of the required Textures and
// Samplers.
using TextureDataCache = PipelineDataCache<SkTextureDataBlock>;

} // namespace skgpu::graphite

#endif // skgpu_graphite_PipelineDataCache_DEFINED
