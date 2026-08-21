#include "transpiler/transpiler.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "semantic/symbol_table.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

// -- RAII-обёртка пары mapStart/mapStop: mapStart в конструкторе, mapStop в деструкторе.
//    Гарантирует закрытие маппинга даже при раннем выходе (return/EXPECT). Некопируем.
//    Range для mapStop всегда совпадает с range, переданным в mapStart.
class MapperScope {
  public:
    MapperScope(SourceMapWriter& mapper, MapperRange range, MapperFile out)
    : m_mapper(mapper)
    , m_range(range) {
        m_mapper.mapStart(range, out);
    }
    ~MapperScope() { m_mapper.mapStop(m_range); }
    MapperScope(const MapperScope&) = delete;
    MapperScope& operator=(const MapperScope&) = delete;

  private:
    SourceMapWriter& m_mapper;
    MapperRange m_range;
};

// -- Helper: собрать операторы тела и диапазон блока из узла-тела
//    (ScopeBlock/Sequence → m_body + range блока; одиночный statement → сам узел) --
static void collectBodyStatements(const AstNodePtr& bodyNode, std::vector<AstNodePtr>& out, MapperRange& blockRange) {
    out.clear();
    blockRange = {};
    if (!bodyNode) {
        return;
    }
    // Блочный узел (ScopeBlock/sequence/ModuleNode) → его m_body + range; иначе - одиночный statement.
    // as_sequence() (virtual) устраняет kind/static_cast; Attr исключён через is_block_kind.
    if (is_block_kind(bodyNode->kind())) {
        out = bodyNode->as_sequence()->m_body;
        blockRange = bodyNode->range();
    } else {
        out.push_back(bodyNode);
        blockRange = bodyNode->range();
    }
}

// Проверяет, завершается ли тело функции явным return (для инжекта `return 0;` в entry-функцию).
static bool bodyEndsWithReturn(const std::vector<AstNodePtr>& body) {
    return !body.empty() && body.back() && body.back()->kind() == ParserToken::Kind::ReturnStmt;
}

CppTranspiler::CppTranspiler(Context& ctx, const SymbolTable* resolvedTypes)
: m_ctx(ctx)
, m_resolvedTypes(resolvedTypes) {
}

bool CppTranspiler::isSuppressedDoc(ParserToken::Kind k) const {
    // Флаг «comments» включён = комментарии ВЫВОДЯТСЯ; подавление - флаг выключен (-Wno-comments).
    return k == ParserToken::Kind::Document && !m_ctx.opts().is_enabled(FlagKind::Comments);
}

// Эмитит документирующий комментарий, привязанный к узлу объявления (AstNodeBase::documentation).
// Строками с текущим отступом; `##`/`##<` нормализуются в C++-валидные `///`/`///<` (как visit_Document).
// Подавляется флагом -Wno-comments (Comments выключен = не выводить), как и sibling-Document.
void CppTranspiler::emitDocumentation(const AstNodeBase& node, MapperFile output_idx) {
    if (node.documentation.empty() || !m_ctx.opts().is_enabled(FlagKind::Comments)) {
        return;
    }
    size_t pos = 0;
    const std::string_view all = node.documentation;
    while (pos <= all.size()) {
        const size_t nl = all.find('\n', pos);
        const std::string_view line = (nl == std::string_view::npos) ? all.substr(pos) : all.substr(pos, nl - pos);
        m_ctx.source().output_append(output_idx, indentPrefix());
        if (line.starts_with("##")) {
            m_ctx.source().output_append(output_idx, "///");
            m_ctx.source().output_append(output_idx, line.substr(2));
        } else {
            m_ctx.source().output_append(output_idx, line);
        }
        m_ctx.source().output_append(output_idx, "\n");
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
}

void CppTranspiler::generateToFile(const std::vector<AstNodePtr>& ast_nodes, MapperFile output_idx) {
    const AstNodeBase* prev = nullptr;
    for (const auto& node : ast_nodes) {
        if (!node) {
            continue;
        }
        if (isSuppressedDoc(node->kind())) {
            continue;
        }
        emitBlockSeparator(prev, *node, output_idx);
        generateNodeToFile(*node, output_idx);
        prev = node.get();
    }
    if (!ast_nodes.empty()) {
        m_ctx.source().output_append(output_idx, "\n");
    }
    // МЕХАНИЗМ №1 - ПО ТИПУ: только ПОСЛЕ полного обхода AST формируем инклуды из собранных
    // типов (m_usedTypes), затем препендим все директивы (emitCollectedIncludes).
    collectTypeIncludes();
    emitCollectedIncludes(output_idx);
}

void CppTranspiler::collectLinkLib(const AstNodeAttr& node) {
    const AttrPool& pool = m_ctx.attrs();
    auto link_id = pool.lookup(attr::Link);
    if (!link_id.has_value() || !node.has_attr(*link_id)) {
        return;
    }
    const std::vector<std::string>* args = node.attr_args(*link_id);
    // @[link("имя")] - ровно один аргумент (имя библиотеки).
    if (!args || args->size() != 1 || (*args)[0].empty()) {
        return;
    }
    m_linkLibs.insert("-l" + (*args)[0]);
}

void CppTranspiler::emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx) {
    // Перевод строки между блоками вставляется только если они в исходнике на разных
    // строках: строка конца prev != строка начала node. Если строки совпадают (блоки
    // намеренно на одной строке) - перевод строки не выводится, вместо него пробел.
    if (!prev) {
        return; // первый блок - перевод строки не нужен
    }
    const bool sameSourceLine = m_ctx.source().line(prev->range().end) == m_ctx.source().line(node.range().begin);
    if (!sameSourceLine) {
        m_ctx.source().output_append(output_idx, "\n");
    } else {
        emitSameLineSpace(node.text(), output_idx);
    }
}

void CppTranspiler::emitSameLineSpace(std::string_view nextText, MapperFile output_idx) {
    // Для читаемости между блоками на одной строке ставим пробел, но не дублируем его,
    // если на границе уже есть пробельный символ (например, EMBED-содержимое с ведущими/
    // хвостовыми пробелами).
    const std::string_view body = m_ctx.source().output_body(output_idx);
    const bool prevEndsWithSpace = !body.empty() && (body.back() == ' ' || body.back() == '\t');
    const bool nextStartsWithSpace = !nextText.empty() && (nextText.front() == ' ' || nextText.front() == '\t');
    if (!prevEndsWithSpace && !nextStartsWithSpace) {
        m_ctx.source().output_append(output_idx, " ");
    }
}

void CppTranspiler::emitSequenceBody(const Sequence& node, MapperFile output_idx) {
    // Sequence, ScopeBlock or ModuleNode → walk body. Метки именованных блоков и goto
    // вставляются анализатором (lowering) как отдельные узлы LabelStmt/GotoStmt - здесь
    // только кодогенерация: каждый узел m_body эмитится по своему kind.

    const bool inBlock = indentLevel() > 0;
    const AstNodeBase* prev = nullptr;
    for (const auto& child : node.m_body) {
        if (!child) {
            continue;
        }
        if (isSuppressedDoc(child->kind())) {
            continue;
        }
        if (inBlock) {
            // Внутри блока - каждый оператор с новой строки с отступом (нормальное форматирование).
            // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход.
            // Перевод строки добавляется ПОСЛЕ каждого реально выведенного узла (а не до),
            // чтобы подавленные доки/пустые bundle'ы не оставляли «сиротских» пустых строк.
            const size_t before = m_ctx.source().output_body(output_idx).size();
            // Документирующий комментарий объявления (из term->m_docs) - строками с отступом.
            emitDocumentation(*child, output_idx);
            if (!is_block_kind(child->kind())) {
                m_ctx.source().output_append(output_idx, indentPrefix());
            }
            generateNodeToFile(*child, output_idx);
            if (m_ctx.source().output_body(output_idx).size() != before) {
                const std::string_view body = m_ctx.source().output_body(output_idx);
                if (body.empty() || body.back() != '\n') {
                    m_ctx.source().output_append(output_idx, "\n");
                }
            }
        } else {
            // На верхнем уровне (indent==0) - прежнее поведение (зеркалирование строк исходника).
            emitBlockSeparator(prev, *child, output_idx);
            // Документирующий комментарий объявления (из term->m_docs) - строками без отступа.
            emitDocumentation(*child, output_idx);
            generateNodeToFile(*child, output_idx);
            prev = child.get();
        }
    }
}

// Placeholder для нереализованных expression-only kinds: "{}" только в expression-контексте.
void CppTranspiler::emitPlaceholderExpr(MapperFile output_idx) {
    if (m_exprDepth > 0) {
        m_ctx.source().output_append(output_idx, "{}");
    }
}

// Унифицированный вывод текста как вложенного выражения (только при m_exprDepth>0).
void CppTranspiler::emitExprText(std::string_view text) {
    if (m_exprDepth > 0) {
        m_ctx.source().output_append(m_out, text);
    }
}

// Вставка preprocessor-инклуда (если требуется типом) в начало выходного файла.
// Инклуд рантайм-типа помечается ведущим '@' (см. TypeRegistry::preprocInclude):
// '@' срезается, путь заголовка запоминается в m_runtimeHeaders (ТОЛЬКО реально
// использованные), а в файл пишется настоящая директива #include "<path>".
void CppTranspiler::recordRequiredInclude(std::string_view include) const {
    if (include.empty()) {
        return;
    }
    if (include.front() == '@') {
        include.remove_prefix(1);
        const std::string bare(include);
        if (m_runtimeHeaders.count(bare)) {
            return; // уже записан
        }
        m_runtimeHeaders.insert(bare);
        m_requiredIncludes.insert("#include \"" + bare + "\"");
        return;
    }
    m_requiredIncludes.insert(std::string(include));
}

// -- МЕХАНИЗМ №1 - ПО ТИПУ (TypeRegistry): сбор ТИПОВ во время обхода AST, инклуды ПОСЛЕ --
// Во время эмиссии resolveCppTypeId/recordUsedType только отмечают использованные типы
// (канонические TypeId) в m_usedTypes. Сами директивы инклудов из типов формируются
// ПОСЛЕ полного обхода AST (collectTypeIncludes → emitCollectedIncludes).
void CppTranspiler::recordUsedType(TypeId type_id) const {
    m_usedTypes.insert(m_ctx.types().getCanonicalTypeId(type_id));
}

void CppTranspiler::collectTypeIncludes() const {
    for (TypeId id : m_usedTypes) {
        for (const auto& inc : m_ctx.types().getPreprocIncludes(id)) {
            recordRequiredInclude(inc);
        }
        // std::shared_ptr/std::weak_ptr/std::unique_ptr (виды ссылок) требуют <memory>.
        const RefType rt = getRefType(getKindFromId(id));
        if (rt == RefType::kShared || rt == RefType::kWeak || rt == RefType::kUnique) {
            recordRequiredInclude("#include <memory>");
        }
    }
}

std::optional<std::string> CppTranspiler::emitTypeName(TypeId type_id, std::string_view displayName) {
    // МЕХАНИЗМ №1 - ПО ТИПУ: resolveCppTypeId только отмечает тип как использованный
    // (recordUsedType → m_usedTypes); инклуды из типов формируются ПОСЛЕ обхода AST.
    auto resolved = resolveCppTypeId(type_id, displayName);
    if (!resolved) {
        return std::nullopt;
    }
    return std::move(resolved->first);
}

std::string CppTranspiler::emitTypeNameForNode(const AstNodeBase* type_node) {
    if (!type_node || type_node->kind() != ParserToken::Kind::TypeName) {
        return ""; // нет типа-аннотации - caller решает (напр. параметр без типа → auto)
    }
    // Резолвим TypeId, затем применяем ортогональные квалификаторы из атрибутов узла ТИПА
    // (`fmt: @[reftype(ptr)]@ StrChar^`): ReadOnly → const, reftype → вид ссылки. Так тип
    // параметра/переменной с атрибутами получает то же C++-имя, что вычислила семантика.
    auto type_id = resolveTypeIdByName(type_node->text());
    if (!type_id.has_value()) {
        m_ctx.report(type_node->range(), OptKind::ParseError, "unable to generate C++ type '{}'", type_node->text());
        return "";
    }
    TypeId applied = *type_id;
    // Тип-определение массива `:Elem[3]`/`:Elem[3,4]`: размерности из `[...]` (IdentType::dims)
    // превращают базовый тип в структурный Array<Elem,dims> - так семантика (resolveType) и
    // кодогенерация согласованы (N-D - без кодогенерации: диагностика «не реализовано» ниже).
    if (const auto* it = dynamic_cast<const IdentType*>(type_node)) {
        if (it->dims() && !it->dims()->empty()) {
            std::vector<uint64_t> dims;
            for (const auto& d : *it->dims()) {
                if (!d || d->kind() != ParserToken::Kind::IntLiteral) {
                    continue;
                }
                unsigned long long v = 0;
                try {
                    v = std::stoull(std::string(d->text()), nullptr, 0);
                } catch (...) {
                    v = 0;
                }
                dims.push_back(v);
            }
            if (!dims.empty()) {
                // Определение типа массива `:Elem[3]`: изменяемый массив (std::vector) с известной
                // размерностью (согласовано с семантикой resolveType). N-D - без кодогенерации.
                applied = m_ctx.types().getOrCreateArrayType(*type_id, std::move(dims));
            }
        }
    }
    if (const AstNodeAttr* a = type_node->as_attr()) {
        const AttrPool& pool = m_ctx.attrs();
        if (a->has_attr(pool, attr::ReadOnly)) {
            applied = withConst(applied);
        }
        if (auto rid = pool.lookup(attr::Reftype); rid.has_value() && a->has_attr(*rid)) {
            if (const std::vector<std::string>* args = a->attr_args(*rid); args && !args->empty()) {
                if (auto rk = refTypeFromString(args->front())) {
                    applied = m_ctx.types().applyRefType(applied, *rk);
                }
            }
        }
    }
    // Многомерное определение массива `:Bool[3,4]`: тип регистрируется (семантика), но генерация
    // C++ не реализована («не реализовано» - только на кодогенерации, как у вложенных литералов).
    if (isMultiDimArray(applied, m_ctx.types())) {
        m_ctx.report(type_node->range(), OptKind::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        return "";
    }
    // resolveCppTypeId (m_resolvedTypes-устойчивый резолв по имени) записывает все инклуды типа.
    if (auto resolved = resolveCppTypeId(applied, type_node->text())) {
        return std::move(resolved->first);
    }
    // Fallback запрещён: нерезолвящееся имя типа - ВСЕГДА ошибка с обязательной диагностикой.
    m_ctx.report(type_node->range(), OptKind::ParseError, "unable to generate C++ type '{}'", type_node->text());
    return "";
}

// Рантайм-символ по типизированному идентификатору: заголовки из компайлтайм-таблицы
// (types/runtime_symbols.hpp). Enum вместо строки - опечатка в имени символа невозможна.
// ЕДИНСТВЕННЫЙ способ записи заголовков рантайм-символа.
void CppTranspiler::recordRuntimeSymbolHeaders(RuntimeSymbolId id) const {
    for (const auto& h : runtimeSymbolHeaders(id)) {
        recordRequiredInclude(h);
    }
}

// Скан текста EMBED-вставки ({% %}) на имена рантайм-символов (substring) и запись их
// заголовков через recordRuntimeSymbolHeaders(id). Для EMBED типа нет - это единственный
// способ определить нужные заголовки. Отдельный хелпер, не перегрузка записи.
void CppTranspiler::recordRuntimeSymbolsInText(std::string_view text) const {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const std::string_view sym = runtimeSymbolName(id);
        if (text.find(sym) != std::string_view::npos) {
            recordRuntimeSymbolHeaders(id);
        }
    }
}

void CppTranspiler::emitCollectedIncludes(MapperFile output_idx) {
    for (const auto& inc : m_requiredIncludes) {
        m_ctx.source().output_prepend(output_idx, inc);
    }
}

