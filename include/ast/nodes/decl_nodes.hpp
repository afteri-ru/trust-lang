#pragma once
// include/ast/nodes/decl_nodes.hpp
// Declaration AST nodes: Decl-based declarations (VarDecl, FuncDecl, ClassDecl,
// RecordDecl) and DestructureDecl.

#include "ast/token_base.hpp"
#include "ast/slots/slot_engine.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/severity.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace trust {

class Context;

/// Добавляет m_type - тип объявления (может быть nullptr).
class Decl : public IdentName {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
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

  protected:
    /// Kind-only: имя задаёт наследник (assignNameFromRaw).
    explicit Decl(ParserToken::Kind k)
    : IdentName(k) {}

    /// Базовый терм-конструктор объявления: явное raw-имя + kind объявления.
    Decl(ParserToken::Kind k, TermPtr term, std::string_view rawName, AttrPool* pool)
    : IdentName(k, term, rawName, pool) {}
};

/// Вид алиаса объявления (`:-`): имя/область/модуль. `None` — обычное объявление;
/// `aliasTarget()` обязателен тогда и только тогда, когда вид не `None`.
enum class AliasKind { None, Name, Namespace, Module };

/// FuncDecl - объявление функции.
/// kind = FuncDecl.
/// Наследует Decl: имя функции (с % префиксом) в text(), тип в m_type.
/// Поддерживает:
///   %func(params):Type := { body }  - полное определение
///   %func(params):Type := ... ;     - предварительное объявление (m_body = nullopt)
class FuncDecl : public Decl {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(Decl, SLOTS_FuncDecl)
    FuncDecl() { m_kind = ParserToken::Kind::FuncDecl; }

    /// Терм-конструктор: имя функции читается из Term (у операторного терма `::=` - из m_left).
    explicit FuncDecl(TermPtr term, AttrPool* pool = nullptr);

    /// Uniform term-constructor for the generated factory (kind = FuncDecl).
    /// При ctx != nullptr строит m_params/m_body/m_type; для CREATE_NAME-функции (сигнатура
    /// в m_left) - ещё и мутацию term->m_mapperRange до [имя, оператор] (признак функции).
    FuncDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор: имя задано явно, без TermPtr.
    FuncDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::FuncDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Строка сигнатуры функции (для контекст-макроса @__FUNCSIG__):
    /// "ns::name(params):Ret". namespace_path - текущая область имён (для полного имени),
    /// пустая - имя без префикса. Имя без ведущего '%'.
    [[nodiscard]] std::string signature(std::string_view namespace_path) const;

    /// АЛИАС (`f :- \path::g;`, `ns:: :- \lib::ns::;`, `\m :- \lib;`): новое имя существующей
    /// сущности/области/модуля. Не определение и не отдельная сигнатура: `m_type`/`m_params`/`m_body`
    /// пусты, `aliasTarget` — ссылка на цель. C++-сущность для алиаса НЕ эмитится.
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

    /// Тело функции (опционально - для предварительных объявлений)
    std::optional<std::vector<AstNodePtr>> m_body;

    /// Импорт нативной функции `<name>(...):T := %native...;` - АЛИАС: C++-функция НЕ эмитится,
    /// вызовы trust-имени `name(...)` переписываются в прямой вызов `native(...)` (см. m_nativeName).
    /// В отличие от forward-декларации `%name(...) := ...;`, здесь имя/аргументы могут отличаться.
    bool m_isNativeImport = false;

    /// C++-имя импортируемой нативной функции (без ведущего '%'), напр. "abs" или "std::sqrt".
    std::string m_nativeName;

    /// Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` - тип-конструктор.
    /// Регистрирует параметризованный тип (structural), а НЕ функцию: `vector<Int32>` эмитится как
    /// `std::vector<int64_t>` с on-use инклудом. m_nativeName = C++-имя шаблона ("std::vector").
    bool m_isNativeTemplateCtor = false;

