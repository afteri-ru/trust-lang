#include "transpiler/emit/stmt_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/token_base.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "ir/lowering.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include "utils/error.hpp"
#include <format>
#include <memory>
#include <string_view>

namespace trust {

void StmtEmitter::emitBlockBodyToFile(const std::vector<AstNodePtr>& body, MapperRange blockRange, MapperFile output_idx, bool mapBlock,
                                      const std::string& beforeCloseLabel, const std::string& afterOpen, const std::vector<AstNodePtr>* preTrust,
                                      const std::vector<AstNodePtr>* postTrust) {
    // mapBlock=false: не оборачиваем тело собственным маппингом (do-while - begin тела
    // совпадает с begin statement'а, иначе коллизия ключа в mapStop).
    const bool hasBlockRange = !blockRange.isInvalid() && mapBlock;

    // Тело маппится отдельно из диапазона блока, чтобы скобки { } отображались в C++.
    std::optional<MapperScope> scope;
    if (hasBlockRange) {
        scope.emplace(m_ectx.m_services.source(), blockRange, output_idx);
    }

    // Нормальное многострочное форматирование: '{' в конце строки, операторы - с отступом.
    // Входим во вложенный скоуп: отступ +1 (функция/блок - только уровень отступа).
    m_ectx.m_services.source().output_append(output_idx, " {");
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
    m_ectx.m_services.source().output_append(output_idx, "\n");
    // Внутри C++ compound statement: пользовательские блоки эмитятся как `{ }`, не как
    // `namespace { }` (namespace внутри compound statement в C++ недопустим).
    const bool savedCppBlock = m_ectx.m_mode.inCppBlock;
    m_ectx.m_mode.inCppBlock = true;
    // afterOpen - текст сразу после '{' (например, установка флага while-else).
    if (!afterOpen.empty()) {
        m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(output_idx, afterOpen);
        m_ectx.m_services.source().output_append(output_idx, "\n");
    }
    // Пред-условия функции (--solver-mode=assert): проверка сразу после '{' (параметры в скоупе).
    if (preTrust) {
        m_driver.m_contract.emitTrustChecks(*preTrust);
    }

    for (const auto& child : body) {
        if (!child) {
            continue;
        }
        if (m_driver.isSuppressedDoc(child->kind())) {
            continue;
        }
        const size_t before = m_ectx.m_services.source().output_body(output_idx).size();
        // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход;
        // для остальных операторов - отступ текущего блока.
        if (!is_block_kind(child->kind())) {
            m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        }
        m_driver.generateNodeToFile(*child, output_idx);
        // Не оставлять пустую строку, если узел ничего не эмитил (напр. подавленный doc-bundle),
        // и не дублировать перевод строки, если блок уже закончился '\n' (emitSequenceBody).
        if (m_ectx.m_services.source().output_body(output_idx).size() != before) {
            const std::string_view body = m_ectx.m_services.source().output_body(output_idx);
            if (body.empty() || body.back() != '\n') {
                m_ectx.m_services.source().output_append(output_idx, "\n");
            }
        }
    }
    // continue-метка do-while / именованного блока вставляется перед закрывающей '}'.
    if (!beforeCloseLabel.empty()) {
        m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(output_idx, beforeCloseLabel);
        m_ectx.m_services.source().output_append(output_idx, "\n");
    }
    // Пост-условия функции (--solver-mode=assert): проверка перед '}' (возвращаемое значение в скоупе).
    if (postTrust) {
        m_driver.m_contract.emitTrustChecks(*postTrust);
    }
    m_ectx.m_mode.inCppBlock = savedCppBlock;
    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_services.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(output_idx, "}");
}

std::string StmtEmitter::emitSyntheticVar(const VarDecl& vd, MapperFile output_idx) {
    const std::string name = utils::cppDeclName(vd.text());
    EXPECT(!name.empty() && "synthetic temporary must have a C++ name");
    const TypeId vt = vd.inferredType.getTypeId();
    std::string typePart;
    if (vt != INVALID_TYPE_ID) {
        // C++-имя типа строит единый принтер из реестра (display-имя типа, не переменной).
        auto cppType = m_driver.m_type.emitTypeName(vt);
        EXPECT(cppType.has_value() && !cppType->empty() && "synthetic temporary type must render to a C++ name");
        if (vd.has_attr(m_ectx.m_services.attrs(), attr::ReadOnly)) {
            m_ectx.m_services.source().output_append(output_idx, "const ");
        }
        typePart = *cppType + " ";
    } else {
        // Синтетическая временная обязана быть типизирована анализатором (инвариант «временные —
        // уровень анализатора»). Единственное исключение - Locker-темп `__cap_N`: trust::Locker не
        // языковой тип (ir/hir/lowering), конкретного C++-имени в реестре нет -> `auto`. Любой другой
        // неинферированный тип - внутренняя ошибка, а НЕ тихий auto-fallback.
        if (std::string_view(vd.text()).rfind(kLockerTempPrefix, 0) != 0) {
            FAULT("internal: synthetic temporary '{}' has no inferred type; the analyzer must resolve it before transpilation", vd.text());
        }
        typePart = "auto ";
    }
    m_ectx.m_services.source().output_append(output_idx, typePart + name + " = ");
    m_driver.emitExpr(vd.m_initializer.get());
    m_ectx.m_services.source().output_append(output_idx, ";");
    m_ectx.m_services.source().output_append(output_idx, "\n");
    // Хвостовой отступ НЕ добавляем: вызывающий позиционирует следующую строку (match/while-else
    // добавляют indentPrefix; деструктуризация использует собственный `ind` для каждой строки).
    return name;
}

void StmtEmitter::emitCompoundScope(const ScopeBlock& n) {
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "{");
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "\n");
    m_driver.emitSequenceBody(n, m_ectx.m_output.out);
    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}");
}

