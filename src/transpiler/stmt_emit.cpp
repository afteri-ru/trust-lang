// Generated: src/transpiler/stmt_emit.cpp
#include "transpiler/stmt_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token_base.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_visitor.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/solver.hpp"
#include "syntax/term.h"
#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "transpiler/diag.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>

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
        scope.emplace(m_ectx.m_ctx.source(), blockRange, output_idx);
    }

    // Нормальное многострочное форматирование: '{' в конце строки, операторы - с отступом.
    // Входим во вложенный скоуп: отступ +1 (функция/блок - только уровень отступа).
    m_ectx.m_ctx.source().output_append(output_idx, " {");
    m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
    m_ectx.m_ctx.source().output_append(output_idx, "\n");
    // Внутри C++ compound statement: пользовательские блоки эмитятся как `{ }`, не как
    // `namespace { }` (namespace внутри compound statement в C++ недопустим).
    const bool savedCppBlock = m_ectx.m_inCppBlock;
    m_ectx.m_inCppBlock = true;
    // afterOpen - текст сразу после '{' (например, установка флага while-else).
    if (!afterOpen.empty()) {
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, afterOpen);
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
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
        const size_t before = m_ectx.m_ctx.source().output_body(output_idx).size();
        // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход;
        // для остальных операторов - отступ текущего блока.
        if (!is_block_kind(child->kind())) {
            m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        }
        m_driver.generateNodeToFile(*child, output_idx);
        // Не оставлять пустую строку, если узел ничего не эмитил (напр. подавленный doc-bundle),
        // и не дублировать перевод строки, если блок уже закончился '\n' (emitSequenceBody).
        if (m_ectx.m_ctx.source().output_body(output_idx).size() != before) {
            const std::string_view body = m_ectx.m_ctx.source().output_body(output_idx);
            if (body.empty() || body.back() != '\n') {
                m_ectx.m_ctx.source().output_append(output_idx, "\n");
            }
        }
    }
    // continue-метка do-while / именованного блока вставляется перед закрывающей '}'.
    if (!beforeCloseLabel.empty()) {
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, beforeCloseLabel);
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    }
    // Пост-условия функции (--solver-mode=assert): проверка перед '}' (возвращаемое значение в скоупе).
    if (postTrust) {
        m_driver.m_contract.emitTrustChecks(*postTrust);
    }
    m_ectx.m_inCppBlock = savedCppBlock;
    m_ectx.m_scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(output_idx, "}");
}

void StmtEmitter::generateIfToFile(const IfStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ectx.m_ctx.source(), node.range(), output_idx);

    // if (cond) { then }
    m_ectx.m_ctx.source().output_append(output_idx, "if (");
    m_driver.emitExpr(node.m_cond.get());
    m_ectx.m_ctx.source().output_append(output_idx, ")");
    m_driver.emitBodyNode(node.m_body, output_idx);

    // else if (cond2) { body2 } ...
    for (const auto& [cond, body] : node.m_elseifs) {
        m_ectx.m_ctx.source().output_append(output_idx, " else if (");
        m_driver.emitExpr(cond.get());
        m_ectx.m_ctx.source().output_append(output_idx, ")");
        m_driver.emitBodyNode(body, output_idx);
    }

    // else { ... }
    if (node.m_else) {
        m_ectx.m_ctx.source().output_append(output_idx, " else");
        m_driver.emitBodyNode(node.m_else, output_idx);
    }
}

void StmtEmitter::generateWhileToFile(const WhileStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ectx.m_ctx.source(), node.range(), output_idx);

    // while-else: в C++ нет 'while...else'. Флаг «вошёл ли цикл хотя бы раз» создаёт LOWERING
    // (m_elseFlag, `bool _weN := 0;`); здесь эмитим его объявление и используем имя:
    //   while (cond) { _weN = true; body; } if (!_weN) { else; }
    std::string flag;
    if (node.m_else) {
        const VarDecl* f = nullptr;
        if (node.m_elseFlag && node.m_elseFlag->kind() == ParserToken::Kind::VarDecl) {
            f = static_cast<const VarDecl*>(node.m_elseFlag.get());
        }
        if (!f) {
            m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "while-else flag temporary is missing (lowering did not synthesize it)");
            return;
        }
        flag = emitSyntheticVar(*f, output_idx);
        if (flag.empty()) {
            m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "unable to generate C++ type for while-else flag '{}'", f->text());
            return;
        }
        // emitSyntheticVar закончил строкой перевода; позиционируем `while` на отступ statement'а.
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
    }

    m_ectx.m_ctx.source().output_append(output_idx, "while (");
    m_driver.emitExpr(node.m_cond.get());
    m_ectx.m_ctx.source().output_append(output_idx, ")");
    m_driver.emitBodyNode(node.m_body, output_idx, /*mapBlock=*/true, /*beforeClose=*/"", /*afterOpen=*/(flag.empty() ? "" : flag + " = true;"));

    if (node.m_else) {
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "if (!" + flag + ")");
        m_driver.emitBodyNode(node.m_else, output_idx);
    }
}

void StmtEmitter::generateDoWhileToFile(const DoWhileStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ectx.m_ctx.source(), node.range(), output_idx);

    m_ectx.m_ctx.source().output_append(output_idx, "do");
    // Тело не маппится отдельно: begin тела совпадает с begin statement'а (do-while начинается
    // с '{'), иначе коллизия trustKey в mapStop. Всё покрывает единый range statement'а.
    // continue-метка именованного блока вставляется анализатором в конец тела (LabelStmt).
    m_driver.emitBodyNode(node.m_body, output_idx, /*mapBlock=*/false);
    m_ectx.m_ctx.source().output_append(output_idx, " while (");
    m_driver.emitExpr(node.m_cond.get());
    m_ectx.m_ctx.source().output_append(output_idx, ");");
}

