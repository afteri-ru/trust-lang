#include "transpiler/transpiler.hpp"
#include "diag/flag_values.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/declaration_ids.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/partial_switch.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

namespace trust {

// Полнота контракта exhaustive consumer (ast/kind_dispatch.hpp): новый kind в PARSER_TOKEN_KINDS
// без обработчика в CppTranspiler не компилируется.
static_assert(hasAllKindHandlers<CppTranspiler>(), "CppTranspiler обязан реализовать visit_<Kind> для каждого kind");

// Регистрирует на static-init feature-флаги, которыми владеет компонент transpiler
// (Comments/Assert/Backtrace), включая их дефолты (включены). ParseError - базовая
// диагностика, регистрируется в diag/applyRegisteredDiagnostics.
namespace {
struct TranspilerDiagnosticsRegistrar {
    TranspilerDiagnosticsRegistrar() {
        registerDiagnostics([](Options& opts) {
            // Авто-регистрация флагов-диагностик (`opts.add_flag(...)` + значение по умолчанию,
            // если задано): Assert и Backtrace управляются через -W/-Wno-.
            TRANSPILER_W_FLAG_LIST(TRANSPILER_FL_ADD_OPT)
            // Comments — ПОВЕДЕНЧЕСКИЙ флаг компилятора (НЕ диагностика): управляется через
            // -fcomments/-fno-comments, а не -Wcomments/-Wno-comments (add_flag_nonw → w_diagnostic=false).
            opts.add_flag_nonw(trust::transpiler::FlagKind::Comments);
            opts.set_flag_validator(trust::transpiler::FlagKind::Comments, [](std::string_view v) { return parseBoolFlagValue(v).has_value(); });
            // Overflow-check - ПОВЕДЕНЧЕСКИЙ флаг (НЕ диагностика): детекция знакового
            // целочисленного переполнения в арифметике (+,-,* и +=,-=,*=) с runtime-IntMinus.
            // Управляется через -foverflow-check/-fno-overflow-check (add_flag_nonw → w_diagnostic=false).
            opts.add_flag_nonw(trust::transpiler::FlagKind::OverflowCheck);
            opts.set_flag_validator(trust::transpiler::FlagKind::OverflowCheck, [](std::string_view v) { return parseBoolFlagValue(v).has_value(); });
            // Проверки `assert`/`verify` включены по умолчанию (безопасность по умолчанию);
            // отключаются через `-Wno-assert`.
            opts.set_enabled(transpiler::FlagKind::Assert, true);
            // Стек вызовов при провале assert/verify печатается ПО УМОЛЧАНИЮ; отключение - -Wno-backtrace.
            opts.set_enabled(transpiler::FlagKind::Backtrace, true);
            // Комментарии в C++-выводе выводятся по умолчанию; подавление - через -fno-comments
            // (флаг «comments» выключен = подавлять). См. TRANSPILER_FLAG_LIST(Comments).
            opts.set_enabled(transpiler::FlagKind::Comments, true);
            // Контроль целочисленного переполнения включён по умолчанию (безопасность по умолчанию);
            // отключается через -fno-overflow-check.
            opts.set_enabled(transpiler::FlagKind::OverflowCheck, true);
        });
    }
};
const TranspilerDiagnosticsRegistrar kTranspilerDiagnostics;
} // namespace

analysis::BehavioralModes codegenModes(analysis::BehavioralModes modes, const Options& opts) {
    modes.overflowCheck = opts.is_enabled(transpiler::FlagKind::OverflowCheck);
    modes.comments = opts.is_enabled(transpiler::FlagKind::Comments);
    modes.backtrace = opts.is_enabled(transpiler::FlagKind::Backtrace);
    return modes;
}

CppTranspiler::CppTranspiler(CodegenServices services, analysis::BehavioralModes behavioral)
: m_ectx(std::move(services), std::move(behavioral))
, m_type(m_ectx, *this)
, m_decl(m_ectx, *this)
, m_stmt(m_ectx, *this)
, m_expr(m_ectx, *this)
, m_contract(m_ectx, *this) {
}

bool CppTranspiler::isSuppressedDoc(ParserToken::Kind k) const {
    // Флаг «comments» включён = комментарии ВЫВОДЯТСЯ; подавление - флаг выключен (-fno-comments).
    return k == ParserToken::Kind::Document && !m_ectx.m_behavioral.comments;
}

// Эмитит документирующий комментарий, привязанный к узлу объявления (AstNodeBase::documentation).
// Строками с текущим отступом; `##`/`##<` нормализуются в C++-валидные `///`/`///<` (как visit_Document).
// Подавляется флагом -fno-comments (Comments выключен = не выводить), как и sibling-Document.
void CppTranspiler::emitDocumentation(const AstNodeBase& node, MapperFile output_idx) {
    if (node.documentation.empty() || !m_ectx.m_behavioral.comments) {
        return;
    }
    // Документирующий комментарий всегда с новой строки: если текущая позиция НЕ в начале строки
    // (напр. после '}' предыдущего оператора), добавляем перевод ПЕРЕД комментарием. Отступ (только
    // пробелы после '\n') строкой не считается — лишний перевод не дублируется.
    const std::string_view body = m_ectx.m_services.source().output_body(output_idx);
    if (!outputAtLineStart(body)) {
        m_ectx.m_services.source().output_append(output_idx, "\n");
    }
    size_t pos = 0;
    const std::string_view all = node.documentation;
    while (pos <= all.size()) {
        const size_t nl = all.find('\n', pos);
        const std::string_view line = (nl == std::string_view::npos) ? all.substr(pos) : all.substr(pos, nl - pos);
        m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        if (line.starts_with("##")) {
            m_ectx.m_services.source().output_append(output_idx, "///");
            m_ectx.m_services.source().output_append(output_idx, line.substr(2));
        } else {
            m_ectx.m_services.source().output_append(output_idx, line);
        }
        m_ectx.m_services.source().output_append(output_idx, "\n");
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
}

void CppTranspiler::configureModuleUnit(std::string include, const std::vector<const AstNodeBase*>& published) {
    m_ectx.m_output.ownHeaderInclude = "#include \"" + include + "\"";
    m_ectx.m_output.selfHeaderInclude = std::move(include);
    m_ectx.m_declFacts.publishedByHeader.clear();
    for (const AstNodeBase* node : published) {
        if (node == nullptr) {
            continue;
        }
        const DeclId id = declarationIdOf(*node);
        EXPECT(id.isValid() && "published declaration must carry DeclId (R23)");
        m_ectx.m_declFacts.publishedByHeader.insert(id);
    }
}

void CppTranspiler::generateToFile(const HirModule& hir, MapperFile output_idx) {
    const AstNodeBase* prev = nullptr;
    for (const auto& node : hir.nodes()) {
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
    if (!hir.nodes().empty()) {
        m_ectx.m_services.source().output_append(output_idx, "\n");
    }
    // МЕХАНИЗМ №1 - ПО ТИПУ: только ПОСЛЕ полного обхода AST формируем инклуды из собранных
    // типов (m_ectx.m_accumulators.usedTypes), затем препендим все директивы (emitCollectedIncludes).
    // POD-проверки инстанциаций Struct-шаблонов уже собраны при первом использовании типа
    // (порядок объявлений) и эмитятся ПОСЛЕ инклудов (в конце файла).
    m_type.collectTypeIncludes();
    m_type.emitCollectedIncludes(output_idx);
    m_type.emitStructPodAsserts(output_idx);
    // Единица трансляции модуля включает СВОЙ заголовок: типы интерфейса определены там, а в этом
    // TU они намеренно не повторяются (`configureModuleUnit`).
    if (!m_ectx.m_output.selfHeaderInclude.empty()) {
        m_ectx.m_services.source().output_prepend_leading(output_idx, "#include \"" + m_ectx.m_output.selfHeaderInclude + "\"\n\n");
    }
}

void CppTranspiler::generateHeaderToFile(const HirModule& hir, MapperFile output_idx, ModuleIndex module, const std::vector<AstNodePtr>& exports,
                                         const std::vector<std::string>& moduleIncludes, const std::set<DeclId>& publishedDecls, std::string ownHeaderName) {
    // Собственный заголовок: include типа из ЭТОГО модуля в его же интерфейс не эмитится.
    m_ectx.m_output.ownHeaderInclude = ownHeaderName.empty() ? std::string() : "#include \"" + ownHeaderName + "\"";
    m_ectx.m_output.currentModule = module;
    std::set<DeclId> exportTerms;
    for (const auto& exported : exports) {
        if (exported) {
            const DeclId id = declarationIdOf(*exported);
            EXPECT(id.isValid() && "exported declaration must carry DeclId (R23)");
            exportTerms.insert(id);
        }
    }
    if (!exportTerms.empty()) {
        // Публикуемый набор нужен эмиттеру members/free-функций: ссылка на модульный символ
        // ВНЕ набора (напр. `__::`) невыразима в заголовке шаблона/constexpr - это диагностика.
        m_ectx.m_declFacts.headerPublishedDecls = publishedDecls;
        // Namespace модуля открывает САМ эмиттер (группировка «вне/внутри конверта»): нативные
        // декларации и entry остаются вне него.
        m_ectx.m_mode.forwardDeclOnly = true;
        m_decl.emitModuleHeaderBody(hir.nodes(), exportTerms, output_idx);
        m_ectx.m_mode.forwardDeclOnly = false;
        m_ectx.m_declFacts.headerPublishedDecls.clear();
    }
    // Инклуды по использованным типам — тот же механизм, что у `.cppt` (собираются на эмиссии).
    m_type.collectTypeIncludes();
    m_type.emitCollectedIncludes(output_idx);
    // `#pragma once` и umbrella-заголовки РЕЭКСПОРТИРУЕМЫХ модулей — первыми в файле
    // (leading-буфер). Свои объявления фасада идут ниже.
    std::string leading = "#pragma once\n";
    if (!moduleIncludes.empty()) {
        leading += "\n";
        for (const auto& include : moduleIncludes) {
            leading += "#include \"" + include + "\"\n";
        }
    }
    leading += "\n";
    m_ectx.m_services.source().output_prepend_leading(output_idx, leading);
}

namespace {

/// Текст, с которого начнётся вывод блока (для пробела на одной строке). Прозрачные
/// синтетические обёртки (SemicolonStmt/ErrorExpr) собственного текста НЕ имеют -
/// разворачиваем ребёнка явно; узел без текста (нет WithText в классе) - пусто.
std::string_view blockLeadingText(const AstNodeBase& node) {
    if (node.kind() == ParserToken::Kind::SemicolonStmt) {
        const auto& semi = static_cast<const SemicolonStmt&>(node);
        return semi.m_expr ? blockLeadingText(*semi.m_expr) : std::string_view{};
    }
    if (node.kind() == ParserToken::Kind::ErrorExpr) {
        const auto& error = static_cast<const ErrorExpr&>(node);
        return error.m_original ? blockLeadingText(*error.m_original) : std::string_view{};
    }
    const HasText* text = node.as_text();
    return text != nullptr ? text->text() : std::string_view{};
}

} // namespace

void CppTranspiler::emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx) {
    // Перевод строки между блоками вставляется только если они в исходнике на разных
    // строках: строка конца prev != строка начала node. Если строки совпадают (блоки
    // намеренно на одной строке) - перевод строки не выводится, вместо него пробел.
    if (!prev) {
        return; // первый блок - перевод строки не нужен
    }
    const bool sameSourceLine = m_ectx.m_services.source().line(prev->range().end) == m_ectx.m_services.source().line(node.range().begin);
    if (!sameSourceLine) {
        m_ectx.m_services.source().output_append(output_idx, "\n");
    } else {
        emitSameLineSpace(blockLeadingText(node), output_idx);
    }
}

void CppTranspiler::emitSameLineSpace(std::string_view nextText, MapperFile output_idx) {
    // Для читаемости между блоками на одной строке ставим пробел, но не дублируем его,
    // если на границе уже есть пробельный символ (например, EMBED-содержимое с ведущими/
    // хвостовыми пробелами).
    const std::string_view body = m_ectx.m_services.source().output_body(output_idx);
    const bool prevEndsWithSpace = !body.empty() && (body.back() == ' ' || body.back() == '\t');
    const bool nextStartsWithSpace = !nextText.empty() && (nextText.front() == ' ' || nextText.front() == '\t');
    if (!prevEndsWithSpace && !nextStartsWithSpace) {
        m_ectx.m_services.source().output_append(output_idx, " ");
    }
}

void CppTranspiler::emitBlockChild(const AstNodeBase& child, MapperFile output_idx) {
    const size_t before = m_ectx.m_services.source().output_body(output_idx).size();
    if (!is_block_kind(child.kind())) {
        m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
    }
    if (child.isFilePrivate()) {
        // Файл-приватное объявление внутри области имён (`ns:: { ... }`) - та же изоляция
        // C++-имени, что и на верхнем уровне модуля (см. emitSequenceBody).
        ++m_ectx.m_output.hiddenNamespaceDepth;
        m_ectx.m_services.source().output_append(output_idx, "namespace {\n");
        emitDocumentation(child, output_idx);
        generateNodeToFile(child, output_idx);
        m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix() + "}\n");
        --m_ectx.m_output.hiddenNamespaceDepth;
    } else {
        emitDocumentation(child, output_idx);
        generateNodeToFile(child, output_idx);
    }
    if (m_ectx.m_services.source().output_body(output_idx).size() != before) {
        const std::string_view body = m_ectx.m_services.source().output_body(output_idx);
        if (body.empty() || body.back() != '\n') {
            m_ectx.m_services.source().output_append(output_idx, "\n");
        }
    }
}

void CppTranspiler::emitSequenceBody(const Sequence& node, MapperFile output_idx) {
    // Sequence, ScopeBlock or ModuleNode → walk body. Метки именованных блоков и goto
    // вставляются анализатором (lowering) как отдельные узлы LabelStmt/GotoStmt - здесь
    // только кодогенерация: каждый узел m_body эмитится по своему kind.

    const bool inBlock = m_ectx.m_output.indentLevel() > 0;
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
            emitBlockChild(*child, output_idx);
        } else {
            // На верхнем уровне (indent==0) - зеркалирование строк исходника.
            emitBlockSeparator(prev, *child, output_idx);
            if (child->isFilePrivate()) {
                // Файл-приватное объявление (вне интерфейса файла): C++-имя не должно совпадать
                // с одноимённым приватным объявлением другого файла (иначе duplicate symbol при
                // линковке) ⇒ anonymous namespace, как у `__::`. Порядок объявлений сохраняется;
                // документирующий комментарий остаётся при объявлении.
                ++m_ectx.m_output.hiddenNamespaceDepth;
                m_ectx.m_services.source().output_append(output_idx, "namespace {\n");
                emitDocumentation(*child, output_idx);
                generateNodeToFile(*child, output_idx);
                m_ectx.m_services.source().output_append(output_idx, "}\n");
                --m_ectx.m_output.hiddenNamespaceDepth;
            } else {
                emitDocumentation(*child, output_idx);
                generateNodeToFile(*child, output_idx);
            }
            prev = child.get();
        }
    }
}

