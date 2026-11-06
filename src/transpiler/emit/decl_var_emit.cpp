#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/kind_dispatch.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_data.hpp"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>

namespace trust {

namespace {

/// Тип deleter'а объявления: у `unique` он входит в identity типа (`RefTypeData`), иначе (shared) —
/// факт объявления (`VarDecl::deleterType`). Резолва имени по тексту нет (R39).
[[nodiscard]] TypeId declarationDeleterType(const CodegenServices& services, const VarDecl& var) {
    const RefTypeData* refType = services.readTypes().store().getTypeDataAs<RefTypeData>(var.inferredType.getTypeId());
    if (refType != nullptr && refType->deleterType != INVALID_TYPE_ID) {
        return refType->deleterType;
    }
    return var.deleterType();
}

} // namespace

bool DeclEmitter::declaredRefCppType(const VarDecl& var_node, QualType declared, std::string& cpp_type) {
    if (!declared.isValid()) {
        m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "reference type of '{}' is not materialized by semantic analysis",
                                 var_node.text());
        return false;
    }
    auto nm = m_driver.m_type.emitTypeName(declared.getTypeId());
    if (!nm || nm->empty()) {
        m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type for reference variable '{}'", var_node.text());
        return false;
    }
    cpp_type = std::move(*nm);
    return true;
}

bool DeclEmitter::resolveRefPointee(const VarDecl& var_node, QualType refType, std::optional<TypeId>& ref_base_id, std::string& ref_pointee_cpp) {
    const QualType pointee = m_ectx.m_services.readTypes().store().getPointeeType(refType);
    if (!pointee.isValid()) {
        m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "reference variable '{}' has no pointee type", var_node.text());
        return false;
    }
    ref_base_id = pointee.getTypeId();
    // Pointee C++-имя (для trust::make_unique<PoT> у эксклюзивного владения).
    auto pn = m_driver.m_type.emitTypeName(*ref_base_id);
    if (!pn || pn->empty()) {
        m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ pointee type for reference variable '{}'",
                                 var_node.text());
        return false;
    }
    ref_pointee_cpp = std::move(*pn);
    return true;
}

