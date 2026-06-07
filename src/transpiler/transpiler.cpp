#include "transpiler/transpiler.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include <format>

namespace trust {

CppTranspiler::CppTranspiler(Context& ctx)
: m_ctx(ctx) {
}

void CppTranspiler::generateToFile(const std::vector<AstNodePtr>& ast_nodes, MapperFile output_idx) {
    const AstNodeBase* prev = nullptr;
    for (const auto& node : ast_nodes) {
        if (!node)
            continue;
        emitBlockSeparator(prev, *node, output_idx);
        generateNodeToFile(*node, output_idx);
        prev = node.get();
    }
    if (!ast_nodes.empty())
        m_ctx.source().output_append(output_idx, "\n");
}

void CppTranspiler::emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx) {
    // Перевод строки между блоками вставляется только если они в исходнике на разных
    // строках: строка конца prev != строка начала node. Если строки совпадают (блоки
    // намеренно на одной строке) — перевод строки не выводится, вместо него пробел.
    if (!prev)
        return; // первый блок — перевод строки не нужен
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
    if (!prevEndsWithSpace && !nextStartsWithSpace)
        m_ctx.source().output_append(output_idx, " ");
}

void CppTranspiler::generateNodeToFile(const AstNodeBase& node, MapperFile output_idx) {
    if (node.kind() == ParserToken::Kind::END)
        return;

    // Sequence, ScopeBlock or ModuleNode → walk body
    if (node.kind() == ParserToken::Kind::sequence || node.kind() == ParserToken::Kind::ScopeBlock || node.kind() == ParserToken::Kind::ModuleDecl) {
        auto* scope = static_cast<const Sequence*>(&node);
        const AstNodeBase* prev = nullptr;
        for (const auto& child : scope->m_body) {
            if (!child)
                continue;
            emitBlockSeparator(prev, *child, output_idx);
            generateNodeToFile(*child, output_idx);
            prev = child.get();
        }
        return;
    }

    // VarDecl → variable declaration
    if (node.kind() == ParserToken::Kind::VarDecl) {
        auto* var = static_cast<const VarDecl*>(&node);
        generateVarDeclToFile(*var, output_idx);
        return;
    }

    // Type decl or expression statement → Binary
    if (is_binary_kind(node.kind())) {
        auto* binary = static_cast<const Binary*>(&node);
        if (binary->kind() == ParserToken::Kind::TypeDecl) {
            generateTypeDeclToFile(*binary, output_idx);
            return;
        }
        // All other Binary: assignment (=, +=, -=, *=, /=, //=, %=)
        generateExprStmtToFile(*binary, output_idx);
        return;
    }

    // Return/Throw statement
    if (node.kind() == ParserToken::Kind::ReturnStmt || node.kind() == ParserToken::Kind::ThrowStmt) {
        auto* ret = static_cast<const JumpStmt*>(&node);
        m_ctx.source().mapStart(node.range(), output_idx);
        if (node.kind() == ParserToken::Kind::ReturnStmt) {
            if (ret->m_value) {
                std::string val_str = generateExpr(ret->m_value.get());
                m_ctx.source().output_append(output_idx, std::format("return {};", val_str));
            } else {
                m_ctx.source().output_append(output_idx, "return;");
            }
        } else {
            if (ret->m_value) {
                std::string val_str = generateExpr(ret->m_value.get());
                m_ctx.source().output_append(output_idx, std::format("throw {};", val_str));
            } else {
                m_ctx.source().output_append(output_idx, "throw;");
            }
        }
        m_ctx.source().mapStop(node.range());
        return;
    }

    // Standalone literals — map and emit
    if (node.kind() == ParserToken::Kind::IntLiteral || node.kind() == ParserToken::Kind::StringLiteral) {
        m_ctx.source().mapStart(node.range(), output_idx);
        m_ctx.source().output_append(output_idx, generateExpr(&node));
        m_ctx.source().output_append(output_idx, ";");
        m_ctx.source().mapStop(node.range());
        return;
    }

    // Function declaration
    if (node.kind() == ParserToken::Kind::FuncDecl) {
        auto* func = static_cast<const FuncDecl*>(&node);
        generateFuncDeclToFile(*func, output_idx);
        return;
    }

    // Standalone EmbedExpr — emit raw text with source mapping
    if (node.kind() == ParserToken::Kind::EmbedExpr) {
        m_ctx.source().mapStart(node.range(), output_idx);
        m_ctx.source().output_append(output_idx, generateExpr(&node));
        m_ctx.source().mapStop(node.range());
        return;
    }
}

