// Generated: src/transpiler/type_emit.cpp
#include "transpiler/type_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "analysis/symbol_table.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/ref_type.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/int_literal.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>
#include <set>
#include <vector>

namespace trust {

void TypeEmitter::collectLinkLib(const AstNodeAttr& node) {
    const AttrPool& pool = m_ectx.m_ctx.attrs();
    auto link_id = pool.lookup(attr::Link);
    if (!link_id.has_value() || !node.has_attr(*link_id)) {
        return;
    }
    const std::vector<std::string>* args = node.attr_args(*link_id);
    // @[link("имя")] - ровно один аргумент (имя библиотеки).
    if (!args || args->size() != 1 || (*args)[0].empty()) {
        return;
    }
    m_ectx.m_linkLibs.insert("-l" + (*args)[0]);
}

// Сбор зависимого C++-заголовка из @[include("header")@] на нативной декларации/типе.
// Форма эмитируемой директивы (см. attr::Include):
//   - аргумент с ведущим `"` → `#include "<...>"` (локальный инклуд);
//   - аргумент с ведущим `<` → `#include <...>` (директива задана целиком);
//   - иначе (голое имя) → `#include <name>` (угловой по умолчанию).
void TypeEmitter::collectInclude(const AstNodeAttr& node) {
    const AttrPool& pool = m_ectx.m_ctx.attrs();
    auto inc_id = pool.lookup(attr::Include);
    if (!inc_id.has_value() || !node.has_attr(*inc_id)) {
        return;
    }
    const std::vector<std::string>* args = node.attr_args(*inc_id);
    if (!args || args->empty()) {
        return;
    }
    for (const std::string& h : *args) {
        if (h.empty()) {
            continue;
        }
        if (h.front() == '"' || h.front() == '<') {
            recordRequiredInclude("#include " + h);
        } else {
            recordRequiredInclude("#include <" + h + ">");
        }
    }
}

// Вставка preprocessor-инклуда (если требуется типом) в начало выходного файла.
// Инклуд рантайм-типа помечается ведущим '@' (см. TypeRegistry::preprocInclude):
// '@' срезается, путь заголовка запоминается в m_runtimeHeaders (ТОЛЬКО реально
// использованные), а в файл пишется настоящая директива #include "<path>".
void TypeEmitter::recordRequiredInclude(std::string_view include) const {
    if (include.empty()) {
        return;
    }
    if (include.front() == '@') {
        include.remove_prefix(1);
        const std::string bare(include);
        if (m_ectx.m_runtimeHeaders.count(bare)) {
            return; // уже записан
        }
        m_ectx.m_runtimeHeaders.insert(bare);
        m_ectx.m_requiredIncludes.insert("#include \"" + bare + "\"");
        return;
    }
    m_ectx.m_requiredIncludes.insert(std::string(include));
}

// -- МЕХАНИЗМ №1 - ПО ТИПУ (TypeRegistry): сбор ТИПОВ во время обхода AST, инклуды ПОСЛЕ --
// Во время эмиссии resolveCppTypeId/recordUsedType только отмечают использованные типы
// (канонические TypeId) в m_usedTypes. Сами директивы инклудов из типов формируются
// ПОСЛЕ полного обхода AST (collectTypeIncludes → emitCollectedIncludes).
void TypeEmitter::recordUsedType(TypeId type_id) const {
    m_ectx.m_usedTypes.insert(m_ectx.m_ctx.types().getCanonicalTypeId(type_id));
}

void TypeEmitter::collectTypeIncludes() const {
    // Обход с рекурсией в структурные ссылочные узлы (RefTypeData): у узла пустые
    // preprocIncludes, а инклуды его детей (pointee/deleter) должны попасть в вывод
    // (напр. unique<T,D>: T и D - нативные классы с @[include]/@trust/resource.hpp).
    std::vector<TypeId> work(m_ectx.m_usedTypes.begin(), m_ectx.m_usedTypes.end());
    std::set<TypeId> seen;
    while (!work.empty()) {
        const TypeId id = work.back();
        work.pop_back();
        if (!seen.insert(id).second) {
            continue;
        }
        for (const auto& inc : m_ectx.m_ctx.types().getPreprocIncludes(id)) {
            recordRequiredInclude(inc);
        }
        // Рантайм-заголовки вида ссылки - единый источник (refTypeRuntimeIncludes); sync-обёртка
        // определяется наличием РЕАЛЬНОГО типа политики в узле (RefTypeData::accessPolicyType).
        const auto* rd = m_ectx.m_ctx.types().getTypeDataAs<RefTypeData>(id);
        const bool hasSync = rd != nullptr && rd->accessPolicyType != INVALID_TYPE_ID;
        const RefType rt = getRefType(getKindFromId(id));
        for (std::string_view inc : refTypeRuntimeIncludes(rt, hasSync)) {
            recordRequiredInclude(inc);
        }
        // Структурный ссылочный узел: инклуды pointee, deleter и политики доступа.
        if (rd != nullptr) {
            if (rd->pointeeType != INVALID_TYPE_ID) {
                work.push_back(rd->pointeeType);
            }
            if (rd->deleterType != INVALID_TYPE_ID) {
                work.push_back(rd->deleterType);
            }
            if (rd->accessPolicyType != INVALID_TYPE_ID) {
                work.push_back(rd->accessPolicyType);
            }
        }
    }
}

// POD-проверки для КОНКРЕТНЫХ инстанциаций Struct-шаблонов. Сам шаблон-определение assert НЕ
// получает (static_assert над шаблоном зависит от T). Собираются после обхода AST по m_usedTypes.
void TypeEmitter::collectStructPodAsserts() const {
    for (const TypeId id : m_ectx.m_usedTypes) {
        if (!m_ectx.m_ctx.types().isStructType(id)) {
            continue;
        }
        const auto* rd = m_ectx.m_ctx.types().recordData(id);
        if (rd == nullptr || rd->templateOf == INVALID_TYPE_ID) {
            continue; // обычный Struct - assert уже эмитирован при объявлении
        }
        auto cpp = m_ectx.m_ctx.types().getCppTypeName(id);
        if (!cpp) {
            continue;
        }
        recordRequiredInclude("#include <type_traits>");
        const std::string tmplName(m_ectx.m_ctx.types().getFullTypeName(rd->templateOf));
        m_ectx.m_structPodAsserts.push_back("static_assert(std::is_trivial_v<" + *cpp + "> && std::is_standard_layout_v<" + *cpp + ">, \"trust: Struct '" +
                                            tmplName + "' must be POD\");");
    }
}

void TypeEmitter::emitStructPodAsserts(MapperFile output_idx) const {
    for (const std::string& a : m_ectx.m_structPodAsserts) {
        m_ectx.m_ctx.source().output_append(output_idx, a + "\n");
    }
}

bool TypeEmitter::isRecordTemplateAnnotation(const AstNodeBase* type_node) const {
    if (!type_node || !type_node->is<IdentType>()) {
        return false;
    }
    const auto* it = type_node->as<IdentType>();
    if (!it->isTemplate()) {
        return false;
    }
    std::string_view tname = it->text();
    if (!tname.empty() && tname.front() == ':') {
        tname.remove_prefix(1);
    }
    auto base = m_ectx.m_ctx.types().findType(tname);
    return base.has_value() && m_ectx.m_ctx.types().isRecordTemplate(*base);
}

std::optional<std::string> TypeEmitter::emitTypeName(TypeId type_id, std::string_view displayName) {
    // МЕХАНИЗМ №1 - ПО ТИПУ: resolveCppTypeId только отмечает тип как использованный
    // (recordUsedType → m_ectx.m_usedTypes); инклуды из типов формируются ПОСЛЕ обхода AST.
    auto resolved = resolveCppTypeId(type_id, displayName);
    if (!resolved) {
        return std::nullopt;
    }
    return std::move(resolved->first);
}

std::string TypeEmitter::emitTypeNameForNode(const AstNodeBase* type_node) {
    if (!type_node || type_node->kind() != ParserToken::Kind::TypeName) {
        return ""; // нет типа-аннотации - caller решает (напр. параметр без типа → auto)
    }
    // Типовой параметр активного шаблон-класса (`: T` в поле/методе): рендерится как имя
    // параметра (`T`) внутри `template<typename T> struct ...`, без манглинга/резолва в реестре.
    {
        std::string_view tn = type_node->text();
        if (!tn.empty() && tn.front() == ':') {
            tn.remove_prefix(1);
        }
        for (const auto& p : m_ectx.m_activeTemplateParams) {
            if (p == tn) {
                return std::string(tn);
            }
        }
    }
    // Резолвим TypeId, затем применяем ортогональные квалификаторы из атрибутов узла ТИПА
    // (`fmt: @[reftype(ptr)@] StrChar^`): ReadOnly → const, reftype → вид ссылки. Так тип
    // параметра/переменной с атрибутами получает то же C++-имя, что вычислила семантика.
    auto type_id = resolveTypeIdByName(type_node->text());
    if (!type_id.has_value()) {
        m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_node->text());
        return "";
    }
    TypeId applied = *type_id;
    // Шаблон-тип `Box<Int32>` / `vector<Int32>`: резолвим ПОЛНУЮ инстанциацию (как семантика
    // resolveTypeRef) - базовое имя (resolveTypeIdByName) дало бы только абстрактный шаблон без
    // типовых аргументов. Пользовательский record-шаблон → c_Box<int32_t>; нативный →
    // NativeTemplateTypeData (встроенные контейнеры std::vector/array → :Array).
    const auto* it = type_node->as<IdentType>();
    if (it->isTemplate()) {
        std::string_view tname = it->text();
        if (!tname.empty() && tname[0] == ':') {
            tname.remove_prefix(1);
        }
        auto& reg = m_ectx.m_ctx.types();
        auto base = reg.findType(tname);
        // Резолв типовых аргументов (общий для record- и native-шаблонов).
        const auto resolveCppArgs = [&]() {
            std::vector<TypeId> args;
            if (it->templateArgs()) {
                for (const auto& a : *it->templateArgs()) {
                    if (!a) {
                        continue;
                    }
                    TypeId at = INVALID_TYPE_ID;
                    if (a->kind() == ParserToken::Kind::TypeName) {
                        std::string_view an = a->text();
                        if (!an.empty() && an.front() == ':') {
                            an.remove_prefix(1);
                        }
                        if (auto r = resolveTypeIdByName(an)) {
                            at = *r;
                        }
                    } else if (a->kind() == ParserToken::Kind::Ident) {
                        if (auto r = reg.findType(a->text())) {
                            at = *r;
                        }
                    }
                    if (at != INVALID_TYPE_ID) {
                        args.push_back(at);
                    }
                }
            }
            return args;
        };
        if (base.has_value() && reg.isRecordTemplate(*base)) {
            applied = reg.getOrCreateRecordTemplateInstance(*base, resolveCppArgs());
        } else if (base.has_value() && reg.isNativeTemplateType(*base)) {
            const std::string cppTpl = std::string(reg.nativeTemplateCppName(*base));
            std::vector<TypeId> args = resolveCppArgs();
            if (cppTpl == "std::vector" && !args.empty()) {
                applied = reg.getOrCreateArrayType(args[0]);
            } else {
                std::string_view inc = reg.getPreprocInclude(*base);
                applied = reg.getOrCreateNativeTemplateType(cppTpl, std::move(args), inc);
            }
        }
    }
    // Тип-определение массива `:Elem[3]`/`:Elem[3,4]`: размерности из `[...]` (IdentType::dims)
    // превращают базовый тип в структурный Array<Elem,dims> - так семантика (resolveTypeRef) и
    // кодогенерация согласованы (N-D - без кодогенерации: диагностика «не реализовано» ниже).
    if (it->dims() && !it->dims()->empty()) {
        std::vector<uint64_t> dims;
        for (const auto& d : *it->dims()) {
            // Семантика уже провалидировала размерности (целый литерал, в диапазоне): невалидное
            // значение здесь - ошибка логики, а НЕ тихий пропуск/0.
            EXPECT(d && d->kind() == ParserToken::Kind::IntLiteral && "array dimension must be an integer literal");
            unsigned long long v = 0;
            try {
                v = std::stoull(stripDigitSeparators(d->text()), nullptr, 0);
            } catch (...) {
                FAULT("array dimension '{}' is out of range", std::string(d->text()));
            }
            dims.push_back(v);
        }
        if (!dims.empty()) {
            // Определение типа массива `:Elem[3]`: изменяемый массив (std::vector) с известной
            // размерностью (согласовано с семантикой resolveTypeRef). N-D - без кодогенерации.
            applied = m_ectx.m_ctx.types().getOrCreateArrayType(*type_id, std::move(dims));
        }
    }
    if (const AstNodeAttr* a = type_node->as_attr()) {
        const AttrPool& pool = m_ectx.m_ctx.attrs();
        if (a->has_attr(pool, attr::ReadOnly)) {
            applied = setFlag(applied, SymbolFlag::Const);
        }
        if (auto rid = pool.lookup(attr::Reftype); rid.has_value() && a->has_attr(*rid)) {
            if (const std::vector<std::string>* args = a->attr_args(*rid); args && !args->empty()) {
                if (auto rk = refKindFromAttrArgs(args)) {
                    applied = m_ectx.m_ctx.types().applyRefType(applied, *rk);
                }
            }
        }
    }
    // Многомерное определение массива `:Bool[3,4]`: тип регистрируется (семантика), но генерация
    // C++ не реализована («не реализовано» - только на кодогенерации, как у вложенных литералов).
    if (isMultiDimArray(applied, m_ectx.m_ctx.types())) {
        m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "многомерные массивы пока не реализованы: транслируются как тензоры (LibTorch)");
        return "";
    }
    // resolveCppTypeId (m_ectx.m_resolvedTypes-устойчивый резолв по имени) записывает все инклуды типа.
    if (auto resolved = resolveCppTypeId(applied, type_node->text())) {
        return std::move(resolved->first);
    }
    // Fallback запрещён: нерезолвящееся имя типа - ВСЕГДА ошибка с обязательной диагностикой.
    m_ectx.m_ctx.report(type_node->range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_node->text());
    return "";
}