std::string StmtEmitter::emitSyntheticVar(const VarDecl& vd, MapperFile output_idx) {
    const std::string name = utils::name_to_cpp(vd.text());
    if (name.empty()) {
        return {};
    }
    const TypeId vt = vd.inferredType;
    std::string typePart;
    if (vt != INVALID_TYPE_ID) {
        // displayName - КАНОНИЧЕСКОЕ имя типа (а не имя переменной): для пользовательских типов
        // (enum/variant) resolveCppTypeId использует его как C++-имя (иначе имя `_matchN` дало бы `c__matchN`).
        auto cppType = m_driver.m_type.emitTypeName(vt, m_ectx.m_ctx.types().getFullTypeName(vt));
        if (!cppType || cppType->empty()) {
            return {};
        }
        if (vd.has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly)) {
            m_ectx.m_ctx.source().output_append(output_idx, "const ");
        }
        typePart = *cppType + " ";
    } else {
        // Без выведенного типа (напр. временная копия источника деструктуризации) - auto (дедуцируется).
        typePart = "auto ";
    }
    m_ectx.m_ctx.source().output_append(output_idx, typePart + name + " = ");
    m_driver.emitExpr(vd.m_initializer.get());
    m_ectx.m_ctx.source().output_append(output_idx, ";");
    m_ectx.m_ctx.source().output_append(output_idx, "\n");
    // Хвостовой отступ НЕ добавляем: вызывающий позиционирует следующую строку (match/while-else
    // добавляют indentPrefix; деструктуризация использует собственный `ind` для каждой строки).
    return name;
}

void StmtEmitter::generateMatchToFile(const MatchStmt& node, MapperFile output_idx) {
    MapperScope scope(m_ectx.m_ctx.source(), node.range(), output_idx);

    // Scrutivee-временная (_matchN) создана LOWERING как const VarDecl в AST (инвариант «временные
    // создаёт анализатор»). Здесь эмитим объявление и читаем её тип для выбора switch/enum/if.
    const VarDecl* temp = nullptr;
    if (node.m_tempDecl && node.m_tempDecl->kind() == ParserToken::Kind::VarDecl) {
        temp = static_cast<const VarDecl*>(node.m_tempDecl.get());
    }
    if (!temp) {
        m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "match scrutinee temporary is missing (lowering did not synthesize it)");
        return;
    }
    // Синтетическое объявление временной эмитим через emitSyntheticVar (без собственного source-map:
    // у синтетического узла невалидный range, и mapStart это запрещает; весь match мапится общим
    // MapperScope выше). Возвращает C++-имя временной или "" при ошибке типа.
    const std::string tmp = emitSyntheticVar(*temp, output_idx);
    if (tmp.empty()) {
        m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "unable to generate C++ type for match scrutinee '{}'", temp->text());
        return;
    }
    const TypeId vt = temp->inferredType;
    // emitSyntheticVar закончил строкой перевода; позиционируем следующую строку на отступ statement'а.
    m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());

    // Атрибут @[matcher("fn")]: переопределение функции сравнения (по значению). В каждой ветке
    // вместо (tmp == pattern) эмитим вызов fn(tmp, pattern). Имя доверенной функции резолвим в
    // C++-символ так же, как visit_Ident: нативный импорт-алиас либо манглинг trust-имени;
    // нативный символ '%...' срезает '%'.
    std::string matcherCpp;
    if (const auto matcher_id = m_ectx.m_ctx.attrs().lookup(attr::Matcher); matcher_id.has_value() && node.has_attr(*matcher_id)) {
        if (const std::vector<std::string>* args = node.attr_args(*matcher_id); args && !args->empty() && !(*args)[0].empty()) {
            const std::string mname = (*args)[0];
            if (mname.front() == '%') {
                matcherCpp = mname.substr(1);
            } else if (const auto it = m_ectx.m_nativeImports.find(mname); it != m_ectx.m_nativeImports.end()) {
                matcherCpp = it->second;
            } else {
                matcherCpp = utils::name_to_cpp(mname);
            }
        }
    }

    // Оператор сопоставления (node.m_op):
    //   "~>" / "~~>" / "~~~>" - сопоставление ПО ТИПУ (trust::typeMatches<T>) - только if/else;
    //   "" / "=>" / "==>" / "===>" - сравнение ПО ЗНАЧЕНИЮ: целочисленное значение/перечисление
    //                               эмитится как C++ switch, остальные типы (строки и т.п.) -
    //                               как if/else-if цепочка.
    const bool typeMatch = (node.m_op == "~>" || node.m_op == "~~>" || node.m_op == "~~~>");
    // Matcher (функция сравнения) неприменим к switch (case-метки требуют констант) - только if/else.
    const bool useMatcher = !matcherCpp.empty();

    if (!typeMatch && !useMatcher && canEmitMatchSwitch(node)) {
        emitMatchSwitch(node, tmp, vt, output_idx);
    } else {
        emitMatchIfElse(node, tmp, typeMatch, matcherCpp, output_idx);
    }
}

