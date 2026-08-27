// module_loader.cpp - реализация ModuleLoader
#include "module_loader/module_loader.hpp"
#include "diag/context.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "ast/term_to_ast.hpp"

#include <filesystem>

namespace trust {
namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Конструктор
// ----------------------------------------------------------------------------

ModuleLoader::ModuleLoader(Context& ctx)
: m_ctx(ctx) {
}

// ----------------------------------------------------------------------------
// pushModule / popModule - стек текущего модуля + активный модуль в Context
// ----------------------------------------------------------------------------

void ModuleLoader::pushModule(std::size_t idx) {
    EXPECT(idx < registry().count());
    m_moduleStack.push_back(idx);
    m_ctx.setCurrentModule(idx);
}

void ModuleLoader::popModule() {
    EXPECT(!m_moduleStack.empty());
    m_moduleStack.pop_back();
    if (m_moduleStack.empty()) {
        m_ctx.resetCurrentModule();
    } else {
        m_ctx.setCurrentModule(m_moduleStack.back());
    }
}

// ----------------------------------------------------------------------------
// -- Метод A: parseSourceModule - модуль из уже зарегистрированного source-файла --
// ----------------------------------------------------------------------------

std::size_t ModuleLoader::parseSourceModule(std::string_view moduleId, MapperFile src, MapperRange range) {
    // Циклическая зависимость: запись уже есть в реестре, но модуль ещё не загружен.
    if (auto existing = indexOf(moduleId); existing && !registry().isLoaded(*existing)) {
        m_ctx.diag().report(Severity::Fatal, range, "Cyclic module dependency detected: '{}'", moduleId);
        throw std::runtime_error("unreachable");
    }

    std::size_t idx = registry().getOrLoad(moduleId);
    if (registry().isLoaded(idx)) {
        return idx; // кеш
    }

    m_indexByName[std::string(moduleId)] = idx;

    pushModule(idx);

    // Макросы, определённые внутри этого модуля, изолируются в отдельном скоупе
    // и удаляются при выходе из модуля (PopScope). Базовый скоуп (dsl) остаётся видимым
    // во всех модулях, макросы вызывающего модуля наследуются (поиск по стеку сверху вниз).
    bool pushedScope = false;
    if (auto macro = m_ctx.macro()) {
        macro->PushScope();
        pushedScope = true;
    }

    try {
        // Рекурсивный вызов парсера для нового модуля. Парсим из переданного
        // source-файла, чтобы маппинги привязывались к реальному файлу, а не к
        // новому in-memory псевдо-источнику (имя с префиксом '@').
        Parser parser(m_ctx);
        TermPtr term = parser.ParseWithSource(src, /*expand_module=*/true);

        registry().setBody(idx, std::move(term));
    } catch (...) {
        // Гарантируем снятие скоупа и стека модулей даже при FatalError.
        if (pushedScope) {
            m_ctx.macro()->PopScope();
        }
        popModule();
        throw;
    }

    if (pushedScope) {
        m_ctx.macro()->PopScope();
    }
    popModule();

    return idx;
}

// ----------------------------------------------------------------------------
// Метод B: ensureLoaded - имя → резолв файла → Метод A
// ----------------------------------------------------------------------------

std::size_t ModuleLoader::ensureLoaded(std::string_view moduleId, MapperRange range) {
    // Циклическая зависимость по исходному имени: модуль уже регистрируется, но ещё не загружен.
    if (auto existing = indexOf(moduleId); existing && !registry().isLoaded(*existing)) {
        m_ctx.diag().report(Severity::Fatal, range, "Cyclic module dependency detected: '{}'", moduleId);
        throw std::runtime_error("unreachable");
    }

    std::string basePath = resolveModulePath(currentModuleIndex(), moduleId);

    // Порядок: .trust → .src (в .trust в будущем будет хеш .src файла).
    std::string trustPath = basePath + ".trust";
    if (fs::exists(trustPath)) {
        // Заглушка: формат .trust (ELF/бинарный) ещё не спроектирован.
        m_ctx.diag().report(Severity::Fatal, range, "Module '{}': .trust modules are not supported yet", moduleId);
        throw std::runtime_error("unreachable");
    }

    std::string srcPath = basePath + ".src";
    if (!fs::exists(srcPath)) {
        m_ctx.diag().report(Severity::Fatal, range, "Module '{}' not found (tried .trust, .src)", moduleId);
        throw std::runtime_error("unreachable");
    }

    // Загружаем файл и парсим модуль из его source-файла.
    MapperFile file = m_ctx.source().load_file(srcPath);
    std::size_t idx = parseSourceModule(srcPath, file, range);
    // Alias: исходный идентификатор (напр. \mod) → индекс, чтобы indexOf() работал.
    m_indexByName[std::string(moduleId)] = idx;
    return idx;
}

// ----------------------------------------------------------------------------
// resolveModulePath - преобразование moduleId в filePath по правилам путей
// ----------------------------------------------------------------------------

std::string ModuleLoader::resolveModulePath(std::size_t callerIdx, std::string_view moduleId) const {
    std::string callerPath = registry().moduleName(callerIdx);
    fs::path callerDir = fs::path(callerPath).parent_path();

    std::string_view id = moduleId;

    if (id.starts_with("\\\\\\")) {
        id = id.substr(3);
        std::string path(id);
        for (auto& ch : path) {
            if (ch == '\\') {
                ch = '/';
            }
        }
        return path;
    }

    if (id.starts_with("\\\\")) {
        id = id.substr(2);
        std::string rel(id);
        for (auto& ch : rel) {
            if (ch == '\\') {
                ch = '/';
            }
        }
        return (fs::path(m_ctx.source().baseDirectory()) / rel).generic_string();
    }

    if (id.starts_with("\\") && !id.starts_with("\\\\")) {
        id = id.substr(1);
        std::string rel(id);
        for (auto& ch : rel) {
            if (ch == '\\') {
                ch = '/';
            }
        }
        return (callerDir / rel).generic_string();
    }

    FAULT("Invalid module identifier '{}': must start with \\, \\\\, or \\\\\\", moduleId);
    return {};
}

// ----------------------------------------------------------------------------
// indexOf - lookup по имени/каноническому пути (без загрузки)
// ----------------------------------------------------------------------------

std::optional<std::size_t> ModuleLoader::indexOf(std::string_view moduleIdOrName) const {
    auto it = m_indexByName.find(std::string(moduleIdOrName));
    if (it == m_indexByName.end()) {
        return std::nullopt;
    }
    return it->second;
}

// ----------------------------------------------------------------------------
// isLoaded - по индексу
// ----------------------------------------------------------------------------

bool ModuleLoader::isLoaded(std::size_t index) const {
    return registry().isLoaded(index);
}

const TermPtr& ModuleLoader::body(std::size_t index) const {
    return registry().body(index);
}

const std::string& ModuleLoader::moduleName(std::size_t index) const {
    return registry().moduleName(index);
}

void ModuleLoader::setInterface(std::size_t index, std::vector<TermPtr> interface) {
    registry().setInterface(index, std::move(interface));
}

const std::vector<TermPtr>& ModuleLoader::interface(std::size_t index) const {
    return registry().interface(index);
}

bool ModuleLoader::hasInterface(std::size_t index) const {
    return registry().hasInterface(index);
}

void ModuleLoader::setBodyAst(std::size_t index, std::vector<AstNodePtr> bodyAst) {
    registry().setBodyAst(index, std::move(bodyAst));
}

const std::vector<AstNodePtr>& ModuleLoader::bodyAst(std::size_t index) const {
    return registry().bodyAst(index);
}

// ----------------------------------------------------------------------------
// currentModuleIndex
// ----------------------------------------------------------------------------

std::size_t ModuleLoader::currentModuleIndex() const {
    if (m_moduleStack.empty()) {
        FAULT("ModuleLoader::currentModuleIndex() called on empty stack");
    }
    return m_moduleStack.back();
}

} // namespace trust