// module-level определения (функции/типы/области) эмитятся на namespace как обычно; top-level
// операторы (вкл. `x := ...`) собираются и эмитятся телом синтезированной entry-функции
// `<модуль>__main__` (python-подобно: переменные скрипта - локальные в main). pipeline затем
// дописывает `int main(){ return <entry>(); }` (см. pipeline.cpp single-file-ветку).
void CppTranspiler::emitSingleFileModuleBody(const ModuleNode& node, MapperFile output_idx) {
    using Kind = ParserToken::Kind;
    // Виды узлов, остающиеся НА ВЕРХНЕМ УРОВНЕ (namespace): определения (функции/типы/области).
    // Всё прочее (VarDecl/вызовы/управление/...) - top-level операторы скрипта.
    const auto isModuleDefKind = [](Kind k) {
        // Частичный предикат множества: прочие kinds - top-level операторы скрипта (осознанно).
        TRUST_PARTIAL_ENUM_SWITCH_BEGIN
        switch (k) {
        case Kind::FuncDecl:
        case Kind::ModuleDecl:
        case Kind::EnumDecl:
        case Kind::StructDecl:
        case Kind::ClassDecl:
        case Kind::TypeDecl:
        case Kind::NameDecl:
        case Kind::ScopeBlock:
        case Kind::EmbedExpr: // {% %} - сырой C++ на namespace (напр. embed-`__main__`), не скрипт-оператор
            return true;
        default:
            return false;
        }
        TRUST_PARTIAL_ENUM_SWITCH_END
    };

    std::vector<const AstNodeBase*> scripts;
    const AstNodeBase* prev = nullptr;
    for (const auto& child : node.m_body) {
        if (!child) {
            continue;
        }
        if (isSuppressedDoc(child->kind())) {
            continue;
        }
        if (isModuleDefKind(child->kind())) {
            emitBlockSeparator(prev, *child, output_idx);
            emitDocumentation(*child, output_idx);
            generateNodeToFile(*child, output_idx);
            prev = child.get();
        } else {
            scripts.push_back(child.get());
        }
    }

    if (scripts.empty()) {
        // Чистая библиотека (ни entry, ни top-level операторов): ничего не генерируем.
        // pipeline в single-file-режиме выдаст диагностику (нет `<модуль>__main__` в .cppt).
        return;
    }

    // Синтезируем entry-функцию `<модуль>__main__` (её имя задал Pipeline через
    // configureSingleFile и совпадает с ожидаемым pipeline entry_func_name).
    const std::string entryName = m_ectx.m_unit.singleFileEntryName.empty() ? "trust_script__main__" : m_ectx.m_unit.singleFileEntryName;
    m_ectx.m_services.source().output_append(output_idx, "\nint " + entryName + "() {\n");
    m_ectx.m_output.scopeStack.push_back({/*indent=*/1});
    m_ectx.m_mode.inCppBlock = true;
    for (const AstNodeBase* child : scripts) {
        if (!child) {
            continue;
        }
        if (isSuppressedDoc(child->kind())) {
            continue;
        }
        emitBlockChild(*child, output_idx);
    }
    m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix() + "return 0;\n");
    m_ectx.m_mode.inCppBlock = false;
    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_services.source().output_append(output_idx, "}\n");
}

