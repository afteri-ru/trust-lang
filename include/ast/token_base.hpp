#pragma once

#include "ast/token.hpp"
#include "ast/slots/slot_engine.hpp"
#include "attrs/attr.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "syntax/term/term_types.h"
#include "types/visibility.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"
#include "utils/partial_switch.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trust {

// Forward declaration (full definition in ast/convert_services.hpp). Нужен для uniform
// терм-конструктора (kind, term, ConvertServices* ctx) генерируемой фабрики.
struct ConvertServices;

// Forward declarations
class AttrPool;
class AstNodeAttr;
class Sequence;
class HasText;
enum class PropertyKind; // ast/trust_prop.hpp (тип trust-контракта: pre/post/check/...)

/// Может ли узел данного kind нести признак иммутабельности (суффикс '^' в тексте имени)?
[[nodiscard]] inline bool canHaveImmutableQualifier(ParserToken::Kind kind) noexcept {
    // Частичный предикат множества: неперечисленные kinds иммутабельность не несут.
    TRUST_PARTIAL_ENUM_SWITCH_BEGIN
    switch (kind) {
    case ParserToken::Kind::Ident:       // имена: arg^, @name^, $name^, value^
    case ParserToken::Kind::TypeName:    // :Type^
    case ParserToken::Kind::ArgNode:     // параметры: arg^, $1^
    case ParserToken::Kind::VarDecl:     // x^
    case ParserToken::Kind::NameDecl:    // x^ := ...
    case ParserToken::Kind::FuncDecl:    // func^
    case ParserToken::Kind::StructDecl:  // class^
    case ParserToken::Kind::RefTakeExpr: // *^ - take с иммутабельностью
    case ParserToken::Kind::RefMakeExpr: // &^ - ptr с иммутабельностью
    case ParserToken::Kind::CallExpr:    // method^() - const-вызов: '^' на имени callee (ReadOnly на вызове)
        return true;
    default:
        return false;
    }
    TRUST_PARTIAL_ENUM_SWITCH_END
}

/// kind, чей текст является ИМЕНЕМ (носитель правила assignNameFromRaw):
/// идентификаторы/типы/объявления/аргументы. Только для них применяется срез ':'/'^'.
[[nodiscard]] inline bool isNameCarrierKind(ParserToken::Kind kind) noexcept {
    TRUST_PARTIAL_ENUM_SWITCH_BEGIN
    switch (kind) {
    case ParserToken::Kind::Ident:
    case ParserToken::Kind::TypeName:
    case ParserToken::Kind::VarDecl:
    case ParserToken::Kind::NameDecl:
    case ParserToken::Kind::FuncDecl:
    case ParserToken::Kind::ClassDecl:
    case ParserToken::Kind::StructDecl:
    case ParserToken::Kind::ArgNode:
        return true;
    default:
        return false;
    }
    TRUST_PARTIAL_ENUM_SWITCH_END
}

/// AstNodeBase - базовый класс для всех узлов AST.
/// Хранит kind и MapperRange (provenance). Term в узле НЕ хранится (используется только
/// в конструкторе для извлечения range). ТЕКСТ узла здесь не хранится: его несёт только
/// `WithText<Base>` (mixin `HasText`), поэтому «есть ли у узла текст» определяется классом.
class AstNodeBase {
  public:
    ParserToken::Kind kind() const noexcept { return m_kind; }

    /// Редкое, но осознанное исключение из инварианта «kind фиксируется при создании»:
    /// используется, когда терм-слой строит механически НЕоднозначный узел, а его класс
    /// определяется ПОЗЖЕ в анализаторе по типу из реестра (напр. литерал-конструкция
    /// `:Type(...)`/`(...):Type` становится кортежем Tuple только после резолва типа).
    /// `END` — не kind узла, а «kind ещё не задан» (default-конструктор): переход из него
    /// считается инициализацией. Переклассификация РЕАЛЬНЫХ kind допускается только внутри
    /// одного класса AST (`sameNodeClass`) — иначе смена затронула бы формат хранения/смысл
    /// текста (напр. перевод узла в оператор/блок).
    void setKind(ParserToken::Kind k) {
        EXPECT((m_kind == ParserToken::Kind::END || sameNodeClass(m_kind, k)) &&
               "setKind may only initialise an unset (END) kind or refine kind within the same AST node class");
        m_kind = k;
    }