void CppTranspiler::generateNodeToFile(const AstNodeBase& node, MapperFile output_idx) {
    if (node.kind() == ParserToken::Kind::END) {
        return;
    }

    // Единая диспетчеризация ПО KIND (см. ast/kind_visitor.hpp): класс - наследник абстрактного
    // KindVisitor, поэтому dispatchKind вызывает соответствующий visit_<Kind> прямо на *this.
    // Каждый kind реализован (или явный no-op): пропуск генерации не даст скомпилироваться.
    m_out = output_idx;
    dispatchKind(node, *this);
}
void CppTranspiler::generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx) {
    // Флаг линковки нативной библиотеки из @[link("имя")].
    collectLinkLib(var_node);

    // Манглинг trust-имени в C++-идентификатор (срез '%' у нативных имён).
    std::string var_name = utils::name_to_cpp(var_node.text());
    if (var_name.empty()) {
        return;
    }

    // Determine type: типизированное имя → тип аннотации; нетипизированное → выведенный
    // анализатором конкретный тип (inferred join по истории присвоений), иначе std::any.
    std::string cpp_type;
    const bool typed = var_node.m_type && var_node.m_type->kind() == ParserToken::Kind::TypeName;
    if (typed) {
        // Резолв по имени узла-типа + запись инклудов. Нерезолвящееся имя → emitTypeNameForNode
        // сам выводит диагностику и возвращает "".
        cpp_type = emitTypeNameForNode(var_node.m_type.get());
        if (cpp_type.empty()) {
            return;
        }
    } else {
        // Нетипизированная переменная: выведенный семантикой конкретный тип, либо ЯВНО помеченный
        // std::any (семантика маркирует Any для тип-less инициализаторов - тип-имя, embed, вызов
        // с неизвестным результатом, отрицательный литерал - и для forward-объявлений без типа).
        // Any - обычный выводимый тип → эмитим единообразно emitTypeName(inferred). INVALID у
        // переменной - ошибка вывода: тихий fallback на std::any запрещён (AGENTS rule 5).
        TypeId inferred = var_node.inferredType;
        // Страховка: forward-объявление, чей тип семантика не пометила (напр. stdlib/Any не
        // зарегистрирован), мог быть завершён последующим определением - добиваем по символу.
        if (inferred == INVALID_TYPE_ID && m_resolvedTypes) {
            if (const Symbol* s = m_resolvedTypes->resolve(var_node.text())) {
                inferred = s->type;
            }
        }
        if (inferred == INVALID_TYPE_ID) {
            if (var_node.m_initializer) {
                m_ctx.report(var_node.range(), OptKind::ParseError, "unable to infer type for variable '{}'", var_node.text());
            } else {
                m_ctx.report(var_node.range(), OptKind::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            }
            return;
        }
        std::optional<std::string> name = emitTypeName(inferred, var_node.text());
        if (!name || name->empty()) {
            m_ctx.report(var_node.range(), OptKind::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            return;
        }
        cpp_type = std::move(*name);
    }

    // Инклуды типа не нужны здесь: emitTypeName отметил тип (m_usedTypes), инклуды будут
    // сформированы из них ПОСЛЕ обхода AST (collectTypeIncludes).

    MapperScope scope(m_ctx.source(), var_node.range(), output_idx);
    // Константность ОБЪЯВЛЕНИЯ переменной - attr::ReadOnly на узле ('^' на имени или
    // @[readonly]@). НЕ берётся из бита Symbol::type: переменная может стать константной
    // позже (became-const, `x := 42; x^ += 1;`), но её ДЕКЛАРАЦИЯ обязана остаться не-const
    // (переменная мутировалась до финализации). Признак на узле = const «в типе» объявления.
    // Префикс влияет на смещение имени в выводе (source-map): имя идёт после "<prefix> <cpp_type> ".
    std::string prefix;
    if (var_node.has_attr(m_ctx.attrs(), attr::ReadOnly)) {
        prefix += "const ";
    }
    if (var_node.has_attr(m_ctx.attrs(), attr::ThreadLocal)) {
        prefix += "thread_local ";
    }
    // Forward-объявление `x:Type := ...;` → C++ extern-декларация переменной (объявление без
    // определения); иначе - определение с инициализатором. Смещение имени в выводе зависит
    // от наличия префикса "extern " (source-map).
    uint32_t namePrefixLen = static_cast<uint32_t>(prefix.length()) + static_cast<uint32_t>(cpp_type.length()) + 1;
    if (!var_node.m_initializer || m_forwardDeclOnly) {
        m_ctx.source().output_append(output_idx, "extern " + prefix + cpp_type + " " + var_name + ";");
        namePrefixLen += 7; // strlen("extern ")
    } else {
        m_ctx.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
        emitExpr(var_node.m_initializer.get());
        m_ctx.source().output_append(output_idx, ";");
    }

    // Добавляем маппинг имени переменной для hover-ссылок.
    // Диапазон trust-имени: nameRange() берёт диапазон реального имени из m_term->m_left
    // (важно при макро-раскрытии, где range() самого узла - оператор). Fallback - имя
    // в начале range() (когда range() покрывает всю строку, m_term->m_left отсутствует).
    MapperRange trustNameRange = var_node.nameRange();
    if (trustNameRange.isInvalid()) {
        MapperLocation trustNameBegin = m_ctx.source().makeLoc(var_node.range().begin.fileIdx(), var_node.range().begin.offset());
        MapperLocation trustNameEnd = m_ctx.source().makeLoc(
            trustNameBegin.fileIdx(), trustNameBegin.offset() + static_cast<uint32_t>(utils::strip_native_prefix(var_node.text()).size()));
        trustNameRange = MapperRange(trustNameBegin, trustNameEnd);
    }
    // Имя выводится сразу после префикса "<cpp_type> " (или "extern <cpp_type> " для forward).
    // trust-имя в маппинге - исходное (var_node.text(), для нативных с '%'), cpp-имя - манглированное.
    mapDeclaredName(output_idx, trustNameRange, namePrefixLen, var_node.text(), var_name);

    // Экспортируются ОПРЕДЕЛЕНИЯ на верхнем уровне модуля в НЕ анонимной области имён
    // (глобальная '::' и именованные 'ns::' - с квалификацией, напр. "ns::x").
    // Локальные (внутри функций/блоков кода), скрытая область '_' и forward-объявления
    // (нет инициализатора → нет определения, `&::name` не связался бы) - не экспортируются.
    if (var_node.m_initializer && !var_name.empty() && !m_inCppBlock && m_hiddenNamespaceDepth == 0) {
        m_exports.push_back({std::string(var_node.text()), qualifiedCppName(var_name), buildTrustForwardDecl(var_node)});
    }
}

void CppTranspiler::generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx) {
    auto* left = binary_node.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident) {
        m_ctx.report(binary_node.range(), OptKind::ParseError, "type declaration must have a name on the left");
        return;
    }

    // Имя типа-алиаса: манглинг trust-имени в C++-идентификатор (MyInt → c_MyInt).
    std::string type_name = utils::name_to_cpp(left->text());

    auto* right = binary_node.m_right.get();
    // Enum/Variant-объявление: `Color ::= :Enum(...)` / `(...):Enum`, `Value ::= :Variant(...)` /
    // `(...):Variant` - правая часть DictLiteral с аннотацией типа «Enum»/«Variant».
    if (right && right->kind() == ParserToken::Kind::DictLiteral) {
        const auto& dl = static_cast<const DictLiteralNode&>(*right);
        if (dl.m_type && dl.m_type->text() == "Enum") {
            // Тип обязан быть зарегистрирован семантикой (analyzeEnumDecl). Если его нет -
            // инвариантное нарушение: без диагностики молча ничего не эмитим.
            auto tid = m_ctx.types().findType(left->text());
            if (!tid) {
                m_ctx.report(binary_node.range(), OptKind::ParseError, "enum type '{}' is not registered", left->text());
                return;
            }
            // Отображение объявления (trust-range → cpp): MapperScope охватывает struct +
            // out-of-class определения. Маппинг имени типа - внутри emitEnumStruct (оффсет
            // вычисляется из фактического вывода, а не из магической константы).
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ctx.source(), binary_node.range(), output_idx);
            }
            emitEnumStruct(left->text(), dl, *tid, output_idx, left->range());
            return;
        }
        if (dl.m_type && dl.m_type->text() == "Variant") {
            auto tid = m_ctx.types().findType(left->text());
            if (!tid) {
                m_ctx.report(binary_node.range(), OptKind::ParseError, "variant type '{}' is not registered", left->text());
                return;
            }
            std::unique_ptr<MapperScope> scope;
            if (!binary_node.range().begin.isInvalid()) {
                scope = std::make_unique<MapperScope>(m_ctx.source(), binary_node.range(), output_idx);
            }
            emitVariantStruct(left->text(), dl, *tid, output_idx, left->range());
            return;
        }
    }
    // База алиаса: TypeName (:Int32) или Ident (MyInt - существующий алиас/переменная).
    // Оба разрешаются по имени через resolveCppType.
    if (!right || (right->kind() != ParserToken::Kind::TypeName && right->kind() != ParserToken::Kind::Ident)) {
        m_ctx.report(binary_node.range(), OptKind::ParseError, "unsupported type alias definition");
        return;
    }

    // Resolve base type (canonical chain + cpp name + инклуды через emitTypeName).
    // Правая часть всегда тип (семантика '::=' отклоняет переменную справа), поэтому fallback
    // на переменную не нужен: при невозможности вывода базы - явная ошибка.
    std::string base_cpp;
    if (auto tid = m_ctx.types().findType(right->text())) {
        if (auto n = emitTypeName(*tid, right->text())) {
            base_cpp = std::move(*n);
        }
    }
    if (base_cpp.empty()) {
        m_ctx.report(right->range(), OptKind::ParseError, "unable to generate C++ type '{}'", right->text());
        return;
    }

    MapperScope scope(m_ctx.source(), binary_node.range(), output_idx);
    const std::string using_prefix = "using ";
    std::string cpp_line = using_prefix + type_name + " = " + base_cpp + ";";
    m_ctx.source().output_append(output_idx, cpp_line);

    // Add name mapping for the type name (hover links): name starts right after the "using " prefix.
    // trust-имя в маппинге - исходное (left->text()), cpp-имя - манглированное (type_name).
    mapDeclaredName(output_idx, left->range(), static_cast<uint32_t>(using_prefix.length()), left->text(), type_name);
}

// C++-литерал значения члена по типу: StrChar 'x' → "x", StrWide → L"x", Rational num\den →
// trust::Rational("num\den") (обратная косая экранируется), числовые/прочие как есть.
// Единый источник форматирования значений членов Enum/Variant (устраняет дублирование).
static std::string memberValueCpp(const TypeRegistry& reg, TypeId mt, std::string raw) {
    const TypeId mc = reg.getCanonicalTypeId(mt);
    const TypeId sc = reg.getCanonicalTypeId(reg.getType(type::StrChar));
    const TypeId sw = reg.getCanonicalTypeId(reg.getType(type::StrWide));
    const TypeId rat = reg.getCanonicalTypeId(reg.getType(type::Rational));
    if (mc == sc) {
        return "\"" + raw + "\"";
    }
    if (mc == sw) {
        return "L\"" + raw + "\"";
    }
    if (mc == rat) {
        std::string esc;
        esc.reserve(raw.size() * 2);
        for (const char c : raw) {
            if (c == '\\') {
                esc += "\\\\";
            } else {
                esc += c;
            }
        }
        return "trust::Rational(\"" + esc + "\")";
    }
    return raw;
}

// Эмиссия enum-типа: `struct c_Color : trust::Enum<ValueCpp, N> { using ...; static const члены; };`
// + out-of-class `const c_Color c_Color::c_MEMBER{value, ordinal};`. Generic-логика (значение/
// ординал, конструкторы, операторы сравнения по ординалу, count()) - в рантайм-шаблоне trust::Enum;
// кодогенерация эмитит только данные члена. Члены - static const (out-of-class), т.к.
// static constexpr собственного типа невозможен (неполный тип в точке объявления).
void CppTranspiler::emitEnumStruct(std::string_view enum_trust, const DictLiteralNode& dict, TypeId enum_id, MapperFile output_idx, MapperRange typeNameRange) {
    // Значения членов читаются из EnumTypeData (вычислены семантикой с автоинкрементом);
    // AST-словарь dict нужен только для диапазона диагностики.
    const std::string enum_cpp = utils::name_to_cpp(enum_trust);
    const auto* ed = m_ctx.types().getTypeDataAs<EnumTypeData>(enum_id);
    if (!ed) {
        m_ctx.report(dict.range(), OptKind::ParseError, "enum '{}' has no member data", enum_trust);
        return;
    }

    auto vn = emitTypeName(ed->valueType, std::string(enum_trust) + ".Value");
    if (!vn) {
        m_ctx.report(dict.range(), OptKind::ParseError, "unable to generate C++ type for enum '{}' value type", enum_trust);
        return;
    }
    std::string valueCpp = std::move(*vn);
    const size_t N = ed->members.size();

    // Рантайм-шаблон trust::Enum: заголовок из trust-runtime (механизм «@»-заголовков).
    recordRequiredInclude("@trust/enum.hpp");

    const std::string base = "trust::Enum<" + valueCpp + ", " + std::to_string(N) + ", " + enum_cpp + ">";
    std::string out;
    out += "struct ";
    const uint32_t typeNameOff = static_cast<uint32_t>(out.size());
    out += enum_cpp + " : " + base + " {\n";
    out += "    using " + base + "::Enum;\n";
    // Декларации членов + их name-маппинг (hover): член выводится как `static const c_Level c_NAME;`
    // - имя сразу после "<enum_cpp> ". Источник диапазона члена - ArgNode из dict.m_body (тот же
    // порядок, что и ed->members). Для синтетических/без-range узлов маппинг пропускается.
    {
        size_t midx = 0;
        for (const auto& el : dict.m_body) {
            if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (midx >= ed->members.size()) {
                break;
            }
            const std::string cname = utils::name_to_cpp(ed->members[midx].name);
            out += "    static const " + enum_cpp + " ";
            const uint32_t nameOff = static_cast<uint32_t>(out.size());
            out += cname + ";\n";
            if (m_ctx.source().mappingActive() && !el->range().begin.isInvalid()) {
                mapDeclaredName(output_idx, el->range(), nameOff, ed->members[midx].name, cname);
            }
            ++midx;
        }
    }
    // Таблица имя↔значение (для fromName/fromValue в trust::Enum); порядок = объявление (ordinal).
    // `static inline const` (C++17 inline-переменная): позволяет in-class инициализацию и для
    // НЕ-литеральных типов значений (Rational), инициализируемых в рантайме; fromName/fromValue
    // (не constexpr) читают её во время выполнения. Для литеральных типов тоже корректно.
    out += "    static inline const trust::EnumMember<" + valueCpp + "> kMembers[" + std::to_string(N) + "] = {";
    // Значения членов: скалярные литералы (ast::is_literal_kind) форматируются по типу
    // (memberValueCpp). Составные литералы (массив/словарь/диапазон) НЕ реализованы - «не
    // реализовано» выдаётся ЗДЕСЬ, по факту невозможности сгенерировать C++-литерал значения.
    std::string defs;
    size_t idx = 0;
    for (const auto& el : dict.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        if (idx >= ed->members.size()) {
            break;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        const AstNodeBase* valNode = a.m_value.get();
        if (a.text().empty() && valNode && valNode->kind() == ParserToken::Kind::Ident) {
            valNode = nullptr; // безнарный член - имя в значении, значения нет
        }
        if (valNode && !is_literal_kind(valNode->kind())) {
            m_ctx.report(el->range(), OptKind::ParseError, "значение члена enum '{}' (массив/словарь/диапазон) ещё не реализовано", enum_trust);
            return;
        }
        const std::string val_str = memberValueCpp(m_ctx.types(), ed->valueType, ed->members[idx].value);
        const std::string cname = utils::name_to_cpp(ed->members[idx].name);
        if (idx) {
            out += ", ";
        }
        out += "{" + val_str + ", \"" + ed->members[idx].name + "\"}";
        defs += "const " + enum_cpp + " " + enum_cpp + "::" + cname + "{" + val_str + ", " + std::to_string(idx) + "};\n";
        ++idx;
    }
    out += "};\n";
    out += "};\n";
    out += defs;
    // Маппинг имени типа: оффсет из фактического вывода (сразу после "struct "), а не магическая
    // константа. Для синтетических узлов без исходного range маппинг пропускается.
    if (m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, typeNameOff, enum_trust, enum_cpp);
    }
    m_ctx.source().output_append(output_idx, out);
}

