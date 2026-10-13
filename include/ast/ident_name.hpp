// include/ast/ident_name.hpp
// IdentName - AST-узел для идентификатора с методами проверки, нормализации, манглинга.

#pragma once

#include "ast/token_base.hpp"
#include "ast/slots/slot_engine.hpp"
#include "attrs/attr_builtin.hpp"
#include "location/module_index.hpp"
#include "types/type_id.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class AttrPool; // forward declaration

/// Единственная точка правила имени: срез ведущего ':' (TypeName) и хвостового '^'
/// (кроме специального имени "$^"), интернирование итогового spelling и attr::ReadOnly.
/// Текст хранится в mixin HasText узла (text()); сам raw здесь не хранится.
void assignNameFromRaw(AstNodeAttr& node, HasText& text, ParserToken::Kind kind, std::string_view raw, AttrPool* pool);

/// IdentName - узел AST для хранения идентификатора.
/// kind всегда ParserToken::Kind::Ident.
/// Имя хранится в mixin HasText (интернированный view) и доступно через text().
class IdentName : public WithText<AstNodeAttr> {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_IdentName)
    IdentName()
    : WithText<AstNodeAttr>() {
        m_kind = ParserToken::Kind::Ident;
    }

    explicit IdentName(std::string name, AttrPool* pool = nullptr);

    /// Терм-конструктор: имя читается из Term и нормализуется ровно в одном месте
    /// (assignNameFromRaw): срез '^' (кроме "$^") + attr::ReadOnly. Объявлен здесь, определён в .cpp.
    IdentName(TermPtr term, AttrPool* pool = nullptr);
    /// Uniform term-constructor for the generated factory (kind всегда Ident).
    /// Дети не строятся (Ident→CallExpr решается в override visit_NAME).
    /// ctx не используется: ReadOnly для этого пути применяет convertAttrsToNode (idempotent).
    IdentName(ParserToken::Kind /*k*/, TermPtr term, Context* /*ctx*/ = nullptr)
    : IdentName(term) {}

    /// Объявленный узел, к которому разрешилась ссылка (VarDecl/ArgNode/цель DestructureDecl).
    /// Невладеющая ссылка, заполняется семантикой при resolution; nullptr - имя не разрешено
    /// (или это не value-ссылка). Нужен flow-анализам после обхода (SymbolTable уже пуста).
    const AstNodeBase* resolvedDecl = nullptr;

    /// Тип, ОБОЗНАЧАЕМЫЙ этим идентификатором (`Color.RED`, `MyStr.count`, `Cls.st()`,
    /// `Big ::= MyInt`, callee-конструктор, имя `TypeDecl`). INVALID — идентификатор не тип.
    /// Материализует семантика; кодоген рендерит тип-операнд по этому полю и НЕ резолвит имя.
    /// `mutable`: факт пишется через логически-const сервисы (`lookupOrError`/`recordTypeName`).
    mutable TypeId resolvedTypeId{INVALID_TYPE_ID};

    /// Type-state ЗНАЧЕНИЯ, на которое ссылается идентификатор (identity + const/inferred).
    /// Материализует семантика в точке использования и НЕ меняет после (аналог типа
    /// DeclRefExpr у Clang). Invalid — идентификатор не является value-ссылкой
    /// (тип-имя, `_`, runtime/интринсик, неразрешённое имя). Авторитет для `exprState`.
    QualType valueType{};

    /// Идентификатор обозначает тип (а не значение).
    [[nodiscard]] bool isTypeName() const noexcept { return resolvedTypeId.isValid(); }

    /// Модуль ссылочной формы `\path::name` — индекс УЖЕ загруженного модуля (загрузки нет).
    /// `text()` при этом хранит полный spelling (`\a::f`); семантика нормализует его на
    /// найденное trust-имя (как `$x`/`%x`). `nullopt` — обычное имя.
    std::optional<ModuleIndex> moduleRef;

    /// text() — интернированный view из HasText.

    /// Раскрывает ведущий квалификатор "@::" на текущую область имён namespace_path
    /// (пустая - без префикса): `@::x` + ns=`a::b` → `a::b::x`. Возвращает true, если имя
    /// было изменено. Используется анализатором для контекст-макроса `@:: foo`.
    bool expandQualified(std::string_view namespace_path);

    /// Implicit conversion to string_view for compatibility.
    operator std::string_view() const noexcept { return text(); }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    // -- Тип имени --
    [[nodiscard]] bool is_simple() const noexcept;
    [[nodiscard]] bool is_qualified() const noexcept;
    [[nodiscard]] bool is_special() const noexcept;
    [[nodiscard]] bool is_internal() const noexcept;

    // -- Квалификаторные признаки --
    [[nodiscard]] bool is_macro() const noexcept;           // начинается с '@'
    [[nodiscard]] bool is_local() const noexcept;           // начинается с '$' (одиночный, не $$)
    [[nodiscard]] bool is_static() const noexcept;          // содержит '::'
    [[nodiscard]] bool is_field() const noexcept;           // начинается с '.'
    [[nodiscard]] bool is_module() const noexcept;          // начинается с '\'
    [[nodiscard]] bool is_type() const noexcept;            // начинается с ':'
    [[nodiscard]] bool is_native() const noexcept;          // начинается с '%'
    [[nodiscard]] bool is_absolute_module() const noexcept; // '\\' в начале
    [[nodiscard]] bool is_relative_module() const noexcept; // '\' в начале (одинарный)

    // -- Специальные имена --
    [[nodiscard]] bool is_arg_ref() const noexcept;     // $1..$N
    [[nodiscard]] bool is_self() const noexcept;        // $0
    [[nodiscard]] bool is_parent() const noexcept;      // $$
    [[nodiscard]] bool is_args_dict() const noexcept;   // $*
    [[nodiscard]] bool is_last_result() const noexcept; // $^

    // -- Имя без квалификатора --
    [[nodiscard]] std::string_view bareName() const noexcept;

    // -- Валидация --
    static constexpr size_t max_name_length = 64;
    static bool is_valid_simple_name(std::string_view s) noexcept;
    static bool is_valid_module_name(std::string_view s) noexcept;

    // -- Разбивка квалифицированного имени на фрагменты --
    [[nodiscard]] std::vector<std::string_view> parts() const;

    // -- Манглинг / деманглинг --
    [[nodiscard]] IdentName mangle(std::string_view module_name) const;
    static IdentName demangle(std::string_view mangled);

    // -- Преобразование имени модуля в файловый путь --
    static std::filesystem::path module_name_to_path(std::string_view module_name, const std::filesystem::path& base_dir,
                                                     const std::filesystem::path& sys_dir = "/");

    // -- Преобразование файлового пути в имя модуля --
    static IdentName path_to_module_name(const std::filesystem::path& path, const std::filesystem::path& base_dir);

    // -- Сравнение по тексту (для замены std::string) --
    friend bool operator==(const IdentName& a, const IdentName& b) noexcept { return a.text() == b.text(); }
    friend bool operator!=(const IdentName& a, const IdentName& b) noexcept { return a.text() != b.text(); }
    friend bool operator==(const IdentName& a, std::string_view b) noexcept { return a.text() == b; }
    friend bool operator==(std::string_view a, const IdentName& b) noexcept { return a == b.text(); }

  protected:
    /// Терм-конструктор с явным kind (IdentType → TypeName): raw = term->getText().
    IdentName(TermPtr term, ParserToken::Kind k, AttrPool* pool = nullptr);

    /// Manual-конструктор с явным kind (без TermPtr).
    IdentName(ParserToken::Kind k, std::string_view name, AttrPool* pool);

    /// Терм-конструктор с ЯВНЫМ raw-именем (Decl-семейство: имя может лежать в m_left,
    /// а не в тексте операторного терма). range берётся из term, текст — из rawName.
    IdentName(ParserToken::Kind k, TermPtr term, std::string_view rawName, AttrPool* pool);

    /// Kind-only (текст задаёт наследник через assignNameFromRaw).
    explicit IdentName(ParserToken::Kind k)
    : WithText<AstNodeAttr>(k) {}

    // Имя хранится в mixin HasText (view), доступно через text().
};

} // namespace trust
