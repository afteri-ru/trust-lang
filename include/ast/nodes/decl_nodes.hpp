#pragma once

#include "ast/token_base.hpp"
#include "location/decl_id.hpp"
#include "ast/slots/slot_engine.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/severity.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "types/visibility.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace trust {

struct ConvertServices;
class DeclCppFacts;

/// Нативное имя trust-сущности (функции/переменной) — ОПЦИОНАЛЬНЫЙ факт объявления.
/// Импорт (`name := %cpp ...;`): реализация — внешний C/C++-символ, собственный C++-код
/// не эмитится. Экспорт (`@[export("fib")]`/`@[export("ns::fib")]` + тело): trust-реализация
/// публикуется в C++ под заданным именем. Направление и ABI задаются ЯВНО (не выводятся из имени).
/// ABI нативной привязки — ОДНА функция (не поле): `::` в C++-имени ⇒ C++, иначе C.
enum class CppLinkage { C, Cpp };

[[nodiscard]] inline CppLinkage cppLinkageOf(std::string_view cppName) noexcept {
    return cppName.find("::") == std::string_view::npos ? CppLinkage::C : CppLinkage::Cpp;
}

/// Добавляет m_type - тип объявления (может быть nullptr).
class Decl : public IdentName {
  public:
    AST_SLOT_METHOD(IdentName, SLOTS_Decl)
    Decl() = default;

    /// Manual-конструктор: имя задано явно, без TermPtr.
    explicit Decl(std::string text)
    : IdentName(std::move(text)) {}

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Тип объявления (nullptr если тип не указан или выводится).
    AstNodePtr m_type;

    /// Статический член нативного класса (`@::name` в `ClassDecl::m_body`): тип-ВЛАДЕЛЕЦ для
    /// C++-проекции имени и `@[include]` (on-use). INVALID_TYPE_ID — не статический член нативного класса.
    TypeId m_nativeOwner{INVALID_TYPE_ID};

    /// Языковой предикат «у объявления есть нативная C/C++-привязка» (импорт `name := %cpp ...;` /
    /// экспорт `@[export("...")]`). Роль (импорт/экспорт) — ПРОИЗВОДНАЯ от наличия определения.
    /// Само C/C++-имя — ЦЕЛЕВОЙ факт, хранится ВНЕ языковой модели (`DeclCppFacts`, владелец реестра;
    /// R1/R4/R37), читается через аксессор хранилища по `declId()`.
    bool m_bindsNativeCpp = false;

    /// Есть ли у объявления нативная C/C++-привязка (языковой предикат).
    [[nodiscard]] bool bindsNativeCpp() const noexcept { return m_bindsNativeCpp; }

    /// ВРЕМЕННЫЙ носитель целевого C/C++-имени на фазе конвертации Term→AST (до появления `DeclId`).
    /// `assignDeclarationIds` переносит его в хранилище (`DeclCppFacts`) и очищает узел: после
    /// назначения id языковая модель целевое C++-имя НЕ хранит (R1/R4). Писатель — конвертер.
    std::optional<std::string> m_pendingNativeCppName;

    /// Идентичность объявления (R23): назначает владелец (`assignDeclarationIds`) в порядке объявлений
    /// модуля-владельца; переживает клонирование. Значение по умолчанию — «id отсутствует» (синтетика).
    [[nodiscard]] DeclId declId() const noexcept { return m_declId; }

    /// Роль нативной привязки — ПРОИЗВОДНАЯ: (bindsNativeCpp, isDefinition()).
    /// import — реализация внешняя, собственного определения нет; export — trust-реализация
    /// публикуется под C/C++-именем.
    enum class NativeRole : uint8_t { None, Import, Export };

    /// Вид объявления (ПРОИЗВОДНЫЙ от payload наследника). База отвечает «определение» для узлов
    /// без понятия определения (ClassDecl — интерфейс нативного класса).
    [[nodiscard]] virtual bool isDefinition() const noexcept { return true; }
    /// Предварительное объявление без определения (см. isDefinition).
    [[nodiscard]] virtual bool isForwardDeclaration() const noexcept { return !isDefinition(); }