void StmtEmitter::emitNamespaceScope(const ScopeBlock& n, const std::string& name) {
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "namespace");
    if (!name.empty()) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, " " + name);
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, " {");
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "\n");
    m_driver.emitSequenceBody(n, m_ectx.m_output.out);
    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "}");
}

void StmtEmitter::visit_ScopeBlock(const ScopeBlock& n) {
    // Блок интерфейса файла: объявления эмитятся на верхнем уровне модуля без фигурных скобок
    // (эквивалент `::`), поэтому в TU модуля это прототипы/определения типов.
    if (n.isModuleInterface()) {
        m_driver.emitSequenceBody(n, m_ectx.m_output.out);
        return;
    }
    const std::string_view text = n.text();
    // Безымянный блок кода: последовательность операторов как одно выражение.
    const bool isCodeBlock = text.empty() || text == "{";
    // Именованный блок-метка: одно имя (идентификатор) без '::' - label { ... }.
    const bool isLabel = !isCodeBlock && !n.is_hidden() && text.find("::") == std::string_view::npos;
    // Глобальная область имён: ':: { ... }'.
    const bool isGlobalNs = text == "::";

    // Блок кода / именованная метка разрешены ТОЛЬКО внутри функции/класса.
    if (isCodeBlock || isLabel) {
        if (!m_ectx.m_mode.inCppBlock) {
            if (isLabel) {
                m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "named block '{}' is only allowed inside a function", std::string(n.name()));
            } else {
                m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "unnamed code block is only allowed inside a function");
            }
            return;
        }
        emitCompoundScope(n);
        return;
    }

    // Область имён (`ns::`, `_`) разрешена только на верхнем уровне модуля.
    if (!isGlobalNs && m_ectx.m_mode.inCppBlock) {
        m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "namespace block is not allowed inside a function");
        return;
    }

    // Глобальная область '::' - содержимое без namespace-обёртки.
    if (isGlobalNs) {
        m_driver.emitSequenceBody(n, m_ectx.m_output.out);
        return;
    }

    // Protected (`_::`) — плоское module-level пространство: C++-имя совпадает с public-формой,
    // видимость ограничивает Trust-семантика. Private (`__::`) — anonymous namespace.
    if (n.is_protected()) {
        m_driver.emitSequenceBody(n, m_ectx.m_output.out);
        return;
    }
    const bool hidden = n.is_private();
    const std::string nsName = hidden ? "" : utils::cppNamespacePath(m_ectx.m_output.namespaceCppName(text));
    if (hidden) {
        ++m_ectx.m_output.hiddenNamespaceDepth;
    } else {
        m_ectx.m_output.namespaceStack.push_back(nsName);
    }
    emitNamespaceScope(n, nsName);
    if (hidden) {
        --m_ectx.m_output.hiddenNamespaceDepth;
    } else {
        m_ectx.m_output.namespaceStack.pop_back();
    }
}