    /// Если узел несёт собственный текст (класс наследует `WithText<Base>`), возвращает его
    /// хранилище; иначе nullptr. Текст читается ТОЛЬКО через него: у не-текстовых узлов
    /// метода text() нет вовсе (compile-time), а обобщённый код обязан проверить as_text().
    [[nodiscard]] virtual HasText* as_text() noexcept { return nullptr; }
    [[nodiscard]] virtual const HasText* as_text() const noexcept { return nullptr; }

    /// Диапазон в исходном файле. Для узла с term возвращает его range; для узла БЕЗ term
    /// (ручной/test-only, синтетический) - НЕВАЛИДНЫЙ range ({}), без EXPECT: родительские узлы
    /// вычисляют свой охват на лету по ранжам детей (см. Binary/ControlFlowStmt/MatchStmt/
    /// VarDecl/FuncDecl::range), поэтому ручные дети обязаны мягко сообщать отсутствие
    /// source-range. Виртуальный: синтетические узлы (LabelRef/SemicolonStmt/JumpStmt-lowering)
    /// переопределяют его, возвращая явно заданный range вместо чтения из term.
    [[nodiscard]] virtual MapperRange range() const { return m_range; }

    [[nodiscard]] bool empty() const noexcept { return m_kind == ParserToken::Kind::END; }

    /// If this node is an AstNodeAttr, returns itself; otherwise nullptr.
    [[nodiscard]] virtual AstNodeAttr* as_attr() noexcept { return nullptr; }
    [[nodiscard]] virtual const AstNodeAttr* as_attr() const noexcept { return nullptr; }

    /// If this node is a Sequence (or derived - ScopeBlock/ModuleNode), returns itself;
    /// otherwise nullptr. Позволяет обходчикам получать m_body без kind/static_cast.
    [[nodiscard]] virtual Sequence* as_sequence() noexcept { return nullptr; }
    [[nodiscard]] virtual const Sequence* as_sequence() const noexcept { return nullptr; }

    /// Типизированный доступ к узлу БЕЗ RTTI (замена dynamic_cast/static_pointer_cast):
    ///   is<T>() - true, если динамический класс узла - T или ПРОИЗВОДНЫЙ от T (проверка
    ///             базового класса тоже работает: is<IdentName>() верно для IdentType/VarDecl/
    ///             FuncDecl). Отображение kind->класс - из PARSER_TOKEN_KINDS.
    ///   as<T>() - downcast; при неверном kind/классе - FAULT (НЕ тихий nullptr).
    /// T обязан быть классом из канонического перечня (AST_SLOT_CLASSES, ast/slots/node_slots_x.hpp).
    /// Определения - в конце ast_nodes.hpp (нужны полные типы классов).
    template <class T>
    [[nodiscard]] bool is() const noexcept;
    template <class T>
    [[nodiscard]] T* as();
    template <class T>
    [[nodiscard]] const T* as() const;

    /// Установить канонический охват узла (вычисляется ОДИН раз при построении).
    void setRange(MapperRange r) noexcept { m_range = r; }

    /// Типизированный доступ к trust-контрактам объявления (`m_trust`): отдаёт ТОЛЬКО узлы
    /// TrustContract (без RTTI), устраняя повторяющийся фильтр dynamic_cast у потребителей.
    /// Пусто, если контрактов нет.
    [[nodiscard]] std::vector<const TrustContract*> trustContracts() const;
    [[nodiscard]] std::vector<TrustContract*> trustContracts();
    /// Есть ли trust-контракт указанного типа (pre/post/check/invariant/type).
    [[nodiscard]] bool hasTrustProperty(PropertyKind kind) const;

    /// Единый источник истины «kind → дети»: заполняет out указателями на дочерние
    /// слоты (позволяет заменять узлы при обходе, напр. раскрытие ContextMacro).
    /// Для листов - пусто. Не мутирует дерево. Определён в ast_children.cpp.
    void collectChildren(std::vector<AstNodePtr*>& out);

    /// Все дочерние узлы AST (обобщённый обход) - const-обёртка над collectChildren,
    /// возвращает копии узлов. Для листов - пустой вектор.
    /// Определён в ast_children.cpp (нужны полные типы узлов).
    [[nodiscard]] std::vector<AstNodePtr> children() const;