    /// Роль нативной привязки; переопределяется там, где «определение для экспорта» строже
    /// (VarDecl: только реальное значение).
    [[nodiscard]] virtual NativeRole nativeRole() const noexcept {
        if (!bindsNativeCpp()) {
            return NativeRole::None;
        }
        return isDefinition() ? NativeRole::Export : NativeRole::Import;
    }

    /// Импорт: нативное имя есть, собственного определения нет (реализация — внешний символ).
    [[nodiscard]] bool isNativeImport() const noexcept { return nativeRole() == NativeRole::Import; }
    /// Экспорт: нативное имя есть и есть определение (trust-реализация под C/C++-именем).
    [[nodiscard]] bool isNativeExport() const noexcept { return nativeRole() == NativeRole::Export; }

  protected:
    /// Kind-only: имя задаёт наследник (assignNameFromRaw).
    explicit Decl(ParserToken::Kind k)
    : IdentName(k) {}

    /// Базовый терм-конструктор объявления: явное raw-имя + kind объявления.
    Decl(ParserToken::Kind k, TermPtr term, std::string_view rawName, AttrPool* pool)
    : IdentName(k, term, rawName, pool) {}

  private:
    // Запись id — только у зоны-владельца (`assignDeclarationIds`); потребители читают через `declId()`.
    friend void assignDeclarationIds(const std::vector<AstNodePtr>& roots, std::string_view moduleLogicalName, DeclCppFacts* declFacts);
    DeclId m_declId{};
};

/// Вид алиаса объявления (`:-`): имя/область. `None` — обычное объявление;
/// `aliasTarget()` обязателен тогда и только тогда, когда вид не `None`.
enum class AliasKind { None, Name, Namespace };

/// Вид определения функции/записи (единственная модель; payload — `Definition`).
enum class DefinitionKind : uint8_t { None, Body, ForwardedTo };

/// Определение объявления: payload'ы + ПРОИЗВОДНЫЙ вид (единственный источник — наличие payload).
/// Payload'ы приватны и задаются только фабриками: состояние «тело И переадресация» невозможно.
class Definition {
  public:
    [[nodiscard]] DefinitionKind kind() const noexcept {
        if (m_body.has_value()) {
            return DefinitionKind::Body;
        }
        return m_forwardedTo.has_value() ? DefinitionKind::ForwardedTo : DefinitionKind::None;
    }
    [[nodiscard]] bool isDefinition() const noexcept { return m_body.has_value() || m_forwardedTo.has_value(); }
    [[nodiscard]] bool hasBody() const noexcept { return m_body.has_value(); }
    [[nodiscard]] bool isDelegate() const noexcept { return m_forwardedTo.has_value(); }
    /// Тело (engaged, возможно пустое); nullptr — тела нет.
    [[nodiscard]] const std::vector<AstNodePtr>* body() const noexcept { return m_body ? &*m_body : nullptr; }
    [[nodiscard]] std::vector<AstNodePtr>* body() noexcept { return m_body ? &*m_body : nullptr; }
    /// trust-цель переадресации (без '%'); nullptr — переадресации нет.
    [[nodiscard]] const std::string* forwardedTo() const noexcept { return m_forwardedTo ? &*m_forwardedTo : nullptr; }

    static Definition fromBody(std::vector<AstNodePtr> statements) {
        Definition d;
        d.m_body = std::move(statements);
        return d;
    }
    static Definition forwardTo(std::string target) {
        EXPECT(!target.empty() && "forwarded target must be non-empty");
        EXPECT(target.front() != '%' && "forwarded target is a trust name (native import is a declaration fact)");
        Definition d;
        d.m_forwardedTo = std::move(target);
        return d;
    }

  private:
    std::optional<std::vector<AstNodePtr>> m_body; ///< тело (engaged, возможно пустое)
    std::optional<std::string> m_forwardedTo;      ///< trust-цель переадресации (без '%')
};