// Рантайм-символ по типизированному идентификатору: заголовки из компайлтайм-таблицы
// (types/runtime_symbols.hpp). Enum вместо строки - опечатка в имени символа невозможна.
// ЕДИНСТВЕННЫЙ способ записи заголовков рантайм-символа.
void TypeEmitter::recordRuntimeSymbolHeaders(RuntimeSymbolId id) const {
    for (const auto& h : runtimeSymbolHeaders(id)) {
        recordRequiredInclude(h);
    }
}

// Скан текста EMBED-вставки ({% %}) на имена рантайм-символов (substring) и запись их
// заголовков через recordRuntimeSymbolHeaders(id). Для EMBED типа нет - это единственный
// способ определить нужные заголовки. Отдельный хелпер, не перегрузка записи.
void TypeEmitter::recordRuntimeSymbolsInText(std::string_view text) const {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const std::string_view sym = runtimeSymbolName(id);
        if (text.find(sym) != std::string_view::npos) {
            recordRuntimeSymbolHeaders(id);
        }
    }
}

void TypeEmitter::emitCollectedIncludes(MapperFile output_idx) {
    for (const auto& inc : m_ectx.m_requiredIncludes) {
        m_ectx.m_ctx.source().output_prepend(output_idx, inc);
    }
}

std::optional<TypeId> TypeEmitter::resolveTypeIdByName(std::string_view trustName) const {
    // Если задана разрешённая семантикой таблица символов - используем её TypeId как
    // единый источник с анализом (скоуп-стек к моменту кодогенерации сброшен к глобальному;
    // для builtin-имён в скоупе записи нет → fallback на реестр).
    if (m_ectx.m_resolvedTypes) {
        if (const Symbol* s = m_ectx.m_resolvedTypes->resolve(trustName)) {
            if (s->type != INVALID_TYPE_ID) {
                return s->type;
            }
        }
    }
    return m_ectx.m_ctx.types().findType(trustName);
}