    /// Слоты узла: владеемые (Traversed/Internal) + невладеющие ссылки (см. ast/slots/node_slots_x.hpp).
    /// ЕДИНСТВЕННЫЙ источник «состав узла»; классы со слотами переопределяют метод макросом
    /// `AST_SLOT_METHOD(BASE, ROWS)` из ast/slots/slot_engine.hpp. Базовая реализация (ast_slots.cpp)
    /// визитирует только `m_trust` — слот, принадлежащий ЛЮБОМУ узлу.
    virtual void forEachSlot(AstSlotVisitor& v);

    /// Dump token contents for debugging
    [[nodiscard]] virtual std::string dump(size_t indent = 0) const;

    /// Документирующий комментарий (`///`, `##`, `/**`, т.ч. хвостовой `///<`/`##<`),
    /// привязанный к объявлению. Заполняется TermToAstConverter::convert из term->m_docs
    /// (см. include/syntax/term/term.h); для LSP hover/док (SymbolCollectorHook::finalize).
    /// Пуст, если у объявления нет документирующего комментария. Владелец текста — пул строк
    /// компиляции; запись — ТОЛЬКО через setDocumentation (инвариант владения: fast-path сеттера
    /// опирается на то, что текущее значение уже интернировано).
    std::string_view documentation;

    /// Записать документирующий комментарий как интернированный view пула строк компиляции
    /// (тот же путь, что у `HasText`). Пустая строка не интернируется; повторная запись того же
    /// view - no-op.
    void setDocumentation(std::string_view text) {
        if (text.data() == documentation.data() && text.size() == documentation.size()) {
            return;
        }
        documentation = internedTextSlot(text);
    }

    /// Trust-конструкции (pre/post/assert), привязанные к объявлению (`@( ... @)` и т.п.
    /// после имени в `:=`/`::=`). НЕ входят в children()/collectChildren - анализатор и
    /// транспилятор полностью их игнорируют. Заполняется TermToAstConverter::convert
    /// из конд-термов в name->m_sequence (см. include/syntax/term/term.h). Пуст по умолчанию.
    std::vector<AstNodePtr> m_trust;

    /// Узел ИСХОДНОЙ AST, из которого получен этот узел HIR-клона (единственное хранимое
    /// направление соответствия AST↔HIR). nullptr — узел AST (origin не нужен) либо узел,
    /// СОЗДАННЫЙ lowering'ом (источника в исходной AST нет). Невладеющая ссылка: исходная AST
    /// живёт дольше HIR (как и ссылки на узлы вне клонируемого дерева).
    const AstNodeBase* origin = nullptr;

    /// Видимость объявления (`trust::Visibility` — тот же тип, что у варианта наследования базы и
    /// у областей имён; маркеры `::`/`_::`/`__::`). Смысл зависит от контекста объявления:
    ///  - module-level объявление: Public — экспорт файла, Protected — доступно файлам той же
    ///    группы/пакета (`_::`), Private — только текущему файлу (`__::` и вне блока интерфейса);
    ///    единственный писатель — загрузчик модулей;
    ///  - член тела класса (VarDecl/FuncDecl): C++-доступ public/protected/private; ставит конвертер
    ///    Term→AST из standalone-маркера тела, читает кодоген (секции доступа). Класс-скоупы
    ///    регистрируются под `localKey`, поэтому на ключи/экспорт-поверхность это не влияет.
    using Visibility = trust::Visibility;
    Visibility visibility = Visibility::Public;

    [[nodiscard]] bool isFilePrivate() const noexcept { return visibility == Visibility::Private; }

    /// Объявление — реализация экспортного имени, не входящая в экспортную поверхность модуля
    /// (определение типа из тела за forward'ом интерфейса). Ставит загрузчик; SymbolTable не
    /// публикует такое объявление импортёрам — клиент видит только forward. Ключи регистрации
    /// и видимость НЕ меняются (тип остаётся тем же TypeId).
    bool isExportImplementation = false;

    /// Модуль, которому принадлежит узел (идентичность объявления в модели модулей).
    /// ЕДИНСТВЕННЫЙ писатель — загрузчик (штамп тела модуля сразу после конвертации Term→AST);
    /// семантика/кодоген только читают. `kInvalid` — узел вне модуля (stdlib DSL, синтетика).
    /// Факт НЕ выводится обратным сканом таблицы символов: ссылки несут узел объявления
    /// (`IdentName::resolvedDecl`), поэтому C++-имя получает модуль объявления, а не справочника.
    ModuleIndex module = ModuleIndex::invalid();