    /// Объявление шаблонного native-import (`<T> run(t:AsyncTask<:T>):T := %trust::sync_wait...;`).
    /// В отличие от m_isNativeTemplateCtor это ФУНКЦИЯ-алиас: C++-функция не эмитится, вызовы
    /// переписываются в m_nativeName, а типизация результата идёт через унификацию T.
    bool m_isNativeTemplateImport = false;

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

    /// C++-суффикс имени, ПОРОЖДЁННЫЙ идентичностью объявления (`FunctionIdentity::cppSuffix`
    /// по объявленной сигнатуре и её binder-контексту). Заполняется семантикой ВСЕГДА; кодоген
    /// применяет его безусловно (кроме entry, операторов и нативных имён). Два объявления имеют
    /// одинаковое C++-имя тогда и только тогда, когда они - одна и та же C++-перегрузка.
    std::string m_cppSuffix;

  private:
    AliasKind m_aliasKind = AliasKind::None;
    AstNodePtr m_aliasTarget; ///< null ⇔ AliasKind::None (инвариант `setAlias`).
};

/// ClassDecl - forward-объявление (нативного) класса через `::=`:
///   @include("string")  String ::= %std::string { ... };            - нативный класс (RHS-имя с '%')
///   @include("utility") <T1,T2> Pair ::= %std::pair<T1,T2> { ... }; - ЯВНАЯ реализация шаблона
///   @include("utility") <T1,T2> Pair ::= <T1,T2> %std::pair { ... };- ОБОБЩЁННАЯ (сахар, отложена)
/// trust-имя класса - в text() (слева от `::=`), RHS-имя (native/обычное) - в m_nativeName
/// (C++-имя без ведущего '%'). Класс НЕ имеет тела на Trust-стороне: члены (методы/поля/
/// конструкторы/статич-члены) - forward-объявления (`:= ...`), хранятся в m_body; реализация
/// класса находится в C++-заголовке (инклуд - из @[include], см. attr::Include). Транспилятор
/// НЕ эмитит `struct`, а использует нативное C++-имя (инклуд on-use).
class ClassDecl : public Decl {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(Decl, SLOTS_ClassDecl)
    ClassDecl() { m_kind = ParserToken::Kind::ClassDecl; }

    /// Терм-конструктор: читает из операторного терма `::=` (m_left = trust-имя с типовыми
    /// параметрами шаблона, m_right = RHS CLASS-терм с нативным именем/аргументами/членами).
    ClassDecl(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор (test-only): trust-имя задано явно.
    explicit ClassDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::ClassDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// C++-имя класса (без '%'): "std::string" / "std::pair". Пусто - не native (обычное имя).
    std::string m_nativeName;

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
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(Decl, SLOTS_RecordDecl)
    RecordDecl() { m_kind = ParserToken::Kind::StructDecl; }

    /// Терм-конструктор: читает из операторного терма `::=` (m_left = trust-имя,
    /// m_right = CLASS-терм: текст = первая база, m_right-цепочка = остальные базы,
    /// m_sequence = члены тела).
    RecordDecl(ParserToken::Kind k, TermPtr term, Context* ctx = nullptr);

    /// Manual-конструктор (test-only): trust-имя задано явно.
    explicit RecordDecl(std::string text)
    : Decl(std::move(text)) {
        m_kind = ParserToken::Kind::StructDecl;
    }

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    /// Понижение членов тела: тела методов (FuncDecl::m_body) должны пройти lowering
    /// (SemicolonStmt для операторов-выражений и т.п.), иначе assignment-операторы в
    /// методах эмитятся без завершающей ';'.

    /// Базовые типы (IdentType). Для Struct обычно единственная абстрактная база `:Struct`;
    /// для Class - `:Class` или пользовательские Class-типы (наследование, возможно несколько).
    std::vector<AstNodePtr> m_baseTypes;

    /// Типовые параметры шаблона (аналог ClassDecl::m_templateParams). nullopt - НЕ шаблон.
    std::optional<std::vector<AstNodePtr>> m_templateParams;

    /// Члены тела: nullopt - ПРЕДВАРИТЕЛЬНОЕ (forward) объявление (форма БЕЗ тела
    /// `:Name ::= :Base ...;` → C++ incomplete `struct c_Name;`, членов нет); engaged (возможно
    /// ПУСТОЙ вектор) - полное определение (`{ ... }`, в т.ч. пустое `{ };`). Как FuncDecl::m_body.
    std::optional<std::vector<AstNodePtr>> m_body;
};

/// VarDecl - объявление переменной.
/// kind = VarDecl.
/// text() - имя переменной (чистое, без префиксов).
/// Поддерживает:
///   x := expr        - text()="x", m_type=nullptr, m_initializer=expr
///   x:Type := expr   - text()="x", m_type=IdentType, m_initializer=expr
///   x := expr mut    - мутабельность выражается атрибутом узла (future)
class VarDecl : public Decl {
  public:
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(Decl, SLOTS_VarDecl)
    VarDecl() { m_kind = ParserToken::Kind::VarDecl; }