void StmtEmitter::visit_ReturnStmt(const JumpStmt& n) {
    // НЕИМЕНОВАННОЕ `++ value ++` - ВСЕГДА «положительное прерывание» (сквозной возврат через
    // вызовы функций), НЕ зависит от наличия/глубины обработчиков перехвата: throw trust::IntPlus.
    // ИМЕНОВАННОЕ `func ++ value ++` - обычный return из функции.
    if (!n.m_label) {
        m_driver.m_type.recordRequiredInclude("@stdlib/interrupts.hpp");
        MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
        if (n.m_value) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "throw trust::IntPlus(");
            m_driver.emitExpr(n.m_value.get());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ");");
        } else {
            // `++ _ ++` - void-прерывание (`_` - пустое значение).
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "throw trust::IntPlus();");
        }
        return;
    }
    // Пост-условия (--solver-mode=assert): проверка перед `return <value>` со связыванием имени функции
    // к возвращаемому значению (hoist в temp, чтобы выражение вычислялось один раз). Для void-return
    // (без значения) пост-условия не проверяются (нет возвращаемого значения). Функция известна из
    // узла (m_funcDecl, помечает семантика) - без текущего контекста в транспиляторе.
    if (n.m_funcDecl && n.m_value) {
        std::vector<AstNodePtr> post;
        for (const auto& t : n.m_funcDecl->m_trust) {
            if (t && t->is<TrustContract>() && t->as<TrustContract>()->kind == PropertyKind::Post) {
                post.push_back(t);
            }
        }
        if (!post.empty()) {
            // Маппим сгенерированный код на диапазон оператора return (иначе он наследует
            // маппинг тела функции → неправильный диапазон: клик по `return ...;` не ведёт
            // в C++ и обратно). Как в visit_BreakStmt/visit_SemicolonStmt.
            MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
            // Hoist-временная возврата создана СЕМАНТИКОЙ (m_tempDecl, инвариант «временные создаёт
            // анализатор»): эмитим её объявление через emitSyntheticVar (const <Ret> c___trust_res_N
            // = <value>;) и читаем её C++-имя для пост-проверок и return.
            const VarDecl* temp = nullptr;
            if (n.m_tempDecl && n.m_tempDecl->kind() == ParserToken::Kind::VarDecl) {
                temp = static_cast<const VarDecl*>(n.m_tempDecl.get());
            }
            if (!temp) {
                m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "return value temporary is missing (analyzer did not synthesize it)");
                return;
            }
            const std::string tmp = emitSyntheticVar(*temp, m_ectx.m_output.out);
            if (tmp.empty()) {
                m_ectx.m_services.report(n.range(), diag::DiagId::ParseError, "unable to generate C++ type for return temporary '{}'", requiredText(*temp));
                return;
            }
            { // RAII: имя функции → temp возврата (восстанавливается в деструкторе).
                ResultGuard guard(m_ectx, std::string(requiredText(*n.m_funcDecl)), tmp);
                m_driver.m_contract.emitTrustChecks(post);
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_ectx.m_output.indentPrefix());
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, "return " + tmp + ";\n");
            return;
        }
    }
    m_driver.emitJumpValue("return", n);
}

void StmtEmitter::visit_ThrowStmt(const JumpStmt& n) {
    // Отрицательное прерывание (-- err -- / -- --) - ошибка: бросаем trust::IntMinus
    // (переносимое значение - в value(); классификация по классу прерывания, см. errors.md).
    m_driver.m_type.recordRequiredInclude("@stdlib/interrupts.hpp");
    MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
    if (n.m_value) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "throw trust::IntMinus(");
        m_driver.emitExpr(n.m_value.get());
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, ");");
    } else {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "throw trust::IntMinus();");
    }
}