    AstNodeBase() = default;

    /// Kind is set at construction and (normally) never changes (like Clang's StmtClass).
    /// Единственное исключение - `setKind`, используемое анализатором для уточнения
    /// механически созданного kind (DictLiteral -> Tuple) по типу из реестра.
    virtual ~AstNodeBase() = default;

    /// Построить узел из term: из него извлекается ТОЛЬКО range. Текст (если класс его несёт)
    /// читает `WithText`/`HasText`. Сам Term не сохраняется — AST не удерживает дерево разбора.
    AstNodeBase(ParserToken::Kind k, TermPtr term);

  protected:
    /// Construct with kind only (manual/synthetic node; range invalid by default).
    explicit AstNodeBase(ParserToken::Kind k)
    : m_kind(k) {}

    ParserToken::Kind m_kind{ParserToken::Kind::END};

    /// Происхождение узла (исходный диапазон; у ручных/синтетических - невалиден).
    MapperRange m_range{};
};

/// Истина, если kind - объявление ТИПА (`TypeDecl`, запись Struct/Class, нативный ClassDecl).
/// Используется для материализации «идентификатор обозначает тип» (`IdentName::resolvedTypeId`).
[[nodiscard]] inline bool isTypeDeclKind(ParserToken::Kind k) {
    return k == ParserToken::Kind::TypeDecl || k == ParserToken::Kind::StructDecl || k == ParserToken::Kind::ClassDecl;
}

/// Истина, если kind - объявление (для привязки документирующего комментария к узлу).
inline bool isDeclKindForDocs(ParserToken::Kind k) {
    return k == ParserToken::Kind::VarDecl || k == ParserToken::Kind::FuncDecl || k == ParserToken::Kind::TypeDecl || k == ParserToken::Kind::ArgNode;
}

/// AstNodeAttr - расширение AstNodeBase с поддержкой атрибутов.
class AstNodeAttr : public AstNodeBase {
  public:
    /// Read-only access to attributes
    const std::vector<AttrId>& attrs() const noexcept { return m_attrs; }

    AstNodeAttr() = default;

    virtual ~AstNodeAttr() = default;

    /// Construct with kind and the source Term.
    AstNodeAttr(ParserToken::Kind k, TermPtr term)
    : AstNodeBase(k, term) {}

  public:
    /// Uniform term-constructor for the generated factory (kind + source Term).
    /// AstNodeAttr хранит только attrs/docs - детей не строит (ctx игнорируется).
    AstNodeAttr(ParserToken::Kind k, TermPtr term, ConvertServices* /*ctx*/)
    : AstNodeBase(k, term) {}

    /// Construct with kind only (no source Term).
    explicit AstNodeAttr(ParserToken::Kind k)
    : AstNodeBase(k) {}

    /// Add an attribute ID to this token.
    /// @param manual - set the manual bit (source-level attribute, default true).
    void add_attr(AttrId id, bool manual = true) { m_attrs.push_back(manual ? detail::with_manual(id) : id); }

    /// Check if this token has a specific attribute by ID.
    /// Search is performed by the base index, ignoring the bitmask, so
    /// manual/auto and builtin/user variants match the same attribute.
    [[nodiscard]] bool has_attr(AttrId id) const {
        AttrId idx = id & detail::kAttrIndexMask;
        return std::find_if(m_attrs.begin(), m_attrs.end(), [idx](AttrId stored) { return (stored & detail::kAttrIndexMask) == idx; }) != m_attrs.end();
    }

    /// Check if this token has an attribute by name (delegates to has_attr(AttrId)).
    [[nodiscard]] bool has_attr(const AttrPool& pool, std::string_view name) const;

    /// Хранит список строковых аргументов атрибута (`@[link("m")]` → ["m"]).
    /// Атрибут может иметь один или несколько аргументов; аргументы конвертируются
    /// в строки при создании AstNode (convertAttrsToNode). Ключ - индекс атрибута
    /// (id & kAttrIndexMask). Пуст, если атрибут аргументов не несёт.
    void set_attr_args(AttrId id, std::vector<std::string> args);

    /// Возвращает список аргументов атрибута (nullptr, если не задано).
    [[nodiscard]] const std::vector<std::string>* attr_args(AttrId id) const noexcept;