void StmtEmitter::emitMatchIfElse(const MatchStmt& node, const std::string& tmp, bool typeMatch, const std::string& matcherCpp, MapperFile output_idx) {
    // Последовательное сравнение с шаблонами: if / else-if / else.
    //   по значению: (tmp == p1) || (tmp == p2) ...
    //   по типу:     trust::typeMatches<T>(tmp) - для полиморфных значений (классы прерываний
    //               IntAny/*) dynamic_cast c учётом подтипов, для не-полиморфных - точное
    //               typeid-сравнение. Паттерн - TypeName (его C++-имя резолвит emitTypeNameForNode).
    bool first = true;
    for (const auto& c : node.m_cases) {
        m_ectx.m_ctx.source().output_append(output_idx, first ? "if (" : " else if (");
        first = false;
        for (size_t j = 0; j < c.patterns.size(); ++j) {
            if (j) {
                m_ectx.m_ctx.source().output_append(output_idx, " || ");
            }
            if (typeMatch) {
                std::string cpp = m_driver.m_type.emitTypeNameForNode(c.patterns[j].get());
                if (cpp.empty()) {
                    continue; // emitTypeNameForNode уже сообщил диагностику
                }
                m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
                m_ectx.m_ctx.source().output_append(output_idx, "trust::typeMatches<" + cpp + ">(" + tmp + ")");
            } else if (!matcherCpp.empty()) {
                // Переопределённая функция сравнения: matcher(tmp, pattern).
                m_ectx.m_ctx.source().output_append(output_idx, matcherCpp + "(" + tmp + ", ");
                m_driver.emitExpr(c.patterns[j].get());
                m_ectx.m_ctx.source().output_append(output_idx, ")");
            } else {
                m_ectx.m_ctx.source().output_append(output_idx, tmp + " == ");
                m_driver.emitExpr(c.patterns[j].get());
            }
        }
        m_ectx.m_ctx.source().output_append(output_idx, ")");
        m_driver.emitBodyNode(c.body, output_idx);
    }
    if (node.m_default) {
        m_ectx.m_ctx.source().output_append(output_idx, " else");
        m_driver.emitBodyNode(node.m_default, output_idx);
    }
}

bool StmtEmitter::isIntegralTypeId(TypeId tid) const {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    const Group g = getGroup(getKindFromId(m_ectx.m_ctx.types().getCanonicalTypeId(tid)));
    return g == Group::kIntegers || g == Group::kUnsigned || g == Group::kLogical;
}

bool StmtEmitter::canEmitMatchSwitch(const MatchStmt& node) const {
    if (!node.m_tempDecl || node.m_tempDecl->kind() != ParserToken::Kind::VarDecl) {
        return false;
    }
    const TypeId vt = static_cast<const VarDecl*>(node.m_tempDecl.get())->inferredType;
    if (vt == INVALID_TYPE_ID) {
        return false;
    }
    const TypeId canonical = m_ectx.m_ctx.types().getCanonicalTypeId(vt);
    const Group g = getGroup(getKindFromId(canonical));

    // Целочисленное значение → switch по tmp, если все шаблоны - целые литералы (case-метки
    // обязаны быть константными выражениями). Не-литеральный шаблон → fallback на if/else.
    if (g == Group::kIntegers || g == Group::kUnsigned) {
        for (const auto& c : node.m_cases) {
            if (c.patterns.empty()) {
                return false;
            }
            for (const auto& p : c.patterns) {
                if (!p || p->kind() != ParserToken::Kind::IntLiteral) {
                    return false;
                }
            }
        }
        return true;
    }

    // Enum → switch по tmp.value (лежащий целочисленный тип значений): каждый шаблон обязан быть
    // членом enum, разрешимым в статическую константу. Иначе - fallback на if/else.
    if (g == Group::kEnums) {
        const auto* ed = m_ectx.m_ctx.types().getTypeDataAs<EnumTypeData>(canonical);
        if (!ed || !isIntegralTypeId(ed->valueType)) {
            return false;
        }
        for (const auto& c : node.m_cases) {
            if (c.patterns.empty()) {
                return false;
            }
            for (const auto& p : c.patterns) {
                if (!enumMemberConstant(p.get())) {
                    return false;
                }
            }
        }
        return true;
    }

    return false;
}

std::optional<std::string> StmtEmitter::enumMemberConstant(const AstNodeBase* pattern) const {
    // Шаблон - доступ к члену enum `Color.RED` (MemberAccess: слева - имя типа enum, справа - член).
    if (!pattern || pattern->kind() != ParserToken::Kind::MemberAccess) {
        return std::nullopt;
    }
    const auto& ma = static_cast<const Binary&>(*pattern);
    if (!ma.m_left || ma.m_left->kind() != ParserToken::Kind::Ident || !ma.m_right) {
        return std::nullopt;
    }
    const auto tid = m_ectx.m_ctx.types().findType(ma.m_left->text());
    if (!tid.has_value() || !isEnumType(*tid, m_ectx.m_ctx.types())) {
        return std::nullopt;
    }
    const auto* ed = m_ectx.m_ctx.types().getTypeDataAs<EnumTypeData>(*tid);
    if (!ed) {
        return std::nullopt;
    }
    const std::string memberName = std::string(ma.m_right->text());
    for (const auto& m : ed->members) {
        if (m.name == memberName) {
            if (m.value.empty()) {
                return std::nullopt; // значение члена не нормализовано - не статическая константа
            }
            return memberValueCpp(m_ectx.m_ctx.types(), ed->valueType, m.value);
        }
    }
    return std::nullopt;
}