// Placeholder для нереализованных expression-only kinds: "{}" только в expression-контексте.
void CppTranspiler::emitPlaceholderExpr(MapperFile output_idx) {
    if (m_ectx.m_mode.exprDepth > 0) {
        m_ectx.m_services.source().output_append(output_idx, "{}");
    }
}

// Унифицированный вывод текста как вложенного выражения (только при m_mode.exprDepth>0).
void CppTranspiler::emitExprText(std::string_view text) {
    if (m_ectx.m_mode.exprDepth > 0) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, text);
    }
}

void CppTranspiler::generateNodeToFile(const AstNodeBase& node, MapperFile output_idx) {
    // KIND_DISPATCH_GUARD_BEGIN - внутри блока запрещены if/switch по kind() (см. cmake guard).
    // Единая диспетчеризация ПО KIND (ast/kind_dispatch.hpp, exhaustive consumer): каждый kind
    // реализован - эмиссия либо явный reject/skip; полнота набора проверена static_assert выше.
    // END - не узел: dispatchEmit падает (FAULT), тихого пропуска нет (AGENTS §5).
    m_ectx.m_output.out = output_idx;
    // В statement-контексте expression-kind допустим как standalone-выражение (так его видят
    // блоки и unit-тесты); контекстная проверка выполняется для обратного направления -
    // statement-kind в emitExpr.
    dispatchEmit(node, *this);
    // KIND_DISPATCH_GUARD_END
}

