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

// ── Helper: собрать операторы тела и диапазон блока из узла-тела
//    (ScopeBlock/Sequence → m_body + range блока; одиночный statement → сам узел) ──
static void collectBodyStatements(const AstNodePtr& bodyNode, std::vector<AstNodePtr>& out, MapperRange& blockRange) {
    out.clear();
    blockRange = {};
    if (!bodyNode)
        return;
    if (bodyNode->kind() == ParserToken::Kind::ScopeBlock || bodyNode->kind() == ParserToken::Kind::sequence) {
        auto* seq = static_cast<const Sequence*>(bodyNode.get());
        out = seq->m_body;
        blockRange = bodyNode->range();
    } else {
        out.push_back(bodyNode);
        blockRange = bodyNode->range();
    }
}

// ── Helper: имя C++-метки из trust-имени блока/label.
//    Убирает '::' (и прочие ':'): "outer_loop::" → "outer_loop".
//    Используется и для эмиссии меток именованных блоков, и для goto именованных break/continue,
//    чтобы оба имени совпадали.
static std::string cleanLabelName(std::string_view name) {
    std::string clean;
    clean.reserve(name.size());
    for (char c : name)
        if (c != ':')
            clean += c;
    return clean;
}

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

        // Именованный блок: break-метка <имя>_break после тела; continue-метка <имя>_continue
        // ставится первым циклом в теле блока (через m_pendingContinueLabel) — ПОСЛЕ инициализации,
        // чтобы goto <имя>_continue просто переоценивал условие цикла, не повторяя инициализацию.
        std::string breakLabel;
        if (m_inFunction && node.kind() == ParserToken::Kind::ScopeBlock) {
            auto* sb = static_cast<const ScopeBlock*>(&node);
            if (!sb->is_anonymous() && !sb->is_hidden()) {
                const std::string clean = cleanLabelName(sb->name());
                if (!clean.empty()) {
                    m_pendingContinueLabel = clean + "_continue";
                    breakLabel = clean + "_break";
                }
            }
        }

        const bool inBlock = m_indent > 0;
        const AstNodeBase* prev = nullptr;
        bool first = true;
        for (const auto& child : scope->m_body) {
            if (!child)
                continue;
            if (inBlock) {
                // Внутри блока — каждый оператор с новой строки с отступом (нормальное форматирование).
                // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход.
                if (!first)
                    m_ctx.source().output_append(output_idx, "\n");
                const bool isBlockChild = child->kind() == ParserToken::Kind::ScopeBlock || child->kind() == ParserToken::Kind::sequence ||
                                          child->kind() == ParserToken::Kind::ModuleDecl;
                if (!isBlockChild)
                    m_ctx.source().output_append(output_idx, indentPrefix());
                generateNodeToFile(*child, output_idx);
            } else {
                // На верхнем уровне (indent==0) — прежнее поведение (зеркалирование строк исходника).
                emitBlockSeparator(prev, *child, output_idx);
                generateNodeToFile(*child, output_idx);
                prev = child.get();
            }
            first = false;
        }
        // Если в блоке нет цикла — pending-метка не потреблена (continue по блоку без цикла не корректен).
        m_pendingContinueLabel.clear();
        if (!breakLabel.empty())
            m_ctx.source().output_append(output_idx, breakLabel + ":;");
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

    // Control flow: if / while / do-while
    if (node.kind() == ParserToken::Kind::IfStmt) {
        generateIfToFile(*static_cast<const IfStmt*>(&node), output_idx);
        return;
    }
    if (node.kind() == ParserToken::Kind::WhileStmt) {
        generateWhileToFile(*static_cast<const WhileStmt*>(&node), output_idx);
        return;
    }
    if (node.kind() == ParserToken::Kind::DoWhileStmt) {
        generateDoWhileToFile(*static_cast<const DoWhileStmt*>(&node), output_idx);
        return;
    }

    // break / continue → goto
    if (node.kind() == ParserToken::Kind::BreakStmt || node.kind() == ParserToken::Kind::ContinueStmt) {
        generateBreakContinueToFile(*static_cast<const JumpStmt*>(&node), output_idx);
        return;
    }

    // match → временная переменная + if/else-if/else
    if (node.kind() == ParserToken::Kind::MatchingStmt) {
        generateMatchToFile(*static_cast<const MatchStmt*>(&node), output_idx);
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

        // Тело маппится отдельно из блока m_right, чтобы скобки { } отображались в C++.
        // functionName: функция — top-level именованный блок (для именованных break/continue на имя функции).
        emitBlockBodyToFile(*func_node.m_body, blockRange, output_idx, /*mapBlock=*/true, /*beforeCloseLabel=*/"", /*inFunction=*/true, name);
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

void CppTranspiler::emitBlockBodyToFile(const std::vector<AstNodePtr>& body, MapperRange blockRange, MapperFile output_idx, bool mapBlock,
                                        const std::string& beforeCloseLabel, bool inFunction, const std::string& functionName, const std::string& afterOpen) {
    // mapBlock=false: не оборачиваем тело собственным маппингом (do-while — begin тела
    // совпадает с begin statement'а, иначе коллизия ключа в mapStop).
    const bool hasBlockRange = !blockRange.isInvalid() && mapBlock;

    // Тело маппится отдельно из диапазона блока, чтобы скобки { } отображались в C++.
    if (hasBlockRange)
        m_ctx.source().mapStart(blockRange, output_idx);

    // Нормальное многострочное форматирование: '{' в конце строки, операторы — с отступом.
    m_ctx.source().output_append(output_idx, " {");
    m_indent++;
    m_ctx.source().output_append(output_idx, "\n");
    // afterOpen — текст сразу после '{' (например, установка флага while-else).
    if (!afterOpen.empty()) {
        m_ctx.source().output_append(output_idx, indentPrefix());
        m_ctx.source().output_append(output_idx, afterOpen);
        m_ctx.source().output_append(output_idx, "\n");
    }

    CppTranspiler body_gen(m_ctx);
    body_gen.m_inFunction = m_inFunction || inFunction;                                   // метки именованных блоков — только внутри функций
    body_gen.m_currentFuncName = functionName.empty() ? m_currentFuncName : functionName; // имя текущей функции
    body_gen.m_indent = m_indent;
    for (const auto& child : body) {
        if (!child)
            continue;
        // Для блочных детей (ScopeBlock/sequence/ModuleDecl) отступ выставляет их собственный обход;
        // для остальных операторов — отступ текущего блока.
        const bool isBlockChild =
            child->kind() == ParserToken::Kind::ScopeBlock || child->kind() == ParserToken::Kind::sequence || child->kind() == ParserToken::Kind::ModuleDecl;
        if (!isBlockChild)
            m_ctx.source().output_append(output_idx, indentPrefix());
        body_gen.generateNodeToFile(*child, output_idx);
        m_ctx.source().output_append(output_idx, "\n");
    }
    // continue-метка do-while / именованного блока вставляется перед закрывающей '}'.
    if (!beforeCloseLabel.empty()) {
        m_ctx.source().output_append(output_idx, indentPrefix());
        m_ctx.source().output_append(output_idx, beforeCloseLabel);
        m_ctx.source().output_append(output_idx, "\n");
    }
    m_indent--;
    m_ctx.source().output_append(output_idx, indentPrefix());
    m_ctx.source().output_append(output_idx, "}");
    if (hasBlockRange)
        m_ctx.source().mapStop(blockRange);
}

void CppTranspiler::emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock, const std::string& beforeCloseLabel,
                                 const std::string& afterOpen) {
    std::vector<AstNodePtr> stmts;
    MapperRange blockRange;
    collectBodyStatements(body, stmts, blockRange);
    emitBlockBodyToFile(stmts, blockRange, output_idx, mapBlock, beforeCloseLabel, /*inFunction=*/false, /*functionName=*/"", afterOpen);
}