void StmtEmitter::emitMatchSwitch(const MatchStmt& node, const std::string& tmp, TypeId valueType, MapperFile output_idx) {
    // Scrutivee-временная уже эмитирована выше (m_tempDecl). Enum-значение - структура
    // trust::Enum<Value,N,Self>; switch по её лежащему целочисленному полю `value` (case-метки -
    // скалярные константы членов).
    const bool enumValue = valueType != INVALID_TYPE_ID && isEnumType(m_ectx.m_ctx.types().getCanonicalTypeId(valueType), m_ectx.m_ctx.types());

    m_ectx.m_ctx.source().output_append(output_idx, "switch (" + tmp + (enumValue ? ".value" : "") + ") {");
    m_ectx.m_ctx.source().output_append(output_idx, "\n");
    m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});

    for (const auto& c : node.m_cases) {
        // Case-метки ветки: по одной на шаблон (несколько элементов одного условия → несколько
        // `case` подряд). После последней метки тело следует сразу ("{" подставит emitBodyNode).
        for (size_t j = 0; j < c.patterns.size(); ++j) {
            m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
            m_ectx.m_ctx.source().output_append(output_idx, "case ");
            if (enumValue) {
                m_ectx.m_ctx.source().output_append(output_idx, *enumMemberConstant(c.patterns[j].get()));
            } else {
                m_driver.emitExpr(c.patterns[j].get());
            }
            m_ectx.m_ctx.source().output_append(output_idx, ":");
            if (j + 1 < c.patterns.size()) {
                m_ectx.m_ctx.source().output_append(output_idx, "\n");
            }
        }
        // Тело ветки + break (C++ switch «проваливается» без break - обязателен после каждой ветки).
        m_driver.emitBodyNode(c.body, output_idx);
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "break;");
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    }

    if (node.m_default) {
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "default:");
        m_driver.emitBodyNode(node.m_default, output_idx);
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "break;");
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    }

    m_ectx.m_scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(output_idx, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(output_idx, "}");
    m_ectx.m_ctx.source().output_append(output_idx, "\n");
}

void StmtEmitter::emitCompoundScope(const ScopeBlock& n) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "{");
    m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "\n");
    m_driver.emitSequenceBody(n, m_ectx.m_out);
    m_ectx.m_scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "}");
}

void StmtEmitter::emitNamespaceScope(const ScopeBlock& n, const std::string& name) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "namespace");
    if (!name.empty()) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, " " + name);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, " {");
    m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "\n");
    m_driver.emitSequenceBody(n, m_ectx.m_out);
    m_ectx.m_scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "}");
}

void StmtEmitter::visit_ScopeBlock(const ScopeBlock& n) {
    const std::string_view text = n.text();
    // Безымянный блок кода: последовательность операторов как одно выражение.
    const bool isCodeBlock = text.empty() || text == "{";
    // Именованный блок-метка: одно имя (идентификатор) без '::' - label { ... }.
    const bool isLabel = !isCodeBlock && !n.is_hidden() && text.find("::") == std::string_view::npos;
    // Глобальная область имён: ':: { ... }'.
    const bool isGlobalNs = text == "::";

    // Блок кода / именованная метка разрешены ТОЛЬКО внутри функции/класса.
    if (isCodeBlock || isLabel) {
        if (!m_ectx.m_inCppBlock) {
            if (isLabel) {
                m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "named block '{}' is only allowed inside a function", std::string(n.name()));
            } else {
                m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unnamed code block is only allowed inside a function");
            }
            return;
        }
        emitCompoundScope(n);
        return;
    }

    // Область имён (`ns::`, `_`) разрешена только на верхнем уровне модуля.
    if (!isGlobalNs && m_ectx.m_inCppBlock) {
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "namespace block is not allowed inside a function");
        return;
    }

    // Глобальная область '::' - содержимое без namespace-обёртки.
    if (isGlobalNs) {
        m_driver.emitSequenceBody(n, m_ectx.m_out);
        return;
    }

    // Именованная 'ns::' либо скрытая '_' область имён.
    const bool hidden = n.is_hidden();
    const std::string nsName = hidden ? "" : utils::name_to_cpp(m_ectx.namespaceCppName(text));
    if (hidden) {
        ++m_ectx.m_hiddenNamespaceDepth;
    } else {
        m_ectx.m_namespaceStack.push_back(nsName);
    }
    emitNamespaceScope(n, nsName);
    if (hidden) {
        --m_ectx.m_hiddenNamespaceDepth;
    } else {
        m_ectx.m_namespaceStack.pop_back();
    }
}

void StmtEmitter::visit_DestructureDecl(const DestructureDecl& n) {
    // `t1, ..., tN := [... ]source;` - деструктуризация. Spread (`... source`) - коллекция (Dict,
    // pop_front + «остаток»); без `...` - кортеж (std::get). `_` - skip (потребляется, не связывается).
    if (n.m_targets.empty() || !n.m_source) {
        // Семантика всегда заполняет цели и источник; пустой узел - инвариантное нарушение.
        // Вместо тихого no-op (AGENTS rule 5 «no silent fallback») - явная диагностика.
        m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "destructuring requires at least one target and a source expression");
        return;
    }
    // DestructureDecl - НЕ statement-выражение (не оборачивается SemicolonStmt, который добавляет
    // mapStart/mapStop), поэтому собственный маппинг здесь обязателен: иначе оператор раскрытия
    // словаря/кортежа не имел бы записи в source map и не мапился бы на выходной .cppt. Весь
    // диапазон оператора `t1, ..., tN := [... ]source;` покрывает все эмитируемые строки
    // (temp-источник + runtime-guard + pop_front'ы / std::get + rest) - как у ControlFlowStmt.
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    if (n.m_isSpread) {
        emitDestructureDict(n);
    } else {
        emitDestructureTuple(n);
    }
}