/// FuncDecl - объявление функции.
/// kind = FuncDecl.
/// text() — trust-имя (без '%'); тип — в m_type, C++-привязка — целевой факт `DeclCppFacts` (по `declId()`).
class FuncDecl : public Decl {
  public:
    AST_SLOT_METHOD(Decl, SLOTS_FuncDecl)
    FuncDecl() { m_kind = ParserToken::Kind::FuncDecl; }

    /// Терм-конструктор: имя функции читается из Term (у операторного терма `::=` - из m_left).
    explicit FuncDecl(TermPtr term, AttrPool* pool = nullptr);

    /// Uniform term-constructor for the generated factory (kind = FuncDecl).
    /// При ctx != nullptr строит m_params/m_body/m_type; для CREATE_NAME-функции (сигнатура
    /// в m_left) - ещё и мутацию term->m_mapperRange до [имя, оператор] (признак функции).
    FuncDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr.
    FuncDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::FuncDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Строка сигнатуры функции (для контекст-макроса @__FUNCSIG__):
    /// "ns::name(params):Ret". namespace_path - текущая область имён (для полного имени),
    /// пустая - имя без префикса.
    [[nodiscard]] std::string signature(std::string_view namespace_path) const;

    /// АЛИАС (`f :- g;`, `:A ::- T;`, `a :- ns::;`): новое имя существующей сущности/области.
    /// Синтаксис единый (LHS — только NAME, как в C++); вид (имя/тип/область) различается природой
    /// цели и материализуется семантикой. Не определение и не отдельная сигнатура: `m_type`/`m_params`/
    /// `m_body` пусты, `aliasTarget` — ссылка на цель. C++-сущность для алиаса НЕ эмитится.
    [[nodiscard]] AliasKind aliasKind() const noexcept { return m_aliasKind; }
    [[nodiscard]] const AstNodePtr& aliasTarget() const noexcept { return m_aliasTarget; }
    /// Слот цели для обхода детей/клонирования AST.
    [[nodiscard]] AstNodePtr* aliasTargetSlot() noexcept { return &m_aliasTarget; }
    void setAlias(AliasKind kind, AstNodePtr target, MapperRange range);

    /// Диапазон тела-блока `{ ... }`; invalid, если тела нет.
    MapperRange m_blockRange{};

    /// Диапазон тела функции - блока `{ ... }` (term->m_right), если доступен.
    /// Используется для зеркалирования раскладки исходника ({ и } на тех же строках).
    /// Возвращает invalid range, если терм/блок недоступны (test-only узлы).
    [[nodiscard]] MapperRange blockRange() const noexcept { return m_blockRange; }

    /// Параметры функции (каждый - ArgNode)
    std::optional<std::vector<AstNodePtr>> m_params;

    /// ЕДИНОЕ определение функции: тело ИЛИ переадресация реализации к trust-цели.
    /// Нативная привязка — языковой предикат `bindsNativeCpp()` (не вид определения).
    Definition m_definition;

    /// Типовые параметры объявленного нативного шаблона (`<T>` → [ArgNode("T")]). Каждый - ArgNode
    /// (имя в text(), при B2/B3 - тип value-параметра / ограничение в m_type, дефолт в m_value).
    /// Пусто = шаблон без типовых параметров (допускается). B1 - только типовые без ограничений.
    std::optional<std::vector<AstNodePtr>> m_templateParams;

    /// Захваты лямбды: каждый - ArgNode с именем захваченной переменной (тип резолвит семантика).
    /// Первая итерация - ТОЛЬКО захват по значению `[x]`. Источник истины «это лямбда»:
    /// `has_value()` (у лямбды список захватов есть всегда, возможно ПУСТОЙ `[]`); у именованной
    /// функции - `std::nullopt`. Отдельного bool-флага нет (единый источник, как `m_body`/`m_template`).
    std::optional<std::vector<AstNodePtr>> m_captures;