void CppTranspiler::generateVarDeclToFile(const VarDecl& var_node, MapperFile output_idx) {
    std::string var_name{var_node.text()};
    if (var_name.empty())
        return;

    // Determine type
    TypeId type_id = INVALID_TYPE_ID;
    if (var_node.m_type && var_node.m_type->kind() == ParserToken::Kind::TypeName) {
        // Typed: x:Type := expr
        auto type_opt = m_ctx.types().findType(var_node.m_type->text());
        if (type_opt.has_value())
            type_id = *type_opt;
    } else {
        // Untyped: x := expr → type = Any
        auto any_opt = m_ctx.types().findType(type_generic::Any);
        if (any_opt.has_value())
            type_id = *any_opt;
    }

    // Resolve to canonical type for codegen
    TypeId canonical = m_ctx.types().getCanonicalTypeId(type_id);

    // Insert preprocessor include
    std::string_view include = m_ctx.types().getPreprocInclude(canonical);
    if (!include.empty()) {
        m_ctx.source().output_prepend(output_idx, include);
    }

    // Get C++ type name
    auto cpp_name = m_ctx.types().getCppTypeName(canonical);
    std::string cpp_type = cpp_name ? std::move(*cpp_name) : "auto";

    // Generate initializer
    std::string rhs = "{}";
    if (var_node.m_initializer) {
        rhs = generateExpr(var_node.m_initializer.get());
    }

    m_ctx.source().mapStart(var_node.range(), output_idx);
    std::string cpp_line = std::format("{} {} = {};", cpp_type, var_name, rhs);
    m_ctx.source().output_append(output_idx, cpp_line);

    // Добавляем маппинг имени переменной для hover-ссылок
    {
        auto stackEntry = m_ctx.source().mapStackTop();
        uint32_t nameOffset = stackEntry.outputBegin.offset() + static_cast<uint32_t>(cpp_type.length()) + 1; // +1 for space
        MapperLocation nameLocBegin = m_ctx.source().makeLoc(output_idx, nameOffset);
        MapperLocation nameLocEnd = m_ctx.source().makeLoc(output_idx, nameOffset + static_cast<uint32_t>(var_name.length()));
        MapperRange cppNameRange(nameLocBegin, nameLocEnd);

        // Диапазон trust-имени: nameRange() берёт диапазон реального имени из m_term->m_left
        // (важно при макро-раскрытии, где range() самого узла — оператор). Fallback — имя
        // в начале range() (когда range() покрывает всю строку, m_term->m_left отсутствует).
        MapperRange trustNameRange;
        if (!var_node.nameRange().isInvalid()) {
            trustNameRange = var_node.nameRange();
        } else {
            MapperLocation trustNameBegin = m_ctx.source().makeLoc(var_node.range().begin.fileIdx(), var_node.range().begin.offset());
            MapperLocation trustNameEnd = m_ctx.source().makeLoc(trustNameBegin.fileIdx(), trustNameBegin.offset() + static_cast<uint32_t>(var_name.length()));
            trustNameRange = MapperRange(trustNameBegin, trustNameEnd);
        }
        m_ctx.source().addNameMapping(trustNameRange, cppNameRange, var_name, var_name);
    }

    // Запоминаем имя переменной для экспортной таблицы
    if (!var_name.empty()) {
        m_exports.push_back({var_name, var_name});
    }

    m_ctx.source().mapStop(var_node.range());
}

