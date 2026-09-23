#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <msgpack.h>

namespace trust {

// ══════════════════════════════════════════════════════════════
//           Константы формата msgpack-массива верхнего уровня
// ══════════════════════════════════════════════════════════════

enum : uint8_t {
    kFieldMajor = 0,        // uint8 - TRUST_VERSION_MAJOR
    kFieldMinor = 1,        // uint8 - TRUST_VERSION_MINOR
    kFieldInputFiles = 2,   // array of strings
    kFieldOutputFiles = 3,  // array of strings
    kFieldInputHashes = 4,  // array of uint64
    kFieldOutputHashes = 5, // array of uint64
    kFieldRanges = 6,       // array of input-grouped range entries
    kFieldNames = 7,        // array of input-grouped name entries
    kFieldMacros = 8,       // array of input-grouped macro entries (body→def)
    kFieldCount = 9,        // общее количество полей
};

// ══════════════════════════════════════════════════════════════
//           Константы для grouped range entry
// ══════════════════════════════════════════════════════════════
//
// Формат: [[[beginOff, delta, cppBeginOff, cppDelta], ...], ...]
//
// Группировка: двухуровневая (input → output).
// trustFileIdx и cppFileIdx не хранятся - вычисляются из позиции:
//   trustFileIdx = inputIdx + 1
//   cppFileIdx   = outputIdx + 1 с флагом FILE_BITS
//
// delta = endOffset - beginOffset (0 для точечной позиции)

enum : uint8_t {
    kRangeGroupFieldBeginOff = 0,
    kRangeGroupFieldDelta = 1,
    kRangeGroupFieldCppBeginOff = 2,
    kRangeGroupFieldCppDelta = 3,
    kRangeGroupFieldCount = 4,
};

// ══════════════════════════════════════════════════════════════
//           Константы для grouped name entry
// ══════════════════════════════════════════════════════════════
//
// Формат: [[[beginOff, delta, cppBeginOff, cppDelta,
//            trustName, cppName], ...], ...]
//
// Группировка: двухуровневая (input → output), как и для range.
// trustFileIdx и cppFileIdx не хранятся.

enum : uint8_t {
    kNameGroupFieldBeginOff = 0,
    kNameGroupFieldDelta = 1,
    kNameGroupFieldCppBeginOff = 2,
    kNameGroupFieldCppDelta = 3,
    kNameGroupFieldTrustName = 4,
    kNameGroupFieldCppName = 5,
    kNameGroupFieldCount = 6,
};

// ══════════════════════════════════════════════════════════════
//           Константы для grouped macro entry (input→input)
// ══════════════════════════════════════════════════════════════
//
// Формат: [[[bodyBeginOff, bodyDelta, defBeginOff, defDelta], ...], ...]
//
// Группировка: двухуровневая (input → input).
// Оба FileIdx вычисляются как input (без флага OUTPUT_BIT).
//
// body - диапазон тела макроса (куда сработал макрос),
// def  - диапазон определения макроса.

enum : uint8_t {
    kMacroGroupFieldBodyBeginOff = 0,
    kMacroGroupFieldBodyDelta = 1,
    kMacroGroupFieldDefBeginOff = 2,
    kMacroGroupFieldDefDelta = 3,
    kMacroGroupFieldCount = 4,
};

// ══════════════════════════════════════════════════════════════
//                        MsgpackWriter
// ══════════════════════════════════════════════════════════════
//
// RAII-обёртка над msgpack_sbuffer + msgpack_packer.
// Не копируется, перемещается.
//
class MsgpackWriter {
  public:
    MsgpackWriter()
    : m_packer(nullptr) {
        msgpack_sbuffer_init(&m_sbuf);
        m_packer = msgpack_packer_new(&m_sbuf, msgpack_sbuffer_write);
    }

    ~MsgpackWriter() {
        if (m_packer != nullptr) {
            msgpack_packer_free(m_packer);
        }
        msgpack_sbuffer_destroy(&m_sbuf);
    }

    // Non-copyable
    MsgpackWriter(const MsgpackWriter&) = delete;
    MsgpackWriter& operator=(const MsgpackWriter&) = delete;