    /// Лямбда-выражение `[captures](params):Ret { body }` (term TermID::LAMBDA)? Определяется
    /// наличием списка захватов (у лямбды он есть всегда, в т.ч. пустой `[]`).
    [[nodiscard]] bool isLambda() const noexcept { return m_captures.has_value(); }

    /// Перегружаемый ОПЕРАТОР (объявление через СИСТЕМНОЕ имя - dunder: `__eq__(...)`). Признак
    /// производный от имени (types/operator_registry.hpp); member-форма - в теле класса/struct,
    /// free - на верхнем уровне модуля. Проверка «имя реализовано» и правила member/free -
    /// в types/operator_registry.hpp.
    bool m_isOperator = false;

    /// Доступ к единому определению (единственный источник вида — payload `Definition`).
    [[nodiscard]] DefinitionKind definitionKind() const noexcept { return m_definition.kind(); }
    [[nodiscard]] bool isDefinition() const noexcept override { return m_definition.isDefinition(); }
    [[nodiscard]] bool isForward() const noexcept { return !isDefinition(); }
    [[nodiscard]] bool isDelegate() const noexcept { return m_definition.isDelegate(); }
    [[nodiscard]] bool hasBody() const noexcept { return m_definition.hasBody(); }
    /// Имя цели делегата (`:= name ...;`), raw; nullptr — вид не делегат.
    [[nodiscard]] const std::string* forwardingTarget() const noexcept { return m_definition.forwardedTo(); }
    /// Тело функции; nullptr — определения-тела нет.
    [[nodiscard]] const std::vector<AstNodePtr>* body() const noexcept { return m_definition.body(); }
    [[nodiscard]] std::vector<AstNodePtr>* body() noexcept { return m_definition.body(); }
    /// Нативная привязка — ОПЦИОНАЛЬНЫЙ факт объявления (см. `Decl::nativeBinding`): импорт
    /// задаётся RHS-целью `:= %cpp ...` (реализация — внешний символ), экспорт —
    /// атрибутами `@[export("...")]`/`@[export("...")]` (trust-реализация под C/C++-именем).
    /// Отдельных полей/флагов под «native»/`m_delegate`/`m_isNativeImport` нет.
    /// Есть ли реализация (тело | переадресация | нативный импорт)? Производная от единого вида
    /// состояния + нативной привязки; пустое тело `f() := {};` — определение.
    [[nodiscard]] bool hasImplementation() const noexcept { return isDefinition() || isNativeImport(); }

  private:
    AliasKind m_aliasKind = AliasKind::None;
    AstNodePtr m_aliasTarget; ///< null ⇔ AliasKind::None (инвариант `setAlias`).
};

/// База класса/структуры trust: тип-ссылка (`:Base` / `:Base<:T>`) + вариант наследования.
/// `access` всегда выставляет владелец; дефолт — Public (private/protected — явным маркером).
struct TypeBase {
    AstNodePtr type;
    Visibility access{Visibility::Public};
};

/// Нативная база генерируемого класса/структуры: готовое C++-имя (без `%`) + вариант наследования.
struct NativeBase {
    std::string cppName;
    Visibility access{Visibility::Public};
};

/// ClassDecl - forward-объявление (нативного) класса через `::=`:
///   @include("string")  String ::= %std::string { ... };            - нативный класс (RHS-имя с '%')
///   @include("utility") <T1,T2> Pair ::= %std::pair<T1,T2> { ... }; - ЯВНАЯ реализация шаблона
///   @include("utility") <T1,T2> Pair ::= <T1,T2> %std::pair { ... }; // обобщённая форма
/// trust-имя класса - в text() (слева от `::=`), C++-имя — целевой факт `DeclCppFacts` (по `declId()`)
/// (без ведущего '%'; RHS без '%' — ошибка). Класс НЕ имеет тела на Trust-стороне: члены (методы/поля/
/// конструкторы/статич-члены) - forward-объявления (`:= ...`), хранятся в m_body; реализация
/// класса находится в C++-заголовке (инклуд - из @[include], см. attr::Include). Транспилятор
/// НЕ эмитит `struct`, а использует нативное C++-имя (инклуд on-use).
class ClassDecl : public Decl {
  public:
    AST_SLOT_METHOD(Decl, SLOTS_ClassDecl)
    ClassDecl() { m_kind = ParserToken::Kind::ClassDecl; }