// Эмиссия Variant-типа (гетерогенный): `struct c_Value { using Variant = std::variant<...>;
// static const <T> c_MEMBER; ... }` + out-of-class определения (значения из DictLiteral RHS).
// Каждый член - константа СВОЕГО типа (из VariantTypeData); `Value.RED` → c_Value::c_RED.
void CppTranspiler::emitVariantStruct(std::string_view variant_trust, const DictLiteralNode& dict, TypeId variant_id, MapperFile output_idx,
                                      MapperRange typeNameRange) {
    const std::string vcpp = utils::name_to_cpp(variant_trust);
    const auto* vd = m_ctx.types().getTypeDataAs<VariantTypeData>(variant_id);
    if (!vd) {
        m_ctx.report(dict.range(), OptKind::ParseError, "variant '{}' has no member data", variant_trust);
        return;
    }

    recordRequiredInclude("#include <variant>");

    // C++-имена типов членов (emitTypeName записывает их инклуды).
    std::vector<std::string> memberCpp;
    memberCpp.reserve(vd->members.size());
    for (const auto& m : vd->members) {
        auto n = emitTypeName(m.type, "");
        if (!n) {
            m_ctx.report(dict.range(), OptKind::ParseError, "unable to generate C++ type for variant '{}' member '{}'", variant_trust, m.name);
            return;
        }
        memberCpp.push_back(std::move(*n));
    }

    std::string out;
    out += "struct ";
    const uint32_t typeNameOff = static_cast<uint32_t>(out.size());
    out += vcpp + " {\n";
    out += "    using Variant = std::variant<";
    for (size_t i = 0; i < vd->members.size(); ++i) {
        if (i) {
            out += ", ";
        }
        out += memberCpp[i];
    }
    out += ">;\n";
    // Декларации членов + их name-маппинг (hover): `static const <T> c_NAME;` - имя сразу после
    // "<T> ". Источник диапазона члена - ArgNode из dict.m_body (тот же порядок, что и vd->members).
    {
        size_t midx = 0;
        for (const auto& el : dict.m_body) {
            if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (midx >= vd->members.size()) {
                break;
            }
            const std::string cname = utils::name_to_cpp(vd->members[midx].name);
            out += "    static const " + memberCpp[midx] + " ";
            const uint32_t nameOff = static_cast<uint32_t>(out.size());
            out += cname + ";\n";
            if (m_ctx.source().mappingActive() && !el->range().begin.isInvalid()) {
                mapDeclaredName(output_idx, el->range(), nameOff, vd->members[midx].name, cname);
            }
            ++midx;
        }
    }
    out += "    static constexpr int count() { return " + std::to_string(vd->members.size()) + "; }\n";
    out += "};\n";
    // out-of-class определения: значения из ArgNode (имя в text(), значение в m_value); без
    // значения → ordinal. Форматирование значения по типу члена - единый memberValueCpp.
    // Значение члена Variant - AST-выражение (источник истины - ArgNode.m_value); реестр хранит
    // только разрешённый тип (VariantMemberData).
    size_t i = 0;
    for (const auto& el : dict.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        if (i >= vd->members.size()) {
            break;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        std::string mname = std::string(a.text());
        AstNodePtr valNode = a.m_value;
        // Безнарный член `x` (имя="" и значение-Ident) - имя лежит в значении, значение отбрасываем.
        if (mname.empty() && valNode && valNode->kind() == ParserToken::Kind::Ident) {
            mname = std::string(valNode->text());
            valNode = nullptr;
        }
        // Значение члена Variant: скалярные литералы (ast::is_literal_kind) форматируются по типу.
        // Составные литералы (массив/словарь/диапазон) НЕ реализованы - «не реализовано» выдаётся
        // ЗДЕСЬ, по факту невозможности сгенерировать C++-литерал значения.
        if (valNode && !is_literal_kind(valNode->kind())) {
            m_ctx.report(el->range(), OptKind::ParseError, "значение члена variant '{}' (массив/словарь/диапазон) ещё не реализовано", variant_trust);
            return;
        }
        std::string val_str = std::to_string(i);
        if (valNode) {
            val_str = memberValueCpp(m_ctx.types(), vd->members[i].type, std::string(valNode->text()));
        }
        out += "const " + memberCpp[i] + " " + vcpp + "::" + utils::name_to_cpp(mname) + "{" + val_str + "};\n";
        ++i;
    }
    // Маппинг имени типа: оффсет из фактического вывода (сразу после "struct "), а не магическая
    // константа. Для синтетических узлов без исходного range маппинг пропускается.
    if (m_ctx.source().mappingActive() && !typeNameRange.begin.isInvalid()) {
        mapDeclaredName(output_idx, typeNameRange, typeNameOff, variant_trust, vcpp);
    }
    m_ctx.source().output_append(output_idx, out);
}

void CppTranspiler::emitExpr(const AstNodeBase* node) {
    if (!node) {
        m_ctx.source().output_append(m_out, "{}");
        return;
    }
    // Единая диспетчеризация ПО KIND (ast/kind_visitor.hpp) - как и для statement.
    // Различие statement/expression - глубина m_exprDepth: visit_<Kind> добавляет
    // mapStart/mapStop и ';' только на верхнем уровне (m_exprDepth==0); вложенный
    // вызов emitExpr (m_exprDepth>0) - только текст выражения (без ';' и map).
    ++m_exprDepth;
    dispatchKind(*node, *this);
    --m_exprDepth;
}

void CppTranspiler::emitBinaryOpRaw(const Binary& binary_node) {
    const auto op = binary_node.text();
    // Потоковый вывод бинарного оператора, включая '//'/'//=' (целочисленное деление).
    if (utils::isIntDivOp(op) && !utils::isCompoundAssignOp(op)) {
        m_ctx.source().output_append(m_out, "static_cast<int64_t>(");
        emitExpr(binary_node.m_left.get());
        m_ctx.source().output_append(m_out, ") / static_cast<int64_t>(");
        emitExpr(binary_node.m_right.get());
        m_ctx.source().output_append(m_out, ")");
    } else if (m_exprDepth == 0 && utils::isIntDivOp(op) && utils::isCompoundAssignOp(op)) {
        // //= - только statement (присваивание); как вложенное выражение не используется.
        emitExpr(binary_node.m_left.get());
        m_ctx.source().output_append(m_out, " = static_cast<int64_t>(");
        emitExpr(binary_node.m_left.get());
        m_ctx.source().output_append(m_out, ") / static_cast<int64_t>(");
        emitExpr(binary_node.m_right.get());
        m_ctx.source().output_append(m_out, ")");
    } else {
        // LHS: для простого присвоения "=" - это адрес хранения (any_cast неприменим);
        // иначе (арифметика/составные) - значение, может требовать std::any_cast.
        const bool plainAssign = (binary_node.kind() == ParserToken::Kind::AssignOp && utils::isPlainAssignOp(op));
        if (binary_node.m_left) {
            if (plainAssign) {
                emitExpr(binary_node.m_left.get());
            } else {
                emitBinaryOperand(binary_node.m_left.get(), binary_node.lhsType, binary_node.commonType);
            }
        }
        m_ctx.source().output_append(m_out, " " + std::string(op) + " ");
        if (binary_node.m_right) {
            emitBinaryOperand(binary_node.m_right.get(), binary_node.rhsType, binary_node.commonType);
        }
    }
}

void CppTranspiler::emitBinaryOperand(const AstNodeBase* operand, TypeId operandType, TypeId castType) {
    // std::any-операнд: привести к конкретному типу (commonType). Элемент словаря возвращает
    // trust::TypedValue → типизированный доступ .getAs<Cpp>() (fast-path variant); переменная
    // типа std::any → std::any_cast<Cpp>.
    if (operand && operandType != INVALID_TYPE_ID && castType != INVALID_TYPE_ID) {
        if (isAnyType(operandType, m_ctx.types())) {
            auto cpp = emitTypeName(castType, "");
            if (cpp) {
                const bool dictElement = operand->kind() == ParserToken::Kind::MemberAccess || operand->kind() == ParserToken::Kind::ArrayAccess;
                if (dictElement) {
                    m_ctx.source().output_append(m_out, "(");
                    emitExpr(operand);
                    m_ctx.source().output_append(m_out, ").getAs<" + *cpp + ">()");
                } else {
                    // std::any → универсальный runtime-конвертер anyToInt64. Точный
                    // `std::any_cast<Cpp>` ломался на гетерогенных значениях словаря (bool, int8,
                    // int64...), а конвертер принимает любую числовую/bool-категорию (dict.hpp).
                    // anyToInt64 требует <any> - подключаем по-типу (тип операнда Any).
                    recordUsedType(operandType);
                    m_ctx.source().output_append(m_out, "trust::detail::anyToInt64(");
                    emitExpr(operand);
                    m_ctx.source().output_append(m_out, ")");
                }
                return;
            }
        }
    }
    emitExpr(operand);
}

void CppTranspiler::emitBinaryStmtOrExpr(const Binary& binary_node) {
    // statement-root (m_exprDepth==0, как ребёнок SemicolonStmt): текст без внешних скобок;
    // маппинг и ';' добавляет SemicolonStmt. Вложенное выражение (m_exprDepth>0): '(lhs op rhs)'.
    const bool stmt = (m_exprDepth == 0);
    if (stmt) {
        emitBinaryOpRaw(binary_node);
    } else {
        m_ctx.source().output_append(m_out, "(");
        emitBinaryOpRaw(binary_node);
        m_ctx.source().output_append(m_out, ")");
    }
}

std::optional<TypeId> CppTranspiler::resolveTypeIdByName(std::string_view trustName) const {
    // Если задана разрешённая семантикой таблица символов - используем её TypeId как
    // единый источник с анализом (скоуп-стек к моменту кодогенерации сброшен к глобальному;
    // для builtin-имён в скоупе записи нет → fallback на реестр).
    if (m_resolvedTypes) {
        if (const Symbol* s = m_resolvedTypes->resolve(trustName)) {
            if (s->type != INVALID_TYPE_ID) {
                return s->type;
            }
        }
    }
    return m_ctx.types().findType(trustName);
}

std::optional<std::pair<std::string, std::string_view>> CppTranspiler::resolveCppType(std::string_view trustName) const {
    auto type_id = resolveTypeIdByName(trustName);
    if (!type_id.has_value()) {
        return std::nullopt;
    }
    return resolveCppTypeId(*type_id, trustName);
}

// Разрешение по уже известному TypeId. displayName - trust-имя, сохраняемое для пользовательского
// алиаса; встроенные типы и встроенные алиасы - каноническое C++-имя.
std::optional<std::pair<std::string, std::string_view>> CppTranspiler::resolveCppTypeId(TypeId type_id, std::string_view displayName) const {
    TypeId canonical = m_ctx.types().getCanonicalTypeId(type_id);
    // Кортеж - структурный/компайлтайм-тип без единого runtime-представления: всегда конкретный
    // std::tuple, тип которого выводится из инициализатора (std::make_tuple). Голого C++-имени
    // у типа `:Tuple` нет → объявление переменной эмитится как `auto`. Плоский `:Tuple` и
    // структурный кортеж (TupleTypeData) - оба.
    if (canonical == m_ctx.types().getType(type_category::Tuple) || m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kTuple)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Параметризованный Range<Elem> (структурный, TypeDataKind::kRange): конкретный C++-шаблон
    // `trust::Range<ElemCpp>`, где ElemCpp - элементный тип из RangeTypeData (рекурсивно через
    // emitTypeName). В отличие от абстрактного `:Range` (ветка ниже → auto) здесь имя конкретно.
    if (m_ctx.types().isRangeType(canonical)) {
        const TypeId elem = m_ctx.types().rangeElementType(canonical);
        std::string elemCpp;
        if (elem != INVALID_TYPE_ID) {
            if (auto en = resolveCppTypeId(elem, "Range.Element")) { // рекурсивно (const-метод)
                elemCpp = std::move(en->first);
            }
        }
        if (elemCpp.empty()) {
            elemCpp = "std::any";
        }
        recordUsedType(canonical); // включит @trust/range.hpp + dict/rational (getPreprocIncludes)
        return std::make_pair("trust::Range<" + elemCpp + ">", m_ctx.types().getPreprocInclude(canonical));
    }
    // Диапазон `:Range` - абстрактный универсальный тип (как :Dict), конкретное C++-представление
    // `trust::Range<Elem>` (шаблон по элементному типу) выводится из инициализатора-литерала
    // `..` в visit_RangeExpr. Голого C++-имени у `:Range` нет → объявление переменной эмитится
    // как `auto`, инициализатор задаёт конкретный `trust::Range<Elem>`. (Модель: кортеж `:Tuple`.)
    if (canonical == m_ctx.types().getType(type_category::Range)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Параметризованный Array<Elem> (структурный, ArrayTypeData): конкретный C++-шаблон
    // `std::vector<ElemCpp>` (mutable) или `std::array<ElemCpp,N>` (константная/фиксированная).
    // ElemCpp - элементный тип из ArrayTypeData (рекурсивно через resolveCppTypeId).
    if (m_ctx.types().isArrayType(canonical)) {
        const TypeId elem = m_ctx.types().arrayElementType(canonical);
        std::string elemCpp;
        if (elem != INVALID_TYPE_ID) {
            if (auto en = resolveCppTypeId(elem, "Array.Element")) { // рекурсивно (const-метод)
                elemCpp = std::move(en->first);
            }
        }
        if (elemCpp.empty()) {
            elemCpp = "std::any";
        }
        const bool cst = typeIsConst(type_id); // kConstFlag-бит в TypeId (`:Array^`), как у любых типов
        if (cst) {
            uint64_t n = 0;
            const auto& dims = m_ctx.types().arrayDimensions(canonical);
            if (!dims.empty()) {
                n = dims.front();
            }
            recordRequiredInclude("#include <array>");
            return std::make_pair("std::array<" + elemCpp + ", " + std::to_string(n) + ">", std::string_view{});
        }
        recordRequiredInclude("#include <vector>");
        return std::make_pair("std::vector<" + elemCpp + ">", std::string_view{});
    }
    // Массив `:Array` - абстрактный универсальный тип (как :Dict/:Range): конкретное
    // C++-представление `std::vector<Elem>` выводится из инициализатора-литерала (visit_ArrayInit).
    // Голого C++-имени нет → объявление переменной эмитится как `auto`.
    if (canonical == m_ctx.types().getType(type::Array)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Константность (kConstFlag) - ортогональный квалификатор, учитывается здесь: префикс `const `
    // добавляется к базовому имени (каноника снимает бит, поэтому getCppTypeName(canonical) его
    // не видит). Для пользовательских алиасов const добавляется к trust-имени.
    const bool isConst = typeIsConst(type_id);

    // Спецправило: узкая строка как указатель на константные данные
    // (`fmt: @[reftype(ptr)]@ StrChar^`) - это C-строка `const char*` (а НЕ `const std::string*`).
    // Такой тип совместим с C-функциями, принимающими форматную строку (printf и др.); при
    // вызове StrChar-аргумент конвертируется в .c_str() (см. visit_CallExpr).
    if (isConst && getRefType(getKindFromId(canonical)) == RefType::kPtr) {
        TypeKind baseKind = withRefType(getKindFromId(canonical), RefType::kValue);
        TypeId baseId = (static_cast<uint64_t>(baseKind) << 32) | (static_cast<uint32_t>(getIndexFromId(canonical)) & 0xFFFFFFFFu);
        if (m_ctx.types().getCanonicalTypeId(baseId) == m_ctx.types().getType(type::StrChar)) {
            return std::make_pair(std::string("const char*"), m_ctx.types().getPreprocInclude(canonical));
        }
    }

    // Enum-тип (Group::kEnums, EnumTypeData): C++-имя - манглинг trust-имени (самодостаточная
    // struct, объявленная visit_EnumDecl); у типа нет единого preproc-include.
    if (m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kEnum)) {
        std::string name = utils::name_to_cpp(displayName);
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(std::move(name), std::string_view{});
    }
    // Variant-тип (Group::kVariants, VariantTypeData): C++-имя - манглинг trust-имени (struct c_Value).
    if (m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kVariant)) {
        std::string name = utils::name_to_cpp(displayName);
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(std::move(name), std::string_view{});
    }

    auto cpp_name = m_ctx.types().getCppTypeName(canonical);
    if (!cpp_name) {
        return std::nullopt;
    }
    std::string_view include = m_ctx.types().getPreprocInclude(canonical);

    // МЕХАНИЗМ №1 - ПО ТИПУ: отмечаем тип как использованный (не файлы!). Инклуды из собранных
    // типов формируются ПОСЛЕ обхода AST (collectTypeIncludes), а не в момент резолва.
    recordUsedType(canonical);

    // Пользовательский алиас (зарегистрирован семантикой) сохраняет своё trust-имя в коде;
    // встроенные типы и встроенные алиасы (Integer, String, Char...) маппятся на каноническое
    // C++-имя (int64_t, std::string...). Include всегда берётся у канонического (базового) типа.
    // Признак пользовательского типа - явный (isUserDefinedType), а не по sourceRange.
    if (m_ctx.types().isUserDefinedType(type_id)) {
        // Пользовательский алиас сохраняет своё trust-имя в C++-коде, но в виде корректного
        // C++-идентификатора (манглинг: MyInt → c_MyInt), чтобы совпадать с объявлением `using c_MyInt = ...`.
        std::string name = utils::name_to_cpp(displayName);
        if (isConst) {
            name = "const " + name;
        }
        return std::make_pair(std::move(name), include);
    }
    if (isConst) {
        return std::make_pair(std::string("const ") + *cpp_name, include);
    }
    return std::make_pair(std::move(*cpp_name), include);
}

void CppTranspiler::mapDeclaredName(MapperFile output_idx, MapperRange trustRange, uint32_t prefixLen, std::string_view name, std::string_view cppName) {
    // Подавленный маппинг (forward-decl на сайте импорта): mapStart не пушил стек, маппить нечего.
    if (m_ctx.source().mappingSuppressed()) {
        return;
    }
    // Оффсет всегда от mapStackTop().outputBegin (инклуды output_prepend сдвигают начало
    // вывода - нельзя предполагать, что вывод начинается с offset 1). См. memory (transpiler).
    const auto stackEntry = m_ctx.source().mapStackTop();
    const uint32_t nameOffset = stackEntry.outputBegin.offset() + prefixLen;
    MapperLocation nameBegin = m_ctx.source().makeLoc(output_idx, nameOffset);
    MapperLocation nameEnd = m_ctx.source().makeLoc(output_idx, nameOffset + static_cast<uint32_t>(cppName.length()));
    MapperRange cppNameRange(nameBegin, nameEnd);
    m_ctx.source().addNameMapping(trustRange, cppNameRange, name, cppName);
}

void CppTranspiler::generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx) {
    // Нативный импорт `<name>(...) := %native...;` - алиас: C++-функция НЕ эмитится.
    // Регистрируем trust-имя → нативное C++-имя; вызовы name(...) будут переписаны в native(...).
    if (func_node.m_isNativeImport) {
        m_nativeImports[std::string(func_node.text())] = func_node.m_nativeName;
        return;
    }
    m_ctx.source().mapStart(func_node.range(), output_idx);

    // Флаг линковки нативной библиотеки из @[link("имя")].
    collectLinkLib(func_node);

    // Точка входа модуля: DSL-макрос `@main` раскрывается в `<имя_модуля>__main__`. Pipeline
    // генерирует `_main.cppt` с `extern int <имя_модуля>__main__(); int main(){ return …; }`,
    // поэтому entry-функция эмитится с СЫРЫМ именем (без манглинга `c_`) и типом возврата `int`.
    const std::string trust_name = std::string(func_node.text());
    const bool isEntry = func_node.m_body && !m_inCppBlock && m_hiddenNamespaceDepth == 0 && trust_name.ends_with("__main__");
    // Function name: манглинг trust-имени в C++-идентификатор (срез '%' у нативных функций).
    // Entry - без манглинга, иначе не совпадёт с `extern int <имя>__main__()` в _main.cppt.
    std::string name = isEntry ? std::string(utils::strip_native_prefix(trust_name)) : utils::name_to_cpp(trust_name);

    // Return type (инклуды типа записываются через emitTypeName). None/Void → "void"; нет аннотации → "void".
    // Явный, но нерезолвящийся тип → emitTypeNameForNode выводит диагностику, прерываем функцию.
    std::string ret_type = "void";
    if (func_node.m_type && func_node.m_type->kind() == ParserToken::Kind::TypeName) {
        const std::string_view rt = func_node.m_type->text();
        if (rt == "Void" || rt == "None") {
            ret_type = "void";
        } else {
            ret_type = emitTypeNameForNode(func_node.m_type.get());
            if (ret_type.empty()) {
                return; // emitTypeNameForNode уже вывел диагностику
            }
        }
    }
    // Entry-функция без явного типа возврата должна быть `int` (иначе не слинкуется
    // `extern int <имя>__main__()` из _main.cppt).
    if (isEntry && func_node.m_type == nullptr) {
        ret_type = "int";
    }

    // Квалификаторы функции из атрибутов. Лидирующие (до типа возврата): FuncConst ->
    // __attribute__((const)), FuncPure -> __attribute__((pure)), FuncConstexpr -> constexpr.
    // Завершающий (после ')'): NoExcept -> noexcept. ReadOnly у функций не обрабатывается.
    std::string lead;
    if (func_node.has_attr(m_ctx.attrs(), attr::FuncConst)) {
        lead += "__attribute__((const)) ";
    }
    if (func_node.has_attr(m_ctx.attrs(), attr::FuncPure)) {
        lead += "__attribute__((pure)) ";
    }
    if (func_node.has_attr(m_ctx.attrs(), attr::FuncConstexpr)) {
        lead += "constexpr ";
    }
    std::string trail;
    if (func_node.has_attr(m_ctx.attrs(), attr::NoExcept)) {
        trail += " noexcept";
    }

    // Нативная декларация (`%...`): правило линковки - без '::' линкуется как C-символ
    // (extern "C", напр. libc/libm `sqrt`, `open`, `abs`); с '::' - C++-линковка (`std::...`).
    // Импорт-алиасы (`name(...) := %sym...;`) вернулись выше - сюда попадают только
    // forward-decl и определения.
    // extern "C" добавляется ТОЛЬКО forward-декларациям (нет тела): это настоящие C-символы.
    // Определения (с телом) - пользовательские C++-функции; extern "C" для них неверен
    // (напр. auto-возврат кортежа несовместим с C-линковкой) и не нужен - при наличии
    // forward-объявления определение наследует C-линковку по правилу [dcl.link].
    if (trust_name.starts_with('%') && !func_node.m_body.has_value() && utils::strip_native_prefix(trust_name).find("::") == std::string::npos) {
        lead = "extern \"C\" " + lead;
    }

    // Parameters
    std::string params_str;
    std::vector<std::pair<const ArgNode*, uint32_t>> param_name_positions; // (node, name offset within signature)
    if (func_node.m_params) {
        const uint32_t sig_prefix =
            static_cast<uint32_t>(lead.length()) + static_cast<uint32_t>(ret_type.length()) + 1 + static_cast<uint32_t>(name.length()) + 1;
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                params_str += ", ";
            }
            auto* param_node = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!param_node || param_node->kind() != ParserToken::Kind::ArgNode) {
                params_str += "std::any"; // дефектный узел (семантика отсекает) - тип Any
                continue;
            }
            if (param_node->text() == "...") {
                // Вариативность: trust `...` - свойство компилятора (произвольное число
                // аргументов); в C++ это чистая variadic-метка `...` (без имени и типа).
                // C++ требует `...` последним параметром, что и гарантируется грамматикой.
                params_str += "...";
                continue;
            }
            // Param type (инклуды записываются через emitTypeName); None/Void → "void";
            // без аннотации типа → :Any (std::any). Явный, но нерезолвящийся тип → ошибка.
            std::string param_type;
            if (param_node->m_type && param_node->m_type->kind() == ParserToken::Kind::TypeName) {
                const std::string_view pt = param_node->m_type->text();
                if (pt == "Void" || pt == "None") {
                    param_type = "void";
                } else {
                    param_type = emitTypeNameForNode(param_node->m_type.get());
                    if (param_type.empty()) {
                        return; // emitTypeNameForNode уже вывел диагностику - функция невалидна
                    }
                }
            } else if (auto aid = m_ctx.types().findType(type_generic::Any)) {
                // Нетипизированный параметр - тип :Any (std::any); инклуд записывает emitTypeName.
                auto anyName = emitTypeName(*aid, type_generic::Any);
                EXPECT(anyName.has_value() && "untyped parameter: Any type must have a C++ name");
                param_type = std::move(*anyName);
            } else {
                EXPECT(false && "untyped parameter: Any type must be registered");
            }
            // Param name: манглинг trust-имени параметра в C++-идентификатор (a → c_a).
            // Безымянный параметр (только тип) эмитится без имени - C++ это допускает
            // (void f(int32_t)), и к нему нельзя обратиться из тела.
            std::string param_name = utils::name_to_cpp(param_node->text());
            if (param_name.empty()) {
                params_str += param_type;
            } else {
                // Name offset within signature: params_str already holds everything emitted so far
                // (separators, previous params), so the type size is taken directly.
                uint32_t name_pos = sig_prefix + static_cast<uint32_t>(params_str.size()) + static_cast<uint32_t>(param_type.length()) + 1;
                params_str += param_type + " " + param_name;
                param_name_positions.emplace_back(param_node, name_pos);
            }
        }
    }

    // Emit function signature
    std::string sig = std::format("{}{} {}({}){}", lead, ret_type, name, params_str, trail);
    m_ctx.source().output_append(output_idx, sig);

    // Add name mappings (function name + parameter names) for hover links.
    // Имя функции выводится сразу после "<lead>ret_type " (offset = lead.length()+ret_type.length()+1).
    if (!name.empty()) {
        MapperLocation trustFnBegin = m_ctx.source().makeLoc(func_node.range().begin.fileIdx(), func_node.range().begin.offset());
        MapperLocation trustFnEnd =
            m_ctx.source().makeLoc(trustFnBegin.fileIdx(), trustFnBegin.offset() + static_cast<uint32_t>(utils::strip_native_prefix(func_node.text()).size()));
        MapperRange trustFnNameRange(trustFnBegin, trustFnEnd);
        mapDeclaredName(output_idx, trustFnNameRange, static_cast<uint32_t>(lead.length()) + static_cast<uint32_t>(ret_type.length()) + 1, func_node.text(),
                        name);
    }

    // Parameter names: name_pos - оффсет имени от начала сигнатуры (= начала mapStart).
    for (const auto& [param_node, name_pos] : param_name_positions) {
        std::string raw_param = std::string(param_node->text());
        if (raw_param.empty()) {
            continue; // placeholder argN is not backed by a real source name
        }
        std::string cpp_param = utils::name_to_cpp(raw_param);
        mapDeclaredName(output_idx, param_node->range(), name_pos, raw_param, cpp_param);
    }

    // Сигнатура (src [имя, оператор]) смапплена - закрываем её отдельно от тела.
    m_ctx.source().mapStop(func_node.range());

    // Body or forward declaration
    if (func_node.m_body && !m_forwardDeclOnly) {
        // Зеркалируем раскладку исходника: '{' и '}' размещаются по строкам блока,
        // переносы между '{' и первым оператором / последним оператором и '}' зависят
        // от того, на одной ли они строке исходника.
        MapperRange blockRange = func_node.blockRange();

        // Тело функции: convertSeq уже развернул SEQUENCE-контейнер тела, поэтому
        // func_node.m_body - плоский список операторов; пользовательские блоки остаются
        // ScopeBlock-узлами и оборачиваются visit_ScopeBlock. Отдельного сплющивания не нужно.
        // Entry-функция без явного `return` в конце получает `return 0;` перед '}' (иначе
        // «control reaches end of non-void function»).
        std::string beforeClose;
        if (isEntry && !bodyEndsWithReturn(*func_node.m_body)) {
            beforeClose = "return 0;";
        }
        m_scopeStack.push_back({indentLevel()});
        emitBlockBodyToFile(*func_node.m_body, blockRange, output_idx, /*mapBlock=*/true, beforeClose);
        m_scopeStack.pop_back();
    } else {
        // Forward declaration
        m_ctx.source().output_append(output_idx, ";");
    }

    // Экспортируются ОПРЕДЕЛЕНИЯ функций на верхнем уровне модуля в НЕ анонимной области имён
    // (квалифицированно для 'ns::'); из '_' и локальных, а также forward-объявления
    // (нет тела → нет определения, `&::name` не связался бы) - не экспортируются.
    if (func_node.m_body && !name.empty() && !m_inCppBlock && m_hiddenNamespaceDepth == 0) {
        m_exports.push_back({std::string(func_node.text()), qualifiedCppName(name), buildTrustForwardDecl(func_node)});
    }
}

