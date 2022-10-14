/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tests/Test.h"

#include "src/core/SkPaintParamsKey.h"
#include "src/core/SkShaderCodeDictionary.h"

#include "src/gpu/graphite/ContextPriv.h"

namespace {

SkPaintParamsKey create_key_with_data(SkPaintParamsKeyBuilder* builder,
                                      int snippetID,
                                      SkSpan<const uint8_t> dataPayload) {
    SkDEBUGCODE(builder->checkReset());

    builder->beginBlock(snippetID);
    builder->addBytes(dataPayload.size(), dataPayload.data());
    builder->endBlock();

    return builder->lockAsKey();
}

SkPaintParamsKey create_key(SkPaintParamsKeyBuilder* builder, int snippetID, int size) {
    SkASSERT(size <= 1024);
    static constexpr uint8_t kDummyData[1024] = {};
    return create_key_with_data(builder, snippetID, SkSpan(kDummyData, size));
}

} // anonymous namespace

// These are intended to be unit tests of the SkPaintParamsKeyBuilder and SkPaintParamsKey.
DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyWithInvalidCodeSnippetIDTest, reporter, context) {

    SkShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();
    SkPaintParamsKeyBuilder builder(dict);

    // Invalid code snippet ID, key creation fails.
    SkPaintParamsKey key = create_key(&builder, kBuiltInCodeSnippetIDCount, /*size=*/32);
    REPORTER_ASSERT(reporter, key.isErrorKey());
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyValidBlockSizeTest, reporter, context) {

    SkShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();
    SkPaintParamsKeyBuilder builder(dict);

    // _Just_ on the edge of being too big
    static const int kMaxBlockDataSize = SkPaintParamsKey::kMaxBlockSize -
                                         sizeof(SkPaintParamsKey::Header);
    static constexpr SkPaintParamsKey::DataPayloadField kDataFields[] = {
            {"data", SkPaintParamsKey::DataPayloadType::kByte, kMaxBlockDataSize},
    };

    int userSnippetID = dict->addUserDefinedSnippet("keyAlmostTooBig", kDataFields);
    SkPaintParamsKey key = create_key(&builder, userSnippetID, kMaxBlockDataSize);

    // Key is created successfully.
    REPORTER_ASSERT(reporter, key.sizeInBytes() == SkPaintParamsKey::kMaxBlockSize);
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyTooLargeBlockSizeTest, reporter, context) {

    SkShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();
    SkPaintParamsKeyBuilder builder(dict);

    // Too big by one byte
    static const int kBlockDataSize = SkPaintParamsKey::kMaxBlockSize -
                                      sizeof(SkPaintParamsKey::Header) + 1;
    static constexpr SkPaintParamsKey::DataPayloadField kDataFields[] = {
            {"data", SkPaintParamsKey::DataPayloadType::kByte, kBlockDataSize},
    };

    int userSnippetID = dict->addUserDefinedSnippet("keyTooBig", kDataFields);
    SkPaintParamsKey key = create_key(&builder, userSnippetID, kBlockDataSize);

    // Key creation fails.
    REPORTER_ASSERT(reporter, key.isErrorKey());
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyEqualityChecksSnippetID, reporter, context) {

    SkShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();
    static const int kBlockDataSize = 4;
    static constexpr SkPaintParamsKey::DataPayloadField kDataFields[] = {
            {"data", SkPaintParamsKey::DataPayloadType::kByte, kBlockDataSize},
    };

    int userSnippetID1 = dict->addUserDefinedSnippet("key1", kDataFields);
    int userSnippetID2 = dict->addUserDefinedSnippet("key2", kDataFields);

    SkPaintParamsKeyBuilder builderA(dict);
    SkPaintParamsKeyBuilder builderB(dict);
    SkPaintParamsKeyBuilder builderC(dict);
    SkPaintParamsKey keyA = create_key(&builderA, userSnippetID1, kBlockDataSize);
    SkPaintParamsKey keyB = create_key(&builderB, userSnippetID1, kBlockDataSize);
    SkPaintParamsKey keyC = create_key(&builderC, userSnippetID2, kBlockDataSize);

    // Verify that keyA matches keyB, and that it does not match keyC.
    REPORTER_ASSERT(reporter, keyA == keyB);
    REPORTER_ASSERT(reporter, keyA != keyC);
    REPORTER_ASSERT(reporter, !(keyA == keyC));
    REPORTER_ASSERT(reporter, !(keyA != keyB));
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyEqualityChecksData, reporter, context) {

    SkShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();
    static const int kBlockDataSize = 4;
    static constexpr SkPaintParamsKey::DataPayloadField kDataFields[] = {
            {"data", SkPaintParamsKey::DataPayloadType::kByte, kBlockDataSize},
    };

    int userSnippetID = dict->addUserDefinedSnippet("key", kDataFields);

    static constexpr uint8_t kData [kBlockDataSize] = {1, 2, 3, 4};
    static constexpr uint8_t kData2[kBlockDataSize] = {1, 2, 3, 99};

    SkPaintParamsKeyBuilder builderA(dict);
    SkPaintParamsKeyBuilder builderB(dict);
    SkPaintParamsKeyBuilder builderC(dict);
    SkPaintParamsKey keyA = create_key_with_data(&builderA, userSnippetID, kData);
    SkPaintParamsKey keyB = create_key_with_data(&builderB, userSnippetID, kData);
    SkPaintParamsKey keyC = create_key_with_data(&builderC, userSnippetID, kData2);

    // Verify that keyA matches keyB, and that it does not match keyC.
    REPORTER_ASSERT(reporter, keyA == keyB);
    REPORTER_ASSERT(reporter, keyA != keyC);
    REPORTER_ASSERT(reporter, !(keyA == keyC));
    REPORTER_ASSERT(reporter, !(keyA != keyB));
}