    /// Терм-конструктор: читает из операторного терма `::=` (m_left = trust-имя с типовыми
    /// параметрами шаблона, m_right = RHS CLASS-терм с нативным именем/аргументами/членами).
    ClassDecl(ParserToken::Kind k, TermPtr term, ConvertServices* ctx = nullptr);

    /// Manual-конструктор (test-only): trust-имя задано явно.
    explicit ClassDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::ClassDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Типовые параметры trust-шаблона (`<T1,T2>` слева → [ArgNode("T1"), ArgNode("T2")]).
    /// nullopt - НЕ шаблон; has_value (в т.ч. пустой вектор) - шаблон (может быть без типовых
    /// параметров). Как у FuncDecl::m_templateParams. Шаблонность - по has_value, а не флагом.
    std::optional<std::vector<AstNodePtr>> m_templateParams;

    /// Параметры/аргументы реализации (`%std::pair<T1,T2>`): имена из RHS-списка. Для явной
    /// формы 1:1 совпадают с m_templateParams. nullopt - НЕТ явных аргументов реализации
    /// (обобщённая форма `<T1,T2> name ::= <T1,T2> %std::pair`). Хранится для будущих не-1:1
    /// отображений; кодоген не требует заполнения (тип регистрируется по cppName + типовые
    /// параметры).
    std::optional<std::vector<std::string>> m_templateArgs;

    /// true - ОБОБЩЁННАЯ форма `<T1,T2> name ::= <T1,T2> %std::pair` (шаблон + НЕТ явных
    /// аргументов реализации) → маппинг типовых параметров 1:1; false - явная реализация
    /// `name<T1,T2>` (аргументы в m_templateArgs). ВЫВОДИТСЯ из заполненности m_template,
    /// как у любого шаблона: это шаблон (m_templateParams.has_value()) и НЕТ явных
    /// аргументов реализации (m_templateArgs = nullopt) ⇒ обобщённая привязка.
    [[nodiscard]] bool isGenericTemplate() const noexcept { return m_templateParams.has_value() && !m_templateArgs.has_value(); }

    /// Базисы ГЕНЕРИРУЕМОГО класса (RHS-цепочка после C++-имени). НАТИВНЫЕ базисы
    /// (`Name = %cpp, %base {...}`) — готовые C++-имена (НЕ тип-ссылки, не резолвятся);
    /// обычные (`:Base`) — `m_baseTypes` (IdentType, резолвится семантикой). Смешивать нельзя.
    /// Вариант наследования (public/protected/private) хранится рядом с каждой базой.
    std::vector<NativeBase> m_nativeBaseCppNames;
    std::vector<TypeBase> m_baseTypes;

    /// Члены интерфейса (методы/поля/конструкторы/статич-члены): FuncDecl/VarDecl, forward (`:= ...`).
    std::vector<AstNodePtr> m_body;
};

/// RecordDecl - объявление пользовательского структурного типа (`:Name ::= :Base{, :Base}{ ... };`).
/// kind = StructDecl. ЕДИНЫЙ узел для Struct (`:Struct`-база, строго POD) и Class
/// (`:Class`-база либо пользовательский record-класс, допускает наследование). Различие
/// Struct/Class - ТОЛЬКО именем абстрактной базы, которое семантика превращает в Group
/// TypeKind (kStructs/kClassDefs); сам узел одинаков (не несёт struct/class-флага).
/// Пример:
///   :Point  ::= :Struct{ x:Int32 := 0; y:Int32 := 0; };
///   :Animal ::= :Class { name:StrChar := "x"; speak() := {...}; };
///   :Dog    ::= :Animal{ _age:Int32 := 0; };
/// trust-имя - в text() (слева от `::=`); базы - в m_baseTypes (IdentType);
/// члены - в m_body (VarDecl-поля, FuncDecl-методы); C++-struct генерирует транспилятор.
class RecordDecl : public Decl {
  public:
    AST_SLOT_METHOD(Decl, SLOTS_RecordDecl)
    RecordDecl() { m_kind = ParserToken::Kind::StructDecl; }