void StmtEmitter::visit_BreakStmt(const JumpStmt& n) {
    // Безымянный break - обычный C++ break;. Именованный break анализатор переписывает
    // в GotoStmt (или void-ReturnStmt при break по имени функции), поэтому сюда доходит
    // только безымянный.
    MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "break;");
}

void StmtEmitter::visit_ContinueStmt(const JumpStmt& n) {
    // Безымянный continue - обычный C++ continue;. Именованный continue анализатор
    // переписывает в GotoStmt, поэтому сюда доходит только безымянный.
    MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "continue;");
}

// Goto/Label - синтетические узлы lowering: goto по метке / определение метки.
// Маппинг НЕ строится: у синтетических узлов нет исходного trust-текста, а их range
// (блок/цикл) уже смапплен реальными узлами (иначе коллизия trustKey в mapStop).
void StmtEmitter::visit_GotoStmt(const LabelRef& n) {
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "goto " + std::string(n.text()) + ";");
}

void StmtEmitter::visit_LabelStmt(const LabelRef& n) {
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::string(n.text()) + ":;");
}

// SemicolonStmt - выражение в позиции оператора: выражение в statement-root (без скобок) +
// завершающая ';'. Маппинг range берётся от обёрнутого выражения (SemicolonStmt::range() делегирует).
void StmtEmitter::visit_SemicolonStmt(const SemicolonStmt& n) {
    // Сброс `x = _;` (присваивание маркера `_` - «без значения»): НЕ эмитит запись значения.
    // Значение не определено (переменная снова неинициализирована; анализатор запрещает чтение
    // до записи), поэтому `c_x = c__;` - мусорный код. Оператор сброса кода не даёт (no-op).
    if (n.m_expr && n.m_expr->kind() == ParserToken::Kind::AssignOp) {
        const auto& b = static_cast<const Binary&>(*n.m_expr);
        if (isPlainAssignOp(b.m_op) && isNoneMarker(b.m_right.get())) {
            return; // reset `x = _;`: без записи значения (и без ';')
        }
    }
    MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
    if (n.m_expr) {
        m_driver.generateNodeToFile(*n.m_expr, m_ectx.m_output.out);
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ";");
    // После присваивания в переменную типа с trust-условиями проверяем значение заново
    // (тип-утверждение; при создании/объявлении проверка уже была).
    m_driver.m_contract.emitTypeChecksAfterAssignment(n.m_expr.get());
}

// Control flow.
void StmtEmitter::visit_LastResultCapture(const LastResultCapture& n) {
    // Синтетический узел «результат последней операции»: открывает собственный скоуп { }.
    // Внутри: (1) временная m_temp (const VarDecl `__trust_last_N`), объявленная в начале узла,
    // (2) источник m_source (значение перед выходом сохраняется во временную), (3) потребитель
    // m_sink (лист `$^` уже заменён ссылкой на временную). Временная живёт до закрытия скоупа.
    std::vector<AstNodePtr> body;
    body.reserve(3);
    if (n.m_temp) {
        body.push_back(n.m_temp);
    }
    if (n.m_source) {
        body.push_back(n.m_source);
    }
    if (n.m_sink) {
        body.push_back(n.m_sink);
    }
    // emitBlockBodyToFile: маппинг по n.range() (у синтетического узла может быть невалидным ->
    // hasBlockRange=false, скобки { } всё равно эмитятся, скоуп закрывается).
    emitBlockBodyToFile(body, n.range(), m_ectx.m_output.out, /*mapBlock=*/true, /*beforeCloseLabel=*/"", /*afterOpen=*/"");
}

void StmtEmitter::visit_IfStmt(const IfStmt& n) {
    generateIfToFile(n, m_ectx.m_output.out);
}