void CppTranspiler::generateIfToFile(const IfStmt& node, MapperFile output_idx) {
    m_ctx.source().mapStart(node.range(), output_idx);

    // if (cond) { then }
    m_ctx.source().output_append(output_idx, "if (");
    m_ctx.source().output_append(output_idx, generateExpr(node.m_cond.get()));
    m_ctx.source().output_append(output_idx, ")");
    emitBodyNode(node.m_then, output_idx);

    // else if (cond2) { body2 } ...
    for (const auto& [cond, body] : node.m_elseifs) {
        m_ctx.source().output_append(output_idx, " else if (");
        m_ctx.source().output_append(output_idx, generateExpr(cond.get()));
        m_ctx.source().output_append(output_idx, ")");
        emitBodyNode(body, output_idx);
    }

    // else { ... }
    if (node.m_else) {
        m_ctx.source().output_append(output_idx, " else");
        emitBodyNode(node.m_else, output_idx);
    }

    m_ctx.source().mapStop(node.range());
}

void CppTranspiler::generateWhileToFile(const WhileStmt& node, MapperFile output_idx) {
    m_ctx.source().mapStart(node.range(), output_idx);

    // Если именованный блок поставил pending-метку continue — потребляем её здесь (ставим перед циклом,
    // ПОСЛЕ инициализации блока), чтобы именованный continue переходил к переоценке условия.
    if (!m_pendingContinueLabel.empty()) {
        m_ctx.source().output_append(output_idx, m_pendingContinueLabel + ":;");
        m_pendingContinueLabel.clear();
    }

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
    m_ctx.source().output_append(output_idx, generateExpr(node.m_cond.get()));
    m_ctx.source().output_append(output_idx, ")");
    emitBodyNode(node.m_body, output_idx, /*mapBlock=*/true, /*beforeClose=*/"", /*afterOpen=*/(flag.empty() ? "" : flag + " = true;"));

    if (node.m_else) {
        m_ctx.source().output_append(output_idx, "\n");
        m_ctx.source().output_append(output_idx, indentPrefix());
        m_ctx.source().output_append(output_idx, "if (!" + flag + ")");
        emitBodyNode(node.m_else, output_idx);
    }

    m_ctx.source().mapStop(node.range());
}