void CppTranspiler::emitExpr(const AstNodeBase* node) {
    // KIND_DISPATCH_GUARD_BEGIN - внутри блока запрещены if/switch по kind() (см. cmake guard).
    if (!node) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "{}");
        return;
    }
    // Единая диспетчеризация ПО KIND (ast/kind_dispatch.hpp, exhaustive consumer) - как и для statement.
    // Различие statement/expression - глубина m_ectx.m_mode.exprDepth: visit_<Kind> добавляет
    // mapStart/mapStop и ';' только на верхнем уровне (m_ectx.m_mode.exprDepth==0); вложенный
    // вызов emitExpr (m_ectx.m_mode.exprDepth>0) - только текст выражения (без ';' и map).
    ++m_ectx.m_mode.exprDepth;
    dispatchEmit(*node, *this, KindCategory::ExprOnly);
    --m_ectx.m_mode.exprDepth;
    // KIND_DISPATCH_GUARD_END
}

std::string CppTranspiler::renderExprToString(const AstNodeBase* node) {
    EXPECT(node != nullptr && "renderExprToString: null expression");
    const MapperFile savedOut = m_ectx.m_output.out;
    const int savedDepth = m_ectx.m_mode.exprDepth;
    const bool savedDefaultArg = m_ectx.m_mode.inDefaultArg;
    const std::string name = std::format("__trust_default_{}.cppt", m_exprRenderCounter++);
    MapperFile tmp = m_ectx.m_services.source().add_output(name);
    m_ectx.m_output.out = tmp;
    m_ectx.m_mode.exprDepth = 0;
    m_ectx.m_mode.inDefaultArg = true;
    m_ectx.m_services.source().suppressMapping();
    // RAII: при исключении внутри emitExpr состояние эмиссии и suppression обязаны быть сняты,
    // а scratch-буфер удалён — иначе следующий save_output увидит «артефакт без карты».
    struct RenderStateGuard {
        CppTranspiler& self;
        MapperFile savedOut;
        MapperFile tmp;
        int savedDepth;
        bool savedDefaultArg;
        ~RenderStateGuard() {
            self.m_ectx.m_output.out = savedOut;
            self.m_ectx.m_mode.exprDepth = savedDepth;
            self.m_ectx.m_mode.inDefaultArg = savedDefaultArg;
            self.m_ectx.m_services.source().resumeMapping();
            (void)self.m_ectx.m_services.source().output_drop(tmp);
        }
    } guard{*this, savedOut, tmp, savedDepth, savedDefaultArg};
    emitExpr(node);
    std::string text = m_ectx.m_services.source().output_result(tmp);
    return text;
}