void CppTranspiler::emitBlockBodyToFile(const std::vector<AstNodePtr>& body, MapperRange blockRange, MapperFile output_idx, bool mapBlock,
                                        const std::string& beforeCloseLabel, const std::string& afterOpen) {
    // mapBlock=false: не оборачиваем тело собственным маппингом (do-while - begin тела
    // совпадает с begin statement'а, иначе коллизия ключа в mapStop).
    const bool hasBlockRange = !blockRange.isInvalid() && mapBlock;

    // Тело маппится отдельно из диапазона блока, чтобы скобки { } отображались в C++.
    std::optional<MapperScope> scope;
    if (hasBlockRange) {
        scope.emplace(m_ctx.source(), blockRange, output_idx);
    }

    // Нормальное многострочное форматирование: '{' в конце строки, операторы - с отступом.
    // Входим во вложенный скоуп: отступ +1 (функция/блок - только уровень отступа).
    m_ctx.source().output_append(output_idx, " {");
    m_scopeStack.push_back({indentLevel() + 1});
    m_ctx.source().output_append(output_idx, "\n");
    // Внутри C++ compound statement: пользовательские блоки эмитятся как `{ }`, не как
    // `namespace { }` (namespace внутри compound statement в C++ недопустим).
    const bool savedCppBlock = m_inCppBlock;
    m_inCppBlock = true;
    // afterOpen - текст сразу после '{' (например, установка флага while-else).
    if (!afterOpen.empty()) {
        m_ctx.source().output_append(output_idx, indentPrefix());
        m_ctx.source().output_append(output_idx, afterOpen);
        m_ctx.source().output_append(output_idx, "\n");
    }

    for (const auto& child : body) {
        if (!child) {
            continue;
        }
        if (isSuppressedDoc(child->kind())) {
            continue;
        }
        const size_t before = m_ctx.source().output_body(output_idx).size();
        // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход;
        // для остальных операторов - отступ текущего блока.
        if (!is_block_kind(child->kind())) {
            m_ctx.source().output_append(output_idx, indentPrefix());
        }
        generateNodeToFile(*child, output_idx);
        // Не оставлять пустую строку, если узел ничего не эмитил (напр. подавленный doc-bundle),
        // и не дублировать перевод строки, если блок уже закончился '\n' (emitSequenceBody).
        if (m_ctx.source().output_body(output_idx).size() != before) {
            const std::string_view body = m_ctx.source().output_body(output_idx);
            if (body.empty() || body.back() != '\n') {
                m_ctx.source().output_append(output_idx, "\n");
            }
        }
    }
    // continue-метка do-while / именованного блока вставляется перед закрывающей '}'.
    if (!beforeCloseLabel.empty()) {
        m_ctx.source().output_append(output_idx, indentPrefix());
        m_ctx.source().output_append(output_idx, beforeCloseLabel);
        m_ctx.source().output_append(output_idx, "\n");
    }
    m_inCppBlock = savedCppBlock;
    m_scopeStack.pop_back();
    m_ctx.source().output_append(output_idx, indentPrefix());
    m_ctx.source().output_append(output_idx, "}");
}

void CppTranspiler::emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock, const std::string& beforeCloseLabel,
                                 const std::string& afterOpen) {
    std::vector<AstNodePtr> stmts;
    MapperRange blockRange;
    collectBodyStatements(body, stmts, blockRange);
    emitBlockBodyToFile(stmts, blockRange, output_idx, mapBlock, beforeCloseLabel, afterOpen);
}

void CppTranspiler::generateIfToFile(const IfStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ctx.source(), node.range(), output_idx);

    // if (cond) { then }
    m_ctx.source().output_append(output_idx, "if (");
    emitExpr(node.m_cond.get());
    m_ctx.source().output_append(output_idx, ")");
    emitBodyNode(node.m_body, output_idx);

    // else if (cond2) { body2 } ...
    for (const auto& [cond, body] : node.m_elseifs) {
        m_ctx.source().output_append(output_idx, " else if (");
        emitExpr(cond.get());
        m_ctx.source().output_append(output_idx, ")");
        emitBodyNode(body, output_idx);
    }

    // else { ... }
    if (node.m_else) {
        m_ctx.source().output_append(output_idx, " else");
        emitBodyNode(node.m_else, output_idx);
    }
}