void CppTranspiler::generateDoWhileToFile(const DoWhileStmt& node, MapperFile output_idx) {
    m_ctx.source().mapStart(node.range(), output_idx);

    m_ctx.source().output_append(output_idx, "do");
    // Тело не маппится отдельно: begin тела совпадает с begin statement'а (do-while начинается
    // с '{'), иначе коллизия trustKey в mapStop. Всё покрывает единый range statement'а.
    // pending-метка именованного блока (continue) вставляется перед '}' — переход к проверке условия.
    std::string closeLabels;
    if (!m_pendingContinueLabel.empty()) {
        closeLabels = m_pendingContinueLabel + ":;";
        m_pendingContinueLabel.clear();
    }
    emitBodyNode(node.m_body, output_idx, /*mapBlock=*/false, closeLabels);
    m_ctx.source().output_append(output_idx, " while (");
    m_ctx.source().output_append(output_idx, generateExpr(node.m_cond.get()));
    m_ctx.source().output_append(output_idx, ");");

    m_ctx.source().mapStop(node.range());
}

void CppTranspiler::generateBreakContinueToFile(const JumpStmt& node, MapperFile output_idx) {
    m_ctx.source().mapStart(node.range(), output_idx);
    const bool isBreak = node.kind() == ParserToken::Kind::BreakStmt;

    if (node.m_label) {
        // Именованное прерывание: goto <имя_блока>_break / _continue.
        // Имя очищается от '::'; именованный блок/цикл выводит соответствующую метку.
        const std::string clean = cleanLabelName(node.m_label->text());
        // Функция — top-level именованный блок: именованный break на её имя = return (void).
        if (isBreak && !m_currentFuncName.empty() && clean == cleanLabelName(m_currentFuncName)) {
            m_ctx.source().output_append(output_idx, "return;");
        } else {
            m_ctx.source().output_append(output_idx, isBreak ? ("goto " + clean + "_break;") : ("goto " + clean + "_continue;"));
        }
    } else {
        // Безымянное прерывание — обычные C++ break/continue (без goto).
        m_ctx.source().output_append(output_idx, isBreak ? "break;" : "continue;");
    }

    m_ctx.source().mapStop(node.range());
}

void CppTranspiler::generateMatchToFile(const MatchStmt& node, MapperFile output_idx) {
    m_ctx.source().mapStart(node.range(), output_idx);

    const uint32_t id = ++m_matchCounter;
    const std::string tmp = "_match" + std::to_string(id);

    // Предварительное вычисление значения во временную переменную (на отдельной строке).
    m_ctx.source().output_append(output_idx, "auto " + tmp + " = " + generateExpr(node.m_value.get()) + ";");
    m_ctx.source().output_append(output_idx, "\n");
    m_ctx.source().output_append(output_idx, indentPrefix());

    // Последовательное сравнение с шаблонами: if / else-if / else.
    bool first = true;
    for (const auto& c : node.m_cases) {
        m_ctx.source().output_append(output_idx, first ? "if (" : " else if (");
        first = false;
        std::string cond;
        for (size_t j = 0; j < c.patterns.size(); ++j) {
            if (j)
                cond += " || ";
            cond += tmp + " == " + generateExpr(c.patterns[j].get());
        }
        m_ctx.source().output_append(output_idx, cond + ")");
        emitBodyNode(c.body, output_idx);
    }
    if (node.m_default) {
        m_ctx.source().output_append(output_idx, " else");
        emitBodyNode(node.m_default, output_idx);
    }

    m_ctx.source().mapStop(node.range());
}

} // namespace trust