    // Movable
    MsgpackWriter(MsgpackWriter&& other) noexcept
    : m_sbuf(other.m_sbuf)
    , m_packer(other.m_packer) {
        msgpack_sbuffer_init(&other.m_sbuf);
        other.m_packer = nullptr;
    }

    MsgpackWriter& operator=(MsgpackWriter&& other) noexcept {
        if (this != &other) {
            if (m_packer != nullptr) {
                msgpack_packer_free(m_packer);
            }
            msgpack_sbuffer_destroy(&m_sbuf);
            m_sbuf = other.m_sbuf;
            m_packer = other.m_packer;
            msgpack_sbuffer_init(&other.m_sbuf);
            other.m_packer = nullptr;
        }
        return *this;
    }

    // -- Методы упаковки --

    void packArray(uint32_t n) { msgpack_pack_array(m_packer, n); }
    void packUint8(uint8_t v) { msgpack_pack_uint8(m_packer, v); }
    void packUint32(uint32_t v) { msgpack_pack_uint32(m_packer, v); }
    void packUint64(uint64_t v) { msgpack_pack_uint64(m_packer, v); }
    void packString(std::string_view sv) {
        msgpack_pack_str(m_packer, sv.size());
        msgpack_pack_str_body(m_packer, sv.data(), sv.size());
    }

    // -- Доступ к данным msgpack_sbuffer (rvalue-квалифицированный) --
    // После вызова writer переходит в moved-from состояние.
    msgpack_sbuffer take_sbuf() && {
        msgpack_sbuffer result = m_sbuf;
        msgpack_sbuffer_init(&m_sbuf);
        if (m_packer != nullptr) {
            msgpack_packer_free(m_packer);
        }
        m_packer = nullptr;
        return result;
    }

  private:
    msgpack_sbuffer m_sbuf;
    msgpack_packer* m_packer;
};

// ══════════════════════════════════════════════════════════════
//                        MsgpackReader
// ══════════════════════════════════════════════════════════════
//
// RAII-обёртка над msgpack_unpacked.
// Конструктор принимает сырые данные и парсит первый объект.
//
class MsgpackReader {
  public:
    MsgpackReader() { msgpack_unpacked_init(&m_msg); }

    // Парсит первый msgpack-объект из data[0..size-1].
    // При ошибке парсинга или отсутствии данных is_valid() == false.
    MsgpackReader(const uint8_t* data, size_t size) {
        msgpack_unpacked_init(&m_msg);
        parse(data, size);
    }

    ~MsgpackReader() { msgpack_unpacked_destroy(&m_msg); }

    // Non-copyable
    MsgpackReader(const MsgpackReader&) = delete;
    MsgpackReader& operator=(const MsgpackReader&) = delete;

    // Movable
    MsgpackReader(MsgpackReader&& other) noexcept
    : m_msg(other.m_msg)
    , m_valid(other.m_valid) {
        msgpack_unpacked_init(&other.m_msg);
        other.m_valid = false;
    }

    MsgpackReader& operator=(MsgpackReader&& other) noexcept {
        if (this != &other) {
            msgpack_unpacked_destroy(&m_msg);
            m_msg = other.m_msg;
            m_valid = other.m_valid;
            msgpack_unpacked_init(&other.m_msg);
            other.m_valid = false;
        }
        return *this;
    }

    // Парсит первый msgpack-объект из data[0..size-1].
    // При успехе is_valid() == true.
    void parse(const uint8_t* data, size_t size) {
        size_t off = 0;
        const char* msgpack_data = reinterpret_cast<const char*>(data);
        msgpack_unpack_return ret = msgpack_unpack_next(&m_msg, msgpack_data, size, &off);
        m_valid = (ret == MSGPACK_UNPACK_SUCCESS);
    }

    bool is_valid() const { return m_valid; }

    // Корневой объект. Вызывать только если is_valid() == true.
    const msgpack_object& root() const { return m_msg.data; }

  private:
    msgpack_unpacked m_msg;
    bool m_valid = false;
};

} // namespace trust