    /// Терм-конструктор: читает из операторного терма `::=` (m_left = trust-имя,
    /// m_right = CLASS-терм: текст = первая база, m_right-цепочка = остальные базы,
    /// m_sequence = члены тела).
    RecordDecl(ParserToken::Kind k, TermPtr term, ConvertServices* ctx = nullptr);

    /// Manual-конструктор (test-only): trust-имя задано явно.
    explicit RecordDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::StructDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Понижение членов тела: тела методов (FuncDecl::m_body) должны пройти lowering
    /// (SemicolonStmt для операторов-выражений и т.п.), иначе assignment-операторы в
    /// методах эмитятся без завершающей ';'.

    /// Базовые типы (IdentType) с вариантом наследования. Для Struct обычно единственная
    /// абстрактная база `:Struct`; для Class - `:Class` или пользовательские Class-типы.
    std::vector<TypeBase> m_baseTypes;

    /// Типовые параметры шаблона (аналог ClassDecl::m_templateParams). nullopt - НЕ шаблон.
    std::optional<std::vector<AstNodePtr>> m_templateParams;

    /// Члены тела: Definition::None - ПРЕДВАРИТЕЛЬНОЕ (forward) объявление (форма БЕЗ тела
    /// `:Name ::= :Base ...;` → C++ incomplete `struct c_Name;`, членов нет); engaged (возможно
    /// ПУСТОЙ вектор) - полное определение (`{ ... }`, в т.ч. пустое `{ };`). Как FuncDecl.
    Definition m_definition;

    [[nodiscard]] bool hasBody() const noexcept { return m_definition.hasBody(); }
    [[nodiscard]] bool isDefinition() const noexcept override { return m_definition.isDefinition(); }
    [[nodiscard]] const std::vector<AstNodePtr>* body() const noexcept { return m_definition.body(); }
    [[nodiscard]] std::vector<AstNodePtr>* body() noexcept { return m_definition.body(); }
};

/// Состояние инициализации переменной — ПРОИЗВОДНОЕ от payload и синтетических фактов узла.
enum class VarInitKind : uint8_t { Forward, DeferredInit, Uninitialized, Initialized, Bound };

/// VarDecl - объявление переменной.
/// kind = VarDecl.
/// text() - имя переменной (чистое, без префиксов).
/// Поддерживает:
///   x := expr        - text()="x", m_type=nullptr, m_initializer=expr
///   x:Type := expr   - text()="x", m_type=IdentType, m_initializer=expr
///   x := expr mut    - мутабельность выражается атрибутом узла (future)
class VarDecl : public Decl {
  public:
    AST_SLOT_METHOD(Decl, SLOTS_VarDecl)
    VarDecl() { m_kind = ParserToken::Kind::VarDecl; }

    /// Терм-конструктор: имя читается из Term (у операторного терма `:=` - из m_left);
    /// тип/инициализатор задаются отдельно.
    VarDecl(TermPtr term, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr, AttrPool* pool = nullptr);