void StmtEmitter::visit_WhileStmt(const WhileStmt& n) {
    generateWhileToFile(n, m_ectx.m_output.out);
}

void StmtEmitter::visit_DoWhileStmt(const DoWhileStmt& n) {
    generateDoWhileToFile(n, m_ectx.m_output.out);
}

void StmtEmitter::visit_MatchingStmt(const MatchStmt& n) {
    generateMatchToFile(n, m_ectx.m_output.out);
}

void StmtEmitter::generateWithToFile(const WithStmt& node, MapperFile output_idx) {
    // Менеджер контекста `with(a=f(), b=g()){body}else{...}` (RAII). В C++ нет «else при исключении
    // в инициализаторе»: эмулируется флагом «все биндинги успешно захвачены». Флаг создаёт LOWERING
    // (m_failFlag, `bool _wN := 0;` - инвариант «временные создаёт анализатор»); здесь только эмитим
    // его объявление и используем имя.
    //   С else:  bool _wN = 0; try { a=...; b=...; _wN = true; {тело} } catch(...){ if(_wN) throw; {else} }
    //   Без else: { a=...; b=...; {тело} }   (простые блоки; исключения пробрасываются, RAII - скоуп).
    // Внешнего MapperScope на весь оператор НЕ создаём: его range перекрывается с range биндингов
    // (first binding начинается на `with`-ключевом) → mapStop-коллизия trustKey. Каждый биндинг
    // (generateVarDeclToFile), тело и else создают собственные MapperScope для своих range.
    auto& out = m_ectx.m_output.out;

    std::string flag;
    if (node.m_else) {
        const VarDecl* f = nullptr;
        if (node.m_failFlag && node.m_failFlag->kind() == ParserToken::Kind::VarDecl) {
            f = static_cast<const VarDecl*>(node.m_failFlag.get());
        }
        if (!f) {
            m_ectx.m_services.report(node.range(), diag::DiagId::ParseError, "with-else flag temporary is missing (lowering did not synthesize it)");
            return;
        }
        flag = emitSyntheticVar(*f, output_idx);
        if (flag.empty()) {
            m_ectx.m_services.report(node.range(), diag::DiagId::ParseError, "unable to generate C++ type for with-else flag '{}'", requiredText(*f));
            return;
        }
        // emitSyntheticVar закончил строкой перевода; позиционируем `try {` на отступ statement'а.
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(out, "try {");
    } else {
        m_ectx.m_services.source().output_append(out, "{");
    }
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
    m_ectx.m_services.source().output_append(out, "\n");

    // Захваты (m_locks - пары (lock, binding)): объявлены ВНУТРИ try (скоуп RAII; при исключении
    // в последующем инициализаторе уже созданные объекты уничтожаются автоматически).
    // Логика пар (после LOWERING):
    //   (Locker-темп VarDecl, binding VarDecl) → `auto __cap_N = ref.lock();` (темп, держится) +
    //                                            биндинг `T& name = *__cap_N;` (авторазыменов);
    //   (Locker-темп VarDecl, nullptr)          → только темп (держится, `_` - без значения);
    //   (RefLockExpr, nullptr)                  → `ref.lock();` (временный лок, снимается сразу);
    //   (nullptr, binding VarDecl)              → обычный value-биндинг.
    for (const auto& [lock, binding] : node.m_locks) {
        if (lock) {
            m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
            if (lock->kind() == ParserToken::Kind::VarDecl) {
                // Locker-темп: auto __cap_N = ref.lock(); (держится; emitSyntheticVar завершает переводом).
                emitSyntheticVar(static_cast<const VarDecl&>(*lock), out);
            } else {
                // Голый лок `with(*ref)`: временный Locker, снимается сразу после выражения.
                m_driver.emitExpr(lock.get());
                m_ectx.m_services.source().output_append(out, ";");
                m_ectx.m_services.source().output_append(out, "\n");
            }
        }
        if (binding) {
            m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
            m_driver.m_decl.generateVarDeclToFile(static_cast<const VarDecl&>(*binding), out);
            m_ectx.m_services.source().output_append(out, "\n");
        }
    }
    // Флаг «все биндинги захвачены» - только ПОСЛЕ всех инициализаторов (исключение в теле → throw).
    if (!flag.empty()) {
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(out, flag + " = true;\n");
    }
    if (node.m_body) {
        m_driver.emitBodyNode(node.m_body, out, /*mapBlock=*/false);
    }

    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(out, "}");
    if (!flag.empty()) {
        // catch(...): если исключение из инициализатора (флаг не установлен) → else (объекты уже
        // уничтожены размоткой стека); если из тела (флаг установлен) → пробрасываем дальше.
        m_ectx.m_services.source().output_append(out, " catch (...) {");
        m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
        m_ectx.m_services.source().output_append(out, "\n");
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(out, "if (" + flag + ") throw;");
        m_ectx.m_services.source().output_append(out, "\n");
        if (node.m_else) {
            m_driver.emitBodyNode(node.m_else, out, /*mapBlock=*/false);
        }
        m_ectx.m_output.scopeStack.pop_back();
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(out, "}");
    }
}