    /// Терм-конструктор: имя читается из Term (у операторного терма `:=` - из m_left);
    /// тип/инициализатор задаются отдельно.
    VarDecl(TermPtr term, AstNodePtr type = nullptr, AstNodePtr initializer = nullptr, AttrPool* pool = nullptr);

    /// Uniform term-constructor for the generated factory (kind = VarDecl).
    /// При ctx != nullptr строит m_type (из m_left->m_type) и m_initializer (из m_right).
    VarDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

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
    /// Слоты узла (единственный источник — ast/slots/node_slots_x.hpp).
    AST_SLOT_METHOD(AstNodeAttr, SLOTS_DestructureDecl)
    DestructureDecl() { m_kind = ParserToken::Kind::DestructureDecl; }

    /// Терм-конструктор: m_targets из цепочки m_left (список имён), m_source из m_right,
    /// m_isSpread - был ли RHS `... source`. rest-цель (`rest...`) - lval с суффиксом ELLIPSIS
    /// в m_right (грамматика assign_item: `lval ELLIPSIS`); m_targetIsRest[i] для неё = true.
    /// m_isAssign - true для `=` (присваивание в существующие цели), false для `:=` (объявление).
    /// m_targetTypeNodes[i] - явная аннотация типа цели (`a:Int32`; из lval->m_type), nullptr если нет.
    /// Определён в ast_decl.cpp.
    DestructureDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx = nullptr);

    [[nodiscard]] std::string dump(size_t indent = 0) const override;

    std::vector<AstNodePtr> m_targets; ///< Имена слева (Ident): [t1, t2, ..., tN] (или `_` = skip)
    std::vector<bool> m_targetIsRest;  ///< per-target: суффикс `...` (rest/«остаток»), параллельно m_targets
    /// per-target явная аннотация типа (`a:Int32`): узел типа (как в lval->m_type), nullptr если нет.
    std::vector<AstNodePtr> m_targetTypeNodes;
    /// per-target тип (для вывода типов целей spread): однородный тип элемента словаря (конкретный
    /// скаляр) → цель типизируется (кодген: any_cast<T>); INVALID → std::any. Параллельно m_targets.
    std::vector<TypeId> m_targetTypes;
    /// per-target тип ДЕКЛАРИРУЕМОЙ переменной (для словаря): явная аннотация (`a:Int32`) или
    /// выведенный natural runtime тип элемента. Отличается от m_targetTypes (тип any_cast = как хранит
    /// Dict, int → int64_t): кодген объявляет переменную этим типом, а any_cast<T> берёт из
    /// m_targetTypes (чтобы соответствовать хранению Dict). Для кортежа не используется.
    std::vector<TypeId> m_targetDeclaredTypes;
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
