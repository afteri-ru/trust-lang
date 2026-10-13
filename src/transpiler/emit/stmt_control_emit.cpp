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
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <format>
#include <memory>
#include <string_view>

namespace trust {

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
            m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "unable to generate C++ type for while-else flag '{}'", requiredText(*f));
            return;
        }
        // emitSyntheticVar закончил строкой перевода; позиционируем `while` на отступ statement'а.
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
    }

    m_ectx.m_ctx.source().output_append(output_idx, "while (");
    m_driver.emitExpr(node.m_cond.get());
    m_ectx.m_ctx.source().output_append(output_idx, ")");
    m_driver.emitBodyNode(node.m_body, output_idx, /*mapBlock=*/true, /*beforeClose=*/"", /*afterOpen=*/(flag.empty() ? "" : flag + " = true;"));

    if (node.m_else) {
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
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
        m_ectx.m_ctx.report(node.range(), diag::DiagId::ParseError, "unable to generate C++ type for match scrutinee '{}'", requiredText(*temp));
        return;
    }
    const TypeId vt = temp->inferredType.getTypeId();
    // emitSyntheticVar закончил строкой перевода; позиционируем следующую строку на отступ statement'а.
    m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());

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
            } else {
                // Семантика уже проверила, что имя — объявленная функция (единственная в сайте),
                // поэтому текстовый поиск нативного адреса однозначен.
                const TextLookup byText = m_ectx.m_declFacts.lookupNativeImportByText(mname);
                matcherCpp = (byText.status == TextLookup::Status::Found) ? *byText.value
                                                                          : m_ectx.cppName(mname, CppEntityKind::kFunction, m_ectx.m_output.currentModule);
            }
        }
    }

    // Оператор сопоставления (node.m_op):
    //   TypeIsA/TypeDuck/TypeStrict - сопоставление ПО ТИПУ (trust::typeMatches<T>) - только if/else;
    //   ValueDefault/Value/ValueExact - сравнение ПО ЗНАЧЕНИЮ: целочисленное значение/перечисление
    //                               эмитится как C++ switch, остальные типы - if/else-if цепочка.
    const bool typeMatch = isTypeMatchOp(node.m_op);
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
    //               IntAny/*) RTTI-приведение с учётом подтипов, для не-полиморфных - точное
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
    const Group g = getGroup(getTypeKind(m_ectx.m_ctx.readTypes().store().desugaredTypeId(tid)));
    return g == Group::kIntegers || g == Group::kUnsigned || g == Group::kLogical;
}

bool StmtEmitter::canEmitMatchSwitch(const MatchStmt& node) const {
    if (!node.m_tempDecl || node.m_tempDecl->kind() != ParserToken::Kind::VarDecl) {
        return false;
    }
    const TypeId vt = static_cast<const VarDecl*>(node.m_tempDecl.get())->inferredType.getTypeId();
    if (vt == INVALID_TYPE_ID) {
        return false;
    }
    const TypeId canonical = m_ectx.m_ctx.readTypes().store().desugaredTypeId(vt);
    const Group g = getGroup(getTypeKind(canonical));

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
        const auto* ed = m_ectx.m_ctx.readTypes().store().getTypeDataAs<EnumTypeData>(canonical);
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
    const auto* leftIdent = ma.m_left ? ma.m_left->as<IdentName>() : nullptr;
    const TypeId lhsType = (leftIdent != nullptr && leftIdent->isTypeName()) ? leftIdent->resolvedTypeId : INVALID_TYPE_ID;
    if (lhsType == INVALID_TYPE_ID || !isEnumType(lhsType, m_ectx.m_ctx.readTypes())) {
        return std::nullopt;
    }
    const auto* ed = m_ectx.m_ctx.readTypes().store().getTypeDataAs<EnumTypeData>(lhsType);
    if (!ed) {
        return std::nullopt;
    }
    const std::string memberName = std::string(requiredText(*ma.m_right));
    for (const auto& m : ed->members) {
        if (m.name == memberName) {
            if (m.value.empty()) {
                return std::nullopt; // значение члена не нормализовано - не статическая константа
            }
            return memberValueCpp(m_ectx.m_ctx.readTypes(), ed->valueType, m.value);
        }
    }
    return std::nullopt;
}

void StmtEmitter::emitMatchSwitch(const MatchStmt& node, const std::string& tmp, TypeId valueType, MapperFile output_idx) {
    // Scrutivee-временная уже эмитирована выше (m_tempDecl). Enum-значение - структура
    // trust::Enum<Value,N,Self>; switch по её лежащему целочисленному полю `value` (case-метки -
    // скалярные константы членов).
    const bool enumValue = valueType != INVALID_TYPE_ID && isEnumType(m_ectx.m_ctx.readTypes().store().desugaredTypeId(valueType), m_ectx.m_ctx.readTypes());

    m_ectx.m_ctx.source().output_append(output_idx, "switch (" + tmp + (enumValue ? ".value" : "") + ") {");
    m_ectx.m_ctx.source().output_append(output_idx, "\n");
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});

    for (const auto& c : node.m_cases) {
        // Case-метки ветки: по одной на шаблон (несколько элементов одного условия → несколько
        // `case` подряд). После последней метки тело следует сразу ("{" подставит emitBodyNode).
        for (size_t j = 0; j < c.patterns.size(); ++j) {
            m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
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
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "break;");
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    }

    if (node.m_default) {
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "default:");
        m_driver.emitBodyNode(node.m_default, output_idx);
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
        m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
        m_ectx.m_ctx.source().output_append(output_idx, "break;");
        m_ectx.m_ctx.source().output_append(output_idx, "\n");
    }

    m_ectx.m_output.scopeStack.pop_back();
    m_ectx.m_ctx.source().output_append(output_idx, m_ectx.m_output.indentPrefix());
    m_ectx.m_ctx.source().output_append(output_idx, "}");
    m_ectx.m_ctx.source().output_append(output_idx, "\n");
}

} // namespace trust
