// Generated: src/transpiler/decl_emit.cpp
#include "transpiler/decl_emit.hpp"
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
#include "types/operator_registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>

namespace trust {

void DeclEmitter::generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx) {
    // Зависимый C++-заголовок (@[include]) и флаг линковки (@[link]) собираем В САМОМ НАЧАЛЕ:
    // они нужны и для нативных импортов (`name := %native...;`) и нативных шаблон-типов, которые
    // НЕ эмитят C++-функцию и выходят ниже ранним return (иначе @include/@link молча терялись).
    m_driver.m_type.collectInclude(func_node);
    m_driver.m_type.collectLinkLib(func_node);

    // Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` - это тип, а НЕ функция:
    // C++-функция не эмитится; конкретное C++-имя (`std::vector<int64_t>`) эмитится при
    // использовании типа (resolveCppTypeId), инклуд - on-use через preprocIncludes типа.
    if (func_node.m_isNativeTemplateCtor) {
        return;
    }
    // Нативный импорт `<name>(...) := %native...;` - алиас: C++-функция НЕ эмитится.
    // Регистрируем trust-имя → нативное C++-имя; вызовы name(...) будут переписаны в native(...).
    if (func_node.m_isNativeImport) {
        m_ectx.m_nativeImports[std::string(func_node.text())] = func_node.m_nativeName;
        return;
    }
    const bool funcRangeValid = !func_node.range().isInvalid();
    if (funcRangeValid) {
        m_ectx.m_ctx.source().mapStart(func_node.range(), output_idx);
    }
    // Точка входа модуля: DSL-макрос `@main` раскрывается в `<имя_модуля>__main__`. Pipeline
    // генерирует `_main.cppt` с `extern int <имя_модуля>__main__(); int main(){ return …; }`,
    // поэтому entry-функция эмитится с СЫРЫМ именем (без манглинга `c_`) и типом возврата `int`.
    const std::string trust_name = std::string(func_node.text());
    const bool isEntry = func_node.m_body && !m_ectx.m_inCppBlock && m_ectx.m_hiddenNamespaceDepth == 0 && trust_name.ends_with("__main__");

    // Контроль переполнения стека: собираем защищаемые функции (атрибут @[stack_check@])
    // в контекст, чтобы при вызове (ExprEmitter::stackCheckExpr) вставить проверку перед callee.
    // size==0 (без аргумента = limit) → check_stack_limit(); size>0 → check_overflow(size).
    {
        const AttrPool& sattrs = m_ectx.m_ctx.attrs();
        if (auto sc = sattrs.lookup(attr::StackCheck); sc.has_value() && func_node.has_attr(*sc)) {
            StackCheckGuard g;
            if (const auto* args = func_node.attr_args(*sc); args && !args->empty()) {
                // Семантика уже валидирует неотрицательное целое (DeclAnalyzer::analyzeFuncDecl);
                // здесь парсим размер для check_overflow(N). При переполнении long (астрономически
                // большой N) насыщаем до большого значения: остаёмся «явной большой проверкой», а НЕ
                // тихо вырождаемся в limit-режим (size==0 → check_stack_limit, существенно слабее).
                try {
                    const long v = std::stol(args->at(0));
                    g.size = (v < 0) ? 0 : v;
                } catch (const std::exception&) {
                    g.size = 1000000000L; // ~1 ГБ свободного стека — на реальном стеке всегда бросок
                }
            }
            m_ectx.m_stackCheck[trust_name] = g;
        }
    }
    // Имена функций модуля - для резолва адресов `--stack-check-functions` в точке входа.
    m_ectx.m_functionNames.insert(trust_name);
    // Function name: манглинг trust-имени в C++-идентификатор (срез '%' у нативных функций).
    // Entry - без манглинга, иначе не совпадёт с `extern int <имя>__main__()` в _main.cppt.
    // Оператор (имя-СИМВОЛ в обратных кавычках) эмитится как C++ `operator<sym>`: `` `==` `` →
    // `operator==`, `` `()` `` → `operator()`, `` `[]` `` → `operator[]` (манглинг trust-имени
    // к символу неприменим). Единая точка вычисления имени - cppFuncName (emit_common.hpp).
    std::string name = cppFuncName(trust_name, func_node.m_isOperator, isEntry);
    EXPECT((!func_node.m_isOperator || !name.empty()) && "operator declaration: unknown operator symbol (semantic must validate first)");
    // Перегруженное имя: C++-имя перегрузки обязано быть УНИКАЛЬНЫМ (иначе C++ выберет не ту -
    // литералы/конверсии в C++ и TrustLang расходятся). Суффикс детерминирован по сигнатуре и
    // совпадает с CallExpr::resolvedCalleeSuffix у вызова. Операторы (operator<sym>) не манглируются.
    if (func_node.m_isOverloaded && !func_node.m_isOperator && !func_node.m_overloadSuffix.empty()) {
        name += func_node.m_overloadSuffix;
    }

    // Return type (инклуды типа записываются через emitTypeName). None/Void → "void"; нет аннотации → "void".
    // Явный, но нерезолвящийся тип → emitTypeNameForNode выводит диагностику, прерываем функцию.
    std::string ret_type = "void";
    if (func_node.m_type && func_node.m_type->kind() == ParserToken::Kind::TypeName) {
        const std::string_view rt = func_node.m_type->text();
        if (rt == type::Void || rt == type::None) {
            ret_type = "void";
        } else {
            ret_type = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
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
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::FuncConst)) {
        lead += "__attribute__((const)) ";
    }
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::FuncPure)) {
        lead += "__attribute__((pure)) ";
    }
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::FuncConstexpr)) {
        lead += "constexpr ";
    }
    std::string trail;
    if (func_node.has_attr(m_ectx.m_ctx.attrs(), attr::NoExcept)) {
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
    // C++-типы параметров ТОЛЬКО (без имён). Используются: (1) для entry-функции
    // (`<модуль>__main__`) - pipeline генерирует совпадающий extern в `_main.cppt`;
    // (2) для указателя ПЕРЕГРУЖЕННОЙ функции в экспорт-таблице (static_cast к сигнатуре).
    std::string param_types_only;
    std::vector<std::pair<const ArgNode*, uint32_t>> param_name_positions; // (node, name offset within signature)
    if (func_node.m_params) {
        const uint32_t sig_prefix =
            static_cast<uint32_t>(lead.length()) + static_cast<uint32_t>(ret_type.length()) + 1 + static_cast<uint32_t>(name.length()) + 1;
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                params_str += ", ";
                param_types_only += ", ";
            }
            auto* param_node = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!param_node || param_node->kind() != ParserToken::Kind::ArgNode) {
                params_str += "std::any"; // дефектный узел (семантика отсекает) - тип Any
                param_types_only += "std::any";
                continue;
            }
            if (isVariadicParamMarker(param_node)) {
                // Вариативность: trust `...` - свойство компилятора (произвольное число
                // аргументов); в C++ это чистая variadic-метка `...` (без имени и типа).
                // C++ требует `...` последним параметром, что и гарантируется грамматикой.
                params_str += "...";
                param_types_only += "...";
                continue;
            }
            // Param type (инклуды записываются через emitTypeName); None/Void → "void";
            // без аннотации типа → :Any (std::any). Явный, но нерезолвящийся тип → ошибка.
            std::string param_type;
            if (param_node->m_type && param_node->m_type->kind() == ParserToken::Kind::TypeName) {
                const std::string_view pt = param_node->m_type->text();
                if (pt == type::Void || pt == type::None) {
                    param_type = "void";
                } else {
                    param_type = m_driver.m_type.emitTypeNameForNode(param_node->m_type.get());
                    if (param_type.empty()) {
                        return; // emitTypeNameForNode уже вывел диагностику - функция невалидна
                    }
                }
            } else if (auto aid = m_ectx.m_ctx.types().findType(type_generic::Any)) {
                // Нетипизированный параметр - тип :Any (std::any); инклуд записывает emitTypeName.
                auto anyName = m_driver.m_type.emitTypeName(*aid, type_generic::Any);
                EXPECT(anyName.has_value() && "untyped parameter: Any type must have a C++ name");
                param_type = std::move(*anyName);
            } else {
                EXPECT(false && "untyped parameter: Any type must be registered");
            }
            // Read-only параметр (`^` в имени) → `const <тип>` (как для переменных: attr::ReadOnly
            // → "const "). Для `void` конст-квалификатор недопустим.
            const bool param_readonly = param_node->has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly);
            const std::string param_cpp_type = (param_readonly && param_type != "void") ? "const " + param_type : param_type;
            // Тип параметра (с учётом const) - для entry-extern и указателя перегрузки.
            param_types_only += param_cpp_type;
            // Param name: манглинг trust-имени параметра в C++-идентификатор (a → c_a).
            // Безымянный параметр (только тип) эмитится без имени - C++ это допускает
            // (void f(int32_t)), и к нему нельзя обратиться из тела.
            std::string param_name = utils::name_to_cpp(param_node->text());
            if (param_name.empty()) {
                params_str += param_cpp_type;
            } else {
                // Name offset within signature: params_str already holds everything emitted so far
                // (separators, previous params), so the type size is taken directly.
                uint32_t name_pos = sig_prefix + static_cast<uint32_t>(params_str.size()) + static_cast<uint32_t>(param_cpp_type.length()) + 1;
                params_str += param_cpp_type + " " + param_name;
                param_name_positions.emplace_back(param_node, name_pos);
            }
        }
    }
    // Entry-функция: фиксируем C++-типы параметров (без имён, с учётом const) для extern
    // в `_main.cppt` - обёртка main обязана объявить ту же сигнатуру, что и тело entry.
    // m_sawEntry отмечает факт эмиссии entry (важно для однофайлового режима -fsingle-file,
    // где pipeline по нему отличает программу от библиотеки/скрипта без `__main__`).
    if (isEntry) {
        m_ectx.m_entryParams = param_types_only;
        m_ectx.m_sawEntry = true;
    }

    // Emit function signature
    std::string sig = std::format("{}{} {}({}){}", lead, ret_type, name, params_str, trail);
    m_ectx.m_ctx.source().output_append(output_idx, sig);

    // Add name mappings (function name + parameter names) for hover links.
    // Имя функции выводится сразу после "<lead>ret_type " (offset = lead.length()+ret_type.length()+1).
    // При невалидном диапазоне функции (напр. функция из раскрытия макроса без валидного
    // первого токена) имя НЕ маппим - makeLoc требует валидный fileIdx.
    if (funcRangeValid && !name.empty()) {
        MapperLocation trustFnBegin = m_ectx.m_ctx.source().makeLoc(func_node.range().begin.fileIdx(), func_node.range().begin.offset());
        MapperLocation trustFnEnd = m_ectx.m_ctx.source().makeLoc(
            trustFnBegin.fileIdx(), trustFnBegin.offset() + static_cast<uint32_t>(utils::strip_native_prefix(func_node.text()).size()));
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
    if (funcRangeValid) {
        m_ectx.m_ctx.source().mapStop(func_node.range());
    }

    // Body or forward declaration
    if (func_node.m_body && !m_ectx.m_forwardDeclOnly) {
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
        // Контроль переполнения стека: в точке входа устанавливаем минимальный резерв (reserve) из
        // опции (--stack-check-reserve / @__OPTION__("stack-check-reserve", ...)) и, при заданном
        // --stack-check-functions, перечень функций для m_stack_limit (как функция %trust_stack_check_set_limit).
        // Выполняется до любых проверок. Заголовок подключаем явно (даже если проверки эмитятся в других модулях).
        std::string afterOpen;
        if (isEntry && analysis::stackCheckActive(m_ectx.m_behavioral)) {
            std::string init;
            // include-список: адреса перечисленных функций (ограничивают m_stack_limit для limit-проверок).
            const auto funcs = m_ectx.m_behavioral.stackCheckFunctions;
            if (!funcs.empty()) {
                const auto addrs = m_ectx.resolveStackCheckAddresses(funcs);
                if (!addrs.empty()) {
                    std::string list;
                    for (size_t i = 0; i < addrs.size(); ++i) {
                        if (i) {
                            list += ", ";
                        }
                        list += addrs[i];
                    }
                    init += "trust::stack_check::set_limit({" + list + "});\n";
                }
            }
            if (auto reserve = m_ectx.m_behavioral.stackCheckReserve) {
                init += "trust::stack_check::set_reserve(" + std::to_string(*reserve) + ");\n";
            }
            if (!init.empty()) {
                m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
                afterOpen = init;
            }
        }
        // Trust-контракты функции (--solver-mode=assert): пред-условия (kind=Pre) и утверждение на
        // функции (kind=Assert) - проверяются при входе; пост-условия (kind=Post) - перед каждым
        // `return <value>` (не-void) со связыванием возвращаемого значения, либо в конце тела (void).
        std::vector<AstNodePtr> preTrust, postTrust;
        for (const auto& t : func_node.m_trust) {
            if (!t) {
                continue;
            }
            if (t->is<TrustContract>() && t->as<TrustContract>()->kind == PropertyKind::Post) {
                postTrust.push_back(t);
            } else {
                preTrust.push_back(t);
            }
        }
        // Не-void функция: пост-условия эмитятся visit_ReturnStmt перед каждым return (имя функции
        // = возвращаемое значение; ReturnStmt сам знает свою функцию через m_funcDecl).
        // Void-функция: пост-условие эмитится в конце тела (перед '}').
        const bool isVoidFunc = (ret_type == "void");
        m_ectx.m_scopeStack.push_back({m_ectx.indentLevel()});
        m_driver.m_stmt.emitBlockBodyToFile(*func_node.m_body, blockRange, output_idx, /*mapBlock=*/true, beforeClose, afterOpen,
                                            preTrust.empty() ? nullptr : &preTrust, (isVoidFunc && !postTrust.empty()) ? &postTrust : nullptr);
        m_ectx.m_scopeStack.pop_back();
    } else {
        // Forward declaration
        m_ectx.m_ctx.source().output_append(output_idx, ";");
    }

    // Экспортируются ОПРЕДЕЛЕНИЯ функций на верхнем уровне модуля в НЕ анонимной области имён
    // (квалифицированно для 'ns::'); из '_' и локальных, а также forward-объявления
    // (нет тела → нет определения, `&::name` не связался бы) - не экспортируются.
    // Операторы экспортируются как и функции: сайт импорта эмитит C++ forward-decl
    // `operator<sym>(...)` (Trust-forward-decl - с символом в обратных кавычках, см.
    // buildTrustForwardDecl), обеспечивая кросс-модульную видимость свободных операторов.
    if (func_node.m_body && !name.empty() && !m_ectx.m_inCppBlock && m_ectx.m_hiddenNamespaceDepth == 0) {
        const std::string cpp_name = m_ectx.qualifiedCppName(name);
        // Перегруженное имя: C++-имя перегрузки УЖЕ уникально (суффикс по сигнатуре, см. `name`),
        // поэтому адрес однозначен (`&::c_f_Int32_`) - отдельного `static_cast` не требуется.
        // Уникализируем лишь ИМЯ trust-записи (перегрузки делят одно trust-имя) - суффикс '#<idx>'.
        std::string export_name = std::string(func_node.text());
        if (func_node.m_isOverloaded) {
            export_name += "#" + std::to_string(m_ectx.m_exports.size());
        }
        m_ectx.m_exports.push_back({export_name, cpp_name, buildTrustForwardDecl(func_node)});
    }
}

