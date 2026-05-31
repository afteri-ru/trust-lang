// attr_pool.hpp — AttrPoolView (read-only interface), AttrPool (mutable registry),
// and ModuleApi (self-contained read-only snapshot with token records) for attributes
//
// AttrPool is the single point of registration for both built-in and
// user-defined attributes. There is no AttrType distinction — every
// attribute is simply a name + optional default parameter values.
//
// AttrPoolView exposes only read-only queries; AttrPool adds registration
// and mutation. export_attrs() returns a self-contained ModuleApi that owns
// all its string data and stores token records without modifying the input.
//
// Usage:
//   AttrPool pool;
//   AttrId id = pool.register_attr("readonly");
//   AttrId id2 = pool.register_attr("my_attr", {AttrParam(int64_t(42))});
//
// If an attribute with the same name already exists, the existing AttrId
// is returned (parameter types are validated on match).
//
// Note: AttrSet is an internal optimization and is not exposed in the public API.
// Use resolve() to flatten an AttrId to individual singletons.
// Use add_multi() for optimal batch attribute assignment to a terminal.

#pragma once

#include "ast/attr.hpp"
#include "diag/diag.hpp"
#include "diag/msgpack_util.hpp"
#include <string_view>
#include <string>
#include <vector>
#include <cstddef>
#include <array>
#include <set>
#include <unordered_map>
#include <span>
#include <memory>

namespace trust {

// Forward declarations
struct TokenInfo;
class Context;

// ────────────────────────────────────────────────────────────────────────────
// AttrPoolView — read-only interface for attribute queries
// ────────────────────────────────────────────────────────────────────────────

class AttrPoolView {
  public:
    virtual ~AttrPoolView() = default;

    // Non-copyable, movable
    AttrPoolView(const AttrPoolView&) = delete;
    AttrPoolView& operator=(const AttrPoolView&) = delete;
    AttrPoolView(AttrPoolView&&) noexcept = default;
    AttrPoolView& operator=(AttrPoolView&&) noexcept = default;

    // ── Queries ──

    /// Look up an attribute by name, return AttrId or nullopt if not found (O(1)).
    [[nodiscard]] virtual std::optional<AttrId> lookup(std::string_view name) const noexcept = 0;

    /// Check if an attribute with the given name exists in the pool (O(1)).
    [[nodiscard]] virtual bool has_attr(std::string_view name) const noexcept = 0;

    /// Get the name of an attribute by its ID (O(1)).
    [[nodiscard]] virtual std::string_view get_name(AttrId id) const = 0;

    /// Resolve an AttrId to a flat vector of singleton attribute IDs.
    /// If id is a singleton, returns {id}.
    /// If id is a set, recursively collects all members, flattening nested sets.
    [[nodiscard]] virtual std::vector<AttrId> resolve(AttrId id) const = 0;

    /// Number of registered singleton attributes
    [[nodiscard]] virtual std::size_t attr_count() const noexcept = 0;

    /// Number of registered sets
    [[nodiscard]] virtual std::size_t set_count() const noexcept = 0;

  protected:
    AttrPoolView() = default;
};

// ────────────────────────────────────────────────────────────────────────────
// AttrPool — mutable attribute registry (inherits AttrPoolView)
// ────────────────────────────────────────────────────────────────────────────

class ModuleApi; // forward declaration

class AttrPool : public AttrPoolView {
  public:
    AttrPool();
    ~AttrPool() override = default;

    // Non-copyable, movable
    AttrPool(const AttrPool&) = delete;
    AttrPool& operator=(const AttrPool&) = delete;
    AttrPool(AttrPool&&) noexcept = default;
    AttrPool& operator=(AttrPool&&) noexcept = default;

    // ── Singleton attribute registration ──

    /// Register an attribute with given name and required parameter types.
    /// For built-in attributes, pass the corresponding BuiltinAttrKind (default kNone for user-defined).
    /// Default parameter values are dummy placeholders of the specified types.
    AttrId register_attr(std::string_view name, std::vector<AttrParamType> required_param_types = {}, BuiltinAttrKind kind = BuiltinAttrKind::kNone);

