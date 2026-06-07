#include "diag/context.hpp"
#include "utils/error.hpp"

#include <filesystem>
#include <system_error>

namespace trust {
namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════
//                         Context
// ══════════════════════════════════════════════════════════════

// ── Конструкторы ──

Context::Context()
: m_sourceMap(std::make_unique<SourceMapWriter>())
, m_diag(std::make_unique<DiagnosticEngine>())
, m_type_registry(std::make_unique<TypeRegistry>(this))
, m_moduleLoader(std::make_unique<ModuleLoader>(*this)) {
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
    m_diag->setOptions(&*m_opts);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

Context::Context(std::string_view basePath, std::string_view tempPath)
: m_sourceMap(std::make_unique<SourceMapWriter>(basePath, tempPath))
, m_diag(std::make_unique<DiagnosticEngine>())
, m_type_registry(std::make_unique<TypeRegistry>(this))
, m_moduleLoader(std::make_unique<ModuleLoader>(*this)) {
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
    m_diag->setOptions(&*m_opts);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
}

// ── Доступ к компонентам ──

SourceMapWriter& Context::source() {
    return *m_sourceMap;
}

const SourceMapWriter& Context::source() const {
    return *m_sourceMap;
}

DiagnosticEngine& Context::diag() {
    return *m_diag;
}

Options& Context::opts() {
    return *m_opts;
}

AttrPool& Context::attrs() {
    if (!m_attr_pool) {
        m_attr_pool = std::make_unique<AttrPool>();
    }
    return *m_attr_pool;
}

const DiagnosticEngine& Context::diag() const {
    return *m_diag;
}

const Options& Context::opts() const {
    return *m_opts;
}

TypeRegistry& Context::types() {
    return *m_type_registry;
}

const AttrPool& Context::attrs() const {
    EXPECT(m_attr_pool);
    return *m_attr_pool;
}

ModuleLoader& Context::loader() {
    EXPECT(m_moduleLoader);
    return *m_moduleLoader;
}

const ModuleLoader& Context::loader() const {
    EXPECT(m_moduleLoader);
    return *m_moduleLoader;
}

std::shared_ptr<Macro> Context::macro() const {
    return m_macro;
}

void Context::setMacro(std::shared_ptr<Macro> macro) {
    m_macro = std::move(macro);
}

} // namespace trust
