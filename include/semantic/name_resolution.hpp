#pragma once

// include/semantic/name_resolution.hpp
// Единый однопроходный проход разрешения имён (обязательное ядро семантики).
// Объединяет бывшие SymbolCollectorPass + NameResolverPass: по мере обхода AST
// строит единую таблицу символов SymbolTable (стек вложенных скоупов), регистрирует
// объявления и разрешает Ident (имя должно быть объявлено до использования).
// Раскрытие контекст-макросов вынесено в отдельный всегда-подключённый хук
// ContextMacroExpander (см. macro_expander.hpp), поэтому ядро здесь - чистый
// разрешитель имён. Общие query-сервисы (контекст области имён/функции,
// resolveType/buildFuncType/isRegisteredRuntimeSymbol) живут в AnalysisContext
// (semantic/pass.hpp) и доступны ядру и любым хукам.
//
// Опциональные анализаторы подключаются ПАРАЛЛЕЛЬНО через InlineAnalysisHook
// и получают те же временные данные (SymbolTable) в реальном времени.

#include "semantic/pass.hpp"
#include "semantic/inline_hook.hpp"
#include "ast/ast_nodes.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class NameResolutionPass {
  public:
    explicit NameResolutionPass(AnalysisContext& actx);

    /// Подключает опциональный анализатор (параллельно к обходу ядра).
    /// ВАЖНО: всегда-подключённый ContextMacroExpander должен быть добавлен до
    /// остальных хуков (его onNode первым раскрывает ContextMacro/квалификатор @::).
    void addHook(std::unique_ptr<InlineAnalysisHook> hook);

    /// Выполняет однопроходное разрешение имён над корневым списком операторов.
    /// В начале обработки каждого узла вызывается хук ContextMacroExpander,
    /// раскрывающий контекст-макросы (ContextMacro → Literal/IdentName, квалификатор
    /// @:: в именах) - в этом же обходе, без отдельного прохода. Мутирует ast_nodes.
    void run(std::vector<AstNodePtr>& ast_nodes);

    /// Финальный проход по подключённым хукам (после завершения обхода).
    void finalize();

  private:
    void analyzeNode(AstNodePtr& self);
    /// Обход реальных детей через единый источник AstNodeBase::collectChildren
    /// (ссылки на слоты, чтобы хук мог заменять узлы). Не открывает скоупы -
    /// это делает analyzeNode.
    void analyzeChildren(AstNodePtr& self);
    /// Анализ литерала словаря: элементы - безымянные значения (сам узел) либо
    /// именованные `name=value` (AssignOp: left=Ident-метка, right=значение).
    /// Имя-метку НЕ резолвим как переменную (это декларативная метка, не ссылка).
    void analyzeDictLiteral(Sequence& dict_node);
    /// Анализ литерала массива `[1,2:Int8,3,]` / `[1,2,3,]:Int32`: анализ элементов,
    /// вывод типа элемента (аннотация `]:Type` → аннотация элемента → join), интернирование
    /// структурного Array<Elem> (сохраняется в DictLiteralNode::arrayType). Вложенные
    /// массивы-литералы (многомерные) - диагностика «не реализовано».
    void analyzeArrayInit(DictLiteralNode& node);
    /// Общий тип элементов массива с сохранением узкой разрядности: `[1,2,3,]` → Int8,
    /// `[100,300,]` → Int16, все одного типа → он, float → Double, строки → Str. Несовместимое → INVALID.
    TypeId arrayElementJoin(const std::vector<TypeId>& elementTypes) const;
    /// Анализ литерала диапазона `start..stop` / `start..stop..step`: резолвит типы операндов,
    /// валидирует (арифметические/Any), вычисляет элементный тип (join) и сохраняет его на
    /// узле (RangeExpr::elementType) для кодогенерации `trust::Range<Elem>`. Тип выражения -
    /// универсальный `:Range`.
    void analyzeRangeExpr(RangeExpr& range_node);
    /// Анализ доступа к элементу словаря: MemberAccess (имя `d.two` или статический
    /// индекс `d.1`) и ArrayAccess (динамический индекс `d[1]`). Объект анализируется;
    /// имя поля справа от '.' НЕ резолвится как переменная; для статического индекса
    /// выполняется проверка по статической размерности объекта (dims).
    void analyzeAccess(Binary& access);
    /// Анализ деструктуризации `t1, ..., tN := [... ]source;`: spread-коллекция (`... source`) -
    /// цели [0..N-2] объявляются std::any (попервый элемент), последняя - «остаток» коллекции;
    /// кортеж (без `...`) - цели связываются std::get<N> с проверкой арности. `_` - skip.
    void analyzeDestructure(DestructureDecl& node);
    /// Деструктуризация кортежа (структурный источник, без `...`): объявление целей по индексам
    /// с типом элемента; проверка числа целей против размера кортежа.
    void analyzeDestructureTuple(DestructureDecl& node, TypeId tupleType);
    /// Объявление одной цели деструктуризации. isRest - «остаток» коллекции (если имя уже
    /// объявлено == источник → мутация, не объявляем). type - конкретный тип (по умолчанию
    /// Any для элемента / Dict для rest). `_` - skip (не объявляем).
    void declareDestructureTarget(HasText& t, bool isRest, TypeId type = INVALID_TYPE_ID);
    /// Цель деструктуризации-ПРИСВАИВАНИЯ (`a, b = ... source`): резолв существующей переменной,
    /// проверка на константность; объявление не создаётся. Заполняет node.m_targetTypes[i] типом
    /// цели (для кодгена any_cast<T>). `_` - skip; rest == источник - мутация (ничего не делаем).
    void assignDestructureTarget(DestructureDecl& node, size_t i, HasText& t, bool isRest);
    /// Явный тип цели из аннотации (`a:Int32`, node.m_targetTypeNodes[i]); INVALID - аннотации нет
    /// или тип не резолвится (диагностируется). Иначе - fallback.
    TypeId explicitTargetType(const DestructureDecl& node, size_t i, TypeId fallback);
    /// True, если текущий скоуп - локальный (в стеке скоупов есть FuncDecl). Единый предикат
    /// для sigil-нормализации имён (analyzeVarDecl / declareDestructureTarget).
    [[nodiscard]] bool isInLocalScope() const;
    /// True, если текущий узел находится ВНУТРИ тела цикла (в стеке скоупов есть скоуп,
    /// созданный WhileStmt/DoWhileStmt). Циклы создают скоуп на время обхода тела (см. analyzeNode).
    [[nodiscard]] bool isInLoop() const;
    /// Нормализация bare-имени в локальном скоупе (`x` → `$x`, опция -Wsigil): правит текст
    /// узла и репортит предупреждение. Возвращает имя для символа (с сигилом при нормализации).
    /// Единый источник sigil-логики для analyzeVarDecl и declareDestructureTarget.
    std::string normalizeLocalSigil(HasText& node, MapperRange range, bool isLocal);
    /// Каноническое имя цели деструктуризации (сигил-нормализация БЕЗ мутации узла и без
    /// предупреждения): bare-имя в локальном скоупе → "$" + имя. Единый источник для
    /// restTargetNameAllowed (совпадает с логикой declareDestructureTarget).
    [[nodiscard]] std::string canonicalTargetName(const HasText& t) const;
    /// Проверка переиспользования имени именованной rest-цели (`rest...`) перед объявлением.
    /// isSpreadDict - спред-словарь (допустима мутация-идиома: rest-цель == самому источнику);
    /// для кортежа rest никогда не мутация. source - узел источника (для сравнения имени при
    /// мутации-идиоме). Возвращает true, если цель можно объявлять (имя свободно / мутация-
    /// идиома); при конфликте с существующей переменной репортит Error и возвращает false.
    /// `_` - skip, не проверяется.
    bool restTargetNameAllowed(HasText& t, bool isSpreadDict, const AstNodeBase* source);
    /// Общий подсчёт слотов-элементов и валидация rest-цели для деструктуризации (spread-словаря и
    /// кортежа): в elementSlots - число не-rest Ident-целей (включая `_`), в hasRest - наличие
    /// rest-маркера (`rest...`/`_...`). Если rest встречается не последним - репортит Error и
    /// возвращает false (вызывающий должен прервать разбор целей). Унифицирует идентичный цикл
    /// в analyzeDestructure и analyzeDestructureTuple.
    bool collectDestructureSlots(const DestructureDecl& node, size_t& elementSlots, bool& hasRest);
    /// Обработка вызова метода на объекте (MemberAccess: obj.method(args)): по типу объекта
    /// ищет метод в реестре типов (TypeRegistry::findMethod), проверяет наличие и количество
    /// аргументов по сигнатуре (метод - функциональный тип) и типизирует результат
    /// (возвращаемый тип). Диагностирует отсутствующий метод / неверное число аргументов.
    void handleMethodCall(Binary& access);
    /// Статическая размерность объекта (число элементов словаря): литерал → размер
    /// m_body; переменная → свойство dims символа; иначе -1 (неизвестна).
    int64_t dictSizeOf(const AstNodeBase* obj) const;
    /// Тип значения элемента словаря по его узлу (литерал → literalType; иначе resolvedType).
    TypeId dictElementType(const AstNodeBase* valueNode) const;
    /// Типы элементов словаря-источника ПО ИНДЕКСУ (для вывода типов целей деструктуризации,
    /// аналогично кортежу): литерал → тип каждого элемента m_body; переменная → dictFieldTypes.
    /// Возвращаются СЫРЫЕ типы (с битом inferred у литералов). Пустой вектор - типы недоступны.
    std::vector<TypeId> dictElementTypes(const AstNodeBase* src) const;
    /// «Естественный» runtime-тип элемента словаря (как хранит Dict): Int8..Int64 → Int64,
    /// UInt* → UInt64, Float*/Double → Double, Bool → Bool, StrChar/StrWide → соответствующий.
    /// Нечисловой/неизвестный → INVALID_TYPE_ID (→ Any).
    TypeId naturalRuntimeType(TypeId nominal) const;
    /// JOIN (максимальный) элементных runtime-типов для widening В ЦИКЛЕ: Bool+Int → Int64,
    /// любой float → Double, однородные строки → Str; несовместимое/неизвестное → INVALID (Any).
    TypeId joinElementTypes(const std::vector<TypeId>& naturalized) const;
    /// Тип поля объекта по ключу доступа (имя/статический индекс для MemberAccess, индекс
    /// для ArrayAccess). INVALID - тип неизвестен (гетерогенный/динамический) → Any.
    TypeId dictFieldTypeOf(const Binary& access) const;
    /// Обработка узла по kind (объявления, типы, Ident); БЕЗ рекурсии в детей -
    /// полный обход детей выполняет analyzeNode через analyzeChildren.
    void handleNode(AstNodePtr& self);
    void analyzeVarDecl(VarDecl& var_node);
    void analyzeTypeDecl(Binary& binary_node);
    /// Объявление enum-типа (`Color ::= :Enum(RED=1, GREEN=2,)` / `(RED=1, GREEN=2,):Enum`, как
    /// Tuple - TypeDecl с правой частью-DictLiteral, аннотация m_type «Enum»). Регистрирует
    /// enum-тип (EnumTypeData), вычисляет единый тип значений (по общим правилам, предупреждение
    /// WidenAny при повышении), биндит имя в скоупе и регистрирует классические методы.
    void analyzeEnumDecl(Binary& binary_node);
    /// Объявление Variant-типа (`Value ::= :Variant(RED:Int64=0, GREEN='g',)` / `(...):Variant`,
    /// как Tuple - TypeDecl с правой частью-DictLiteral, аннотация m_type «Variant»).
    /// Регистрирует гетерогенный Variant-тип (VariantTypeData): тип каждого члена - свой
    /// (выводится из значения/аннотации), биндит имя в скоупе, регистрирует методы.
    void analyzeVariantDecl(Binary& binary_node);
    /// Применяет к базовому типу ортогональные квалификаторы из атрибутов узла:
    /// attr::ReadOnly ('^') → бит const, attr::Reftype → вид ссылки (fast-path бит либо
    /// составной узел для вложенности). Единый источник для переменных и параметров.
    /// Диагностирует неизвестный/отсутствующий вид ссылки. Возвращает новый TypeId.
    TypeId applyRefAttrs(TypeId base, const AstNodeAttr& node, MapperRange range);
    /// Регистрирует имя функции в ТЕКУЩЕМ (внешнем) скоупе.
    void analyzeFuncDecl(FuncDecl& func_node);
    /// Регистрирует параметры функции в скоупе функции (после enterScope).
    void declareFuncParams(FuncDecl& func_node);
    const Symbol* lookupOrError(AstNodeBase& node);

    /// Единый алгоритм разрешения простого имени (имя без сигила/квалификатора): `x` ищется
    /// сначала как локальная `$x` (если такой символ есть в текущем/охватывающем скоупе - текст
    /// узла-ссылки нормализуется на `$x` для согласованного манглинга; name_to_cpp срезает `$` → c_x),
    /// иначе - как есть.
    /// Квалифицированные/сигилные/нативные имена - без изменений. Возвращает символ или nullptr.
    Symbol* resolveSimple(AstNodeBase* node, std::string_view name);
    /// Не-мутирующий вариант разрешения простого имени (для чтения dims/типов до обхода детей).
    const Symbol* resolveSimpleRead(std::string_view name) const;

    // -- Типизация выражений (post-order, см. analyzeNode) --
    /// Пост-порядковая типизация выражения/объявления: вычисляет тип результата,
    /// сохраняет в AnalysisContext (setExprType) и расширяет выводимый (inferred)
    /// тип целевой переменной.
    void typeExpr(AstNodeBase* node);

    /// Компиляйт-тайм проверка типов аргументов вызова на соответствие printf-формату
    /// (атрибут @[format("printf", string_index, first_to_check)]). Вызывается из typeExpr
    /// для CallExpr, когда типы аргументов уже вычислены. Диагностирует несоответствия
    /// спецификаторам формата через OptKind::Format.
    void checkFormatArgs(CallExpr& call);

    /// Компиляйт-тайм проверка аргументов строки-формата `"{}"(args)` / `'{}'(args)` (callee -
    /// строковый литерал): сверка числа плейсхолдеров `{}` с числом аргументов и баланса скобок.
    void checkFormatStringArgs(CallExpr& call);

    /// Доступ к элементу кортежа `t.name` / `t.0` / `t[idx]` (левый операнд - структурный
    /// Tuple-тип): резолв имени/индекса в TupleTypeData, тип результата = тип элемента.
    /// Диагностирует отсутствующий/вне-диапазона элемент.
    void resolveTupleAccess(Binary& n, TypeId tupleType);
    /// Доступ к элементу массива `a[i]` / `a.0`: левый операнд - структурный Array-тип.
    /// Тип результата = элементный тип; статический индекс проверяется по размерности.
    void resolveArrayAccess(Binary& n, TypeId arrayType);

    /// Вычисляет и сохраняет типы операндов/результата бинарного узла
    /// (lhsType/rhsType/resultType/commonType), кладёт результат в кеш типов выражений
    /// и возвращает TypeId результата. Общий хелпер для всех бинарных kinds
    /// (MathOp-группа и AssignOp), устраняет дублирование их типизации.
    TypeId typeBinaryResult(Binary& b);

    /// Расширяет выводимый (inferred) тип целевой переменной (join по истории присвоений)
    /// и обновляет VarDecl::inferredType на узле объявления. lhs - левый операнд (Ident).
    void widenInferredTarget(const AstNodeBase* lhs, TypeId result);

    /// Проверка сужения значения (sourceType) в явно-типизированную цель (targetType):
    /// литерал, влезающий в цель - ок; иначе (переменная/неизвестное шире цели) - ошибка
    /// с fixit-предложением «use cast `:Type(expr)`». valueNode - исходное выражение.
    void checkAssignmentNarrowing(const AstNodeBase* valueNode, TypeId sourceType, TypeId targetType, std::string_view targetName);

    // Вспомогательные: вход/выход скоупа с уведомлением хуков.
    // enterScope фиксирует узел AST, открывший скоуп (creator в SymbolTable).
    void enterScope(const AstNodeBase& node);
    void exitScope();

    AnalysisContext& m_actx;
    std::vector<std::unique_ptr<InlineAnalysisHook>> m_hooks;
};

} // namespace trust
