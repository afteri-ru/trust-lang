#include "diag/mapping.hpp"

#include <cstring>
#include <msgpack.h>

namespace trust {

// ══════════════════════════════════════════════════════════════
//                        Утилиты
// ══════════════════════════════════════════════════════════════

void SourceMapping::ensureSize(std::vector<std::vector<RangeMapEntry>> &vec, size_t idx) {
    if (vec.size() <= idx) {
        vec.resize(idx + 1);
    }
}

bool SourceMapping::isMonotonic(const std::vector<RangeMapEntry> &ranges, SourceLoc begin) {
    if (ranges.empty())
        return true;
    // Диапазоны не должны перекрываться: конец предыдущего < начало нового
    return ranges.back().from.end.packed < begin.packed;
}

std::optional<SourceRange> SourceMapping::findRange(const std::vector<RangeMapEntry> &ranges, SourceLoc loc) {
    if (ranges.empty() || !loc.isValid())
        return std::nullopt;

    // Бинарный поиск: последний entry с begin.packed ≤ loc.packed
    auto it = std::upper_bound(ranges.begin(), ranges.end(), loc.packed, [](uint32_t packed, const RangeMapEntry &e) { return packed < e.from.begin.packed; });

    if (it == ranges.begin())
        return std::nullopt;
    --it;

    // Проверка: loc.packed ≤ end.packed
    if (loc.packed > it->from.end.packed)
        return std::nullopt;

    uint32_t delta = loc.packed - it->from.begin.packed;
    return SourceRange{SourceLoc{it->to.begin.packed + delta}, SourceLoc{it->to.end.packed + delta}};
}

// ══════════════════════════════════════════════════════════════
//                     Добавление маппингов
// ══════════════════════════════════════════════════════════════

bool SourceMapping::addRangeMapping(SourceRange trustRange, SourceRange cppRange) {
    if (!trustRange.begin.isValid() || !trustRange.end.isValid() || !cppRange.begin.isValid() || !cppRange.end.isValid()) {
        return false;
    }

    uint32_t trustIdx = trustRange.begin.fileIdx().raw;
    uint32_t cppIdx = cppRange.begin.fileIdx().raw;

    ensureSize(m_forward, trustIdx);
    ensureSize(m_backward, cppIdx);

    // Проверка монотонности
    if (!isMonotonic(m_forward[trustIdx], trustRange.begin))
        return false;
    if (!isMonotonic(m_backward[cppIdx], cppRange.begin))
        return false;

    RangeMapEntry entry{trustRange, cppRange};

    m_forward[trustIdx].push_back(entry);
    // Для backward-индекса храним запись с переставленными from/to,
    // чтобы findRange() корректно искал по cpp-диапазону.
    m_backward[cppIdx].push_back({cppRange, trustRange});

    return true;
}

bool SourceMapping::addNameMapping(SourceRange trustRange, SourceRange cppRange, std::string_view trustName, std::string_view cppName) {
    if (!addRangeMapping(trustRange, cppRange))
        return false;

    m_nameMappings.push_back({{trustRange, cppRange}, std::string(trustName), std::string(cppName)});

    m_cppToTrustName.emplace(std::string(cppName), std::string(trustName));
    return true;
}

// ══════════════════════════════════════════════════════════════
//                      Поиск диапазона
// ══════════════════════════════════════════════════════════════

std::optional<SourceRange> SourceMapping::getMapTrustToCpp(SourceLoc trustLoc) const {
    if (!trustLoc.isValid() || trustLoc.isOutput())
        return std::nullopt;

    uint32_t idx = trustLoc.fileIdx().raw;
    if (idx >= m_forward.size())
        return std::nullopt;

    return findRange(m_forward[idx], trustLoc);
}

std::optional<SourceRange> SourceMapping::getMapCppToTrust(SourceLoc cppLoc) const {
    if (!cppLoc.isValid() || !cppLoc.isOutput())
        return std::nullopt;

    uint32_t idx = cppLoc.fileIdx().raw;
    if (idx >= m_backward.size())
        return std::nullopt;

    return findRange(m_backward[idx], cppLoc);
}

// ══════════════════════════════════════════════════════════════
//                     Поиск имени
// ══════════════════════════════════════════════════════════════

std::optional<NameRangeInfo> SourceMapping::getCppName(SourceLoc trustLoc, std::string_view trustName) const {
    for (const auto &v : m_nameMappings) {
        if (v.trustName == trustName && trustLoc.packed >= v.range.from.begin.packed && trustLoc.packed <= v.range.from.end.packed) {
            int offset = trustLoc.packed - v.range.from.begin.packed;
            NameRangeInfo result = v;
            result.range.to.begin = SourceLoc{v.range.to.begin.packed + offset};
            result.range.to.end = SourceLoc{v.range.to.end.packed + offset};
            return result;
        }
    }
    return std::nullopt;
}

std::optional<NameRangeInfo> SourceMapping::getTrustName(SourceLoc cppLoc, std::string_view cppName) const {
    for (const auto &v : m_nameMappings) {
        if (v.cppName == cppName && cppLoc.packed >= v.range.to.begin.packed && cppLoc.packed <= v.range.to.end.packed) {
            int offset = cppLoc.packed - v.range.to.begin.packed;
            NameRangeInfo result = v;
            result.range.from.begin = SourceLoc{v.range.from.begin.packed + offset};
            result.range.from.end = SourceLoc{v.range.from.end.packed + offset};
            return result;
        }
    }
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════════
//                     Сериализация (msgpack)
// ══════════════════════════════════════════════════════════════

std::vector<unsigned char> SourceMapping::pack() const {
    std::vector<unsigned char> buf;
    msgpack_packer *pk = msgpack_packer_new(&buf, [](void *ctx, const char *d, size_t len) -> int {
        auto *b = static_cast<std::vector<unsigned char> *>(ctx);
        b->insert(b->end(), d, d + len);
        return 0;
    });

    // Корень: array 2: [range_mappings, name_mappings]
    msgpack_pack_array(pk, 2);

    // [0] range_mappings
    size_t totalRanges = 0;
    for (const auto &vec : m_forward)
        totalRanges += vec.size();
    msgpack_pack_array(pk, totalRanges);
    for (const auto &vec : m_forward) {
        for (const auto &entry : vec) {
            msgpack_pack_array(pk, 4);
    msgpack_pack_uint32(pk, entry.from.begin.packed);
    msgpack_pack_uint32(pk, entry.from.end.packed);
    msgpack_pack_uint32(pk, entry.to.begin.packed);
    msgpack_pack_uint32(pk, entry.to.end.packed);
        }
    }

    // [1] name_mappings
    msgpack_pack_array(pk, m_nameMappings.size());
    for (const auto &v : m_nameMappings) {
        msgpack_pack_array(pk, 6);
        msgpack_pack_uint32(pk, v.range.from.begin.packed);
        msgpack_pack_uint32(pk, v.range.from.end.packed);
        msgpack_pack_uint32(pk, v.range.to.begin.packed);
        msgpack_pack_uint32(pk, v.range.to.end.packed);
        msgpack_pack_str(pk, v.trustName.size());
        msgpack_pack_str_body(pk, v.trustName.data(), v.trustName.size());
        msgpack_pack_str(pk, v.cppName.size());
        msgpack_pack_str_body(pk, v.cppName.data(), v.cppName.size());
    }

    msgpack_packer_free(pk);
    return buf;
}

bool SourceMapping::unpack(const unsigned char *data, size_t size) {
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);

    auto ret = msgpack_unpack_next(&msg, reinterpret_cast<const char *>(data), size, nullptr);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&msg);
        return false;
    }

    // Корень: array 2
    if (msg.data.type != MSGPACK_OBJECT_ARRAY || msg.data.via.array.size < 2) {
        msgpack_unpacked_destroy(&msg);
        return false;
    }

    msgpack_object *root = msg.data.via.array.ptr;

    // [0] range_mappings
    if (root[0].type != MSGPACK_OBJECT_ARRAY) {
        msgpack_unpacked_destroy(&msg);
        return false;
    }
    msgpack_object &rangesArray = root[0];
    for (uint32_t i = 0; i < rangesArray.via.array.size; ++i) {
        msgpack_object &r = rangesArray.via.array.ptr[i];
        if (r.type != MSGPACK_OBJECT_ARRAY || r.via.array.size < 4)
            continue;

        msgpack_object *f = r.via.array.ptr;
        if (f[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[1].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[2].type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
            f[3].type != MSGPACK_OBJECT_POSITIVE_INTEGER)
            continue;

        SourceRange trustRange{SourceLoc{static_cast<uint32_t>(f[0].via.u64)}, SourceLoc{static_cast<uint32_t>(f[1].via.u64)}};
        SourceRange cppRange{SourceLoc{static_cast<uint32_t>(f[2].via.u64)}, SourceLoc{static_cast<uint32_t>(f[3].via.u64)}};
        if (!addRangeMapping(trustRange, cppRange)) {
            msgpack_unpacked_destroy(&msg);
            return false;
        }
    }

    // [1] name_mappings
    // Диапазоны уже восстановлены в [0], добавляем только name-информацию напрямую.
    if (root[1].type != MSGPACK_OBJECT_ARRAY) {
        msgpack_unpacked_destroy(&msg);
        return false;
    }
    msgpack_object &namesArray = root[1];
    for (uint32_t i = 0; i < namesArray.via.array.size; ++i) {
        msgpack_object &v = namesArray.via.array.ptr[i];
        if (v.type != MSGPACK_OBJECT_ARRAY || v.via.array.size < 6)
            continue;

        msgpack_object *f = v.via.array.ptr;
        if (f[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[1].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[2].type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
            f[3].type != MSGPACK_OBJECT_POSITIVE_INTEGER || f[4].type != MSGPACK_OBJECT_STR || f[5].type != MSGPACK_OBJECT_STR)
            continue;

        SourceRange trustRange{SourceLoc{static_cast<uint32_t>(f[0].via.u64)}, SourceLoc{static_cast<uint32_t>(f[1].via.u64)}};
        SourceRange cppRange{SourceLoc{static_cast<uint32_t>(f[2].via.u64)}, SourceLoc{static_cast<uint32_t>(f[3].via.u64)}};
        std::string trustName(f[4].via.str.ptr, f[4].via.str.size);
        std::string cppName(f[5].via.str.ptr, f[5].via.str.size);

        m_nameMappings.push_back({{trustRange, cppRange}, std::move(trustName), std::move(cppName)});
        m_cppToTrustName.emplace(cppName, trustName);
    }

    msgpack_unpacked_destroy(&msg);
    return true;
}

} // namespace trust