void StmtEmitter::emitDestructureDict(const DestructureDecl& n) {
    const std::string ind = m_ectx.indentPrefix();
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
    // Используется только для rest-переменной ниже (сам temp-источник создаёт lowering, m_sourceTemp).
    const bool mutatingRest = !restCpp.empty() && restCpp == srcCpp;
    // Источник: при mutating-rest - сам источник (pop'ы идут прямо в него); иначе - временная копия
    // (создана LOWERING, m_sourceTemp: `auto _trust_dst_N := <source>;`), из которой делаются
    // pop_front и rest-копия (одна оценка источника-выражения, не N раз).
    std::string srcRef = srcCpp;
    if (n.m_sourceTemp && n.m_sourceTemp->kind() == ParserToken::Kind::VarDecl) {
        const VarDecl* tmp = static_cast<const VarDecl*>(n.m_sourceTemp.get());
        srcRef = emitSyntheticVar(*tmp, m_ectx.m_out);
        if (srcRef.empty()) {
            m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to generate C++ type for destructuring source temporary '{}'", tmp->text());
            return;
        }
    }
    // Runtime-недостаток элементов (динамический источник - размер неизвестен на этапе сборки):
    // точная привязка требует, чтобы число элементов было не меньше числа pop'ов. Guard с понятной
    // диагностикой вместо голого std::out_of_range из pop_front (AGENTS rule 5 - без тихого дефолта
    // / None; для кортежа арность проверяется статически - guard здесь не нужен, под-кортежи pop_front
    // не делают).
    // Имя файла и строку в сообщении берём из ИСХОДНОГО .src, а не из сгенерированного C++ (как
    // @__FILE_NAME__/@__FILE_LINE__ для @assert): через SourceMap по диапазону узла деструктуризации.
    const SourceLocation nloc = sourceLocation(m_ectx.m_ctx.source(), n.range());
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
        m_driver.m_type.recordRequiredInclude("@trust/assert.hpp");
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind + "if (" + srcRef + ".size() < " + std::to_string(needPops) + ") trust::trust__abort__(\"" +
                                                              utils::escape_cpp_string(nloc.file) + "\", " + std::to_string(nloc.line) +
                                                              ", \"destructuring: not enough elements in source\");\n");
    }
    // Цели-элементы (НЕ rest): pop_front. Вне цикла - per-element тип элемента (any_cast<T>, где T -
    // runtime-тип: Int8..Int64 → int64_t, Float → double, Bool, StrChar...). ВНУТРИ цикла тип расширен
    // до максимального (Integer/Double); гетерогенность (bool+int) обрабатывается runtime-конвертерами
    // (anyToInt64/anyToDouble/anyToString), а не строгим any_cast. Any → std::any.
    const TypeRegistry& reg = m_ectx.m_ctx.types();
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
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        if (t->text() == "_") {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, srcRef + ".pop_front();\n");
            continue;
        }
        const std::string cppName = utils::name_to_cpp(t->text());
        const TypeId et = (i < n.m_targetTypes.size()) ? n.m_targetTypes[i] : INVALID_TYPE_ID;
        const TypeId etC = (et != INVALID_TYPE_ID) ? reg.getCanonicalTypeId(et) : INVALID_TYPE_ID;
        // Префикс объявления: присваивание (`a = ...`) - без типа; объявление - тип + имя.
        const std::string anyPrefix = n.m_isAssign ? "" : "std::any ";
        if (etC == INVALID_TYPE_ID) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, anyPrefix + cppName + " = " + srcRef + ".pop_front();\n");
            continue;
        }
        // Тип переменной: аннотация цели (m_targetDeclaredTypes) или выведенный (et).
        const TypeId declared = (i < n.m_targetDeclaredTypes.size() && n.m_targetDeclaredTypes[i] != INVALID_TYPE_ID) ? n.m_targetDeclaredTypes[i] : et;
        const auto tname = m_driver.m_type.emitTypeName(declared, t->text());
        if (!tname || tname->empty()) {
            // Тип цели уже резолвлен семантикой (naturalRuntimeType возвращает только типы с
            // C++-именем: Int64/Double/Bool/StrChar/StrWide/Any); сбой emitTypeName - инвариантное
            // нарушение. Не тихий fallback на std::any (AGENTS rule 5) - явная диагностика.
            m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to emit C++ type for destructuring target '{}'", cppName);
            return;
        }
        // Тип any_cast: natural runtime тип ЭЛЕМЕНТА (m_targetTypes) - соответствует хранению Dict
        // (int → int64_t); переменная объявляется типом declared (аннотация/выведенный).
        const auto castName = m_driver.m_type.emitTypeName(et, t->text());
        const std::string castType = (castName && !castName->empty()) ? *castName : *tname;
        const std::string prefix = n.m_isAssign ? "" : (*tname + " ");
        if (n.m_inLoop) {
            if (etC == i64C) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = trust::detail::anyToInt64(" + srcRef + ".pop_front());\n");
            } else if (etC == dblC) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = trust::detail::anyToDouble(" + srcRef + ".pop_front());\n");
            } else if (etC == scC) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = trust::detail::anyToString(" + srcRef + ".pop_front());\n");
            } else {
                // Bool и пр. - однородные: строгий any_cast безопасен (хранится как есть).
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
            }
        } else {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + cppName + " = std::any_cast<" + castType + ">(" + srcRef + ".pop_front());\n");
        }
    }
    // Именованный rest: «остаток» - копия источника после pop'ов (источник не мутируется);
    // в режиме присваивания - `rest = src` (цель уже существует).
    if (!mutatingRest && !restCpp.empty()) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, (n.m_isAssign ? "" : "trust::Dict ") + restCpp + " = " + srcRef + ";\n");
    }
    // mutatingRest - источник уже мутирован pop'ами и является rest; присвоение не нужно.
    // `_...` (отброс остатка) - ничего не генерируем: остаток остаётся во временной переменной.
}