    [[nodiscard]] AstNodeAttr* as_attr() noexcept override { return this; }
    [[nodiscard]] const AstNodeAttr* as_attr() const noexcept override { return this; }

    /// Dump token contents for debugging
    [[nodiscard]] std::string dump(size_t indent = 0) const override;

  protected:
    std::vector<AttrId> m_attrs; ///< Attribute IDs attached to this token
    /// Аргументы атрибутов: индекс атрибута → список строковых значений (см. set_attr_args).
    std::map<AttrId, std::vector<std::string>> m_attrArgs;
};

/// HasText - хранилище собственного текста узла (mixin, НЕ узел AST). Текст сохраняется КАК
/// ЕСТЬ (spelling термина): нормализации здесь нет; правило имени применяет только name-node
/// (assignNameFromRaw в ident_name.hpp). Владелец текста - StringInterner; поле - невладеющий view.
/// Узел получает текст ТОЛЬКО через `WithText<Base>`; у не-текстовых классов метода text() нет
/// вовсе (compile-time). `RawText` (комментарий/embed C++) — маркер назначения без своего кода.
class HasText {
  public:
    HasText() = default;

    /// Терм-конструктор: текст читается из Term как есть и интернируется.
    /// Определён в ast_base.cpp (нужен полный тип Term).
    explicit HasText(TermPtr term);

    /// Manual-конструктор: текст задан явно. Интернируется сразу, поэтому временная строка
    /// вызывающего не переживается.
    explicit HasText(std::string_view text) { set_text_interned(text); }

    /// Написание узла (view в StringInterner компиляции). Не владеет текстом.
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }

    /// Установить написание с интернированием в активный пул строк: временная строка вызывающего
    /// безопасна. Пустое написание не интернируется (view остаётся пустым). Повторная запись того же
    /// view (уже интернированного) ничего не делает.
    void set_text_interned(std::string_view t) {
        if (t.data() == m_text.data() && t.size() == m_text.size()) {
            return;
        }
        m_text = internedTextSlot(t);
    }

  private:
    std::string_view m_text{};
};

/// Тег терм-конструктора: range берётся из Term, текст НЕ читается (написание задаёт наследник,
/// напр. name-node через assignNameFromRaw).
struct NoTextTag {};

/// WithText<Base> - узел Base, несущий собственный текст. Единственный способ получить
/// text()/set_text_interned() доступны ровно у узлов с WithText в базе.
/// Терм-конструкторы читают текст из Term (кроме NoTextTag), manual - интернируют явный текст.
template <class Base>
class WithText : public Base, public HasText {
  public:
    WithText() = default;

    /// Терм-конструктор: range из term, текст term читается как есть.
    WithText(ParserToken::Kind k, TermPtr term)
    : Base(k, term)
    , HasText(term) {}

    /// Uniform-терм-конструктор генерируемой фабрики (Base решает, что делать с ctx).
    WithText(ParserToken::Kind k, TermPtr term, ConvertServices* ctx)
    : Base(k, term, ctx)
    , HasText(term) {}

    /// Терм-конструктор БЕЗ чтения текста: написание задаёт наследник (name-node).
    WithText(ParserToken::Kind k, TermPtr term, NoTextTag)
    : Base(k, term) {}

    /// Manual-конструктор: текст задан явно, без TermPtr.
    WithText(ParserToken::Kind k, std::string_view text)
    : Base(k)
    , HasText(text) {}

    /// Kind-only (manual/synthetic): написание задаёт наследник отдельно.
    explicit WithText(ParserToken::Kind k)
    : Base(k) {}

    [[nodiscard]] HasText* as_text() noexcept override { return this; }
    [[nodiscard]] const HasText* as_text() const noexcept override { return this; }
};

/// RawText - маркер назначения для текста, который НИКОГДА не интерпретируется
/// (комментарий/embed C++). Хранение целиком наследуется от mixin HasText: spelling
/// сохраняется КАК ЕСТЬ и интернируется в пул компиляции; собственной логики нет.
/// Отдельный класс фиксирует намерение в реестре kind→класс (наличие текста выводится
/// из класса-носителя: WithText, без kind-исключений).
/// Trust-имя из C++-вставки (`{% $x %}`/`{% @x %}`) и модуль его объявления.
/// Заполняет семантика; кодоген использует модуль для namespace-квалификации имени.
struct EmbedNameRef {
    std::string_view name; ///< имя БЕЗ сигила (как его видит transformEmbedCpp)
    char sigil = '@';
    ModuleIndex module = ModuleIndex::invalid(); ///< invalid — локальное/встроенное имя
};