void CppTranspiler::generateWhileToFile(const WhileStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ctx.source(), node.range(), output_idx);

    // while-else: в C++ нет 'while...else'. Эмулируем флагом «вошёл ли цикл хотя бы раз»:
    //   bool _weN = false; while (cond) { _weN = true; body; } if (!_weN) { else; }
    std::string flag;
    if (node.m_else) {
        flag = "_we" + std::to_string(++m_whileElseCounter);
        m_ctx.source().output_append(output_idx, "bool " + flag + " = false;");
        m_ctx.source().output_append(output_idx, "\n");
        m_ctx.source().output_append(output_idx, indentPrefix());
    }

    m_ctx.source().output_append(output_idx, "while (");
    emitExpr(node.m_cond.get());
    m_ctx.source().output_append(output_idx, ")");
    emitBodyNode(node.m_body, output_idx, /*mapBlock=*/true, /*beforeClose=*/"", /*afterOpen=*/(flag.empty() ? "" : flag + " = true;"));

    if (node.m_else) {
        m_ctx.source().output_append(output_idx, "\n");
        m_ctx.source().output_append(output_idx, indentPrefix());
        m_ctx.source().output_append(output_idx, "if (!" + flag + ")");
        emitBodyNode(node.m_else, output_idx);
    }
}

void CppTranspiler::generateDoWhileToFile(const DoWhileStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ctx.source(), node.range(), output_idx);

    m_ctx.source().output_append(output_idx, "do");
    // Тело не маппится отдельно: begin тела совпадает с begin statement'а (do-while начинается
    // с '{'), иначе коллизия trustKey в mapStop. Всё покрывает единый range statement'а.
    // continue-метка именованного блока вставляется анализатором в конец тела (LabelStmt).
    emitBodyNode(node.m_body, output_idx, /*mapBlock=*/false);
    m_ctx.source().output_append(output_idx, " while (");
    emitExpr(node.m_cond.get());
    m_ctx.source().output_append(output_idx, ");");
}

void CppTranspiler::generateMatchToFile(const MatchStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ctx.source(), node.range(), output_idx);

    const uint32_t id = ++m_matchCounter;
    const std::string tmp = "_match" + std::to_string(id);

    // Предварительное вычисление значения во временную переменную (на отдельной строке).
    m_ctx.source().output_append(output_idx, "auto " + tmp + " = ");
    emitExpr(node.m_value.get());
    m_ctx.source().output_append(output_idx, ";");
    m_ctx.source().output_append(output_idx, "\n");
    m_ctx.source().output_append(output_idx, indentPrefix());

    // Последовательное сравнение с шаблонами: if / else-if / else.
    bool first = true;
    for (const auto& c : node.m_cases) {
        m_ctx.source().output_append(output_idx, first ? "if (" : " else if (");
        first = false;
        // Последовательное сравнение с шаблонами: (tmp == p1) || (tmp == p2) ...
        for (size_t j = 0; j < c.patterns.size(); ++j) {
            if (j) {
                m_ctx.source().output_append(output_idx, " || ");
            }
            m_ctx.source().output_append(output_idx, tmp + " == ");
            emitExpr(c.patterns[j].get());
        }
        m_ctx.source().output_append(output_idx, ")");
        emitBodyNode(c.body, output_idx);
    }
    if (node.m_default) {
        m_ctx.source().output_append(output_idx, " else");
        emitBodyNode(node.m_default, output_idx);
    }
}

// -- KindVisitor: visit_<Kind> (statement-контекст, потоковый вывод в m_out) --

// Блоки-обёртки: обход тела; Attr - не обход.
void CppTranspiler::visit_sequence(const Sequence& n) {
    emitSequenceBody(n, m_out);
}
// -- Блоки и области видимости --

std::string CppTranspiler::qualifiedCppName(std::string_view name) const {
    if (m_namespaceStack.empty()) {
        return std::string(name);
    }
    std::string q;
    for (const auto& ns : m_namespaceStack) {
        q += ns;
        q += "::";
    }
    q += name;
    return q;
}

std::string CppTranspiler::namespaceCppName(std::string_view text) {
    std::string_view s = text;
    if (s.rfind("::", 0) == 0) {
        s.remove_prefix(2);
    }
    if (s.size() >= 2 && s.substr(s.size() - 2) == "::") {
        s.remove_suffix(2);
    }
    return std::string(s);
}

void CppTranspiler::emitCompoundScope(const ScopeBlock& n) {
    m_ctx.source().output_append(m_out, indentPrefix());
    m_ctx.source().output_append(m_out, "{");
    m_scopeStack.push_back({indentLevel() + 1});
    m_ctx.source().output_append(m_out, "\n");
    emitSequenceBody(n, m_out);
    m_scopeStack.pop_back();
    m_ctx.source().output_append(m_out, indentPrefix());
    m_ctx.source().output_append(m_out, "}");
}

void CppTranspiler::emitNamespaceScope(const ScopeBlock& n, const std::string& name) {
    m_ctx.source().output_append(m_out, indentPrefix());
    m_ctx.source().output_append(m_out, "namespace");
    if (!name.empty()) {
        m_ctx.source().output_append(m_out, " " + name);
    }
    m_ctx.source().output_append(m_out, " {");
    m_scopeStack.push_back({indentLevel() + 1});
    m_ctx.source().output_append(m_out, "\n");
    emitSequenceBody(n, m_out);
    m_scopeStack.pop_back();
    m_ctx.source().output_append(m_out, indentPrefix());
    m_ctx.source().output_append(m_out, "}");
}

void CppTranspiler::visit_ScopeBlock(const ScopeBlock& n) {
    const std::string_view text = n.text();
    // Безымянный блок кода: последовательность операторов как одно выражение.
    const bool isCodeBlock = text.empty() || text == "{";
    // Именованный блок-метка: одно имя (идентификатор) без '::' - label { ... }.
    const bool isLabel = !isCodeBlock && !n.is_hidden() && text.find("::") == std::string_view::npos;
    // Глобальная область имён: ':: { ... }'.
    const bool isGlobalNs = text == "::";

    // Блок кода / именованная метка разрешены ТОЛЬКО внутри функции/класса.
    if (isCodeBlock || isLabel) {
        if (!m_inCppBlock) {
            if (isLabel) {
                m_ctx.report(n.range(), OptKind::ParseError, "named block '{}' is only allowed inside a function", std::string(n.name()));
            } else {
                m_ctx.report(n.range(), OptKind::ParseError, "unnamed code block is only allowed inside a function");
            }
            return;
        }
        emitCompoundScope(n);
        return;
    }

    // Область имён (`ns::`, `_`) разрешена только на верхнем уровне модуля.
    if (!isGlobalNs && m_inCppBlock) {
        m_ctx.report(n.range(), OptKind::ParseError, "namespace block is not allowed inside a function");
        return;
    }

    // Глобальная область '::' - содержимое без namespace-обёртки.
    if (isGlobalNs) {
        emitSequenceBody(n, m_out);
        return;
    }

    // Именованная 'ns::' либо скрытая '_' область имён.
    const bool hidden = n.is_hidden();
    const std::string nsName = hidden ? "" : utils::name_to_cpp(namespaceCppName(text));
    if (hidden) {
        ++m_hiddenNamespaceDepth;
    } else {
        m_namespaceStack.push_back(nsName);
    }
    emitNamespaceScope(n, nsName);
    if (hidden) {
        --m_hiddenNamespaceDepth;
    } else {
        m_namespaceStack.pop_back();
    }
}
void CppTranspiler::visit_ModuleDecl(const ModuleNode& n) {
    if (n.isImport()) {
        // Сайт импорта `\module(mod, masks)`: вместо полного тела - только forward-decl
        // экспортируемого интерфейса (прототипы функций / extern переменных / алиасы типов).
        // Определения живут в отдельном .cppt модуля и связываются линковщиком.
        emitModuleImportDecls(n);
        return;
    }
    // Корневой модуль (главный файл) - полное тело.
    emitSequenceBody(n, m_out);
}

// -- Эмиссия forward-decl экспортов на сайте импорта модуля --

void CppTranspiler::emitModuleImportDecls(const ModuleNode& n) {
    // Множество экспортируемых термов (из отфильтрованного интерфейса сайта импорта).
    // Сопоставление по указателям: m_exports содержат ТЕ ЖЕ TermPtr, что и узел-декларация
    // в m_body, поэтому forward-decl эмитится ровно для отобранных экспортов.
    std::set<const Term*> exportTerms;
    for (const auto& t : n.exports()) {
        if (t) {
            exportTerms.insert(t.get());
        }
    }
    if (exportTerms.empty()) {
        return; // ничего не импортируется (или модуль без экспортов)
    }
    // В forward-режиме объявления подавляют определения (extern/прототип).
    // ВЕСЬ импорт мапится ОДНИМ фрагментом - на месте оператора загрузки модуля (`\module(...)`,
    // n.range()), как при раскрытии макроса. Внутренние forward-decl эмитятся внутри этого одного
    // маппинга; их собственные per-node маппинги подавляются, чтобы не заявлять диапазоны объявлений
    // модуля - эти диапазоны мапит отдельный `.cppt` модуля (иначе коллизия trustKey).
    MapperScope importScope(m_ctx.source(), n.range(), m_out);
    m_forwardDeclOnly = true;
    m_ctx.source().suppressMapping();
    emitImportScope(n.m_body, exportTerms, m_out);
    m_ctx.source().resumeMapping();
    m_forwardDeclOnly = false;
}

void CppTranspiler::emitImportScope(const std::vector<AstNodePtr>& body, const std::set<const Term*>& terms, MapperFile out) {
    for (const auto& node : body) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ScopeBlock) {
            const auto& sb = static_cast<const ScopeBlock&>(*node);
            // Анонимная область `_` и безымянный кодовый блок - не экспортируются.
            if (sb.is_hidden() || sb.is_anonymous()) {
                continue;
            }
            const std::string_view text = sb.text();
            // Глобальная область `::` - содержимое без обёртки.
            if (text == "::") {
                emitImportScope(sb.m_body, terms, out);
                continue;
            }
            // Именованная область `ns::` - оборачиваем в `namespace ns { ... }`.
            const std::string nsName = utils::name_to_cpp(namespaceCppName(text));
            m_namespaceStack.push_back(nsName);
            m_scopeStack.push_back({indentLevel() + 1});
            m_ctx.source().output_append(out, indentPrefix() + "namespace " + nsName + " {\n");
            emitImportScope(sb.m_body, terms, out);
            m_scopeStack.pop_back();
            m_ctx.source().output_append(out, indentPrefix() + "}\n");
            m_namespaceStack.pop_back();
            continue;
        }

        // Forward-decl только для отобранных экспортов (сопоставление по терму-источнику).
        const TermPtr& srcTerm = node->term();
        if (!srcTerm || terms.find(srcTerm.get()) == terms.end()) {
            continue;
        }
        m_ctx.source().output_append(out, indentPrefix());
        switch (node->kind()) {
        case ParserToken::Kind::VarDecl:
            generateVarDeclToFile(static_cast<const VarDecl&>(*node), out);
            break;
        case ParserToken::Kind::FuncDecl:
            generateFuncDeclToFile(static_cast<const FuncDecl&>(*node), out);
            break;
        case ParserToken::Kind::TypeDecl:
            generateTypeDeclToFile(static_cast<const Binary&>(*node), out);
            break;
        default:
            break; // прочие экспорт-формы пока не эмитятся
        }
        m_ctx.source().output_append(out, "\n");
    }
}

// -- Реконструкция Trust-синтаксиса предварительного объявления экспортируемого узла --

std::string CppTranspiler::buildTrustForwardDecl(const AstNodeBase& node) const {
    switch (node.kind()) {
    case ParserToken::Kind::VarDecl: {
        const auto& v = static_cast<const VarDecl&>(node);
        std::string s(v.text());
        if (v.m_type) {
            s += ":";
            s += std::string(v.m_type->text()); // e.g. "Int32" → ":Int32"
        }
        s += " := ...;";
        return s;
    }
    case ParserToken::Kind::FuncDecl: {
        const auto& f = static_cast<const FuncDecl&>(node);
        std::string s(f.text()); // e.g. "%func"
        s += "(";
        if (f.m_params) {
            bool first = true;
            for (const auto& p : *f.m_params) {
                if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                    continue;
                }
                const auto& pd = static_cast<const ArgNode&>(*p);
                if (!first) {
                    s += ", ";
                }
                first = false;
                s += std::string(pd.text());
                if (pd.m_type) {
                    s += ":";
                    s += std::string(pd.m_type->text());
                }
            }
        }
        s += ")";
        if (f.m_type) {
            s += ":";
            s += std::string(f.m_type->text());
        }
        s += " := ...;";
        return s;
    }
    case ParserToken::Kind::TypeDecl: {
        const auto& b = static_cast<const Binary&>(node);
        std::string s = (b.m_left) ? std::string(b.m_left->text()) : std::string(node.text());
        s += " ::= ...;";
        return s;
    }
    default:
        return std::string(node.text()) + " := ...;";
    }
}
void CppTranspiler::visit_Attr(const Sequence&) {
}

// Объявления.
void CppTranspiler::visit_VarDecl(const VarDecl& n) {
    generateVarDeclToFile(n, m_out);
}
void CppTranspiler::visit_FuncDecl(const FuncDecl& n) {
    generateFuncDeclToFile(n, m_out);
}
void CppTranspiler::visit_DestructureDecl(const DestructureDecl& n) {
    // `t1, ..., tN := [... ]source;` - деструктуризация. Spread (`... source`) - коллекция (Dict,
    // pop_front + «остаток»); без `...` - кортеж (std::get). `_` - skip (потребляется, не связывается).
    if (n.m_targets.empty() || !n.m_source) {
        // Семантика всегда заполняет цели и источник; пустой узел - инвариантное нарушение.
        // Вместо тихого no-op (AGENTS rule 5 «no silent fallback») - явная диагностика.
        m_ctx.report(n.range(), OptKind::ParseError, "destructuring requires at least one target and a source expression");
        return;
    }
    // DestructureDecl - НЕ statement-выражение (не оборачивается SemicolonStmt, который добавляет
    // mapStart/mapStop), поэтому собственный маппинг здесь обязателен: иначе оператор раскрытия
    // словаря/кортежа не имел бы записи в source map и не мапился бы на выходной .cppt. Весь
    // диапазон оператора `t1, ..., tN := [... ]source;` покрывает все эмитируемые строки
    // (temp-источник + runtime-guard + pop_front'ы / std::get + rest) - как у ControlFlowStmt.
    MapperScope scope(m_ctx.source(), n.range(), m_out);
    if (n.m_isSpread) {
        emitDestructureDict(n);
    } else {
        emitDestructureTuple(n);
    }
}