void CppTranspiler::emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock, const std::string& beforeCloseLabel,
                                 const std::string& afterOpen) {
    std::vector<AstNodePtr> stmts;
    MapperRange blockRange;
    collectBodyStatements(body, stmts, blockRange);
    m_stmt.emitBlockBodyToFile(stmts, blockRange, output_idx, mapBlock, beforeCloseLabel, afterOpen);
}

// Блоки-обёртки: обход тела; Attr - не обход.
void CppTranspiler::visit_sequence(const Sequence& n) {
    emitSequenceBody(n, m_ectx.m_output.out);
}

// Return/Throw inline; Break/Continue → goto.
void CppTranspiler::emitJumpValue(std::string_view keyword, const JumpStmt& n) {
    // Синтетические jump-узлы lowering (void-return по имени функции) не имеют Term →
    // range невалиден → маппинг пропускается (нет исходного trust-текста).
    const MapperRange r = n.range();
    std::unique_ptr<MapperScope> scope;
    if (!r.isInvalid()) {
        scope = std::make_unique<MapperScope>(m_ectx.m_services.source(), r, m_ectx.m_output.out);
    }
    if (n.m_value) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, keyword);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, " ");
        emitExpr(n.m_value.get());
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ";");
    } else {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, keyword);
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ";");
    }
}

