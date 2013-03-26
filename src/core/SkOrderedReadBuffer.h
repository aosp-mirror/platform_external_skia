/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkOrderedReadBuffer_DEFINED
#define SkOrderedReadBuffer_DEFINED

class SkFlattenableReadBuffer {
public:
};

class SkOrderedReadBuffer : public SkFlattenableReadBuffer {
public:
    SkOrderedReadBuffer(const void*, size_t) {}
};

#endif // SkOrderedReadBuffer_DEFINED