void CppTranspiler::emitDestructureDict(const DestructureDecl& n) {
    const std::string ind = indentPrefix();
    const size_t cnt = n.m_targets.size();
    // C++-имя источника (для rest-мутации == источнику); для не-Ident - пусто.
    std::string srcCpp;
    if (n.m_source && n.m_source->kind() == ParserToken::Kind::Ident) {
        srcCpp = utils::name_to_cpp(n.m_source->text());
    }
    // Именованный rest (`rest...`): C++-имя цели (пропускаем `_...` - отброс).
    std::string restCpp;
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i] && t && t->kind() == ParserToken::Kind::Ident && t->text() != "_") {
            restCpp = utils::name_to_cpp(t->text());
        }
    }
    // Мутация источника на месте: rest-цель == источнику (идиома `item, dict... := ... dict`).
    const bool mutatingRest = !restCpp.empty() && restCpp == srcCpp;
    // Источник: при мутации - сам источник (pop'ы идут прямо в него); иначе - временная копия,
    // из которой делаются pop_front и rest-копия (одна оценка источника-выражения, не N раз).
    std::string srcRef = srcCpp;
    if (!mutatingRest) {
        const std::string tmp = "__trust_dst_" + std::to_string(m_destructureCounter++);
        m_ctx.source().output_append(m_out, ind + "auto " + tmp + " = ");
        emitExpr(n.m_source.get());
        m_ctx.source().output_append(m_out, ";\n");
        srcRef = tmp;
    }
    // Runtime-недостаток элементов (динамический источник - размер неизвестен на этапе сборки):
    // точная привязка требует, чтобы число элементов было не меньше числа pop'ов. Guard с понятной
    // диагностикой вместо голого std::out_of_range из pop_front (AGENTS rule 5 - без тихого дефолта
    // / None; для кортежа арность проверяется статически - guard здесь не нужен, под-кортежи pop_front
    // не делают).
    // Имя файла и строку в сообщении берём из ИСХОДНОГО .src, а не из сгенерированного C++ (как
    // @__FILE_NAME__/@__FILE_LINE__ для @assert): через SourceMap по диапазону узла деструктуризации.
    const auto nrange = n.range();
    std::string srcFile;
    int srcLine = 0;
    if (!nrange.begin.isInvalid()) {
        srcFile = std::string(m_ctx.source().get_file(nrange.begin.fileIdx()).getFilename());
        srcLine = static_cast<int>(m_ctx.source().line_column(nrange.begin).line);
    }
    // Экранирование для C++-строкового литерала (обратный слэш / кавычка).
    auto escapeCppLiteral = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (const char c : s) {
            if (c == '\\' || c == '"') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out;
    };
    size_t needPops = 0;
    for (size_t i = 0; i < cnt; ++i) {
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) {
            continue;
        }
        ++needPops; // каждый не-rest target (включая `_` skip) делает pop_front
    }
    if (needPops > 0) {
        // trust__abort__ определён в trust/assert.hpp - обязательный инклуд (как для @assert).
        // Префикс '@' направляет заголовок в механизм извлечения рантайм-заголовков (extractRuntimeHeader):
        // он извлекается в <build_dir>/trust/assert.hpp + добавляется `-I<build_dir>`, поэтому
        // доступен и в изолированной сборке (--run из произвольного каталога), а не только при
        // запуске из корня проекта с `-I<проект>/include`.
        recordRequiredInclude("@trust/assert.hpp");
        m_ctx.source().output_append(m_out, ind + "if (" + srcRef + ".size() < " + std::to_string(needPops) + ") trust::trust__abort__(\"" +
                                                escapeCppLiteral(srcFile) + "\", " + std::to_string(srcLine) +
                                                ", \"destructuring: not enough elements in source\");\n");
    }
    // Цели-элементы (НЕ rest): pop_front. Вне цикла - per-element тип элемента (any_cast<T>, где T -
    // runtime-тип: Int8..Int64 → int64_t, Float → double, Bool, StrChar...). ВНУТРИ цикла тип расширен
    // до максимального (Integer/Double); гетерогенность (bool+int) обрабатывается runtime-конвертерами
    // (anyToInt64/anyToDouble/anyToString), а не строгим any_cast. Any → std::any.
    const TypeRegistry& reg = m_ctx.types();
    const TypeId i64C = reg.getCanonicalTypeId(reg.getType(type::Int64));
    const TypeId dblC = reg.getCanonicalTypeId(reg.getType(type::Double));
    const TypeId scC = reg.getCanonicalTypeId(reg.getType(type::StrChar));
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) {
            continue;
        }
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        m_ctx.source().output_append(m_out, ind);
        if (t->text() == "_") {
            m_ctx.source().output_append(m_out, srcRef + ".pop_front();\n");
            continue;
        }
        const std::string cppName = utils::name_to_cpp(t->text());
        const TypeId et = (i < n.m_targetTypes.size()) ? n.m_targetTypes[i] : INVALID_TYPE_ID;
        const TypeId etC = (et != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(et) : INVALID_TYPE_ID;
        // Префикс объявления: присваивание (`a = ...`) - без типа; объявление - тип + имя.
        const std::string anyPrefix = n.m_isAssign ? "" : "std::any ";
        if (etC == INVALID_TYPE_ID) {
            m_ctx.source().output_append(m_out, anyPrefix + cppName + " = " + srcRef + ".pop_front();\n");
            continue;
        }
        // Тип переменной: аннотация цели (m_targetDeclaredTypes) или выведенный (et).
        const TypeId declared = (i < n.m_targetDeclaredTypes.size() && n.m_targetDeclaredTypes[i] != INVALID_TYPE_ID) ? n.m_targetDeclaredTypes[i] : et;
        const auto tname = emitTypeName(declared, t->text());
        if (!tname || tname->empty()) {
            // Тип цели уже резолвлен семантикой (naturalRuntimeType возвращает только типы с
            // C++-именем: Int64/Double/Bool/StrChar/StrWide/Any); сбой emitTypeName - инвариантное
            // нарушение. Не тихий fallback на std::any (AGENTS rule 5) - явная диагностика.
            m_ctx.report(n.range(), OptKind::ParseError, "unable to emit C++ type for destructuring target '{}'", cppName);
            return;
        }
        // Тип any_cast: natural runtime тип ЭЛЕМЕНТА (m_targetTypes) - соответствует хранению Dict
        // (int → int64_t); переменная объявляется типом declared (аннотация/выведенный).
        const auto castName = emitTypeName(et, t->text());
        const std::string castType = (castName && !castName->empty()) ? *castName : *tname;
        const std::string prefix = n.m_isAssign ? "" : (*tname + " ");
        if (n.m_inLoop) {
            if (etC == i64C) {
                m_ctx.source().output_append(m_out, prefix + cppName + " = trust::detail::anyToInt64(" + srcRef + ".pop_front());\n");
            } else if (etC == dblC) {
                m_ctx.source().output_append(m_out, prefix + cppName + " = trust::detail::anyToDouble(" + srcRef + ".pop_front());\n");
            } else if (etC == scC) {
                m_ctx.source().output_append(m_out, prefix + cppName + " = trust::detail::anyToString(" + srcRef + ".pop_front());\n");
            } else {
                // Bool и пр. - однородные: строгий any_cast безопасен (хранится как есть).
                m_ctx.source().output_append(m_out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
            }
        } else {
            m_ctx.source().output_append(m_out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
        }
    }
    // Именованный rest: «остаток» - копия источника после pop'ов (источник не мутируется);
    // в режиме присваивания - `rest = src` (цель уже существует).
    if (!mutatingRest && !restCpp.empty()) {
        m_ctx.source().output_append(m_out, ind);
        m_ctx.source().output_append(m_out, (n.m_isAssign ? "" : "trust::Dict ") + restCpp + " = " + srcRef + ";\n");
    }
    // mutatingRest - источник уже мутирован pop'ами и является rest; присвоение не нужно.
    // `_...` (отброс остатка) - ничего не генерируем: остаток остаётся во временной переменной.
}

void CppTranspiler::emitDestructureTuple(const DestructureDecl& n) {
    recordRequiredInclude("#include <tuple>");
    const std::string ind = indentPrefix();
    const size_t cnt = n.m_targets.size();
    // Арность источника-кортежа (для rest): семантика сохранила её на узле (скоуп-стек
    // к моменту кодогенерации сброшен, локальные символы недоступны). Запасной путь - литерал.
    // `m_sourceArity == 0` здесь НЕ является инвариантным нарушением: пустой кортеж `():Tuple`
    // даёт легитимную нулевую арность (например, один rest `r... := t` → пустой make_tuple()).
    // Литерал-фолбэк покрывает прямое построение AST в unit-тестах (без прохода семантики).
    size_t elemCount = n.m_sourceArity;
    if (elemCount == 0 && n.m_source && is_collection_literal_kind(n.m_source->kind())) {
        elemCount = static_cast<const Sequence&>(*n.m_source).m_body.size();
    }
    size_t idx = 0;
    bool first = true;
    // Цели-элементы: std::get<N> (bind или skip `_`).
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        if (i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) {
            continue; // rest обрабатывается отдельно ниже
        }
        if (t->text() == "_") {
            ++idx; // skip-элемент занимает индекс, но не связывается
            continue;
        }
        if (!first) {
            m_ctx.source().output_append(m_out, ind);
        }
        first = false;
        // Префикс объявления: присваивание (`a = ...`) - без типа; явная аннотация (`a:Int32`,
        // m_targetTypes[i] заполнена семантикой) - фиксированный тип; иначе `auto` (элемент кортежа).
        std::string prefix;
        if (n.m_isAssign) {
            prefix.clear();
        } else if (i < n.m_targetTypes.size() && n.m_targetTypes[i] != INVALID_TYPE_ID) {
            const auto tname = emitTypeName(n.m_targetTypes[i], t->text());
            prefix = (tname && !tname->empty()) ? (*tname + " ") : "auto ";
        } else {
            prefix = "auto ";
        }
        m_ctx.source().output_append(m_out, prefix + utils::name_to_cpp(t->text()) + " = std::get<" + std::to_string(idx) + ">(");
        emitExpr(n.m_source.get());
        m_ctx.source().output_append(m_out, ");\n");
        ++idx;
    }
    // Именованный rest (`rest...`): остаток как make_tuple оставшихся элементов (std::get<k>).
    // `_...` (отброс остатка) - ничего не генерируем.
    for (size_t i = 0; i < cnt; ++i) {
        auto* t = n.m_targets[i].get();
        if (!t || t->kind() != ParserToken::Kind::Ident) {
            continue;
        }
        if (!(i < n.m_targetIsRest.size() && n.m_targetIsRest[i]) || t->text() == "_") {
            continue;
        }
        if (!first) {
            m_ctx.source().output_append(m_out, ind);
        }
        first = false;
        const std::string restPrefix = n.m_isAssign ? "" : "auto ";
        m_ctx.source().output_append(m_out, restPrefix + utils::name_to_cpp(t->text()) + " = std::make_tuple(");
        for (size_t k = idx; k < elemCount; ++k) {
            if (k != idx) {
                m_ctx.source().output_append(m_out, ", ");
            }
            m_ctx.source().output_append(m_out, "std::get<" + std::to_string(k) + ">(");
            emitExpr(n.m_source.get());
            m_ctx.source().output_append(m_out, ")");
        }
        m_ctx.source().output_append(m_out, ");\n");
    }
}
void CppTranspiler::visit_ArgNode(const ArgNode&) {
}

// Binary: TypeDecl → объявление типа; прочие → expression statement.
void CppTranspiler::visit_TypeDecl(const Binary& n) {
    generateTypeDeclToFile(n, m_out);
}
// Бинарные statement/expression kinds - единая генерация (m_exprDepth различает контекст).
void CppTranspiler::visit_NameDecl(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}
void CppTranspiler::visit_AssignOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}
// AppendStmt (`X []= v`) - append к контейнеру: `X.push_back(v)`. X - простой контейнер
// (Ident; вложенный LHS отклонён семантикой как «не реализовано»). Ветка кодогенерации
// выбирается по каноническому TypeId контейнера (не по C++-имени): алиасы резолвятся явно
// (String → StrChar, Dictionary → Dict). trust::Dict → push_back(TypedValue), строка →
// push_back/append. Значение RHS - точно таким типом, как хранит контейнер.
void CppTranspiler::visit_AppendStmt(const Binary& n) {
    if (!n.m_left || !n.m_right) {
        emitPlaceholderExpr(m_out);
        return;
    }
    // emitTypeName резолвит C++-имя и записывает инклуды контейнера (сайд-эффект); сам
    // результат не нужен - ветка кодогенерации выбирается по каноническому TypeId ниже.
    if (!emitTypeName(n.lhsType, "")) {
        m_ctx.report(n.range(), OptKind::ParseError, "unable to resolve container type for append '[]='");
        return;
    }
    const TypeRegistry& reg = m_ctx.types();
    const TypeId cid = reg.getCanonicalTypeId(n.lhsType);
    const TypeId dictId = reg.getType(type::Dict);
    const TypeId strCharId = reg.getType(type::StrChar);
    const TypeId strWideId = reg.getType(type::StrWide);

    emitExpr(n.m_left.get());
    if (cid == dictId) {
        if (n.m_right && n.m_right->kind() == ParserToken::Kind::Ellipsis) {
            // Spread-merge `X []= ... dict` → `X.extend(dict)`: добор ВСЕХ элементов словаря-
            // операнда (аналог extend/update). Операнд - m_body[0] узла Ellipsis (литерал
            // → trust::Dict{...}, переменная → c_d2, выражение → его значение).
            m_ctx.source().output_append(m_out, ".extend(");
            const auto& ell = static_cast<const Sequence&>(*n.m_right);
            if (!ell.m_body.empty() && ell.m_body[0]) {
                emitExpr(ell.m_body[0].get());
            } else {
                m_ctx.source().output_append(m_out, "{}");
            }
            m_ctx.source().output_append(m_out, ")");
            return;
        }
        // Dict: только push_back(name, value); безымянный append - пустое имя (позиционный элемент).
        m_ctx.source().output_append(m_out, ".push_back(\"\", ");
        emitTypedDictValue(n.m_right.get(), n.resultType);
        m_ctx.source().output_append(m_out, ")");
    } else if (cid == strCharId || cid == strWideId) {
        const bool wide = (cid == strWideId);
        m_ctx.source().output_append(m_out, ".append(");
        if (wide && n.m_right && n.m_right->kind() == ParserToken::Kind::StrChar) {
            // Узкий символьный литерал 'c' в wide-контейнер → wide-литерал L"c" (char→wchar).
            // Источник - StrChar (одинарные кавычки): голый " экранируем в \" (см. visit_StrChar).
            const std::string_view t = n.m_right->text();
            std::string body;
            body.reserve(t.size());
            for (size_t i = 0; i < t.size(); ++i) {
                const char c = t[i];
                if (c == '\\' && i + 1 < t.size()) { // escape-последовательность - как есть
                    body += c;
                    body += t[++i];
                } else if (c == '"') {
                    body += "\\\"";
                } else {
                    body += c;
                }
            }
            m_ctx.source().output_append(m_out, std::format("L\"{}\"", body));
        } else {
            emitExpr(n.m_right.get());
        }
        m_ctx.source().output_append(m_out, ")");
    } else {
        m_ctx.report(n.range(), OptKind::ParseError, "append '[]=' is not supported for container type '{}'", reg.getFullTypeName(cid));
    }
}
void CppTranspiler::visit_MathOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}
void CppTranspiler::visit_BitwiseOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}
void CppTranspiler::visit_CompareOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}
void CppTranspiler::visit_LogicalOp(const Binary& n) {
    emitBinaryStmtOrExpr(n);
}
// Доступ к элементу словаря: имя/статический индекс (MemberAccess) или динамический индекс
// (ArrayAccess). Для конкретного типа поля - obj.at(key).getAs<Cpp>() (типизированный доступ
// к значению: fast-path variant / std::any); для Any/неизвестного - obj.at(key) (TypedValue,
// дальше any_to в касте).
bool CppTranspiler::emitDictElementAccess(const Binary& n) {
    // Заголовки Dict-типа записаны при объявлении/создании объекта (emitTypeName/resolveCppTypeId);
    // здесь - только тип поля через emitTypeName (единая точка сбора).
    const TypeId rt = n.resultType;
    const bool concrete = (rt != INVALID_TYPE_ID && !isAnyType(rt, m_ctx.types()));
    std::string concreteCpp;
    if (concrete) {
        if (auto cpp = emitTypeName(rt, "")) {
            concreteCpp = std::move(*cpp);
        }
    }
    // объект
    if (n.m_left) {
        emitExpr(n.m_left.get());
    } else {
        m_ctx.source().output_append(m_out, "{}");
    }
    m_ctx.source().output_append(m_out, ".at(");
    // ключ
    if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right && n.m_right->kind() == ParserToken::Kind::IntLiteral) {
        emitExpr(n.m_right.get()); // статический индекс: d.1 → at(1)
    } else if (n.kind() == ParserToken::Kind::MemberAccess && n.m_right) {
        // Имя поля: d.two → at("two").
        m_ctx.source().output_append(m_out, "\"" + utils::escape_cpp_string(n.m_right->text()) + "\"");
    } else if (n.m_right) {
        emitExpr(n.m_right.get()); // динамический индекс: d[expr] → at(expr)
    } else {
        m_ctx.source().output_append(m_out, "0");
    }
    m_ctx.source().output_append(m_out, ")");
    if (concrete && !concreteCpp.empty()) {
        // Типизированный доступ к значению по C++-типу (fast-path variant / std::any).
        m_ctx.source().output_append(m_out, ".getAs<" + concreteCpp + ">()");
    }
    return concrete;
}