    /// Register an attribute with concrete parameter values.
    /// Required param types are derived from the given params.
    AttrId register_attr(std::string_view name, std::vector<AttrParam> default_params, BuiltinAttrKind kind = BuiltinAttrKind::kNone);

    /// Register a variadic attribute where the last (or only) required param type
    /// can repeat 0+ times. The base_type specifies the type of each variadic argument.
    AttrId register_variadic_attr(std::string_view name, AttrParamType base_type, BuiltinAttrKind kind = BuiltinAttrKind::kNone);

    // ── Export ──

    /// Create a self-contained ModuleApi with copies of attributes used by
    /// the given tokens, owning all string data and token records.
    ///
    /// - Collects all AttrId from tokens (resolves sets into singletons)
    /// - Registers singletons in new pool (deduplicating), creates sets
    /// - Stores token name + new AttrId list in ModuleApi (does NOT modify input tokens)
    /// - Source pool (*this) remains unchanged.
    [[nodiscard]] std::unique_ptr<ModuleApi> export_attrs(std::span<TokenInfo*> tokens);

    // ── Batch attribute registration (optimal representation) ──

    /// Register a group of attributes together, finding the most compact representation.
    ///
    /// Steps:
    /// 1. Sort input IDs, remove duplicates.
    /// 2. Search for an existing set with exact match → return {set_id}.
    /// 3. Search for the set with maximum overlap with the input IDs.
    /// 4. If create_set is true: create a new set = best_matching + missing IDs, return {new_set_id}.
    /// 5. If create_set is false: return {best_matching_set_id, missing_id1, missing_id2, ...}.
    ///
    /// @return A minimal vector of AttrIds representing all input attributes.
    [[nodiscard]] std::vector<AttrId> add_multi(std::vector<AttrId> ids, bool create_set = true);

    // ── AttrPoolView overrides ──

    [[nodiscard]] std::optional<AttrId> lookup(std::string_view name) const noexcept final;

    [[nodiscard]] bool has_attr(std::string_view name) const noexcept final;

    [[nodiscard]] std::string_view get_name(AttrId id) const final;

    [[nodiscard]] bool has_attr(BuiltinAttrKind kind) const noexcept;

    /// Get a singleton attribute by ID (FAULT if this is a set).
    [[nodiscard]] const Attr& get(AttrId id) const {
        EXPECT(!is_set(id));
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_attrs.size());
        return m_attrs[idx];
    }

    [[nodiscard]] std::vector<AttrId> resolve(AttrId id) const final { return detail::resolve_attr_set(id, m_sets); }

    [[nodiscard]] std::size_t attr_count() const noexcept final { return m_attrs.size(); }

    [[nodiscard]] std::size_t set_count() const noexcept final { return m_sets.size(); }

    [[nodiscard]] AttrId builtin_id(BuiltinAttrKind kind) const {
        auto idx = static_cast<std::size_t>(kind);
        EXPECT(idx > 0 && idx < kBuiltinAttrCount);
        EXPECT(m_builtin_ids[idx] != 0);
        return m_builtin_ids[idx];
    }

    [[nodiscard]] bool is_builtin(AttrId id, BuiltinAttrKind kind) const { return !is_set(id) && id == builtin_id(kind); }

  private:
    /// Internal registration: all public register methods delegate here.
    /// @param name          attribute name
    /// @param default_params default parameter values (empty = no params)
    /// @param variadic      if true, the last param type can repeat 0+ times
    /// @param kind          built-in kind or kNone for user-defined
    AttrId register_attr_impl(std::string_view name, std::vector<AttrParam> default_params, bool variadic, BuiltinAttrKind kind);

    /// Check if ID is a set (internal)
    [[nodiscard]] static bool is_set(AttrId id) noexcept { return (id & detail::kAttrSetFlag) != 0; }

