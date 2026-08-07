// src/utils/zstd_compress.cpp
// Zstd compression helpers with per-buffer trained dictionary.

#include "utils/zstd_compress.hpp"
#include "utils/error.hpp"
#include "llvm/Support/MD5.h"
#include "llvm/ADT/ArrayRef.h"

#include <zstd.h>
#include <zdict.h>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace trust::detail {

// ── Format constants ──
// (not in anonymous namespace so static functions in the same TU can see them)
static constexpr size_t kOrigSizeBytes = 4;
static constexpr size_t kDictSizeBytes = 4;
static constexpr size_t kChecksumBytes = 8;
static constexpr size_t kHeaderBytes = kOrigSizeBytes + kDictSizeBytes; // 8

static void writeLE32(uint8_t* dst, uint32_t val) noexcept {
    for (int i = 0; i < 4; ++i) {
        dst[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
    }
}

static uint32_t readLE32(const uint8_t* src) noexcept {
    uint32_t val = 0;
    for (int i = 0; i < 4; ++i) {
        val |= static_cast<uint32_t>(src[i]) << (i * 8);
    }
    return val;
}

static uint64_t computeChecksum(const uint8_t* data, size_t size) noexcept {
    auto arr = llvm::ArrayRef<uint8_t>(data, size);
    auto hash = llvm::MD5::hash(arr);
    uint64_t checksum = 0;
    for (int i = 0; i < 8; ++i) {
        checksum |= static_cast<uint64_t>(hash[i]) << (i * 8);
    }
    return checksum;
}

static std::vector<size_t> makeSamples(size_t dataSize) noexcept {
    if (dataSize < 256) {
        return {};
    }
    constexpr size_t kTargetChunk = 4096;
    unsigned numChunks = std::max(2u, static_cast<unsigned>(dataSize / kTargetChunk));
    size_t chunkSize = std::max(size_t{256}, dataSize / numChunks);
    numChunks = static_cast<unsigned>(dataSize / chunkSize);
    if (numChunks < 2) {
        chunkSize = dataSize / 2;
        numChunks = 2;
    }
    std::vector<size_t> sizes;
    sizes.reserve(numChunks);
    size_t remaining = dataSize;
    for (unsigned i = 0; i + 1 < numChunks; ++i) {
        sizes.push_back(chunkSize);
        remaining -= chunkSize;
    }
    sizes.push_back(remaining);
    if (sizes.back() < 64 && sizes.size() >= 2) {
        sizes[sizes.size() - 2] += sizes.back();
        sizes.pop_back();
    }
    return sizes;
}

// zstd_compress — compression with per-buffer trained dictionary.
// Single buffer, no intermediate copies or memcpy of dictionary.
std::vector<unsigned char> zstd_compress(const unsigned char* data, size_t size) {
    if (size == 0 || size > 1024 * 1024 * 1024) {
        FAULT("zstd_compress: invalid input size");
        return {};
    }

    auto sampleSizes = makeSamples(size);
    size_t actualDictSize = 0;
    bool useDict = false;

    const size_t zstdBound = ZSTD_compressBound(size);
    const size_t totalCapacity = kHeaderBytes + kZstdMaxDictSize + zstdBound + kChecksumBytes;
    std::vector<unsigned char> result(totalCapacity);
    uint8_t* const base = result.data();

    // Header: orig_size at offset 0
    writeLE32(base, static_cast<uint32_t>(size));
    // Dict placeholder at offset 4
    uint8_t* dictPos = base + kHeaderBytes;
    if (!sampleSizes.empty()) {
        size_t dictResult = ZDICT_trainFromBuffer(dictPos, kZstdMaxDictSize, data, sampleSizes.data(), static_cast<unsigned>(sampleSizes.size()));
        if (!ZSTD_isError(dictResult) && dictResult > 0) {
            actualDictSize = dictResult;
            useDict = true;
        }
    }
    // Write dict_size at offset 4
    writeLE32(base + kOrigSizeBytes, static_cast<uint32_t>(actualDictSize));

    // Compress — writes immediately after dictionary
    uint8_t* compressedPos = dictPos + actualDictSize;
    size_t compressedCapacity = totalCapacity - (compressedPos - base) - kChecksumBytes;
    size_t compressedSize;
    if (useDict) {
        auto* cctx = ZSTD_createCCtx();
        if (!cctx) {
            FAULT("zstd_compress: failed to create CCtx");
            return {};
        }
        compressedSize = ZSTD_compress_usingDict(cctx, compressedPos, compressedCapacity, data, size, dictPos, actualDictSize, kZstdCompressionLevel);
        ZSTD_freeCCtx(cctx);
    } else {
        compressedSize = ZSTD_compress(compressedPos, compressedCapacity, data, size, kZstdCompressionLevel);
    }
    if (ZSTD_isError(compressedSize)) {
        FAULT("zstd_compress: {}", ZSTD_getErrorName(compressedSize));
        return {};
    }

    // Checksum
    size_t payloadSize = kHeaderBytes + actualDictSize + compressedSize;
    uint64_t checksum = computeChecksum(base, payloadSize);
    writeLE32(base + payloadSize, static_cast<uint32_t>(checksum));
    writeLE32(base + payloadSize + 4, static_cast<uint32_t>(checksum >> 32));

    result.resize(payloadSize + kChecksumBytes);
    return result;
}

std::vector<unsigned char> zstd_decompress(const unsigned char* data, size_t size) {
    if (size < kHeaderBytes + 1 + kChecksumBytes) {
        return {};
    }

    // Verify checksum
    size_t payloadSize = size - kChecksumBytes;
    uint64_t stored = static_cast<uint64_t>(readLE32(data + payloadSize)) | (static_cast<uint64_t>(readLE32(data + payloadSize + 4)) << 32);
    if (computeChecksum(data, payloadSize) != stored) {
        return {};
    }

    const uint8_t* ptr = data;
    uint32_t origSize = readLE32(ptr);
    ptr += kOrigSizeBytes;
    if (origSize == 0 || origSize > 1024 * 1024 * 1024) {
        return {};
    }

    uint32_t dictSize = readLE32(ptr);
    ptr += kDictSizeBytes;
    const uint8_t* dictStart = ptr;
    ptr += dictSize;

    size_t compressedSize = payloadSize - kHeaderBytes - dictSize;
    if (compressedSize == 0) {
        return {};
    }
    const uint8_t* compressedData = ptr;

    // Decompress directly into result vector — no extra copy
    std::vector<unsigned char> result(origSize);
    size_t resultSize;
    if (dictSize > 0) {
        auto* dctx = ZSTD_createDCtx();
        if (!dctx) {
            return {};
        }
        resultSize = ZSTD_decompress_usingDict(dctx, result.data(), origSize, compressedData, compressedSize, dictStart, dictSize);
        ZSTD_freeDCtx(dctx);
    } else {
        resultSize = ZSTD_decompress(result.data(), origSize, compressedData, compressedSize);
    }
    if (ZSTD_isError(resultSize) || resultSize != origSize) {
        return {};
    }

    return result;
}

} // namespace trust::detail