// Лямбда-выражение как ЗНАЧЕНИЕ: `[captures](params)[ -> Ret] { body }`.
// Захваты - только имена по значению (C++-захват копией `[c_x, c_y]`); параметры/тело - как у функции.
// Специализированный путь: НЕ generateFuncDeclToFile (тот эмитит top-level определение и ломает выражение).
void DeclEmitter::emitLambdaExpr(const FuncDecl& func_node) {
    // Список захватов (по значению): trust-имя -> C++-идентификатор (c_x).
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "[");
    bool firstCap = true;
    if (func_node.m_captures) {
        for (const auto& cap : *func_node.m_captures) {
            if (!cap || cap->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (!firstCap) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
            }
            firstCap = false;
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, utils::name_to_cpp(cap->text()));
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "](");
    // Параметры.
    if (func_node.m_params) {
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
            }
            const auto* p = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!p || p->kind() != ParserToken::Kind::ArgNode || p->text() == "...") {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, "...");
                continue;
            }
            // Тип параметра: явный (emitTypeNameForNode) либо :Any (std::any).
            std::string ptype;
            if (p->m_type) {
                if (p->m_type->kind() == ParserToken::Kind::TypeName) {
                    const std::string_view pt = p->m_type->text();
                    if (pt == type::Void || pt == type::None) {
                        ptype = "void";
                    } else {
                        ptype = m_driver.m_type.emitTypeNameForNode(p->m_type.get());
                    }
                } else {
                    ptype = m_driver.m_type.emitTypeNameForNode(p->m_type.get());
                }
            } else if (auto anyId = m_ectx.m_ctx.types().findType(type_generic::Any)) {
                auto anyName = m_driver.m_type.emitTypeName(*anyId, type_generic::Any);
                ptype = anyName ? std::move(*anyName) : std::string("std::any");
            } else {
                ptype = "std::any";
            }
            if (ptype.empty()) {
                ptype = "std::any";
            }
            if (ptype != "void" && p->has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly)) {
                ptype = "const " + ptype;
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ptype);
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, " ");
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, utils::name_to_cpp(p->text()));
            if (p->m_value) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, " = ");
                m_driver.emitExpr(p->m_value.get());
            }
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
    // Тип возврата - только при явной аннотации (`void` для Void/None).
    if (func_node.m_type) {
        std::string ret;
        if (func_node.m_type->kind() == ParserToken::Kind::TypeName) {
            const std::string_view rt = func_node.m_type->text();
            if (rt == type::Void || rt == type::None) {
                ret = "void";
            } else {
                ret = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
            }
        } else {
            ret = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
        }
        if (!ret.empty()) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, " -> ");
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ret);
        }
    }
    // Тело.
    if (func_node.m_body && !m_ectx.m_forwardDeclOnly) {
        m_ectx.m_scopeStack.push_back({m_ectx.indentLevel()});
        m_driver.m_stmt.emitBlockBodyToFile(*func_node.m_body, func_node.blockRange(), m_ectx.m_out, /*mapBlock=*/true, {}, {}, nullptr, nullptr);
        m_ectx.m_scopeStack.pop_back();
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, " {}");
    }
}

} // namespace trust