    /// Get a set by ID (internal)
    [[nodiscard]] const AttrSet& get_set(AttrId id) const {
        EXPECT(is_set(id));
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_sets.size());
        return m_sets[idx];
    }

    /// Record the AttrId for a built-in kind (called during registration)
    void set_builtin_id(BuiltinAttrKind kind, AttrId id) {
        auto idx = static_cast<std::size_t>(kind);
        EXPECT(idx > 0 && idx < kBuiltinAttrCount);
        // Built-in ID slot must be assigned exactly once
        EXPECT(m_builtin_ids[idx] == 0);
        m_builtin_ids[idx] = id;
    }

    /// Create a set of attributes (or return existing one).
    /// Duplicate IDs in input are silently removed.
    AttrId add_set(std::vector<AttrId> ids);

    /// Create a placeholder AttrParam of the given type (for use as default).
    [[nodiscard]] static AttrParam make_placeholder_param(AttrParamType type);

  public:
    /// Intern a string into the pool (stable set storage + dedup).
    [[nodiscard]] std::string_view intern(std::string_view s) {
        if (s.empty())
            return {};
        // Use transparent comparator for heterogeneous lookup with string_view
        auto it = m_strings.find(s);
        if (it != m_strings.end())
            return std::string_view(*it);
        return std::string_view(*m_strings.emplace(s).first);
    }

  public:
    /// Set the Context for this pool (needed for resolving MapperRange to string_view).
    void set_context(class Context& ctx) { m_ctx = &ctx; }

    /// Get the Context pointer (may be null if not set).
    [[nodiscard]] class Context* get_context() const noexcept { return m_ctx; }

  private:
    std::vector<Attr> m_attrs;
    std::vector<AttrSet> m_sets;
    std::set<std::string, std::less<>> m_strings;              // deduplicated string storage (stable references, hetero lookup)
    std::array<AttrId, kBuiltinAttrCount> m_builtin_ids{};     // indexed by BuiltinAttrKind
    std::unordered_map<std::string_view, AttrId> m_name_to_id; // O(1) lookup by name (string_view into set)
    class Context* m_ctx{nullptr};                             ///< Optional Context for resolving MapperRange → string_view.
};

// ────────────────────────────────────────────────────────────────────────────
// ModuleTokenRecord — a single token record stored in ModuleApi
// ────────────────────────────────────────────────────────────────────────────

/// A single token record: token name (as prefix + PackedName into m_name_data) and its attribute.
/// The AttrId value is valid within the owning ModuleApi.
/// m_attr == 0 means no attributes, singleton AttrId means one attribute,
/// set-flagged AttrId means a group of attributes.
struct ModuleTokenRecord {
    uint32_t m_prefix_id{0}; ///< Index into ModuleApi::m_prefixes (0 = no prefix)
    PackedName m_name;       ///< Offset+length of local name (suffix) in ModuleApi::m_name_data
    AttrId m_attr{0};        ///< Attribute ID in the owning ModuleApi (0 = none, set-flag = group)
};

// ────────────────────────────────────────────────────────────────────────────
// ModuleAttrRecord — a single attribute record stored in ModuleApi
// ────────────────────────────────────────────────────────────────────────────

/// A single attribute record: attribute name and its parameter values stored
/// as string fragments (PackedName) in ModuleApi::m_name_data.
struct ModuleAttrRecord {
    PackedName m_name;                ///< Offset+length of attribute name in ModuleApi::m_name_data
    std::vector<PackedName> m_params; ///< Parameter values (serialized as strings) in ModuleApi::m_name_data
};

// ────────────────────────────────────────────────────────────────────────────
// ModuleApi — self-contained read-only snapshot of attribute data + token records
// ────────────────────────────────────────────────────────────────────────────

class ModuleApi : public AttrPoolView {
  public:
    ModuleApi() = default;
    ~ModuleApi() override = default;

    // Non-copyable, movable
    ModuleApi(const ModuleApi&) = delete;
    ModuleApi& operator=(const ModuleApi&) = delete;
    ModuleApi(ModuleApi&&) noexcept = default;
    ModuleApi& operator=(ModuleApi&&) noexcept = default;

    // ── Token record access ──

    /// Access all token records stored in this ModuleApi.
    [[nodiscard]] const std::vector<ModuleTokenRecord>& tokens() const noexcept { return m_tokens; }

    /// Number of token records.
    [[nodiscard]] std::size_t token_count() const noexcept { return m_tokens.size(); }

    /// Get the full name of a token record by index.
    [[nodiscard]] std::string get_token_name(std::size_t index) const;