void StmtEmitter::visit_WithStmt(const WithStmt& n) {
    generateWithToFile(n, m_ectx.m_output.out);
}

void StmtEmitter::visit_AssignmentStmt(const AstNodeAttr& node) {
    // SWAP (`:=:`) конвертируется в Binary(AssignOp); самостоятельный AssignmentStmt не производится.
    faultUnexpectedKind("StmtEmitter", node);
}

void StmtEmitter::visit_BlockStmt(const AstNodeAttr& node) {
    // Служебный блок-маркер не производится; тело обрабатывает владелец.
    faultUnexpectedKind("StmtEmitter", node);
}

void StmtEmitter::visit_ThenBlock(const AstNodeAttr& node) {
    // Служебная ветка if не производится: тело эмитится внутри visit_IfStmt.
    faultUnexpectedKind("StmtEmitter", node);
}

void StmtEmitter::visit_ElseBlock(const AstNodeAttr& node) {
    // Служебная ветка if не производится: тело эмитится внутри visit_IfStmt.
    faultUnexpectedKind("StmtEmitter", node);
}

void StmtEmitter::visit_WhileElseBlock(const AstNodeAttr& node) {
    // Служебная ветка while не производится: тело эмитится внутри visit_WhileStmt.
    faultUnexpectedKind("StmtEmitter", node);
}