// AstNodeAttr-kind'ы, не генерируемые как statement → осознанный Skip (узел не производится).
void CppTranspiler::visit_Program(const AstNodeAttr& node) {
    // Program - корневой маркер, не узел AST (нет производителя).
    faultUnexpectedKind("CppTranspiler", node);
}

// Kind=Unimplemented: конвертер Term→Ast (convertForKind<Unimplemented>) не строит узел и выдаёт
// ошибку, поэтому такой узел в AST не появляется. Достижение кодогена - инвариант-нарушение.
void CppTranspiler::visit_Unimplemented(const AstNodeAttr& node) {
    faultUnexpectedKind("CppTranspiler", node);
}

// Kind=NotApplicable: узел никогда не строится (convertForKind<NotApplicable> → Fatal), в AST не
// появляется. Достижение кодогена - инвариант-нарушение.
void CppTranspiler::visit_NotApplicable(const AstNodeAttr& node) {
    faultUnexpectedKind("CppTranspiler", node);
}

// Kind=TypeSet: набор типов (`:A + :B`) допустим ТОЛЬКО как тип (определение/параметр/возврат).
// Узел доходит до кодогена лишь при использовании набора КАК ЗНАЧЕНИЯ → явная ошибка
// (без молчаливой пустой инициализации).
void CppTranspiler::visit_TypeSet(const Sequence& node) {
    m_ectx.m_services.report(node.range(), diag::DiagId::ParseError, "type set is allowed only as a type, not as a value");
}

void CppTranspiler::visit_ModuleDecl(const ModuleNode& n) {
    m_decl.visit_ModuleDecl(n);
}

void CppTranspiler::visit_VarDecl(const VarDecl& n) {
    m_decl.visit_VarDecl(n);
}

void CppTranspiler::visit_FuncDecl(const FuncDecl& n) {
    m_decl.visit_FuncDecl(n);
}

void CppTranspiler::visit_TypeDecl(const Binary& n) {
    m_decl.visit_TypeDecl(n);
}

void CppTranspiler::visit_NameDecl(const Binary& n) {
    m_decl.visit_NameDecl(n);
}

void CppTranspiler::visit_EnumDecl(const Sequence& n) {
    m_decl.visit_EnumDecl(n);
}

void CppTranspiler::visit_EnumMember(const Sequence& n) {
    m_decl.visit_EnumMember(n);
}

void CppTranspiler::visit_StructDecl(const RecordDecl& n) {
    m_decl.visit_StructDecl(n);
}

void CppTranspiler::visit_ClassDecl(const ClassDecl& n) {
    m_decl.visit_ClassDecl(n);
}