    /// Create a TokenInfo from a token record by index.
    /// Attributes are registered into the given AttrPool (or re-use existing IDs).
    /// EXPECT fails if index is out of range.
    [[nodiscard]] std::shared_ptr<TokenInfo> create_token(std::size_t index, AttrPool& pool) const;

    // ── Сериализация/десериализация (msgpack + zlib) ──

    /// Сериализовать данные в msgpack-формат с zlib-сжатием.
    /// Формат: [orig_size_LE4][zlib_compressed][MD5_checksum_8]
    [[nodiscard]] std::vector<char> packToMsgpack() const;

    /// Десериализовать из msgpack (полный статический factory).
    /// Ожидает формат: [orig_size_LE4][zlib_compressed][MD5_checksum_8]
    static std::unique_ptr<ModuleApi> fromMsgpack(const uint8_t* data, size_t size);

    // ── Сохранение/загрузка из файла ──

    /// Сохранить сериализованные данные в файл.
    /// Возвращает true при успешной записи.
    bool save_to_file(const std::string& path) const;

    /// Загрузить ModuleApi из файла.
    /// Возвращает nullptr при ошибке (файл не найден, повреждённые данные, etc).
    static std::unique_ptr<ModuleApi> load_from_file(const std::string& path);

    // ── AttrPoolView overrides ──

    [[nodiscard]] std::optional<AttrId> lookup(std::string_view name) const noexcept override;

    [[nodiscard]] bool has_attr(std::string_view name) const noexcept override;

    [[nodiscard]] std::string_view get_name(AttrId id) const override;

    [[nodiscard]] std::vector<AttrId> resolve(AttrId id) const override;

    [[nodiscard]] std::size_t attr_count() const noexcept override { return m_attr_records.size(); }

    [[nodiscard]] std::size_t set_count() const noexcept override { return m_sets.size(); }

  private:
    friend class AttrPool; // AttrPool fills this class during export_attrs()

    /// Внутренняя десериализация из уже распарсенного msgpack_object.
    static std::unique_ptr<ModuleApi> unpackFromMsgpackObject(const msgpack_object& obj);

    std::vector<ModuleAttrRecord> m_attr_records;
    std::vector<AttrSet> m_sets;
    std::set<std::string, std::less<>> m_strings;              // owns all string data for attrs and names
    std::unordered_map<std::string_view, AttrId> m_name_to_id; // O(1) lookup by name (string_view into set)
    std::vector<ModuleTokenRecord> m_tokens;                   // token records (prefix + suffix + attrs)

    // Name buffer: all token names and attribute names stored here.
    // m_prefixes[i] = {offset, length} in m_name_data; [0] = {0, 0} (empty prefix).
    std::vector<char> m_name_data;
    std::vector<PackedName> m_prefixes{{PackedName{0, 0}}}; // offset, length; index 0 = empty prefix

    // Internal helpers

    /// Reconstruct full token name from a record.
    [[nodiscard]] std::string get_full_name(const ModuleTokenRecord& rec) const;

    [[nodiscard]] static bool is_set(AttrId id) noexcept { return (id & detail::kAttrSetFlag) != 0; }

    [[nodiscard]] const AttrSet& get_set(AttrId id) const {
        EXPECT(is_set(id));
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_sets.size());
        return m_sets[idx];
    }

    /// Intern a string into the exported pool (stable set storage + dedup).
    [[nodiscard]] std::string_view intern(std::string_view s) {
        if (s.empty())
            return {};
        auto it = m_strings.find(s);
        if (it != m_strings.end())
            return std::string_view(*it);
        return std::string_view(*m_strings.emplace(s).first);
    }

    /// Serialize an AttrParam to a string and store in m_name_data.
    /// Returns a PackedName referencing the stored fragment.
    [[nodiscard]] PackedName append_param_data(const AttrParam& param);

    /// Find the longest prefix in m_prefixes matching a string.
    /// Returns {index, prefix_length_as_offset} or {0, 0} if none.
    /// The returned PackedName: offset=prefix_index, length=prefix_byte_length.
    [[nodiscard]] std::pair<uint32_t, uint8_t> find_longest_prefix(std::string_view s) const;

    /// Add a string to m_name_data and return its PackedName.
    [[nodiscard]] PackedName append_name_data(std::string_view s);
};

} // namespace trust