void CppTranspiler::generateTypeDeclToFile(const Binary& binary_node, MapperFile output_idx) {
    auto* left = binary_node.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident)
        return;

    std::string type_name = std::string(left->text());

    auto* right = binary_node.m_right.get();
    if (!right || right->kind() != ParserToken::Kind::TypeName)
        return;

    // Resolve base type
    auto base_opt = m_ctx.types().findType(right->text());
    if (!base_opt.has_value())
        return;

    TypeId base_type_id = *base_opt;
    TypeId canonical = m_ctx.types().getCanonicalTypeId(base_type_id);

    // Insert preprocessor include
    std::string_view include = m_ctx.types().getPreprocInclude(canonical);
    if (!include.empty()) {
        m_ctx.source().output_prepend(output_idx, include);
    }

    // Get C++ type name
    auto cpp_name = m_ctx.types().getCppTypeName(canonical);
    if (!cpp_name)
        return;

    m_ctx.source().mapStart(binary_node.range(), output_idx);
    const std::string using_prefix = "using ";
    std::string cpp_line = using_prefix + type_name + " = " + *cpp_name + ";";
    m_ctx.source().output_append(output_idx, cpp_line);

    // Add name mapping for the type name (hover links): name starts right after the "using " prefix.
    {
        auto stackEntry = m_ctx.source().mapStackTop();
        uint32_t nameOffset = stackEntry.outputBegin.offset() + static_cast<uint32_t>(using_prefix.length());
        MapperLocation nameLocBegin = m_ctx.source().makeLoc(output_idx, nameOffset);
        MapperLocation nameLocEnd = m_ctx.source().makeLoc(output_idx, nameOffset + static_cast<uint32_t>(type_name.length()));
        MapperRange cppNameRange(nameLocBegin, nameLocEnd);
        m_ctx.source().addNameMapping(left->range(), cppNameRange, type_name, type_name);
    }

    m_ctx.source().mapStop(binary_node.range());
}

std::string CppTranspiler::generateExpr(const AstNodeBase* node) {
    if (!node)
        return "{}";

    // Literals
    if (node->kind() == ParserToken::Kind::IntLiteral) {
        return std::string(node->text());
    }
    if (node->kind() == ParserToken::Kind::StringLiteral) {
        return std::format("\"{}\"", node->text());
    }

    // EmbedExpr → emit raw text (C++ code)
    if (node->kind() == ParserToken::Kind::EmbedExpr) {
        return std::string(node->text());
    }

    // Ident → just emit the name
    if (node->kind() == ParserToken::Kind::Ident) {
        return std::string(node->text());
    }

    // TypeName → emit as-is
    if (node->kind() == ParserToken::Kind::TypeName) {
        return std::string(node->text());
    }

    // Binary → x + y, x - y, etc.
    if (is_binary_kind(node->kind())) {
        auto* binary = static_cast<const Binary*>(node);
        std::string left = generateExpr(binary->m_left.get());
        std::string right = generateExpr(binary->m_right.get());
        auto op = binary->text();
        // Integer division: Trust's // → C++ static_cast (to avoid // being treated as comment)
        if (op == "//")
            return std::format("(static_cast<int64_t>({}) / static_cast<int64_t>({}))", left, right);
        // Compound integer division: //= → x = static_cast<int64_t>(x) / y
        if (op == "//=")
            return std::format("({} = static_cast<int64_t>({}) / static_cast<int64_t>({}))", left, left, right);
        return std::format("({} {} {})", left, op, right);
    }

    return "{}";
}

std::string CppTranspiler::resolveTypeName(const AstNodeBase* type_node) const {
    if (!type_node)
        return "void";
    if (type_node->kind() == ParserToken::Kind::TypeName) {
        std::string_view text = type_node->text();

        // Special cases: None/Void → void
        if (text == "None" || text == "Void")
            return "void";

        // Try to resolve via type registry
        auto type_opt = m_ctx.types().findType(text);
        if (type_opt.has_value()) {
            TypeId canonical = m_ctx.types().getCanonicalTypeId(*type_opt);
            auto cpp_name = m_ctx.types().getCppTypeName(canonical);
            if (cpp_name)
                return *cpp_name;
        }
        return std::string(text);
    }
    return "void";
}