void StmtEmitter::emitDestructureTuple(const DestructureDecl& n) {
    m_driver.m_type.recordRequiredInclude("#include <tuple>");
    const std::string ind = m_ectx.indentPrefix();
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
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        }
        first = false;
        // Префикс объявления: присваивание (`a = ...`) - без типа; явная аннотация (`a:Int32`,
        // m_targetTypes[i] заполнена семантикой) - фиксированный тип; иначе `auto` (элемент кортежа).
        std::string prefix;
        if (n.m_isAssign) {
            prefix.clear();
        } else if (i < n.m_targetTypes.size() && n.m_targetTypes[i] != INVALID_TYPE_ID) {
            const auto tname = m_driver.m_type.emitTypeName(n.m_targetTypes[i], t->text());
            prefix = (tname && !tname->empty()) ? (*tname + " ") : "auto ";
        } else {
            prefix = "auto ";
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, prefix + utils::name_to_cpp(t->text()) + " = std::get<" + std::to_string(idx) + ">(");
        m_driver.emitExpr(n.m_source.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ");\n");
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
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ind);
        }
        first = false;
        const std::string restPrefix = n.m_isAssign ? "" : "auto ";
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, restPrefix + utils::name_to_cpp(t->text()) + " = std::make_tuple(");
        for (size_t k = idx; k < elemCount; ++k) {
            if (k != idx) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_out, ", ");
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "std::get<" + std::to_string(k) + ">(");
            m_driver.emitExpr(n.m_source.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ")");
        }
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ");\n");
    }
}

void StmtEmitter::visit_ReturnStmt(const JumpStmt& n) {
    // НЕИМЕНОВАННОЕ `++ value ++` - ВСЕГДА «положительное прерывание» (сквозной возврат через
    // вызовы функций), НЕ зависит от наличия/глубины обработчиков перехвата: throw trust::IntPlus.
    // ИМЕНОВАННОЕ `func ++ value ++` - обычный return из функции.
    if (!n.m_label) {
        m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
        MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
        if (n.m_value) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "throw trust::IntPlus(");
            m_driver.emitExpr(n.m_value.get());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, ");");
        } else {
            // `++ _ ++` - void-прерывание (`_` - пустое значение).
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "throw trust::IntPlus();");
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
            const auto* tc = dynamic_cast<const TrustContract*>(t.get());
            if (t && tc && tc->kind == PropertyKind::Post) {
                post.push_back(t);
            }
        }
        if (!post.empty()) {
            // Маппим сгенерированный код на диапазон оператора return (иначе он наследует
            // маппинг тела функции → неправильный диапазон: клик по `return ...;` не ведёт
            // в C++ и обратно). Как в visit_BreakStmt/visit_SemicolonStmt.
            MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
            // Hoist-временная возврата создана СЕМАНТИКОЙ (m_tempDecl, инвариант «временные создаёт
            // анализатор»): эмитим её объявление через emitSyntheticVar (const <Ret> c___trust_res_N
            // = <value>;) и читаем её C++-имя для пост-проверок и return.
            const VarDecl* temp = nullptr;
            if (n.m_tempDecl && n.m_tempDecl->kind() == ParserToken::Kind::VarDecl) {
                temp = static_cast<const VarDecl*>(n.m_tempDecl.get());
            }
            if (!temp) {
                m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "return value temporary is missing (analyzer did not synthesize it)");
                return;
            }
            const std::string tmp = emitSyntheticVar(*temp, m_ectx.m_out);
            if (tmp.empty()) {
                m_ectx.m_ctx.report(n.range(), diag::DiagId::ParseError, "unable to generate C++ type for return temporary '{}'", temp->text());
                return;
            }
            { // RAII: имя функции → temp возврата (восстанавливается в деструкторе).
                ResultGuard guard(m_ectx, std::string(n.m_funcDecl->text()), tmp);
                m_driver.m_contract.emitTrustChecks(post);
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, m_ectx.indentPrefix());
            m_ectx.m_ctx.source().output_append(m_ectx.m_out, "return " + tmp + ";\n");
            return;
        }
    }
    m_driver.emitJumpValue("return", n);
}

void StmtEmitter::visit_ThrowStmt(const JumpStmt& n) {
    // Отрицательное прерывание (-- err -- / -- --) - ошибка: бросаем trust::IntMinus
    // (переносимое значение - в value(); классификация по классу прерывания, см. errors.md).
    m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    if (n.m_value) {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "throw trust::IntMinus(");
        m_driver.emitExpr(n.m_value.get());
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, ");");
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_out, "throw trust::IntMinus();");
    }
}

void StmtEmitter::visit_BreakStmt(const JumpStmt& n) {
    // Безымянный break - обычный C++ break;. Именованный break анализатор переписывает
    // в GotoStmt (или void-ReturnStmt при break по имени функции), поэтому сюда доходит
    // только безымянный.
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "break;");
}