void DeclEmitter::generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx) {
    // Флаг линковки нативной библиотеки из @[link("имя")].
    m_driver.m_type.collectLinkLib(var_node);
    // Зависимый C++-заголовок из @[include(\"header\")@] на нативной переменной/типе.
    m_driver.m_type.collectInclude(var_node);
    // Нативный импорт переменной: trust-имя — алиас внешнего C/C++-символа; собственный
    // C++-код не эмитится (forward-декларация обязана прийти из @[include]). Регистрируем
    // C++-имя для ссылок и выходим.
    if (var_node.isNativeImport()) {
        const std::string_view nativeName = m_ectx.m_services.declCppFacts().cppNameOf(var_node.declId());
        EXPECT(!nativeName.empty() && "native import requires a C/C++ name");
        return;
    }

    // C++-имя объявления: trust-имя в своей namespace-области (имя модуля в C++-символ не входит;
    // срез '%' у нативных имён).
    std::string var_name = utils::cppDeclName(var_node.text());
    if (var_name.empty()) {
        return;
    }

    // Семантический тип переменной материализован анализатором (VarDecl::inferredType);
    // кодоген не переприменяет квалификаторы аннотации/узла.
    const QualType declared = var_node.inferredType;
    const RefType var_rt = declared.isValid() ? typeRefOf(declared.getTypeId()) : RefType::kValue;
    const bool ref_wrapper = (var_rt == RefType::kShared || var_rt == RefType::kWeak || var_rt == RefType::kLocker);
    const bool ref_unique = (var_rt == RefType::kUnique);
    const AttrPool& attrs = m_ectx.m_services.attrs();

    // Deleter внешнего ресурса: C++-имя D для `<Wrapper>::adopt(handle, D{})`. Тип D материализован
    // семантикой; кодоген имя НЕ резолвит.
    std::string deleter_cpp;
    const TypeId deleterType = declarationDeleterType(m_ectx.m_services, var_node);
    const AstNodeAttr* annotation = var_node.m_type ? var_node.m_type->as_attr() : nullptr;
    const bool hasDeleterAttr = var_node.has_attr(attrs, attr::Deleter) || (annotation != nullptr && annotation->has_attr(attrs, attr::Deleter));
    EXPECT((!hasDeleterAttr || deleterType.isValid()) && "deleter attribute must be materialized by semantic analysis (R39)");
    if (deleterType.isValid()) {
        const auto nm = m_driver.m_type.emitTypeName(deleterType);
        EXPECT(nm.has_value() && !nm->empty() && "deleter type must have a C++ name (semantic validated the policy type)");
        deleter_cpp = *nm;
    }

    // Determine type: типизированное имя → тип аннотации; нетипизированное → выведенный
    // анализатором конкретный тип (inferred join по истории присвоений), иначе std::any.
    std::string cpp_type;
    // Базовый (pointee) TypeId и его C++-имя для ссылочных переменных (shared/weak/unique):
    // нужны для trust::make_unique<PoT> у эксклюзивного владения и для уникальной эмиссии инициализатора.
    std::optional<TypeId> ref_base_id;
    std::string ref_pointee_cpp;
    const bool typed =
        var_node.m_type && (var_node.m_type->kind() == ParserToken::Kind::TypeName || var_node.m_type->kind() == ParserToken::Kind::RefMakeExpr ||
                            var_node.m_type->kind() == ParserToken::Kind::RefTakeExpr);
    if (typed) {
        if (ref_wrapper || ref_unique) {
            if (!declaredRefCppType(var_node, declared, cpp_type)) {
                return;
            }
            if (!resolveRefPointee(var_node, declared, ref_base_id, ref_pointee_cpp)) {
                return;
            }
        } else if (isNativeRefKind(var_rt)) {
            if (!declaredRefCppType(var_node, declared, cpp_type)) {
                return;
            }
        } else {
            // Для явной аннотации конкретный тип может быть вычислен семантикой: dtype тензора
            // (`:Tensor[dims]`) или сужение абстрактного `:Tuple`/`:Range`/`:Array` по
            // инициализатору. Если он есть - эмитим его; иначе - обычное имя аннотации.
            if (var_node.inferredType.getTypeId() != INVALID_TYPE_ID) {
                if (auto n = m_driver.m_type.emitTypeName(var_node.inferredType)) {
                    cpp_type = std::move(*n);
                }
            } else {
                cpp_type = m_driver.m_type.emitTypeNameForNode(var_node.m_type.get());
            }
            if (cpp_type.empty()) {
                return;
            }
        }
    } else {
        // Нетипизированная переменная: выведенный семантикой конкретный тип, либо ЯВНО помеченный
        // std::any (семантика маркирует Any для тип-less инициализаторов - тип-имя, embed, вызов
        // с неизвестным результатом, отрицательный литерал - и для forward-объявлений без типа).
        // Any - обычный выводимый тип → эмитим единообразно emitTypeName(inferred). INVALID у
        // переменной - ошибка вывода: тихий fallback на std::any запрещён (AGENTS rule 5).
        QualType inferred = var_node.inferredType;
        // Fallback на таблицу символов запрещён: тип обязан быть материализован семантикой на
        // узле (включая forward-объявления, завершённые определением). Отсутствие - дефект
        // материализации; ниже выдаётся диагностика (тихий std::any недопустим, AGENTS п.5).
        if (!inferred.isValid()) {
            if (var_node.m_initializer) {
                m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "unable to infer type for variable '{}'", var_node.text());
            } else {
                m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            }
            return;
        }
        // Ссылочная переменная без явного типа (`&& x := 5`): inferred уже несёт полный ссылочный тип
        // (semantic обернул pointee видом из префиксного сигла). Базу (pointee) берём из типа;
        // отсутствие pointee — нарушение инварианта анализа, а не повод подставить сам тип.
        if (ref_wrapper || ref_unique) {
            if (!resolveRefPointee(var_node, inferred, ref_base_id, ref_pointee_cpp)) {
                return;
            }
        }
        std::optional<std::string> name = m_driver.m_type.emitTypeName(inferred);
        if (!name || name->empty()) {
            m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError, "unable to generate C++ type '{}'", type_generic::Any);
            return;
        }
        cpp_type = std::move(*name);
    }

    // Инклуды типа не нужны здесь: emitTypeName отметил тип (m_ectx.m_accumulators.usedTypes), инклуды будут
    // сформированы из них ПОСЛЕ обхода AST (collectTypeIncludes).

    // Маркер `_` — «объявить без значения»; экспорт без реального инициализатора недопустим.
    const VarInitKind initKind = var_node.initKind();
    const bool noneInit = initKind == VarInitKind::Uninitialized || initKind == VarInitKind::DeferredInit;

    // Экспорт (`@[export("...")]`): точное C/C++-имя; trust-манглинг и модульная изоляция не
    // применяются. C++-имя квалифицированное — открываем namespace-сегменты вокруг определения.
    const std::string nativeName{m_ectx.m_services.declCppFacts().cppNameOf(var_node.declId())};
    std::vector<std::string> nativeNamespaces;
    if (!nativeName.empty()) {
        // Инвариант уже проверен при разборе/семантике (`@[export]` требует реального значения);
        // кодоген не повторяет пользовательскую диагностику — нарушение контракта AST = ICE.
        EXPECT(!noneInit && "export without an initializer must be rejected before codegen");
        if (cppLinkageOf(nativeName) == CppLinkage::C && requiresCppLinkage(cpp_type)) {
            m_ectx.m_services.report(var_node.range(), diag::DiagId::ParseError,
                                     "C-linkage export '{}' requires a C-compatible type (no C++ namespaces/templates)", var_node.text());
            return;
        }
        // Разбор `ns::sub::name` — общий с функцией/типом (`splitCppName`).
        const CppQualifiedParts parts = splitCppName(nativeName);
        nativeNamespaces = parts.namespaces;
        var_name = parts.name;
        for (const std::string& ns : nativeNamespaces) {
            m_ectx.m_services.source().output_append(output_idx, cppNamespaceOpen(ns, /*inlineForm=*/false));
        }
        if (cppLinkageOf(nativeName) == CppLinkage::C) {
            m_ectx.m_services.source().output_append(output_idx, "extern \"C\" {\n");
        }
    }

    MapperScope scope(m_ectx.m_services.source(), var_node.range(), output_idx, m_ectx.m_mode.forwardDeclOnly || m_ectx.m_output.selfHeaderInclude.empty());
    // Константность ОБЪЯВЛЕНИЯ переменной - attr::ReadOnly на узле ('^' на имени или
    // @[readonly@]). Became-const (`x := 42; x^ += 1;`) — flow-факт DA-прохода; декларация
    // остаётся не-const. Признак на узле = const «в типе» объявления.
    // Префикс влияет на смещение имени в выводе (source-map): имя идёт после "<prefix> <cpp_type> ".
    std::string prefix;
    if (var_node.has_attr(m_ectx.m_services.attrs(), attr::ReadOnly)) {
        prefix += "const ";
    }
    if (var_node.has_attr(m_ectx.m_services.attrs(), attr::ThreadLocal)) {
        prefix += "thread_local ";
    }
    // Экспорт readonly: у namespace-scope const внутреннее сцепление — нужен explicit extern.
    if (!nativeName.empty() && !prefix.empty()) {
        prefix = "extern " + prefix;
    }
    // Forward-объявление `x:Type := ...;` → C++ extern-декларация переменной (объявление без
    // определения); иначе - определение с инициализатором. Смещение имени в выводе зависит
    // от наличия префикса "extern " (source-map).
    // Маркер `_` в позиции инициализатора (`x := _;` / `x:Type := _;`) - «объявить без значения»,
    // не инициализатор-значение: кодоген эмитит определение без инициализатора `T c_x;`
    // (запись до чтения гарантирует анализатор). Проверяем по kind (Ident) ДО text(), т.к.
    // text() требует source-терм, а ручные тестовые узлы (RangeExpr/CallExpr и т.п.) его не имеют.
    uint32_t namePrefixLen = static_cast<uint32_t>(prefix.length()) + static_cast<uint32_t>(cpp_type.length()) + 1;
    if (!var_node.m_initializer || m_ectx.m_mode.forwardDeclOnly) {
        const std::string_view externPrefix = "extern ";
        m_ectx.m_services.source().output_append(output_idx, std::string(externPrefix) + prefix + cpp_type + " " + var_name + ";");
        namePrefixLen += static_cast<uint32_t>(externPrefix.size());
    } else {
        // Reference-wrapper (trust::Shared/Weak/Take): explicit-конструкторы, поэтому инициализируем
        // конструкторным стилем `cpp_type name(<init>);` (покрывает и создание нового объекта
        // `Shared<int>(5)`, и копирование из существующей ссылки). Прочие типы - обычной
        // инициализацией.
        // Владеющий ресурс с deleter (@[deleter(D)]): создание через `<Wrapper>::adopt(handle, D{})`
        // (перенос владения сырым handle). Для unique D - часть типа, для shared D стирается.
        const bool adopt_init = !deleter_cpp.empty() && (ref_wrapper || ref_unique) && !noneInit;
        if (adopt_init) {
            m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = " + cpp_type + "::adopt(");
            m_driver.emitExpr(var_node.m_initializer.get());
            m_ectx.m_services.source().output_append(output_idx, ", " + deleter_cpp + "{});");
        } else if (ref_wrapper) {
            // Инициализатор - адресная операция `& shared_var` (RefMakeExpr, даёт weak-временное
            // `trust::Weak<...>(c_x)`): конструкторный стиль `Weak<...> c_w(Weak<...>(c_x))` дал бы
            // most-vexing-parse (тип-внутри-типа) → используем копирующую `cpp_type name = <init>;`
            // (с временного Weak это валидно). Прочие - конструкторный стиль `name(<init>);`.
            const bool addrInit = var_node.m_initializer && var_node.m_initializer->kind() == ParserToken::Kind::RefMakeExpr;
            if (addrInit) {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_services.source().output_append(output_idx, ";");
            } else {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + "(");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_services.source().output_append(output_idx, ");");
            }
        } else if (ref_unique) {
            // Эксклюзивное владение (монопольное). Две формы:
            //  * unique без deleter'а -> trust::StaticUnique<PoT> (inline, zero-cost):
            //    создаётся конструктором со значением `trust::StaticUnique<PoT> name(<init>);`.
            //  * unique с deleter'ом -> trust::Unique<PoT, D> (указательная обёртка).
            // Если инициализатор - сырая C++-вставка `{% %}` (напр. конверсия `$s.to_unique()`),
            // НЕ оборачиваем: вставка уже даёт значение нужного типа.
            const bool embedInit = var_node.m_initializer && var_node.m_initializer->kind() == ParserToken::Kind::EmbedExpr;
            // Имя обёртки StaticUnique - из реестра (не хардкод строкой).
            std::string staticUniqueName;
            if (const auto sid = m_ectx.m_services.readTypes().findType(type::StaticUnique); sid.has_value()) {
                if (auto nm = CppTypeRenderer(m_ectx.m_services.readTypes(), m_ectx.cppFactsView()).render(*sid)) {
                    staticUniqueName = *nm;
                }
            }
            const bool staticUnique = !staticUniqueName.empty() && cpp_type.rfind(staticUniqueName, 0) == 0;
            if (embedInit) {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_services.source().output_append(output_idx, ";");
            } else if (staticUnique) {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + "(");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_services.source().output_append(output_idx, ");");
            } else {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = trust::make_unique<" + ref_pointee_cpp + ">(");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_services.source().output_append(output_idx, ");");
            }
        } else {
            // Локальная переменная без инициализатора (`x:Type := _;`): маркер `_` в позиции
            // инициализатора означает «объявить без значения». Эмитим определение без
            // инициализатора `T c_x;` (НЕ extern). Анализатор гарантирует запись до чтения,
            // иначе - диагностика Error (чтение неинициализированной переменной).
            const bool noInitDef = noneInit; // var_node.m_initializer - маркер `_`
            if (noInitDef) {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + ";");
            } else {
                m_ectx.m_services.source().output_append(output_idx, prefix + cpp_type + " " + var_name + " = ");
                m_driver.emitExpr(var_node.m_initializer.get());
                m_ectx.m_services.source().output_append(output_idx, ";");
            }
        }
        // Trust-условия переменной (--solver-mode=assert): проверка сразу после объявления/инициализации.
        if (!var_node.m_trust.empty()) {
            m_ectx.m_services.source().output_append(output_idx, "\n"); // проверка - на отдельной строке
            m_driver.m_contract.emitTrustChecks(var_node.m_trust);
        }
        // Тип-условия (тип с trust_assert): при создании значения типа (объявление переменной
        // этого типа) проверяем значение - имя типа подставляется как значение переменной.
        // Источник условий - узел декларации типа (VarDecl::m_typeDecl, ставит семантика);
        // trust-имя типа - из аннотации переменной. Без копий и без карт.
        if (var_node.m_typeDecl && var_node.m_initializer && !noneInit && !var_node.m_typeDecl->m_trust.empty()) {
            m_driver.m_contract.emitTypeTrustChecks(var_node.m_typeDecl->m_trust, requiredText(*var_node.m_type), var_name);
        }
    }

    // Добавляем маппинг имени переменной для hover-ссылок.
    // Диапазон trust-имени: nameRange() хранит диапазон реального имени (важно при
    // макро-раскрытии, где range() самого узла - оператор). Fallback - имя в начале range().
    MapperRange trustNameRange = var_node.nameRange();
    // Fallback на range() узла (имя в начале range) - только если range() валиден, иначе
    // makeLoc с невалидным fileIdx упадёт. Финальную проверку делает mapDeclaredName.
    if (trustNameRange.isInvalid() && !var_node.range().isInvalid()) {
        MapperLocation trustNameBegin = m_ectx.m_services.source().makeLoc(var_node.range().begin.fileIdx(), var_node.range().begin.offset());
        MapperLocation trustNameEnd =
            m_ectx.m_services.source().makeLoc(trustNameBegin.fileIdx(), trustNameBegin.offset() + static_cast<uint32_t>(var_node.text().size()));
        trustNameRange = MapperRange(trustNameBegin, trustNameEnd);
    }
    // Имя выводится сразу после префикса "<cpp_type> " (или "extern <cpp_type> " для forward).
    // trust-имя в маппинге - исходное (var_node.text(), для нативных с '%'), cpp-имя - манглированное.
    mapDeclaredName(output_idx, trustNameRange, namePrefixLen, var_node.text(), var_name);

    if (!nativeName.empty()) {
        if (cppLinkageOf(nativeName) == CppLinkage::C) {
            m_ectx.m_services.source().output_append(output_idx, "\n}\n");
        }
        for (std::size_t i = 0; i < nativeNamespaces.size(); ++i) {
            m_ectx.m_services.source().output_append(output_idx, "\n" + cppNamespaceClose(/*inlineForm=*/false));
        }
    }
}

} // namespace trust