void CppTranspiler::visit_StructField(const Sequence& n) {
    m_decl.visit_StructField(n);
}

void CppTranspiler::visit_ScopeBlock(const ScopeBlock& n) {
    m_stmt.visit_ScopeBlock(n);
}

void CppTranspiler::visit_DestructureDecl(const DestructureDecl& n) {
    m_stmt.visit_DestructureDecl(n);
}

void CppTranspiler::visit_ReturnStmt(const JumpStmt& n) {
    m_stmt.visit_ReturnStmt(n);
}

void CppTranspiler::visit_ThrowStmt(const JumpStmt& n) {
    m_stmt.visit_ThrowStmt(n);
}

void CppTranspiler::visit_BreakStmt(const JumpStmt& n) {
    m_stmt.visit_BreakStmt(n);
}

void CppTranspiler::visit_ContinueStmt(const JumpStmt& n) {
    m_stmt.visit_ContinueStmt(n);
}

void CppTranspiler::visit_GotoStmt(const LabelRef& n) {
    m_stmt.visit_GotoStmt(n);
}

void CppTranspiler::visit_LabelStmt(const LabelRef& n) {
    m_stmt.visit_LabelStmt(n);
}

void CppTranspiler::visit_SemicolonStmt(const SemicolonStmt& n) {
    m_stmt.visit_SemicolonStmt(n);
}

void CppTranspiler::visit_LastResultCapture(const LastResultCapture& n) {
    m_stmt.visit_LastResultCapture(n);
}

void CppTranspiler::visit_ErrorExpr(const ErrorExpr& node) {
    // ErrorExpr создаётся diagnose-then-replace и означает уже сообщённую ошибку: кодоген после
    // ошибки не выполняется. Достижение здесь - нарушение инварианта, а не «тихий» пропуск.
    faultUnexpectedKind("CppTranspiler", node);
}

void CppTranspiler::visit_IfStmt(const IfStmt& n) {
    m_stmt.visit_IfStmt(n);
}

void CppTranspiler::visit_WhileStmt(const WhileStmt& n) {
    m_stmt.visit_WhileStmt(n);
}

void CppTranspiler::visit_DoWhileStmt(const DoWhileStmt& n) {
    m_stmt.visit_DoWhileStmt(n);
}

void CppTranspiler::visit_MatchingStmt(const MatchStmt& n) {
    m_stmt.visit_MatchingStmt(n);
}

void CppTranspiler::visit_WithStmt(const WithStmt& n) {
    m_stmt.visit_WithStmt(n);
}

void CppTranspiler::visit_AssignmentStmt(const AstNodeAttr& n) {
    m_stmt.visit_AssignmentStmt(n);
}

void CppTranspiler::visit_BlockStmt(const AstNodeAttr& n) {
    m_stmt.visit_BlockStmt(n);
}

void CppTranspiler::visit_ThenBlock(const AstNodeAttr& n) {
    m_stmt.visit_ThenBlock(n);
}

void CppTranspiler::visit_ElseBlock(const AstNodeAttr& n) {
    m_stmt.visit_ElseBlock(n);
}

void CppTranspiler::visit_WhileElseBlock(const AstNodeAttr& n) {
    m_stmt.visit_WhileElseBlock(n);
}

void CppTranspiler::visit_TryCatchStmt(const TryCatchStmt& n) {
    m_stmt.visit_TryCatchStmt(n);
}

void CppTranspiler::visit_CatchBlock(const CatchBlock& n) {
    m_stmt.visit_CatchBlock(n);
}

void CppTranspiler::visit_MatchingCase(const AstNodeAttr& n) {
    m_stmt.visit_MatchingCase(n);
}

void CppTranspiler::visit_MatchingElseBlock(const AstNodeAttr& n) {
    m_stmt.visit_MatchingElseBlock(n);
}

void CppTranspiler::visit_Attr(const Sequence& n) {
    m_expr.visit_Attr(n);
}

void CppTranspiler::visit_ArgNode(const ArgNode& n) {
    m_expr.visit_ArgNode(n);
}

void CppTranspiler::visit_AssignOp(const Binary& n) {
    m_expr.visit_AssignOp(n);
}

void CppTranspiler::visit_AppendStmt(const Binary& n) {
    m_expr.visit_AppendStmt(n);
}

void CppTranspiler::visit_MathOp(const Binary& n) {
    m_expr.visit_MathOp(n);
}

void CppTranspiler::visit_IncDecOp(const Binary& n) {
    m_expr.visit_IncDecOp(n);
}