// Доступ к элементу словаря по имени или статическому индексу: d.two / d.1.
// Вызов метода на объекте (obj.method(args)) - нативный член C++-объекта, вставляется как есть.
void CppTranspiler::visit_MemberAccess(const Binary& n) {
    // Доступ к enum через имя типа: Color.RED → c_Color::RED; Color.count()/fromName(...) →
    // c_Color::count()/... (тип-уровневые методы; члены и методы - статические члены структуры).
    if (n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
        if (auto tid = m_ctx.types().findType(n.m_left->text())) {
            if (isEnumType(*tid, m_ctx.types())) {
                const std::string enum_cpp = utils::name_to_cpp(n.m_left->text());
                if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                    const auto& call = static_cast<const CallExpr&>(*n.m_right);
                    std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
                    if (!mname.empty() && mname.front() == '%') {
                        mname.erase(0, 1);
                    }
                    m_ctx.source().output_append(m_out, enum_cpp + "::" + mname + "(");
                    if (call.m_args) {
                        for (size_t i = 0; i < call.m_args->size(); ++i) {
                            if (i) {
                                m_ctx.source().output_append(m_out, ", ");
                            }
                            emitExpr((*call.m_args)[i].get());
                        }
                    }
                    m_ctx.source().output_append(m_out, ")");
                    return;
                }
                // Член enum: Color.RED → c_Color::RED.
                const std::string member_cpp = utils::name_to_cpp(n.m_right->text());
                m_ctx.source().output_append(m_out, enum_cpp + "::" + member_cpp);
                return;
            }
            if (isVariantType(*tid, m_ctx.types())) {
                const std::string var_cpp = utils::name_to_cpp(n.m_left->text());
                if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
                    const auto& call = static_cast<const CallExpr&>(*n.m_right);
                    std::string mname = call.m_callee ? std::string(call.m_callee->text()) : std::string();
                    if (!mname.empty() && mname.front() == '%') {
                        mname.erase(0, 1);
                    }
                    m_ctx.source().output_append(m_out, var_cpp + "::" + mname + "(");
                    if (call.m_args) {
                        for (size_t i = 0; i < call.m_args->size(); ++i) {
                            if (i) {
                                m_ctx.source().output_append(m_out, ", ");
                            }
                            emitExpr((*call.m_args)[i].get());
                        }
                    }
                    m_ctx.source().output_append(m_out, ")");
                    return;
                }
                // Член variant: Value.RED → c_Value::c_RED (тип члена).
                const std::string member_cpp = utils::name_to_cpp(n.m_right->text());
                m_ctx.source().output_append(m_out, var_cpp + "::" + member_cpp);
                return;
            }
        }
    }
    if (n.m_right && n.m_right->kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(*n.m_right);
        if (call.m_callee) {
            // Метод на объекте: (объект).<нативный_член>(args). Нативность/константность метода -
            // из полного ключа (findMethodInfo: '%' нативный, '^' константный); нативное имя - из
            // ключа (срез '%'/'^'). const-вызов `obj.method^()` - attr::ReadOnly на ВЫЗОВЕ
            // (convertAttrsToNode/CallExpr) → const_cast<const T&>(obj) (гарантированно const-перегрузка).
            // const_cast-тип T - из TypeId объекта (n.lhsType, сохранён семантикой; кодген не может
            // восстановить его для локальной переменной - скоуп-стек сброшен). Fallback - резолв имени.
            TypeId objType = (n.lhsType != INVALID_TYPE_ID) ? m_ctx.types().getCanonicalTypeId(n.lhsType) : INVALID_TYPE_ID;
            if (objType == INVALID_TYPE_ID && n.m_left && n.m_left->kind() == ParserToken::Kind::Ident) {
                if (auto t = resolveTypeIdByName(n.m_left->text())) {
                    objType = m_ctx.types().getCanonicalTypeId(*t);
                }
            }
            // Нативное имя: из полного ключа совпавшего метода (алиас → ключ цели); иначе - как есть.
            std::string mname(call.m_callee->text());
            std::string native;
            if (objType != INVALID_TYPE_ID) {
                if (auto mi = m_ctx.types().findMethodInfo(objType, mname)) {
                    native = utils::bare_name(mi->key); // срез '%'/'^' → нативное имя (count/size/...)
                }
            }
            if (native.empty()) {
                native = mname;
                if (!native.empty() && native.front() == '%') {
                    native.erase(0, 1);
                }
            }
            // const-вызов `obj.method^()` - attr::ReadOnly на вызове.
            const bool constCall = call.as_attr() && call.as_attr()->has_attr(m_ctx.attrs(), attr::ReadOnly);
            if (constCall) {
                m_ctx.source().output_append(m_out, "const_cast<const ");
                if (auto ct = resolveCppTypeId(objType, "Range.Const")) {
                    m_ctx.source().output_append(m_out, ct->first);
                } else {
                    m_ctx.source().output_append(m_out, "std::any");
                }
                m_ctx.source().output_append(m_out, "&>(");
                emitExpr(n.m_left.get());
                m_ctx.source().output_append(m_out, ")");
            } else {
                m_ctx.source().output_append(m_out, "(");
                emitExpr(n.m_left.get());
                m_ctx.source().output_append(m_out, ")");
            }
            m_ctx.source().output_append(m_out, ".");
            m_ctx.source().output_append(m_out, native);
            m_ctx.source().output_append(m_out, "(");
            if (call.m_args) {
                for (size_t i = 0; i < call.m_args->size(); ++i) {
                    if (i) {
                        m_ctx.source().output_append(m_out, ", ");
                    }
                    emitExpr((*call.m_args)[i].get());
                }
            }
            m_ctx.source().output_append(m_out, ")");
            return;
        }
    }
    if (n.tupleIndex >= 0) {
        emitTupleElementAccess(n);
        return;
    }
    emitDictElementAccess(n);
}
// Динамический доступ по индексу: d[expr].
void CppTranspiler::visit_ArrayAccess(const Binary& n) {
    if (n.tupleIndex >= 0) {
        emitTupleElementAccess(n);
        return;
    }
    // Доступ к элементу массива `a[i]` (структурный Array-тип): `(obj).at(idx)` -
    // безопасный (bounds-check) доступ, как для словаря. lhsType ставит семантика
    // (resolveArrayAccess).
    if (n.lhsType != INVALID_TYPE_ID && m_ctx.types().isArrayType(n.lhsType)) {
        if (n.m_left) {
            m_ctx.source().output_append(m_out, "(");
            emitExpr(n.m_left.get());
            m_ctx.source().output_append(m_out, ").at(");
        }
        if (n.m_right) {
            emitExpr(n.m_right.get());
        }
        m_ctx.source().output_append(m_out, ")");
        return;
    }
    emitDictElementAccess(n);
}

// Кортеж: `t.name` / `t.0` / `t[idx]` → `std::get<index>(obj)`. Индекс резолвит семантика
// (Binary::tupleIndex). std::get возвращает ссылку на конкретный элемент std::tuple.
void CppTranspiler::emitTupleElementAccess(const Binary& n) {
    recordRequiredInclude("#include <tuple>");
    m_ctx.source().output_append(m_out, "std::get<");
    m_ctx.source().output_append(m_out, std::to_string(n.tupleIndex));
    m_ctx.source().output_append(m_out, ">(");
    if (n.m_left) {
        emitExpr(n.m_left.get());
    }
    m_ctx.source().output_append(m_out, ")");
}

// Return/Throw inline; Break/Continue → goto.
void CppTranspiler::emitJumpValue(std::string_view keyword, const JumpStmt& n) {
    // Синтетические jump-узлы lowering (void-return по имени функции) не имеют Term →
    // range невалиден → маппинг пропускается (нет исходного trust-текста).
    const MapperRange r = n.range();
    std::unique_ptr<MapperScope> scope;
    if (!r.isInvalid()) {
        scope = std::make_unique<MapperScope>(m_ctx.source(), r, m_out);
    }
    if (n.m_value) {
        m_ctx.source().output_append(m_out, keyword);
        m_ctx.source().output_append(m_out, " ");
        emitExpr(n.m_value.get());
        m_ctx.source().output_append(m_out, ";");
    } else {
        m_ctx.source().output_append(m_out, keyword);
        m_ctx.source().output_append(m_out, ";");
    }
}
void CppTranspiler::visit_ReturnStmt(const JumpStmt& n) {
    emitJumpValue("return", n);
}
void CppTranspiler::visit_ThrowStmt(const JumpStmt& n) {
    emitJumpValue("throw", n);
}
void CppTranspiler::visit_BreakStmt(const JumpStmt& n) {
    // Безымянный break - обычный C++ break;. Именованный break анализатор переписывает
    // в GotoStmt (или void-ReturnStmt при break по имени функции), поэтому сюда доходит
    // только безымянный.
    MapperScope scope(m_ctx.source(), n.range(), m_out);
    m_ctx.source().output_append(m_out, "break;");
}
void CppTranspiler::visit_ContinueStmt(const JumpStmt& n) {
    // Безымянный continue - обычный C++ continue;. Именованный continue анализатор
    // переписывает в GotoStmt, поэтому сюда доходит только безымянный.
    MapperScope scope(m_ctx.source(), n.range(), m_out);
    m_ctx.source().output_append(m_out, "continue;");
}

// Goto/Label - синтетические узлы lowering: goto по метке / определение метки.
// Маппинг НЕ строится: у синтетических узлов нет исходного trust-текста, а их range
// (блок/цикл) уже смапплен реальными узлами (иначе коллизия trustKey в mapStop).
void CppTranspiler::visit_GotoStmt(const LabelRef& n) {
    m_ctx.source().output_append(m_out, "goto " + n.m_name + ";");
}
void CppTranspiler::visit_LabelStmt(const LabelRef& n) {
    m_ctx.source().output_append(m_out, n.m_name + ":;");
}

// SemicolonStmt - выражение в позиции оператора: выражение в statement-root (без скобок) +
// завершающая ';'. Маппинг range берётся от обёрнутого выражения (SemicolonStmt::range() делегирует).
void CppTranspiler::visit_SemicolonStmt(const SemicolonStmt& n) {
    MapperScope scope(m_ctx.source(), n.range(), m_out);
    if (n.m_expr) {
        generateNodeToFile(*n.m_expr, m_out);
    }
    m_ctx.source().output_append(m_out, ";");
}

// Литерал - всегда только текст (statement-позицию оборачивает SemicolonStmt, добавляя ';').
void CppTranspiler::visit_IntLiteral(const Literal& n) {
    m_ctx.source().output_append(m_out, n.text());
}
void CppTranspiler::visit_RationalLiteral(const Literal& n) {
    // Рациональный литерал `num\den` создаётся непосредственно из строки: эмитим
    // `trust::Rational("num\den")` - парсинг `num\den` выполняет однострочный конструктор
    // Rational. В C++-строковом литерале обратная косая экранируется (`\` → `\\`).
    // Инклуд Rational-типа записывается через emitTypeName (единая точка сбора).
    if (n.typeId != INVALID_TYPE_ID) {
        emitTypeName(n.typeId, "");
    } else if (auto rid = m_ctx.types().findType(type::Rational)) {
        emitTypeName(*rid, "Rational");
    }
    const std::string_view t = n.text();
    std::string escaped;
    escaped.reserve(t.size() * 2);
    for (const char c : t) {
        if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped += c;
        }
    }
    m_ctx.source().output_append(m_out, std::format("trust::Rational(\"{}\")", escaped));
}
void CppTranspiler::visit_StrChar(const Literal& n) {
    // Строка в одинарных кавычках '…' → StrChar → обычная "…". Ограничитель StrChar - ',
    // поэтому голый " в нём допустим; в C++-литерале "…" экранируем его в \" (иначе литерал
    // обрывается). Escape-последовательности trust сохранены как есть и валидны в C++ без
    // изменений - копируем их (backslash + следующий символ) не трогая.
    const std::string_view t = n.text();
    std::string body;
    body.reserve(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '\\' && i + 1 < t.size()) { // escape-последовательность - как есть
            body += c;
            body += t[++i];
        } else if (c == '"') {
            body += "\\\"";
        } else {
            body += c;
        }
    }
    m_ctx.source().output_append(m_out, std::format("\"{}\"", body));
}
void CppTranspiler::visit_StrWide(const Literal& n) {
    // Строка в двойных кавычках "…" → StrWide → wide-строка L"…". Голый " в StrWide невозможен
    // (закрыл бы строку), кавычка вставляется как \" и уже валидна в C++ - экранирование не нужно.
    m_ctx.source().output_append(m_out, std::format("L\"{}\"", n.text()));
}
void CppTranspiler::visit_FloatLiteral(const Literal& n) {
    m_ctx.source().output_append(m_out, n.text());
}

// Контекст-макросы (@__NAMESPACE__, @::, @__FUNCTION__, @__FUNCSIG__, @__FUNCDNAME__)
// раскрываются анализатором (заменяются на Literal/StrChar или имя) до транспиляции.
// До транспилятора такие узлы доходить не должны - это недостижимая ветка (сигнал бага).
void CppTranspiler::visit_ContextMacro(const ContextMacro&) {
    FAULT("ContextMacro reached the transpiler (must be expanded by the analyzer)");
}

// Control flow.
void CppTranspiler::visit_IfStmt(const IfStmt& n) {
    generateIfToFile(n, m_out);
}
void CppTranspiler::visit_WhileStmt(const WhileStmt& n) {
    generateWhileToFile(n, m_out);
}
void CppTranspiler::visit_DoWhileStmt(const DoWhileStmt& n) {
    generateDoWhileToFile(n, m_out);
}
void CppTranspiler::visit_MatchingStmt(const MatchStmt& n) {
    generateMatchToFile(n, m_out);
}

// EmbedExpr - raw text: statement с маппингом, выражение - только текст.
void CppTranspiler::visit_EmbedExpr(const AstNodeAttr& n) {
    // Узкий чек ТОЛЬКО по тексту вставки: рантайм-функции/символы → запись их заголовков
    // (нужно для заголовков, которые физически отсутствуют в каталоге подключаемых файлов
    // и должны быть извлечены из trust-runtime).
    recordRuntimeSymbolsInText(n.text());
    if (m_exprDepth > 0) {
        // C++-вставка: текст остаётся как есть, кроме trust-маркеров $/@ → name_to_cpp.
        std::string converted = utils::transform_embed_cpp(n.text());
        emitExprText(converted);
    } else {
        MapperScope scope(m_ctx.source(), n.range(), m_out);
        emitExpr(&n);
    }
}

// Document - документирующий комментарий: эмитится в statement-позиции. Trust-доки `##`/`##<`
// невалидны в C++ (префикс '#' - препроцессор), поэтому нормализуются в однострочные C++ `///`/`///<`;
// `/** … */` и `///` выводится как есть. Подавление (флаг -Wno-comments) - на уровне обхода.
void CppTranspiler::visit_Document(const AstNodeAttr& n) {
    if (m_exprDepth > 0) {
        return;
    }
    std::string_view t = n.text();
    if (t.starts_with("##")) {
        m_ctx.source().output_append(m_out, "///");
        m_ctx.source().output_append(m_out, t.substr(2));
    } else {
        m_ctx.source().output_append(m_out, t);
    }
}
void CppTranspiler::visit_Ident(const IdentName& n) {
    // Нативный импорт-алиас: вызов trust-имени переписывается в прямой вызов нативного C++-имени.
    auto it = m_nativeImports.find(std::string(n.text()));
    if (it != m_nativeImports.end()) {
        emitExprText(it->second);
        return;
    }
    // Идентификатор в выражении: манглинг trust-имени в C++-идентификатор (x → c_x).
    emitExprText(utils::name_to_cpp(n.text()));
}
void CppTranspiler::visit_TypeName(const IdentType& n) {
    emitExprText(n.text());
}
// CallExpr - только выражение: callee(args). Statement-позицию оборачивает SemicolonStmt.
void CppTranspiler::visit_CallExpr(const CallExpr& n) {
    // Строка-формат: `"{}"(args)` / `'{}'(args)` - callee строковый литерал → std::format.
    if (n.m_callee && (n.m_callee->kind() == ParserToken::Kind::StrWide || n.m_callee->kind() == ParserToken::Kind::StrChar)) {
        emitFormatCall(n);
        return;
    }
    // Рантайм-функции (print/assert): заголовки по имени callee (точное совпадение с
    // рантайм-символом; ведущий '%' срезается, как в семантике) - без скана всего буфера.
    if (n.m_callee) {
        if (const auto id = findRuntimeSymbolByName(n.m_callee->text())) {
            recordRuntimeSymbolHeaders(*id);
        }
    }
    emitExpr(n.m_callee.get());
    m_ctx.source().output_append(m_out, "(");
    if (n.m_args) {
        for (size_t i = 0; i < n.m_args->size(); ++i) {
            if (i) {
                m_ctx.source().output_append(m_out, ", ");
            }
            emitExpr((*n.m_args)[i].get());
        }
    }
    m_ctx.source().output_append(m_out, ")");
}

// Строка-формат `"{}"(args)` / `'{}'(args)` → `std::format(fmt, args...)`. Ширина строки
// задаётся Kind callee: StrWide → L"…" (std::format<wchar_t>), StrChar → "…". Требуется
// #include <format> в генерируемом C++ (записываем через recordRequiredInclude).
void CppTranspiler::emitFormatCall(const CallExpr& n) {
    recordRequiredInclude("#include <format>");
    m_ctx.source().output_append(m_out, "std::format(");
    emitExpr(n.m_callee.get());
    if (n.m_args) {
        for (const auto& arg : *n.m_args) {
            m_ctx.source().output_append(m_out, ", ");
            emitExpr(arg.get());
        }
    }
    m_ctx.source().output_append(m_out, ")");
}