/// Узел текста (в т.ч. C++-вставка `{% ... %}`): текст без структуры.
class RawText : public WithText<AstNodeAttr> {
  public:
    using WithText<AstNodeAttr>::WithText;

    RawText() = default;

    /// Резолв имён вставки (заполняет семантика; пусто для не-embed текстов).
    std::vector<EmbedNameRef> embedNames;
};

/// Текст узла, который ОБЯЗАН быть носителем текста (имя/литерал/оператор/метка).
/// Нарушение инварианта kind→класс - EXPECT (никакого молчаливого пустого текста).
[[nodiscard]] inline std::string_view requiredText(const AstNodeBase& node) {
    const HasText* text = node.as_text();
    EXPECT(text != nullptr && "node must carry text");
    return text != nullptr ? text->text() : std::string_view{};
}

/// Текст узла-носителя или ИМЯ kind для диагностики (у маркеров/контейнеров текста нет).
/// Только для сообщений; семантическое чтение текста - `text()`/`requiredText`.
[[nodiscard]] inline std::string_view displayTextOrKind(const AstNodeBase& node) {
    const HasText* text = node.as_text();
    return text != nullptr ? text->text() : ParserToken::name(node.kind());
}

/// Истина, если узел - маркер `_` (none-значение / «не инициализировано»). Это НЕ признак
/// «без m_initializer»: `_` - ФОРМАЛЬНЫЙ инициализатор (`x := _;` / `x = _;`), который по смыслу
/// означает «объявить/сбросить без значения». Единый предикат для всех потребителей (семантика,
/// кодоген), чтобы не копипастить проверку. Term не нужен (текст уже интернирован в узле).
[[nodiscard]] inline bool isNoneMarker(const AstNodeBase* n) noexcept {
    if (!n || n->kind() != ParserToken::Kind::Ident) {
        return false;
    }
    const HasText* text = n->as_text();
    return text != nullptr && text->text() == "_";
}

/// Истина, если узел - ВАРИАТИВНЫЙ маркер `...` в списке ПАРАМЕТРОВ прототипа (`f(a:Int32, ...)`).
/// Параметр представляется `ArgNode` с текстом "..." (TermID::ELLIPSIS теряется при конверсии в
/// ArgNode - ast_decl создаёт ArgNode из терма), поэтому проверка текстовая (как isNoneMarker).
/// ЕДИНЫЙ предикат для семантики (`buildFuncType`) и кодогена (`decl_func_emit`), чтобы не
/// копипастить сравнение со строкой. НЕ путать с многоточием в АРГУМЕНТАХ вызова (там узел имеет
/// `ParserToken::Kind::Ellipsis`/`Filling`, см. semantic/ellipsis).
[[nodiscard]] inline bool isVariadicParamMarker(const AstNodeBase* n) noexcept {
    if (!n || n->kind() != ParserToken::Kind::ArgNode) {
        return false;
    }
    const HasText* text = n->as_text();
    return text != nullptr && text->text() == "...";
}

namespace detail {
/// Единый формат заголовка dump для узлов с именем: `Name 'text'` (+ необязательный
/// prefix, например ':' для TypeName). Устраняет повторяющийся паттерн в
/// IdentName/IdentType/Decl/ArgNode/ModuleNode dump().
[[nodiscard]] inline std::string dumpQuotedName(ParserToken::Kind kind, std::string_view text, size_t indent, std::string_view prefix = {}) {
    std::string result(indent, ' ');
    result += ParserToken::name(kind);
    if (!text.empty()) {
        result += " '";
        result += prefix;
        result += text;
        result += "'";
    }
    return result;
}
} // namespace detail

/// Единый хелпер иммутабельности ('^' → attr::ReadOnly). Используется:
///   - assignNameFromRaw при создании name-node (срез '^' и атрибут — в одной точке);
///   - конвертером Term→AST для не-имён (`*^`, `&^`, `method^()`).
/// Сам '^' из текста НЕ срезает. Возвращает true, если ReadOnly применён (raw оканчивается '^'
/// И kind допускает квалификатор). Определён в ast_children.cpp (нужен полный тип AttrPool).
bool applyReadonlyFromCaret(AstNodeAttr& node, std::string_view raw, AttrPool* pool);

} // namespace trust