/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkOrderedWriteBuffer_DEFINED
#define SkOrderedWriteBuffer_DEFINED

class SkFlattenableWriteBuffer {
public:
    enum { kCrossProcess_Flag };
};

class SkOrderedWriteBuffer : public SkFlattenableWriteBuffer {
public:
    SkOrderedWriteBuffer(size_t) {}
    virtual ~SkOrderedWriteBuffer() {}
    uint32_t size() { return 0; }
    virtual bool writeToStream(void*) { return false; }
    void setFlags(uint32_t) {}
};

#endif // SkOrderedWriteBuffer_DEFINED