// AstNodeAttr-kind'ы, не генерируемые как statement → no-op.
void CppTranspiler::visit_Program(const AstNodeAttr&) {
}
// Expression-kind'ы без реализованной генерации: как statement - no-op,
// как выражение - placeholder "{}" (сохранение прежнего default из emitExpr).
void CppTranspiler::visit_VarRef(const AstNodeAttr&) {
    emitPlaceholderExpr(m_out);
}
namespace {

// Единая итерация элементов литерала словаря/кортежа/массива. Нормализация term_to_ast::visit_DICT
// гарантирует форму ArgNode (имя в text(), значение в m_value); тип значения элемента -
// ArgNode::resultType (единый источник семантики, см. analyzeDictLiteral). Не-ArgNode элементы
// (out-of-contract) отбрасываются. Устраняет дублирование обхода m_body в visit_Tuple,
// emitDictLiteralBody и emitTypedConstruction.
struct DictElement {
    std::string name;         ///< имя/метка элемента ("" - позиционный)
    const AstNodeBase* value; ///< узел значения (nullptr - пустой элемент)
    TypeId resultType;        ///< тип значения (из семантики)
};
std::vector<DictElement> dictElements(const DictLiteralNode& n) {
    std::vector<DictElement> out;
    out.reserve(n.m_body.size());
    for (const auto& el : n.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        out.push_back(DictElement{std::string(a.text()), a.m_value.get(), a.resultType});
    }
    return out;
}

} // namespace
// Литерал массива `[1,2:Int8,3,]` / `[1,2,3,]:Int32` → `std::vector<Elem>{...}` (mut)
// или `std::array<Elem,N>{...}` (константная/фиксированная форма). Тип элемента - единый
// источник семантики: результат анализа ArrayInit (структурный Array<Elem>, resolveCppTypeId).
void CppTranspiler::visit_ArrayInit(const DictLiteralNode& n) {
    if (n.arrayType == INVALID_TYPE_ID) {
        emitPlaceholderExpr(m_out);
        return;
    }
    emitArrayLiteral(n, n.arrayType);
}

// Эмитит литерал/конструкцию массива: `std::vector<Elem>{v1, v2, ...}` (mutable) либо
// `std::array<Elem,N>{v1, ...}` (константная форма). Тип контейнера берётся из структурного
// Array<Elem> через resolveCppTypeId (записывает инклуды <vector>/<array>). Элементы - ArgNode
// (значение в m_value); именованные/пустые отбрасываются.
void CppTranspiler::emitArrayLiteral(const DictLiteralNode& n, TypeId arrayType) {
    // Многомерный массив (несколько размерностей или вложенные литералы): генерация тензора
    // не реализована → диагностика (анализ/регистрация типа при этом работают).
    if (isMultiDimArray(arrayType, m_ctx.types())) {
        m_ctx.report(n.range(), OptKind::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        emitPlaceholderExpr(m_out);
        return;
    }
    auto container = emitTypeName(arrayType, "Array");
    if (!container) {
        emitPlaceholderExpr(m_out);
        return;
    }
    m_ctx.source().output_append(m_out, *container + "{");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ctx.source().output_append(m_out, ", ");
        }
        first = false;
        emitExpr(el.value);
    }
    m_ctx.source().output_append(m_out, "}");
}
// Литерал словаря/кортежа: `(1, two="2", name=3,)` → trust::Dict{ {"", expr}, {"two", expr}, ... }.
// Контракт: все элементы m_body - Binary(AssignOp) (left=Ident-метка или пустой, right=значение),
// строятся из канонических пар грамматики `args` (term_to_ast::visit_DICT). Тип значения -
// единый источник семантики: Binary::resultType (из resolvedType), см. emitTypedDictValue.
// Литерал словаря/конструкция/каст и кортеж. kind==Tuple → visit_Tuple (std::tuple);
// типизированный `:Type(...)`/`(...):Type` (не Tuple) → emitTypedConstruction (каст/конструктор);
// голый `(...)` → emitDictLiteralBody (trust::Dict). Контракт элементов - Binary(AssignOp)
// (term_to_ast::visit_DICT); тип значения - единый источник Binary::resultType.
void CppTranspiler::visit_DictLiteral(const DictLiteralNode& n) {
    if (n.m_type) {
        emitTypedConstruction(n);
        return;
    }
    emitDictLiteralBody(n);
}

// Кортеж `:Tuple(...)` / `(...):Tuple` (kind==Tuple, выставлен анализатором по типу из реестра)
// → std::tuple (именованные элементы конвертируются в индексы по порядку; имена в C++ не попадают).
void CppTranspiler::visit_Tuple(const DictLiteralNode& n) {
    recordRequiredInclude("#include <tuple>");
    m_ctx.source().output_append(m_out, "std::make_tuple(");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ctx.source().output_append(m_out, ", ");
        }
        first = false;
        emitExpr(el.value);
    }
    m_ctx.source().output_append(m_out, ")");
}

// Литерал диапазона `start..stop` / `start..stop..step` → `trust::Range<Elem>(start, stop[, step])`.
// Универсальный тип `:Range` (как `:Dict`): элементный тип Elem - join типов start/stop/step,
// вычисленный семантикой (RangeExpr::elementType из analyzeRangeExpr) и параметризующий шаблон
// trust::Range<T> при кодогенерации. Рациональные значения оборачиваются trust::Rational(...)
// самим visit_RationalLiteral. Записывает @trust/range.hpp (механизм как у visit_Tuple/<tuple>).
void CppTranspiler::visit_RangeExpr(const RangeExpr& node) {
    // range.hpp самодостаточен, но для toDict/элементов нужны dict.hpp и rational.hpp - их
    // пайплайн извлекает из рантайма ТОЛЬКО по прямому запросу (транзитивные инклуды не
    // отслеживаются), поэтому перечисляем весь транзитивный набор (как у Dict-типа).
    recordRequiredInclude("@trust/range.hpp");
    recordRequiredInclude("@trust/dict.hpp");
    recordRequiredInclude("@trust/rational.hpp");
    // Элементный C++-тип - из семантики (join start/stop/step). INVALID → универсальный Any.
    TypeId elemTid = node.elementType;
    if (elemTid == INVALID_TYPE_ID) {
        elemTid = m_ctx.types().getType(type_generic::Any);
    }
    std::string elemCpp;
    if (auto n = emitTypeName(elemTid, "Range")) {
        elemCpp = std::move(*n);
    } else {
        elemCpp = "std::any";
    }
    m_ctx.source().output_append(m_out, "trust::Range<" + elemCpp + ">(");
    emitExpr(node.start().get());
    m_ctx.source().output_append(m_out, ", ");
    emitExpr(node.stop().get());
    if (node.hasStep()) {
        m_ctx.source().output_append(m_out, ", ");
        emitExpr(node.step().get());
    }
    m_ctx.source().output_append(m_out, ")");
}

// Тело словаря trust::Dict{ {"name", TypedValue}, ... } - для голого `(...)` и для типизированного
// с аннотацией, резолвящейся в сам Dict.
void CppTranspiler::emitDictLiteralBody(const DictLiteralNode& n) {
    // Имя и заголовки Dict-типа - через emitTypeName (единая точка сбора; без хардкода пути).
    auto dictId = m_ctx.types().findType(type::Dict);
    if (!dictId) {
        emitPlaceholderExpr(m_out);
        return;
    }
    auto dictName = emitTypeName(*dictId, "Dict");
    if (!dictName) {
        emitPlaceholderExpr(m_out);
        return;
    }
    m_ctx.source().output_append(m_out, *dictName + "{");
    bool first = true;
    for (const auto& el : dictElements(n)) {
        if (!el.value) {
            continue;
        }
        if (!first) {
            m_ctx.source().output_append(m_out, ", ");
        }
        first = false;
        // Имя поля (у безымянного - пустая строка). Экранируем для C++-строки.
        m_ctx.source().output_append(m_out, "{\"" + utils::escape_cpp_string(el.name) + "\", ");
        // Элемент: TypedValue{kind, значение} - конструктор размещает в быструю ветку variant.
        emitTypedDictValue(el.value, el.resultType);
        m_ctx.source().output_append(m_out, "}");
    }
    m_ctx.source().output_append(m_out, "}");
}

// Типизированная конструкция/каст `:Type(...)`/`(...):Type` (не Tuple). Решение по типу из реестра:
//   - если аннотация резолвится в сам универсальный Dict → обычный словарь (emitDictLiteralBody);
//   - один элемент → каст trust::checked_cast<Type>(v) / trust::any_to<Type>(v);
//   - несколько элементов → конструктор Type(v1, ..., vN).
void CppTranspiler::emitTypedConstruction(const DictLiteralNode& n) {
    const AstNodeBase* typeNode = n.m_type.get();
    if (!typeNode || typeNode->kind() != ParserToken::Kind::TypeName) {
        emitPlaceholderExpr(m_out);
        return;
    }
    // Конструкция массива `:Array(...)` / `:Array^(...)`: семантика интернировала структурный
    // Array<Elem> (DictLiteralNode::arrayType) → эмитим std::vector<Elem>{...}/std::array<Elem,N>{...}.
    if (n.arrayType != INVALID_TYPE_ID) {
        emitArrayLiteral(n, n.arrayType);
        return;
    }
    // Аннотация = сам Dict → словарь (не конструктор).
    if (auto tid = resolveTypeIdByName(typeNode->text()); tid.has_value()) {
        if (auto dictId = m_ctx.types().findType(type::Dict); dictId && m_ctx.types().getCanonicalTypeId(*tid) == m_ctx.types().getCanonicalTypeId(*dictId)) {
            emitDictLiteralBody(n);
            return;
        }
    }
    // Тип-цель → C++ имя (и запись инклудов типа через emitTypeNameForNode). None/Void → "void".
    std::string typeCpp;
    const std::string_view tt = typeNode->text();
    if (tt == "Void" || tt == "None") {
        typeCpp = "void";
    } else {
        typeCpp = emitTypeNameForNode(typeNode);
    }
    if (typeCpp.empty()) {
        emitPlaceholderExpr(m_out);
        return; // emitTypeNameForNode уже вывел диагностику
    }
    // Операнды (значения элементов) в порядке m_body.
    std::vector<const AstNodeBase*> values;
    for (const auto& el : dictElements(n)) {
        if (el.value) {
            values.push_back(el.value);
        }
    }
    if (values.empty()) {
        emitPlaceholderExpr(m_out);
        return;
    }
    if (values.size() == 1) {
        // Каст одного значения. Операнд - элемент словаря: any_to<Type> только когда тип поля
        // неизвестен (Any/INVALID); для конкретного типа - обычный checked_cast.
        const AstNodeBase* operand = values[0];
        bool operandIsAny = false;
        if (operand && (operand->kind() == ParserToken::Kind::MemberAccess || operand->kind() == ParserToken::Kind::ArrayAccess)) {
            const auto& b = static_cast<const Binary&>(*operand);
            // Вызов метода (obj.m(...)) возвращает КОНКРЕТНОЕ C++-значение нативного члена
            // (напр. Range.at(i) → int64), а НЕ trust::TypedValue: для него any_to неприменим -
            // используем checked_cast (как для любого конкретного значения). any_to - только
            // для доступа к элементу словаря (TypedValue), где тип поля неизвестен (Any/INVALID).
            const bool isMethodCall = (b.kind() == ParserToken::Kind::MemberAccess && b.m_right && b.m_right->kind() == ParserToken::Kind::CallExpr);
            if (!isMethodCall) {
                const TypeId rt = b.resultType;
                operandIsAny = (rt == INVALID_TYPE_ID || isAnyType(rt, m_ctx.types()));
            }
        }
        if (operandIsAny) {
            recordRuntimeSymbolHeaders(RuntimeSymbolId::kAnyTo);
            m_ctx.source().output_append(m_out, "trust::any_to<" + typeCpp + ">(");
            emitExpr(operand);
            m_ctx.source().output_append(m_out, ")");
            return;
        }
        recordRuntimeSymbolHeaders(RuntimeSymbolId::kCheckedCast);
        m_ctx.source().output_append(m_out, "trust::checked_cast<" + typeCpp + ">(");
        emitExpr(operand);
        m_ctx.source().output_append(m_out, ")");
        return;
    }
    // Конструктор нескольких значений: Type(v1, ..., vN).
    m_ctx.source().output_append(m_out, typeCpp + "(");
    bool first = true;
    for (const AstNodeBase* v : values) {
        if (!first) {
            m_ctx.source().output_append(m_out, ", ");
        }
        first = false;
        emitExpr(v);
    }
    m_ctx.source().output_append(m_out, ")");
}

// Эмитит trust::TypedValue{kind, значение} для элемента словаря. kind - TypeKind значения,
// вычисленный семантикой (resolvedType) и сохранённый на элементе-AssignOp (Binary::resultType).
// Конструктор TypedValue сам размещает значение в быструю ветку std::variant (по группе kind:
// числа/bool/строки) либо в std::any-ветку (открытые типы, вложенный Dict).
// kind и C++-имя выводятся из TypeId без дублирования логики диапазонов/маппинга группа→имя.
// Единый предикат литералов - ast::is_literal_kind.
void CppTranspiler::emitTypedDictValue(const AstNodeBase* valueNode, TypeId tid) {
    const TypeKind kind = getKindFromId(tid);
    // C++-имя - через emitTypeName (единая точка: резолв + запись инклудов типа).
    auto cpp = emitTypeName(tid, "");

    // kind (TypeKind) - 32-битная битовая кодировка типа. Печатаем в hex (0x…), чтобы были
    // наглядны разряды Group/Data/RefType/…; в C++-литерале эквивалентно десятичному значению.
    m_ctx.source().output_append(m_out, std::format("trust::TypedValue{{0x{:x}, ", kind));
    // Точный C++-тип значения из реестра для литералов. RationalLiteral уже эмитится как
    // trust::Rational(...) - не оборачиваем. Не-литералы (вложенный Dict, переменная, вызов) - как есть.
    if (cpp && !cpp->empty() && is_literal_kind(valueNode->kind()) && valueNode->kind() != ParserToken::Kind::RationalLiteral) {
        m_ctx.source().output_append(m_out, *cpp + "(");
        emitExpr(valueNode);
        m_ctx.source().output_append(m_out, ")");
    } else {
        emitExpr(valueNode);
    }
    m_ctx.source().output_append(m_out, "}");
}
// Каст/конструкция `:Type(...)`/`(...):Type` перенесён в visit_DictLiteral/emitTypedConstruction
// (единый узел DictLiteralNode, решение по типу из реестра). Kind CastExpr удалён.
void CppTranspiler::visit_RefMakeExpr(const Sequence&) {
    emitPlaceholderExpr(m_out);
}
void CppTranspiler::visit_RefTakeExpr(const Sequence&) {
    emitPlaceholderExpr(m_out);
}
void CppTranspiler::visit_Ellipsis(const Sequence&) {
    emitPlaceholderExpr(m_out);
}
void CppTranspiler::visit_AssignmentStmt(const AstNodeAttr&) {
}
void CppTranspiler::visit_BlockStmt(const AstNodeAttr&) {
}
void CppTranspiler::visit_ThenBlock(const AstNodeAttr&) {
}
void CppTranspiler::visit_ElseBlock(const AstNodeAttr&) {
}
void CppTranspiler::visit_WhileElseBlock(const AstNodeAttr&) {
}
void CppTranspiler::visit_TryCatchStmt(const Sequence&) {
}
void CppTranspiler::visit_CatchBlock(const Sequence&) {
}
void CppTranspiler::visit_MatchingCase(const AstNodeAttr&) {
}
void CppTranspiler::visit_MatchingElseBlock(const AstNodeAttr&) {
}
void CppTranspiler::visit_EnumDecl(const Sequence&) {
    // Enum-объявления теперь - TypeDecl с Enum-аннотированным DictLiteral-RHS; реальная
    // эмиссия - в generateTypeDeclToFile → emitEnumStruct. Узел EnumDecl не производится.
}
void CppTranspiler::visit_EnumMember(const Sequence&) {
}
void CppTranspiler::visit_StructDecl(const Sequence&) {
}
void CppTranspiler::visit_StructField(const Sequence&) {
}

// Kind=Unimplemented: конвертер Term→Ast (convertForKind<Unimplemented>) не строит узел и выдаёт
// ошибку, поэтому такой узел в AST не появляется. Метод - no-op (требуется строгим контрактом KindVisitor).
void CppTranspiler::visit_Unimplemented(const AstNodeAttr&) {
}

// Kind=NotApplicable: узел никогда не строится (convertForKind<NotApplicable> → Fatal), в AST не
// появляется. Метод - no-op (требуется строгим контрактом KindVisitor).
void CppTranspiler::visit_NotApplicable(const AstNodeAttr&) {
}

} // namespace trust