void StmtEmitter::visit_ContinueStmt(const JumpStmt& n) {
    // Безымянный continue - обычный C++ continue;. Именованный continue анализатор
    // переписывает в GotoStmt, поэтому сюда доходит только безымянный.
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "continue;");
}

// Goto/Label - синтетические узлы lowering: goto по метке / определение метки.
// Маппинг НЕ строится: у синтетических узлов нет исходного trust-текста, а их range
// (блок/цикл) уже смапплен реальными узлами (иначе коллизия trustKey в mapStop).
void StmtEmitter::visit_GotoStmt(const LabelRef& n) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, "goto " + n.m_name + ";");
}

void StmtEmitter::visit_LabelStmt(const LabelRef& n) {
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, n.m_name + ":;");
}

// SemicolonStmt - выражение в позиции оператора: выражение в statement-root (без скобок) +
// завершающая ';'. Маппинг range берётся от обёрнутого выражения (SemicolonStmt::range() делегирует).
void StmtEmitter::visit_SemicolonStmt(const SemicolonStmt& n) {
    // Сброс `x = _;` (присваивание маркера `_` - «без значения»): НЕ эмитит запись значения.
    // Значение не определено (переменная снова неинициализирована; анализатор запрещает чтение
    // до записи), поэтому `c_x = c__;` - мусорный код. Оператор сброса кода не даёт (no-op).
    if (n.m_expr && n.m_expr->kind() == ParserToken::Kind::AssignOp) {
        const auto& b = static_cast<const Binary&>(*n.m_expr);
        if (utils::isPlainAssignOp(b.text()) && isNoneMarker(b.m_right.get())) {
            return; // reset `x = _;`: без записи значения (и без ';')
        }
    }
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    if (n.m_expr) {
        m_driver.generateNodeToFile(*n.m_expr, m_ectx.m_out);
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_out, ";");
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
    emitBlockBodyToFile(body, n.range(), m_ectx.m_out, /*mapBlock=*/true, /*beforeCloseLabel=*/"", /*afterOpen=*/"");
}

void StmtEmitter::visit_IfStmt(const IfStmt& n) {
    generateIfToFile(n, m_ectx.m_out);
}

void StmtEmitter::visit_WhileStmt(const WhileStmt& n) {
    generateWhileToFile(n, m_ectx.m_out);
}

void StmtEmitter::visit_DoWhileStmt(const DoWhileStmt& n) {
    generateDoWhileToFile(n, m_ectx.m_out);
}

void StmtEmitter::visit_MatchingStmt(const MatchStmt& n) {
    generateMatchToFile(n, m_ectx.m_out);
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
    auto& out = m_ectx.m_out;

    std::string flag;
    if (node.m_else) {
        const VarDecl* f = nullptr;
        if (node.m_failFlag && node.m_failFlag->kind() == ParserToken::Kind::VarDecl) {
            f = static_cast<const VarDecl*>(node.m_failFlag.get());
        }
        if (!f) {
            m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "with-else flag temporary is missing (lowering did not synthesize it)");
            return;
        }
        flag = emitSyntheticVar(*f, output_idx);
        if (flag.empty()) {
            m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "unable to generate C++ type for with-else flag '{}'", f->text());
            return;
        }
        // emitSyntheticVar закончил строкой перевода; позиционируем `try {` на отступ statement'а.
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(out, "try {");
    } else {
        m_ectx.m_ctx.source().output_append(out, "{");
    }
    m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
    m_ectx.m_ctx.source().output_append(out, "\n");

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
            m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
            if (lock->kind() == ParserToken::Kind::VarDecl) {
                // Locker-темп: auto __cap_N = ref.lock(); (держится; emitSyntheticVar завершает переводом).
                emitSyntheticVar(static_cast<const VarDecl&>(*lock), out);
            } else {
                // Голый лок `with(*ref)`: временный Locker, снимается сразу после выражения.
                m_driver.emitExpr(lock.get());
                m_ectx.m_ctx.source().output_append(out, ";");
                m_ectx.m_ctx.source().output_append(out, "\n");
            }
        }
        if (binding) {
            m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
            m_driver.m_decl.generateVarDeclToFile(static_cast<const VarDecl&>(*binding), out);
            m_ectx.m_ctx.source().output_append(out, "\n");
        }
    }
    // Флаг «все биндинги захвачены» - только ПОСЛЕ всех инициализаторов (исключение в теле → throw).
    if (!flag.empty()) {
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(out, flag + " = true;\n");
    }
    if (node.m_body) {
        m_driver.emitBodyNode(node.m_body, out, /*mapBlock=*/false);
    }

    m_ectx.m_scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(out, "}");
    if (!flag.empty()) {
        // catch(...): если исключение из инициализатора (флаг не установлен) → else (объекты уже
        // уничтожены размоткой стека); если из тела (флаг установлен) → пробрасываем дальше.
        m_ectx.m_ctx.source().output_append(out, " catch (...) {");
        m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
        m_ectx.m_ctx.source().output_append(out, "\n");
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(out, "if (" + flag + ") throw;");
        m_ectx.m_ctx.source().output_append(out, "\n");
        if (node.m_else) {
            m_driver.emitBodyNode(node.m_else, out, /*mapBlock=*/false);
        }
        m_ectx.m_scopeStack.pop_back();
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(out, "}");
    }
}

void StmtEmitter::visit_WithStmt(const WithStmt& n) {
    generateWithToFile(n, m_ectx.m_out);
}

void StmtEmitter::visit_AssignmentStmt(const AstNodeAttr&) {
}

void StmtEmitter::visit_BlockStmt(const AstNodeAttr&) {
}