DEF_GRAPHITE_TEST_FOR_ALL_CONTEXTS(KeyBlockReaderWorks, reporter, context) {

    SkShaderCodeDictionary* dict = context->priv().shaderCodeDictionary();
    static const int kCountX = 3;
    static const int kCountY = 2;
    static const int kCountZ = 7;
    static constexpr SkPaintParamsKey::DataPayloadField kDataFields[] = {
            {"ByteX",   SkPaintParamsKey::DataPayloadType::kByte,   kCountX},
            {"Float4Y", SkPaintParamsKey::DataPayloadType::kFloat4, kCountY},
            {"IntZ",    SkPaintParamsKey::DataPayloadType::kInt,    kCountZ},
    };

    int userSnippetID = dict->addUserDefinedSnippet("key", kDataFields);

    static constexpr uint8_t   kDataX[kCountX] = {1, 2, 3};
    static constexpr SkColor4f kDataY[kCountY] = {{4, 5, 6, 7}, {8, 9, 10, 11}};
    static constexpr int32_t   kDataZ[kCountZ] = {-1234567, 13, 14, 15, 16, 17, 7654321};

    SkPaintParamsKeyBuilder builder(dict);
    builder.beginBlock(userSnippetID);
    builder.addBytes(kCountX, kDataX);
    builder.add     (kCountY, kDataY);
    builder.addInts (kCountZ, kDataZ);
    builder.endBlock();

    SkPaintParamsKey key = builder.lockAsKey();

    // Verify that the block reader can extract out our data from the SkPaintParamsKey.
    SkPaintParamsKey::BlockReader reader = key.reader(dict, /*headerOffset=*/0);
    REPORTER_ASSERT(reporter,
                    reader.blockSize() == (sizeof(SkPaintParamsKey::Header) +
                                           sizeof(kDataX) + sizeof(kDataY) + sizeof(kDataZ)));

    SkSpan<const uint8_t> readerDataX = reader.bytes(0);
    REPORTER_ASSERT(reporter, readerDataX.size() == kCountX);
    REPORTER_ASSERT(reporter, 0 == memcmp(readerDataX.data(), kDataX, sizeof(kDataX)));

    SkSpan<const SkColor4f> readerDataY = reader.colors(1);
    REPORTER_ASSERT(reporter, readerDataY.size() == kCountY);
    REPORTER_ASSERT(reporter, 0 == memcmp(readerDataY.data(), kDataY, sizeof(kDataY)));

    SkSpan<const int32_t> readerBytesZ = reader.ints(2);
    REPORTER_ASSERT(reporter, readerBytesZ.size() == kCountZ);
    REPORTER_ASSERT(reporter, 0 == memcmp(readerBytesZ.data(), kDataZ, sizeof(kDataZ)));
}