std::optional<std::pair<std::string, std::string_view>> TypeEmitter::resolveCppType(std::string_view trustName) const {
    auto type_id = resolveTypeIdByName(trustName);
    if (!type_id.has_value()) {
        return std::nullopt;
    }
    return resolveCppTypeId(*type_id, trustName);
}

// Разрешение по уже известному TypeId. displayName - trust-имя, сохраняемое для пользовательского
// алиаса; встроенные типы и встроенные алиасы - каноническое C++-имя.
std::optional<std::pair<std::string, std::string_view>> TypeEmitter::resolveCppTypeId(TypeId type_id, std::string_view displayName) const {
    // ИНВАРИАНТ: UNKNOWN-тип (INVALID_TYPE_ID) не должен доходить до кодогенерации - анализатор обязан
    // разрешить каждый тип. Если это произошло - внутренняя ошибка (а не тихий вывод `auto`): явная
    // диагностика + FAULT (заверение компилятора), как требуют правила без тихих fallback (AGENTS п.5).
    if (type_id == INVALID_TYPE_ID) {
        m_ectx.m_ctx.report(MapperRange{}, diag::DiagId::ParseError,
                            "internal: UNKNOWN type reached code generation; the analyzer must resolve every type before transpilation");
        FAULT("UNKNOWN type (INVALID_TYPE_ID) reached code generation");
    }
    TypeId canonical = m_ectx.m_ctx.types().getCanonicalTypeId(type_id);
    // Вид ссылки (ref-бит ИЛИ структурный узел). Ветви ниже строят БАЗОВОЕ C++-имя (native class,
    // enum, variant, array, range, native template) - вид применяется ЕДИНООБРАЗНО через wrapRefKind,
    // иначе `shared<native class>` терял обёртку (`std::string` вместо `trust::Shared<std::string>`).
    // Структурные ссылочные узлы (unique<T,D>) сюда не попадают (обрабатываются getCppTypeName).
    const RefType refKind = getRefType(getKindFromId(canonical));
    // Единый источник C++-имени вида ссылки (types/ref_type.hpp); deleter сюда не попадает
    // (структурные ref-узлы с D рендерит getCppTypeName).
    const auto wrapRefKind = [&](std::string base) -> std::string { return refTypeCppName(refKind, base); };
    // Кортеж - структурный/компайлтайм-тип без единого runtime-представления: всегда конкретный
    // std::tuple, тип которого выводится из инициализатора (std::make_tuple). Голого C++-имени
    // у типа `:Tuple` нет → объявление переменной эмитится как `auto`. Плоский `:Tuple` и
    // структурный кортеж (TupleTypeData) - оба.
    if (canonical == m_ectx.m_ctx.types().getType(type_category::Tuple) || m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kTuple)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Параметризованный Range<Elem> (структурный, TypeDataKind::kRange): конкретный C++-шаблон
    // `trust::Range<ElemCpp>`, где ElemCpp - элементный тип из RangeTypeData (рекурсивно через
    // emitTypeName). В отличие от абстрактного `:Range` (ветка ниже → auto) здесь имя конкретно.
    if (m_ectx.m_ctx.types().isRangeType(canonical)) {
        const TypeId elem = m_ectx.m_ctx.types().rangeElementType(canonical);
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
        return std::make_pair(wrapRefKind("trust::Range<" + elemCpp + ">"), m_ectx.m_ctx.types().getPreprocInclude(canonical));
    }
    // Диапазон `:Range` - абстрактный универсальный тип (как :Dict), конкретное C++-представление
    // `trust::Range<Elem>` (шаблон по элементному типу) выводится из инициализатора-литерала
    // `..` в visit_RangeExpr. Голого C++-имени у `:Range` нет → объявление переменной эмитится
    // как `auto`, инициализатор задаёт конкретный `trust::Range<Elem>`. (Модель: кортеж `:Tuple`.)
    if (canonical == m_ectx.m_ctx.types().getType(type_category::Range)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Пользовательский нативный шаблон-тип (NativeTemplateTypeData, инстанциация `std::pair<A,B>`):
    // C++-имя из данных типа (не хардкод), аргументы - рекурсивно. Инклуд - из preprocIncludes
    // (механизм №1, on-use). Встроенные контейнеры (`vector`/`array` → `:Array`) сюда НЕ попадают -
    // они резолвятся в Array-структуру и обслуживаются веткой Array ниже.
    if (m_ectx.m_ctx.types().isNativeTemplateType(canonical)) {
        const std::string_view cppTpl = m_ectx.m_ctx.types().nativeTemplateCppName(canonical);
        const auto& args = m_ectx.m_ctx.types().nativeTemplateArgs(canonical);
        std::string result(cppTpl);
        result += "<";
        bool first = true;
        for (const TypeId a : args) {
            if (!first) {
                result += ", ";
            }
            first = false;
            std::string ac;
            if (a != INVALID_TYPE_ID) {
                if (auto en = resolveCppTypeId(a, "NativeTemplate.Arg")) { // рекурсивно (const-метод)
                    ac = std::move(en->first);
                }
            }
            if (ac.empty()) {
                ac = "std::any";
            }
            result += ac;
        }
        result += ">";
        recordUsedType(canonical); // включит preprocIncludes (инклуд шаблона, on-use)
        return std::make_pair(wrapRefKind(std::move(result)), m_ectx.m_ctx.types().getPreprocInclude(canonical));
    }
    // Forward-объявление НАТИВНОГО класса (NativeClassTypeData): C++-имя из данных типа.
    // C++-struct НЕ генерируется (класс определён в заголовке); инклуд - on-use из preprocIncludes.
    if (m_ectx.m_ctx.types().isNativeClassType(canonical)) {
        const std::string_view cppName = m_ectx.m_ctx.types().nativeClassCppName(canonical);
        if (cppName.empty()) {
            return std::nullopt;
        }
        recordUsedType(canonical); // включит @[include] (инклуд класса, on-use)
        return std::make_pair(wrapRefKind(std::string(cppName)), m_ectx.m_ctx.types().getPreprocInclude(canonical));
    }
    // Параметризованный Array<Elem> (структурный, ArrayTypeData): конкретный C++-шаблон
    // `std::vector<ElemCpp>` (mutable) или `std::array<ElemCpp,N>` (константная/фиксированная).
    // ElemCpp - элементный тип из ArrayTypeData (рекурсивно через resolveCppTypeId).
    if (m_ectx.m_ctx.types().isArrayType(canonical)) {
        const TypeId elem = m_ectx.m_ctx.types().arrayElementType(canonical);
        std::string elemCpp;
        if (elem != INVALID_TYPE_ID) {
            if (auto en = resolveCppTypeId(elem, "Array.Element")) { // рекурсивно (const-метод)
                elemCpp = std::move(en->first);
            }
        }
        if (elemCpp.empty()) {
            elemCpp = "std::any";
        }
        const bool cst = testFlag(type_id, SymbolFlag::Const); // kConstFlag-бит в TypeId (`:Array^`), как у любых типов
        if (cst) {
            uint64_t n = 0;
            const auto& dims = m_ectx.m_ctx.types().arrayDimensions(canonical);
            if (!dims.empty()) {
                n = dims.front();
            }
            recordRequiredInclude("#include <array>");
            return std::make_pair(wrapRefKind("std::array<" + elemCpp + ", " + std::to_string(n) + ">"), std::string_view{});
        }
        recordRequiredInclude("#include <vector>");
        return std::make_pair(wrapRefKind("std::vector<" + elemCpp + ">"), std::string_view{});
    }
    // Массив `:Array` - абстрактный универсальный тип (как :Dict/:Range): конкретное
    // C++-представление `std::vector<Elem>` выводится из инициализатора-литерала (visit_ArrayInit).
    // Голого C++-имени нет → объявление переменной эмитится как `auto`.
    if (canonical == m_ectx.m_ctx.types().getType(type::Array)) {
        return std::make_pair(std::string("auto"), std::string_view{});
    }
    // Константность (kConstFlag) - ортогональный квалификатор, учитывается здесь: префикс `const `
    // добавляется к базовому имени (каноника снимает бит, поэтому getCppTypeName(canonical) его
    // не видит). Для пользовательских алиасов const добавляется к trust-имени.
    const bool isConst = testFlag(type_id, SymbolFlag::Const);

    // Спецправило: узкая строка как указатель на константные данные
    // (`fmt: @[reftype(ptr)@] StrChar^`) - это C-строка `const char*` (а НЕ `const std::string*`).
    // Такой тип совместим с C-функциями, принимающими форматную строку (printf и др.); при
    // вызове StrChar-аргумент конвертируется в .c_str() (см. visit_CallExpr).
    if (isConst && getRefType(getKindFromId(canonical)) == RefType::kPtr) {
        TypeKind baseKind = withRefType(getKindFromId(canonical), RefType::kValue);
        TypeId baseId = replaceKind(canonical, baseKind);
        if (m_ectx.m_ctx.types().getCanonicalTypeId(baseId) == m_ectx.m_ctx.types().getType(type::StrChar)) {
            return std::make_pair(std::string("const char*"), m_ectx.m_ctx.types().getPreprocInclude(canonical));
        }
    }

    // Базовое trust-имя типа по реестру (без ref-битов): displayName из вызывающего кода может
    // описывать поле/контекст, а не сам тип, поэтому для именованных пользовательских типов
    // берём имя дескриптора (алиас/record/enum/variant). Ссылочный вид применяется wrapRefKind.
    const auto registryTypeName = [&]() -> std::string {
        const TypeId bare = m_ectx.m_ctx.types().getPointeeType(type_id);
        const TypeDescriptor* d = m_ectx.m_ctx.types().lookup(bare);
        return (d != nullptr && !d->name.empty()) ? std::string(d->name) : std::string(displayName);
    };

    // Enum-тип (Group::kEnums, EnumTypeData): C++-имя - манглинг trust-имени (самодостаточная
    // struct, объявленная visit_EnumDecl); у типа нет единого preproc-include.
    if (m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kEnum)) {
        std::string name = utils::name_to_cpp(registryTypeName());
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(wrapRefKind(std::move(name)), std::string_view{});
    }
    // Variant-тип (Group::kVariants, VariantTypeData): C++-имя - манглинг trust-имени (struct c_Value).
    if (m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kVariant)) {
        std::string name = utils::name_to_cpp(registryTypeName());
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(wrapRefKind(std::move(name)), std::string_view{});
    }
    // Пользовательский Record-тип (Struct/Class, Group::kStructs/kClassDefs, RecordTypeData):
    // обычный/абстрактный шаблон - манглинг trust-имени (`Point` → `c_Point`); инстанциация
    // record-шаблона (`Box<Int32>`) - единый рендер через реестр (`c_Box<int32_t>`).
    if (m_ectx.m_ctx.types().isRecordType(canonical)) {
        std::string name;
        const auto* rd = m_ectx.m_ctx.types().recordData(canonical);
        if (rd != nullptr && rd->templateOf != INVALID_TYPE_ID) {
            // getCppTypeName сам применяет ref-вид, поэтому берём инстанциацию БЕЗ ref-битов
            // (иначе `shared<Box<T>>` → двойная обёртка trust::Shared<trust::Shared<...>>);
            // вид ссылки добавит wrapRefKind ниже.
            auto n = m_ectx.m_ctx.types().getCppTypeName(m_ectx.m_ctx.types().getPointeeType(canonical));
            if (!n) {
                return std::nullopt;
            }
            name = std::move(*n);
            // Записать заголовки типовых аргументов рекурсивно (напр. <cstdint> для c_Box<int32_t>):
            // getCppTypeName их не отмечает (только рендерит), а без include инстант-тип не соберётся.
            for (const TypeId a : rd->templateArgs) {
                (void)resolveCppTypeId(a, "RecordTemplate.Arg");
            }
        } else {
            name = utils::name_to_cpp(registryTypeName());
        }
        if (isConst) {
            name = "const " + name;
        }
        recordUsedType(canonical);
        return std::make_pair(wrapRefKind(std::move(name)), std::string_view{});
    }

    auto cpp_name = m_ectx.m_ctx.types().getCppTypeName(canonical);
    if (!cpp_name) {
        return std::nullopt;
    }
    std::string_view include = m_ectx.m_ctx.types().getPreprocInclude(canonical);

    // МЕХАНИЗМ №1 - ПО ТИПУ: отмечаем тип как использованный (не файлы!). Инклуды из собранных
    // типов формируются ПОСЛЕ обхода AST (collectTypeIncludes), а не в момент резолва.
    recordUsedType(canonical);

    // Пользовательский алиас (зарегистрирован семантикой) сохраняет своё trust-имя в коде;
    // встроенные типы и встроенные алиасы (Integer, String, Char...) маппятся на каноническое
    // C++-имя (int64_t, std::string...). Include всегда берётся у канонического (базового) типа.
    // Признак пользовательского типа - явный (isUserDefinedType), а не по sourceRange.
    // ВАЖНО: структурный ссылочный узел (RefTypeData, напр. unique<T,D>) НЕ алиас - его
    // C++-имя уже построено getCppTypeName (trust::Unique<T,D>); нельзя мапить по displayName.
    const bool ref_node = m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kRefType);
    // Типовой параметр шаблона (`T`) - НЕ пользовательский алиас: C++-имя = имя параметра
    // (рендерит getCppTypeName), а не манглинг trust-имени (иначе `T` → `c_T`/displayName).
    const bool template_param = m_ectx.m_ctx.types().isTemplateParamType(type_id);
    // Функциональный тип (значение-лямбда) - НЕ пользовательский алиас: C++-имя строит
    // getCppTypeName (`std::function<...>`), а не манглинг trust-имени/displayName.
    const bool function_node = m_ectx.m_ctx.types().isTypeDataKind(canonical, TypeDataKind::kFunction);
    if (!ref_node && !function_node && !template_param && refKind == RefType::kValue && m_ectx.m_ctx.types().isUserDefinedType(type_id)) {
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
} // namespace trust