void StmtEmitter::visit_ThenBlock(const AstNodeAttr&) {
}

void StmtEmitter::visit_ElseBlock(const AstNodeAttr&) {
}

void StmtEmitter::visit_WhileElseBlock(const AstNodeAttr&) {
}

void StmtEmitter::visit_TryCatchStmt(const TryCatchStmt& n) {
    // Оператор перехвата `try { A } catch(:T){B} catch(...){C} else{D};` (Вариант A, нативный C++ catch).
    // Диспетчеризация - самим C++ по типу исключения (учёт подтипов); else сворачивается последним
    // оператором try (Python-семантика: выполняется, если прерываний НЕ было).
    // catch(...) (m_type==nullptr) → catch(trust::IntAny&) - любые не перечисленные, должен быть последним.
    m_driver.m_type.recordRequiredInclude("@trust/interrupt.hpp");
    MapperScope scope(m_ectx.m_ctx.source(), n.range(), m_ectx.m_out);
    auto& out = m_ectx.m_out;
    // Отступ перед 'try {' выставляет родитель (TryCatchStmt не блочный kind - см. is_block_kind).
    m_ectx.m_ctx.source().output_append(out, "try {");
    m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
    m_ectx.m_ctx.source().output_append(out, "\n");
    m_driver.emitSequenceBody(n, out);
    if (n.m_else) {
        // else - последним оператором try (Python-семантика: выполняется, если прерываний НЕ было).
        // Документирующий маркер else-ветки (подавляется -fno-comments). Маппинг для него НЕ создаётся:
        // маппинг тела else автоматический (MapperScope на уровне statements ниже).
        if (m_ectx.m_ctx.opts().is_enabled(transpiler::FlagKind::Comments)) {
            m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
            m_ectx.m_ctx.source().output_append(out, "// else\n");
        }
        if (const auto* eseq = n.m_else->as_sequence()) {
            m_driver.emitSequenceBody(*eseq, out); // расплющиваем блок else в try
        } else {
            m_driver.emitBodyNode(n.m_else, out);
        }
    }
    m_ectx.m_scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
    m_ectx.m_ctx.source().output_append(out, "}");
    if (n.m_catches.empty()) {
        // Без веток (напр. `try { A };` = {* A *}, `{- A -}`, `{+ A +}`) - дефолтный catch по классу
        // блока (TermID): BLOCK_PLUS→IntPlus, BLOCK_MINUS→IntMinus, иначе IntAny. Имя типа - из реестра
        // (type::-константы + emitTypeName дают C++-имя trust::IntPlus/IntMinus/IntAny).
        TypeId tid;
        switch (n.term()->getTermID()) {
        case trust::TermID::BLOCK_PLUS:
            tid = m_ectx.m_ctx.types().getType(type::IntPlus);
            break;
        case trust::TermID::BLOCK_MINUS:
            tid = m_ectx.m_ctx.types().getType(type::IntMinus);
            break;
        default:
            tid = m_ectx.m_ctx.types().getType(type::IntAny);
            break;
        }
        std::string defaultCatch;
        if (auto cpp = m_driver.m_type.emitTypeName(tid, ""); cpp) {
            defaultCatch = *cpp;
        }
        m_ectx.m_ctx.source().output_append(out, " catch (" + defaultCatch + "&) {}");
        return;
    }
    for (const auto& cbPtr : n.m_catches) {
        const auto& cb = static_cast<const CatchBlock&>(*cbPtr);
        m_ectx.m_ctx.source().output_append(out, "\n");
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(out, "catch (");
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
                // типы зарегистрированы). При неизвестном типе emitTypeNameForNode выдаёт диагностику.
                ctype = m_driver.m_type.emitTypeNameForNode(typeNode.get());
                if (ctype.empty()) {
                    ctype = std::string(typeNode->text());
                }
            } else {
                // VarDecl без типа (не должно возникать) → IntAny.
                ctype = m_driver.m_type.emitTypeName(m_ectx.m_ctx.types().getType(type::IntAny), "").value_or("trust::IntAny");
            }
        } else {
            // catch-any (_ / none / ...) → IntAny (имя из реестра).
            ctype = m_driver.m_type.emitTypeName(m_ectx.m_ctx.types().getType(type::IntAny), "").value_or("trust::IntAny");
        }
        m_ectx.m_ctx.source().output_append(out, ctype + "&");
        if (!varName.empty()) {
            // Имя связанной переменной catch манглится как обычная локальная (e → c_e),
            // чтобы ссылки из тела catch (visit_Ident → name_to_cpp) совпадали с C++-параметром.
            m_ectx.m_ctx.source().output_append(out, " " + utils::name_to_cpp(varName));
        }
        m_ectx.m_ctx.source().output_append(out, ") {");
        m_ectx.m_scopeStack.push_back({m_ectx.indentLevel() + 1});
        m_ectx.m_ctx.source().output_append(out, "\n");
        m_driver.emitSequenceBody(cb, out);
        m_ectx.m_scopeStack.pop_back();
        m_ectx.m_ctx.source().output_append(out, m_ectx.indentPrefix());
        m_ectx.m_ctx.source().output_append(out, "}");
    }
}

void StmtEmitter::visit_CatchBlock(const CatchBlock&) {
    // Ветки catch эмитятся внутри visit_TryCatchStmt (inline); автономно не встречаются.
}

void StmtEmitter::visit_MatchingCase(const AstNodeAttr&) {
}

void StmtEmitter::visit_MatchingElseBlock(const AstNodeAttr&) {
}
} // namespace trust