void CppTranspiler::visit_BitwiseOp(const Binary& n) {
    m_expr.visit_BitwiseOp(n);
}

void CppTranspiler::visit_CompareOp(const Binary& n) {
    m_expr.visit_CompareOp(n);
}

void CppTranspiler::visit_LogicalOp(const Binary& n) {
    m_expr.visit_LogicalOp(n);
}

void CppTranspiler::visit_MemberAccess(const Binary& n) {
    m_expr.visit_MemberAccess(n);
}

void CppTranspiler::visit_ArrayAccess(const Binary& n) {
    m_expr.visit_ArrayAccess(n);
}

void CppTranspiler::visit_IntLiteral(const Literal& n) {
    m_expr.visit_IntLiteral(n);
}

void CppTranspiler::visit_RationalLiteral(const Literal& n) {
    m_expr.visit_RationalLiteral(n);
}

void CppTranspiler::visit_StrChar(const Literal& n) {
    m_expr.visit_StrChar(n);
}

void CppTranspiler::visit_StrWide(const Literal& n) {
    m_expr.visit_StrWide(n);
}

void CppTranspiler::visit_FloatLiteral(const Literal& n) {
    m_expr.visit_FloatLiteral(n);
}

void CppTranspiler::visit_ContextMacro(const ContextMacro& n) {
    m_expr.visit_ContextMacro(n);
}

void CppTranspiler::visit_CheckAreaStmt(const CheckAreaStmt& n) {
    // Маркер @__CHECK_AREA__ должен быть удалён анализатором (NameResolutionPass).
    // Если он дожил до кодогенерации - ошибка логики, а не тихий пропуск.
    (void)n;
    FAULT("CheckAreaStmt reached the transpiler (must be removed by the analyzer)");
}

void CppTranspiler::visit_DebugStmt(const DebugStmt& n) {
    // Маркеры @__DEBUG__/@__DEBUG_SCOPE__ должны быть удалены анализатором (NameResolutionPass).
    // Если они дожили до кодогенерации - ошибка логики, а не тихий пропуск.
    (void)n;
    FAULT("DebugStmt reached the transpiler (must be removed by the analyzer)");
}

void CppTranspiler::visit_EmbedExpr(const RawText& n) {
    m_expr.visit_EmbedExpr(n);
}

void CppTranspiler::visit_Document(const RawText& n) {
    m_expr.visit_Document(n);
}

void CppTranspiler::visit_Ident(const IdentName& n) {
    m_expr.visit_Ident(n);
}

void CppTranspiler::visit_TypeName(const IdentType& n) {
    m_expr.visit_TypeName(n);
}

void CppTranspiler::visit_CallExpr(const CallExpr& n) {
    m_expr.visit_CallExpr(n);
}

void CppTranspiler::visit_VarRef(const AstNodeAttr& n) {
    m_expr.visit_VarRef(n);
}

void CppTranspiler::visit_ArrayInit(const DictLiteralNode& n) {
    m_expr.visit_ArrayInit(n);
}

void CppTranspiler::visit_DictLiteral(const DictLiteralNode& n) {
    m_expr.visit_DictLiteral(n);
}

void CppTranspiler::visit_Tuple(const DictLiteralNode& n) {
    m_expr.visit_Tuple(n);
}

void CppTranspiler::visit_RangeExpr(const RangeExpr& n) {
    m_expr.visit_RangeExpr(n);
}

void CppTranspiler::visit_RefMakeExpr(const RefMakeExpr& n) {
    m_expr.visit_RefMakeExpr(n);
}

void CppTranspiler::visit_RefTakeExpr(const RefTakeExpr& n) {
    m_expr.visit_RefTakeExpr(n);
}

void CppTranspiler::visit_RefLockExpr(const Sequence& n) {
    m_expr.visit_RefLockExpr(n);
}

void CppTranspiler::visit_RefLockDeref(const Sequence& n) {
    m_expr.visit_RefLockDeref(n);
}

void CppTranspiler::visit_Ellipsis(const Sequence& n) {
    m_expr.visit_Ellipsis(n);
}

void CppTranspiler::visit_Filling(const Sequence& n) {
    m_expr.visit_Filling(n);
}

void CppTranspiler::visit_TrustContract(const TrustContract& n) {
    m_contract.visit_TrustContract(n);
}

void CppTranspiler::visit_TrustElem(const TrustElem& n) {
    m_contract.visit_TrustElem(n);
}

} // namespace trust