    /// Uniform term-constructor for the generated factory (kind = VarDecl).
    /// При ctx != nullptr строит m_type (из m_left->m_type) и m_initializer (из m_right).
    VarDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr; тип/инициализатор задаются отдельно.
    VarDecl(std::string name, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr)
    : Decl(std::move(name))
    , m_initializer(std::move(initializer)) {
        m_kind = ParserToken::Kind::VarDecl;
        m_type = std::move(type);
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Диапазон исходного имени переменной (для source-map/имени).
    /// Базовый term для `x := ...` - это терм оператора ':=' (CREATE_NAME):
    /// его m_mapperRange указывает на оператор, а реальное имя лежит в term->m_left.
    /// Диапазон исходного имени переменной, зафиксированный при построении.
    [[nodiscard]] MapperRange nameRange() const noexcept { return m_nameRange; }

    MapperRange m_nameRange{};

    AstNodePtr m_initializer; ///< Инициализатор (nullptr = нет инициализатора)

    /// Выведенный анализатором конкретный тип (для нетипизированных `x := expr`, inferred).
    /// INVALID_TYPE_ID - тип не выведен. Заполняется NameResolutionPass (typeExpr) и читается
    /// транспилятором для кодогенерации (чтобы не зависеть от скоуп-стека, сброшенного после анализа).
    QualType inferredType{};

    /// Разрешённый семантикой тип deleter'а объявления, когда он НЕ входит в тип (`shared`;
/// `@[deleter(D)]` на переменной или её аннотации). У `unique` D — часть identity типа
/// (`RefTypeData::deleterType`), отдельного поля нет. Кодоген читает оба места без резолва имени.
    TypeId m_deleterType{INVALID_TYPE_ID};
    [[nodiscard]] TypeId deleterType() const noexcept { return m_deleterType; }

    /// Узел декларации ТИПА (TypeDecl) для типизированных переменных доверенного типа
    /// (`x :MyInt := ...`). Ставит NameResolutionPass (analyzeVarDecl) для isTrustedType-типов;
    /// условия типа читаются из него как `m_typeDecl->m_trust` (источник один, без копий).
    /// Не владеющая ссылка в AST модуля - переживает таблицу символов. nullptr - нет/нетрастовый.
    const AstNodeBase* m_typeDecl = nullptr;

    /// Синтетический признак «переменная - биндинг оператора `with`». Ставит конструктор
    /// WithStmt для пары (lock, binding). Используется семантикой для диагностики: биндинг
    /// ссылочного типа без захвата (`with(v = ref)` без `*`/`*^`) - предупреждение.
    bool m_inWith = false;

    /// Синтетическая временная прохода capture `$^` (`__trust_last_N := <source>;`). Ставит
    /// captureLastResult. Семантика использует для точечной диагностики void/no-value источника:
    /// если выведенный тип временной - «нет значения» (Void/None), обращение к `$^` — ошибка
    /// «no value to capture» (а не общая "unable to generate C++ type 'Any'").
    bool m_lastResultTemp = false;

    /// Признак «биндинг `catch(e:T)`»: значение задаёт конструкция, payload-инициализатора нет.
    /// Не выводится из payload; ставит конструктор CatchBlock.
    bool m_isCatchBinding = false;

    /// Роль нативной привязки: import без значения, export только с реальным значением.
    [[nodiscard]] VarInitKind initKind() const noexcept {
        if (!m_initializer) {
            return m_isCatchBinding ? VarInitKind::Bound : VarInitKind::Forward;
        }
        if (isNoneMarker(m_initializer.get())) {
            return m_type != nullptr ? VarInitKind::Uninitialized : VarInitKind::DeferredInit;
        }
        return VarInitKind::Initialized;
    }
    /// Вид определения для символьного слоя: любое состояние, кроме Forward, даёт место хранения.
    [[nodiscard]] bool isDefinition() const noexcept override { return initKind() != VarInitKind::Forward; }
    [[nodiscard]] bool isForwardDeclaration() const noexcept override { return initKind() == VarInitKind::Forward && !bindsNativeCpp(); }
    [[nodiscard]] NativeRole nativeRole() const noexcept override {
        if (!bindsNativeCpp() || m_isCatchBinding) {
            return NativeRole::None;
        }
        return initKind() == VarInitKind::Initialized ? NativeRole::Export : NativeRole::Import;
    }
};

/// DestructureDecl - деструктуризация из коллекции/кортежа: `t1, ..., tN := ... source;` (spread,
/// коллекция: pop_front) или `t1, ..., tN := source;` (структурно: кортеж, std::get). Присваивание
/// в существующие цели - `t1, ..., tN = ... source;` (m_isAssign=true). Семантика: без маркера -
/// ТОЧНАЯ привязка (каждая цель - один элемент; кортеж - арность равна, спред - число целей ==
/// числу элементов для статически-известного размера). Суффикс `...` у имени цели (`rest...`,
/// C++-pack-стиль) - «остаток»: связывает оставшиеся элементы (спред - Dict, кортеж - под-кортеж);
/// `_...` - извлечь элементы, остаток отбросить; одиночный `_` - пропустить ровно один элемент.
/// kind = DestructureDecl.
class DestructureDecl : public AstNodeAttr {
  public:
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_DestructureDecl)
    DestructureDecl() { m_kind = ParserToken::Kind::DestructureDecl; }