void CppTranspiler::generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx) {
    m_ctx.source().mapStart(func_node.range(), output_idx);

    // Function name: strip '%' prefix, replace '::' → '::' for C++ namespaces
    std::string name{func_node.text()};
    // Strip leading '%' if present
    if (!name.empty() && name[0] == '%')
        name.erase(0, 1);
    // Replace Trust's '::' with C++ '::' (already '::' in MMProc output)
    // No transformation needed

    // Return type
    std::string ret_type = resolveTypeName(func_node.m_type.get());

    // Parameters
    std::string params_str;
    std::vector<std::pair<const ParamDecl*, uint32_t>> param_name_positions; // (node, name offset within signature)
    if (func_node.m_params) {
        const uint32_t sig_prefix = static_cast<uint32_t>(ret_type.length()) + 1 + static_cast<uint32_t>(name.length()) + 1;
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0)
                params_str += ", ";
            auto* param_node = static_cast<const ParamDecl*>((*func_node.m_params)[i].get());
            if (!param_node || param_node->kind() != ParserToken::Kind::ParamDecl) {
                params_str += "auto";
                continue;
            }
            // Param type
            std::string param_type;
            if (param_node->m_type) {
                param_type = resolveTypeName(param_node->m_type.get());
            } else {
                param_type = "auto";
            }
            // Param name
            std::string param_name = std::string(param_node->text());
            if (param_name.empty())
                param_name = "arg" + std::to_string(i);
            // Name offset within signature: params_str already holds everything emitted so far
            // (separators, "auto" fallbacks, previous params), so the type size is taken directly.
            uint32_t name_pos = sig_prefix + static_cast<uint32_t>(params_str.size()) + static_cast<uint32_t>(param_type.length()) + 1;
            params_str += param_type + " " + param_name;
            param_name_positions.emplace_back(param_node, name_pos);
        }
    }

    // Emit function signature
    std::string sig = std::format("{} {}({})", ret_type, name, params_str);
    m_ctx.source().output_append(output_idx, sig);

    // Add name mappings (function name + parameter names) for hover links.
    {
        auto stackEntry = m_ctx.source().mapStackTop();
        uint32_t base = stackEntry.outputBegin.offset();

        // Function name: 'name' starts after "ret_type ".
        if (!name.empty()) {
            uint32_t fnNameOffset = base + static_cast<uint32_t>(ret_type.length()) + 1;
            MapperLocation fnBegin = m_ctx.source().makeLoc(output_idx, fnNameOffset);
            MapperLocation fnEnd = m_ctx.source().makeLoc(output_idx, fnNameOffset + static_cast<uint32_t>(name.length()));
            MapperRange cppFnRange(fnBegin, fnEnd);
            MapperLocation trustFnBegin = m_ctx.source().makeLoc(func_node.range().begin.fileIdx(), func_node.range().begin.offset());
            MapperLocation trustFnEnd = m_ctx.source().makeLoc(trustFnBegin.fileIdx(), trustFnBegin.offset() + static_cast<uint32_t>(name.length()));
            MapperRange trustFnNameRange(trustFnBegin, trustFnEnd);
            m_ctx.source().addNameMapping(trustFnNameRange, cppFnRange, func_node.text(), name);
        }

        // Parameter names.
        for (const auto& [param_node, name_pos] : param_name_positions) {
            std::string param_name = std::string(param_node->text());
            if (param_name.empty())
                continue; // placeholder argN is not backed by a real source name
            uint32_t cppNameOffset = base + name_pos;
            MapperLocation pBegin = m_ctx.source().makeLoc(output_idx, cppNameOffset);
            MapperLocation pEnd = m_ctx.source().makeLoc(output_idx, cppNameOffset + static_cast<uint32_t>(param_name.length()));
            MapperRange cppPRange(pBegin, pEnd);
            m_ctx.source().addNameMapping(param_node->range(), cppPRange, param_name, param_name);
        }
    }

    // Сигнатура (src [имя, оператор]) смапплена — закрываем её отдельно от тела.
    m_ctx.source().mapStop(func_node.range());

    // Body or forward declaration
    if (func_node.m_body) {
        // Зеркалируем раскладку исходника: '{' и '}' размещаются по строкам блока,
        // переносы между '{' и первым оператором / последним оператором и '}' зависят
        // от того, на одной ли они строке исходника.
        const MapperRange blockRange = func_node.blockRange();
        const bool hasBlockRange = !blockRange.isInvalid();
        const uint32_t braceLine = hasBlockRange ? m_ctx.source().line(blockRange.begin) : 0;
        const uint32_t closeBraceLine = hasBlockRange ? m_ctx.source().line(blockRange.end) : 0;

        // Тело маппится отдельно из блока m_right, чтобы скобки { } отображались в C++.
        if (hasBlockRange)
            m_ctx.source().mapStart(blockRange, output_idx);

        m_ctx.source().output_append(output_idx, " {");
        if (!func_node.m_body->empty()) {
            // '\n' после '{', если первый оператор на другой строке исходника; иначе пробел.
            const uint32_t firstLine = m_ctx.source().line((*func_node.m_body)[0]->range().begin);
            if (!hasBlockRange || braceLine != firstLine) {
                m_ctx.source().output_append(output_idx, "\n");
            } else {
                emitSameLineSpace((*func_node.m_body)[0]->text(), output_idx);
            }

            // Тело: операторы с переводами строк на границах (emitBlockSeparator).
            CppTranspiler body_gen(m_ctx);
            const AstNodeBase* prev = nullptr;
            for (const auto& child : *func_node.m_body) {
                if (!child)
                    continue;
                body_gen.emitBlockSeparator(prev, *child, output_idx);
                body_gen.generateNodeToFile(*child, output_idx);
                prev = child.get();
            }

            // '\n' перед '}', если последний оператор на другой строке, чем '}'; иначе пробел.
            const uint32_t lastLine = m_ctx.source().line(func_node.m_body->back()->range().end);
            if (!hasBlockRange || closeBraceLine != lastLine) {
                m_ctx.source().output_append(output_idx, "\n");
            } else {
                emitSameLineSpace("}", output_idx);
            }
        } else {
            // Пустое тело: диапазон блока для пустого тела покрывает только '{' (не '}'),
            // поэтому строки { и } надёжно не определить — всегда перевод строки между ними
            // (согласовано с лит-тестом func_decl_simple: { на одной строке, } на следующей).
            m_ctx.source().output_append(output_idx, "\n");
        }
        m_ctx.source().output_append(output_idx, "}");
        if (hasBlockRange)
            m_ctx.source().mapStop(blockRange);
    } else {
        // Forward declaration
        m_ctx.source().output_append(output_idx, ";");
    }

    // Register export
    if (!name.empty()) {
        m_exports.push_back({std::string(func_node.text()), name});
    }
}

void CppTranspiler::generateExprStmtToFile(const Binary& binary_node, MapperFile output_idx) {
    auto* left = binary_node.m_left.get();
    auto* right = binary_node.m_right.get();
    std::string left_str = left ? generateExpr(left) : "";
    std::string right_str = right ? generateExpr(right) : "";
    auto op = binary_node.text();

    m_ctx.source().mapStart(binary_node.range(), output_idx);
    // Integer division: // → static_cast<int64_t>(x) / y
    if (op == "//") {
        m_ctx.source().output_append(output_idx, std::format("static_cast<int64_t>({}) / static_cast<int64_t>({})", left_str, right_str));
    } else if (op == "//=") {
        // Compound integer division: //= → x = static_cast<int64_t>(x) / y
        m_ctx.source().output_append(output_idx, std::format("{} = static_cast<int64_t>({}) / static_cast<int64_t>({})", left_str, left_str, right_str));
    } else {
        m_ctx.source().output_append(output_idx, std::format("{} {} {}", left_str, op, right_str));
    }
    m_ctx.source().output_append(output_idx, ";");
    m_ctx.source().mapStop(binary_node.range());
}

} // namespace trust