void StmtEmitter::visit_TryCatchStmt(const TryCatchStmt& n) {
    // Оператор перехвата `try { A } catch(:T){B} catch(...){C} else{D};` (Вариант A, нативный C++ catch).
    // Диспетчеризация - самим C++ по типу исключения (учёт подтипов); else сворачивается последним
    // оператором try (Python-семантика: выполняется, если прерываний НЕ было).
    // catch(...) (m_type==nullptr) → catch(trust::IntAny&) - любые не перечисленные, должен быть последним.
    m_driver.m_type.recordRequiredInclude("@stdlib/interrupts.hpp");
    MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
    auto& out = m_ectx.m_output.out;
    // Отступ перед 'try {' выставляет родитель (TryCatchStmt не блочный kind - см. is_block_kind).
    m_ectx.m_services.source().output_append(out, "try {");
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
    m_ectx.m_services.source().output_append(out, "\n");
    m_driver.emitSequenceBody(n, out);
    if (n.m_else) {
        // else - последним оператором try (Python-семантика: выполняется, если прерываний НЕ было).
        // Документирующий маркер else-ветки (подавляется -fno-comments). Маппинг для него НЕ создаётся:
        // маппинг тела else автоматический (MapperScope на уровне statements ниже).
        if (m_ectx.m_behavioral.comments) {
            m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
            m_ectx.m_services.source().output_append(out, "// else\n");
        }
        if (const auto* eseq = n.m_else->as_sequence()) {
            m_driver.emitSequenceBody(*eseq, out); // расплющиваем блок else в try
        } else {
            m_driver.emitBodyNode(n.m_else, out);
        }
    }
    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
    m_ectx.m_services.source().output_append(out, "}");
    if (n.m_catches.empty()) {
        // Без веток (напр. `try { A };` = {* A *}, `{- A -}`, `{+ A +}`) - дефолтный catch по классу
        // блока (TermID): BLOCK_PLUS→IntPlus, BLOCK_MINUS→IntMinus, иначе IntAny. Имя типа - из реестра
        // (type::-константы + emitTypeName дают C++-имя trust::IntPlus/IntMinus/IntAny).
        TypeId tid;
        switch (n.m_tryKind) {
        case TryKind::Plus:
            tid = m_ectx.m_services.readTypes().getType(type::IntPlus);
            break;
        case TryKind::Minus:
            tid = m_ectx.m_services.readTypes().getType(type::IntMinus);
            break;
        case TryKind::Any:
            tid = m_ectx.m_services.readTypes().getType(type::IntAny);
            break;
        }
        std::string defaultCatch;
        if (auto cpp = m_driver.m_type.emitTypeName(tid); cpp) {
            defaultCatch = *cpp;
        }
        m_ectx.m_services.source().output_append(out, " catch (" + defaultCatch + "&) {}");
        return;
    }
    for (const auto& cbPtr : n.m_catches) {
        const auto& cb = static_cast<const CatchBlock&>(*cbPtr);
        m_ectx.m_services.source().output_append(out, "\n");
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(out, "catch (");
        std::string ctype;
        std::string varName;
        if (cb.m_binding) {
            // Единый узел catch-аргумента: VarDecl(e,T) → тип из m_type + имя; IdentType(T) → тип.
            AstNodePtr typeNode = cb.m_binding;
            if (cb.m_binding->kind() == ParserToken::Kind::VarDecl) {
                const auto& vd = static_cast<const VarDecl&>(*cb.m_binding);
                typeNode = vd.m_type;
                varName = std::string(vd.text());
            }
            if (typeNode) {
                // Явная ветка catch(:T) - C++-имя из реестра (IntMinus/IntPlus/IntAny и пользовательские
                // типы зарегистрированы). Пустой результат — нарушение контракта анализа, не fallback.
                ctype = m_driver.m_type.emitTypeNameForNode(typeNode.get());
                EXPECT(!ctype.empty() && "catch type must render to a C++ name; semantic analysis must resolve it");
            } else {
                FAULT("catch binding VarDecl must carry a type annotation");
            }
        } else {
            // catch-any (_ / none / ...) → IntAny (имя из реестра).
            auto intAnyName = m_driver.m_type.emitTypeName(m_ectx.m_services.readTypes().getType(type::IntAny));
            EXPECT(intAnyName.has_value() && !intAnyName->empty() && "IntAny must have a registered C++ name before codegen");
            ctype = *intAnyName;
        }
        m_ectx.m_services.source().output_append(out, ctype + "&");
        if (!varName.empty()) {
            // Имя связанной переменной catch манглится как обычная локальная (e → c_e),
            // чтобы ссылки из тела catch (visit_Ident → cppName) совпадали с C++-параметром.
            m_ectx.m_services.source().output_append(out, " " + utils::cppDeclName(varName));
        }
        m_ectx.m_services.source().output_append(out, ") {");
        m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
        m_ectx.m_services.source().output_append(out, "\n");
        m_driver.emitSequenceBody(cb, out);
        m_ectx.m_output.scopeStack.pop_back();
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        m_ectx.m_services.source().output_append(out, "}");
    }
}

void StmtEmitter::visit_CatchBlock(const CatchBlock& node) {
    // Ветки catch эмитятся внутри visit_TryCatchStmt (inline); автономно не встречаются.
    skipNode(node);
}

void StmtEmitter::visit_MatchingCase(const AstNodeAttr& node) {
    // Служебная ветка match не производится: эмитится внутри visit_MatchingStmt.
    faultUnexpectedKind("StmtEmitter", node);
}

void StmtEmitter::visit_MatchingElseBlock(const AstNodeAttr& node) {
    // Служебная ветка match не производится: эмитится внутри visit_MatchingStmt.
    faultUnexpectedKind("StmtEmitter", node);
}
} // namespace trust