    /// Терм-конструктор: цели из цепочки m_left (список имён), m_source из m_right,
    /// m_isSpread - был ли RHS `... source`. rest-цель (`rest...`) - lval с суффиксом ELLIPSIS
    /// в m_right (грамматика assign_item: `lval ELLIPSIS`). m_isAssign - true для `=`,
    /// false для `:=`. Определён в ast_decl.cpp.
    DestructureDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx = nullptr);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Одна цель деструктуризации: имя и её состояние (инвариант «индекс = цель» выражен типом).
    struct Target {
        AstNodePtr name;                      ///< Имя слева (Ident) или `_` (skip)
        AstNodePtr typeNode;                  ///< явная аннотация (`a:Int32`); nullptr — аннотации нет
        TypeId anyCastType{INVALID_TYPE_ID};  ///< тип any_cast<T>: natural runtime тип ЭЛЕМЕНТА (Dict хранит int как int64_t); INVALID → std::any
        TypeId declaredType{INVALID_TYPE_ID}; ///< тип объявляемой переменной (аннотация/выведенный); для кортежа не используется
        bool isRest = false;                  ///< суффикс `...` (rest/«остаток»)
    };
    std::vector<Target> m_targets;
    AstNodePtr m_source;     ///< Источник (справа от `...`, коллекция/кортеж)
    bool m_isSpread = false; ///< true: `... source` (коллекция, pop); false: кортеж (std::get)
    bool m_isAssign = false; ///< true: `t1, ..., tN = source;` (присваивание в существующие цели)
    /// Статическая арность источника, вычисленная семантикой (кортеж - число элементов;
    /// спред-словарь - размер, если известен, иначе 0). Для кортеж-rest в кодогенерации
    /// (make_tuple оставшихся std::get<k>), т.к. скоуп-стек к моменту кодогенерации сброшен.
    size_t m_sourceArity = 0;
    /// Конкретный тип источника (вычислен семантикой). LOWERING создаёт временную-копию
    /// (`m_sourceTemp`) уже ПОСЛЕ семантики и переносит этот тип в `VarDecl::inferredType`
    /// (кодоген не выводит тип и не использует `auto`). INVALID - не вычислим (ошибка семантики).
    QualType m_sourceType{};
    /// Деструктуризация ВНУТРИ цикла (в стеке скоупов есть WhileStmt/DoWhileStmt). Семантика
    /// расширяет тип элемента до максимального (Integer/Double) - для foreach-паттерна; кодген
    /// использует runtime-конвертеры (anyToInt64/anyToDouble), а не строгий any_cast.
    bool m_inLoop = false;

    /// Синтезированная lowering временная копия источника `auto _trust_dst_N := <m_source>;` (при
    /// НЕ-mutating-rest: источник-выражение оценивается один раз, pop'ы идут в копию). Создаёт
    /// lowering (инвариант «временные — уровень анализатора»); транспилятор эмитит её (auto) и
    /// читает имя. nullptr - mutating-rest (попы идут прямо в источник) / не синтезирована.
    AstNodePtr m_sourceTemp{};
};

} // namespace trust
