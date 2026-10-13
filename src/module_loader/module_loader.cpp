// module_loader.cpp - реализация ModuleLoader
#include "module_loader/module_loader.hpp"
#include "module_loader/path/module_path_policy.hpp"
#include "module_loader/interface/interface_install.hpp"
#include "module_loader/module_export.hpp"
#include "session/context.hpp"
#include "sourcemap/writer.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "ast/term_to_ast.hpp"
#include "package/package.hpp"
#include "package/manifest.hpp"
#include "syntax/module_header.hpp"
#include "location/module_name.hpp"
#include "utils/hash.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace trust {
namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Конструктор
// ----------------------------------------------------------------------------

ModuleLoader::ModuleLoader(Context& ctx)
: m_ctx(ctx) {
}

// ----------------------------------------------------------------------------
// ParseScope - RAII восстановление состояния разбора модуля
// ----------------------------------------------------------------------------

class ModuleLoader::ParseScope {
  public:
    explicit ParseScope(ModuleLoader& loader)
    : m_loader(loader)
    , m_ctx(loader.m_ctx)
    , m_previousParseModule(m_ctx.currentParseModule()) {}
    ~ParseScope() {
        m_ctx.setCurrentParseModule(m_previousParseModule);
        if (m_pushedMacroScope) {
            m_ctx.macro()->PopScope();
        }
        m_loader.popModule();
    }
    ParseScope(const ParseScope&) = delete;
    ParseScope& operator=(const ParseScope&) = delete;

    /// Вход в модуль: стек загрузки + изолированный скоуп макросов (базовый скоуп dsl виден
    /// всегда, макросы вызывающего наследуются поиском по стеку сверху вниз) + модуль разбора
    /// для реестра макроопределений (LSP).
    void enter(ModuleIndex idx) {
        m_loader.pushModule(idx);
        if (auto macro = m_ctx.macro()) {
            macro->PushScope();
            m_pushedMacroScope = true;
        }
        m_ctx.setCurrentParseModule(idx);
    }

  private:
    ModuleLoader& m_loader;
    Context& m_ctx;
    std::optional<ModuleIndex> m_previousParseModule;
    bool m_pushedMacroScope = false;
};

// ----------------------------------------------------------------------------
// pushModule / popModule - стек текущего модуля (единственный источник истины)
// ----------------------------------------------------------------------------

void ModuleLoader::pushModule(ModuleIndex idx) {
    EXPECT(idx.value < registry().count());
    m_moduleStack.push_back(idx);
}

void ModuleLoader::popModule() {
    EXPECT(!m_moduleStack.empty());
    m_moduleStack.pop_back();
}

// ----------------------------------------------------------------------------
// -- Метод A: parseSourceModule - модуль из уже зарегистрированного source-файла --
// ----------------------------------------------------------------------------

namespace {

bool lessModuleAlias(const std::pair<std::string, ModuleIndex>& a, const std::pair<std::string, ModuleIndex>& b) {
    return a.first < b.first;
}

// Путь входного файла для проверки заголовка: `getFilename()` нормализован относительно
// baseDirectory source map, поэтому относительное имя разрешается от неё, а не от CWD.
std::filesystem::path sourcePathForHeaderCheck(const SourceMapWriter& source, MapperFile file) {
    std::filesystem::path path(source.get_file(file).getFilename());
    if (!path.is_absolute()) {
        path = std::filesystem::path(source.baseDirectory()) / path;
    }
    return path;
}

/// Логический путь файла относительно scope root — идентичность для файлов БЕЗ заголовка
/// (главная программа) и эталон для проверки контракта заголовка. ЕДИНСТВЕННОЕ место, где
/// идентичность выводится из файловой системы; потребители читают её из реестра.
std::optional<ModulePath> moduleIdentityFromFile(const fs::path& scopeRoot, const fs::path& file, const std::string& packageName) {
    std::error_code ec;
    const fs::path canonicalRoot = fs::weakly_canonical(scopeRoot, ec);
    if (ec) {
        return std::nullopt;
    }
    const fs::path canonicalFile = fs::weakly_canonical(file, ec);
    if (ec) {
        return std::nullopt;
    }
    fs::path rel = fs::relative(canonicalFile, canonicalRoot, ec);
    if (ec || rel.empty()) {
        return std::nullopt;
    }
    for (const fs::path& part : rel) {
        if (part == "..") {
            return std::nullopt;
        }
    }
    rel.replace_extension();
    std::vector<std::string> components;
    for (const fs::path& part : rel) {
        if (!part.empty()) {
            components.push_back(part.string());
        }
    }
    if (components.empty()) {
        return std::nullopt;
    }
    return ModulePath::fromComponents(packageName, std::move(components));
}

} // namespace

ModuleIndex ModuleLoader::parseSourceModule(std::string_view moduleId, MapperFile src, MapperRange range, std::filesystem::path scopeRoot,
                                            std::string packageName, bool allowNoHeader) {
    checkImportCycle(moduleId, range);

    const std::filesystem::path moduleScope = scopeRoot;
    const std::string modulePackage = packageName;
    const ModuleIndex idx = registry().getOrLoad(moduleId);
    if (registry().isLoaded(idx)) {
        // Кеш: тело уже разобрано в scope ПЕРВОЙ загрузки. Повторная загрузка scope не меняет,
        // иначе `registry().scopeRoot(idx)` у последующих потребителей указывал бы на другую область.
        return idx;
    }
    if (!scopeRoot.empty()) {
        registry().setScope(idx, std::move(scopeRoot));
    }

    const std::string& source_text = m_ctx.source().get_file(src).getSource();
    const ModuleHeaderScan header = scanModuleHeader(source_text);
    if (!header.error.empty()) {
        m_ctx.diag().report(Severity::Error, moduleHeaderRange(src, header), "{}", header.error);
    }
    // Идентичность (логический путь) вычисляется ОДИН раз и хранится в реестре: загрузчик не
    // пересчитывает её в других местах (protected-доступ, относительные якоря).
    const std::optional<ModulePath> fileIdentity =
        moduleScope.empty() ? std::nullopt : moduleIdentityFromFile(moduleScope, sourcePathForHeaderCheck(m_ctx.source(), src), modulePackage);
    if (header.found) {
        if (!isValidModuleLogicalName(header.header)) {
            m_ctx.diag().report(Severity::Error, moduleHeaderRange(src, header), "invalid module header '{}'", header.header);
        } else if (!moduleScope.empty()) {
            const std::string expected = fileIdentity.has_value() ? fileIdentity->toString() : std::string{};
            if (expected.empty()) {
                m_ctx.diag().report(Severity::Error, moduleHeaderRange(src, header), "cannot compute module path relative to scope root '{}'",
                                    moduleScope.string());
            } else if (header.header != expected) {
                m_ctx.diag().report(Severity::Error, moduleHeaderRange(src, header), "module header '{}' does not match path (expected '{}')", header.header,
                                    expected);
            }
        }
    } else if (!allowNoHeader && header.error.empty()) {
        const MapperLocation begin = m_ctx.source().makeLoc(src, 0);
        m_ctx.diag().report(Severity::Error, m_ctx.source().makeRange(begin, begin),
                            "module file '{}' has no module header; expected '<path>;' as the first significant line", moduleId);
    }

    if (header.found) {
        registry().setLogicalName(idx, header.header);
    } else {
        const std::filesystem::path filePath(m_ctx.source().get_file(src).getFilename());
        registry().setLogicalName(idx, filePath.stem().string());
    }
    // Идентичность (логический путь): для файла с заголовком она совпадает с логическим именем и
    // выводится из него; для программы без заголовка путь от scope root вычислим только из ФС —
    // он сохраняется как факт загрузки (потребители его не пересчитывают).
    if (!header.found && fileIdentity.has_value()) {
        registry().setIdentity(idx, *fileIdentity);
    }

    // `@__MODULE_NAME__` уникально по компиляции; проверка — по уже загруженным логическим путям.
    const std::string& logical = registry().logicalName(idx);
    const std::string encoded = encodeModuleNameForIdent(logical);
    for (std::size_t k = 0; k < registry().count(); ++k) {
        const ModuleIndex other{k};
        if (other == idx) {
            continue;
        }
        const std::string& otherLogical = registry().logicalName(other);
        if (otherLogical != logical && encodeModuleNameForIdent(otherLogical) == encoded) {
            m_ctx.diag().report(Severity::Error, moduleHeaderRange(src, header),
                                "module paths '{}' and '{}' produce the same @__MODULE_NAME__ '{}'; rename one of them", otherLogical, logical, encoded);
            break;
        }
    }

    // Стек загрузки, скоуп макросов и текущий модуль разбора снимаются RAII-гардом даже при
    // FatalError парсера.
    ParseScope parseScope(*this);
    parseScope.enter(idx);

    // Рекурсивный вызов парсера для нового модуля. Парсим из переданного
    // source-файла, чтобы маппинги привязывались к реальному файлу, а не к
    // новому in-memory псевдо-источнику (имя с префиксом '@').
    Parser parser(m_ctx);
    // Блок интерфейса разбирается грамматикой (skipPrefix = его начало); путь заголовка
    // остаётся вне AST. Внутри блока макросы модуля не раскрываются (fileInterface=true).
    const std::size_t skipPrefix = header.hasInterface ? header.interfaceBegin : (header.found ? header.end : 0);
    TermPtr term = parser.ParseWithSource(src, /*expand_module=*/true, skipPrefix, /*fileInterface=*/header.hasInterface);

    // Единственная конверсия Term→AST: выполняется здесь, сразу после парсинга, чтобы
    // реестр не удерживал Term. Сайты импорта и модульный TU используют эти же узлы.
    std::vector<AstNodePtr> bodyAst;
    convertModuleBody(m_ctx, term, bodyAst);
    if (header.hasInterface) {
        // Интерфейс помечает блок (`isModuleInterface`) — штамп модуля идёт ПОСЛЕ него,
        // иначе блок интерфейса неотличим от анонимного кодового блока.
        installFileInterface(m_ctx, *this, idx, header, bodyAst);
        // Ранняя ошибка установки интерфейса: факты всё равно фиксируются один раз, чтобы
        // потребители не пересчитывали их по другому пути.
        if (!registry().hasInterfaceFacts(idx)) {
            setInterfaceFacts(idx, collectModuleInterface(bodyAst));
        }
    }
    // Идентичность узла: модуль объявления проставляется ОДИН раз загрузчиком.
    for (const AstNodePtr& node : bodyAst) {
        if (node && node->kind() != ParserToken::Kind::ModuleDecl) {
            stampModuleLevelNode(*node, idx);
        }
    }
    if (!header.hasInterface) {
        // Файл без блока интерфейса: экспорт-набор — все top-level объявления.
        setInterfaceFacts(idx, collectModuleInterface(bodyAst));
    }
    registry().setBodyAst(idx, std::move(bodyAst));

    return idx;
}


std::optional<ModulePath> ModuleLoader::resolveAnchorPath(const ModulePath& anchored, ModuleIndex caller) const {
    if (!anchored.isAnchored() || !caller.isValid() || caller.value >= registry().count()) {
        return std::nullopt;
    }
    // База якоря — сохранённая идентичность модуля (вычислена один раз при загрузке).
    const std::optional<ModulePath> base = registry().identity(caller);
    std::vector<std::string> parent;
    std::optional<std::string> package;
    if (base.has_value()) {
        parent = base->components;
        if (base->kind == ModulePath::Kind::Package) {
            package = base->package;
        }
    }

    if (anchored.anchor == ModulePath::Anchor::SelfFile) {
        if (!base.has_value() || !anchored.components.empty()) {
            return std::nullopt;
        }
        return base;
    }
    if (anchored.anchor == ModulePath::Anchor::CurrentDir) {
        if (!parent.empty()) {
            parent.pop_back();
        }
    } else { // PackageRoot
        parent.clear();
    }

    parent.insert(parent.end(), anchored.components.begin(), anchored.components.end());
    if (package.has_value()) {
        return ModulePath::fromComponents(*package, std::move(parent));
    }
    if (parent.empty()) {
        return std::nullopt; // пустой корень проекта не является адресуемым модулем
    }
    return ModulePath::fromComponents({}, std::move(parent));
}

ModuleLoader::ModuleTarget ModuleLoader::resolveModuleTarget(ModuleIndex callerIdx, std::string_view moduleId) const {
    fs::path callerScope = registry().scopeRoot(callerIdx);
    EXPECT(!callerScope.empty() && "ModuleLoader::resolveModuleTarget: caller scope root is not set");

    // ЕДИНСТВЕННЫЙ разбор spelling пути (`ModulePath`): префикс (файл/группа/пакет), компоненты,
    // имя пакета. Никакого повторного подсчёта `\`/`/`-сплита в других местах нет.
    std::optional<ModulePath> parsed = ModulePath::parse(moduleId);
    if (!parsed.has_value()) {
        FAULT("Invalid module identifier '{}': must start with 1, 2 or 3 backslashes", moduleId);
    }
    if (parsed->isAnchored()) {
        const std::optional<ModulePath> resolved = resolveAnchorPath(*parsed, callerIdx);
        if (!resolved.has_value()) {
            FAULT("Relative module anchor '{}' does not denote an addressable module", moduleId);
        }
        parsed = *resolved;
    }

    if (parsed->kind == ModulePath::Kind::Package) {
        std::vector<fs::path> packageRoots;
        packageRoots.push_back(callerScope);
        for (const fs::path& path : m_packagePaths) {
            packageRoots.push_back(path);
        }
        module_path::appendEnvList(packageRoots, "TRUST_PACKAGE_PATH");
        if (const std::optional<fs::path> res = resourceDirectory()) {
            packageRoots.push_back(*res);
        }
        std::error_code cwd_ec;
        packageRoots.push_back(fs::current_path(cwd_ec));
        return {std::move(packageRoots), std::move(*parsed)};
    }

    std::vector<fs::path> moduleRoots;
    moduleRoots.push_back(callerScope);
    for (const fs::path& path : m_modulePaths) {
        moduleRoots.push_back(path);
    }
    return {std::move(moduleRoots), std::move(*parsed)};
}

std::vector<fs::path> ModuleLoader::resolveTargetFiles(const ModuleTarget& target, std::string_view moduleId, MapperRange range,
                                                       std::filesystem::path& outScopeRoot, std::string& outPackageName) {
    outScopeRoot.clear();
    outPackageName.clear();

    if (target.path.kind == ModulePath::Kind::File || target.path.kind == ModulePath::Kind::Group) {
        const module_path::PathForm form =
            target.path.kind == ModulePath::Kind::File ? module_path::PathForm::File : (target.path.components.size() == 1 ? module_path::PathForm::Directory : module_path::PathForm::Either);
        module_path::TargetFiles resolved = module_path::collectTargetFiles(target.path, target.roots, form);
        if (!resolved.error.empty()) {
            reportModuleNotFound(moduleId, range, resolved.error);
        }
        outScopeRoot = std::move(resolved.scopeRoot);
        return std::move(resolved.files);
    }

    fs::path package_root;
    std::string package_error;
    if (!m_packages.resolveRoot(target.path, target.roots, package_root, package_error)) {
        reportModuleNotFound(moduleId, range, package_error);
    }
    outScopeRoot = package_root;
    outPackageName = target.path.package;
    if (target.path.components.empty()) {
        // `\\\pkg` — весь пакет: не резолв пути, а обход групп первого уровня + корневых .src.
        std::string group_error;
        std::vector<fs::path> files = module_path::collectGroupFiles(package_root, /*packageRoot=*/true, group_error);
        if (!group_error.empty()) {
            reportModuleNotFound(moduleId, range, group_error);
        }
        return files;
    }
    module_path::TargetFiles resolved = module_path::collectTargetFiles(target.path, {package_root}, module_path::PathForm::Either);
    if (!resolved.error.empty()) {
        reportModuleNotFound(moduleId, range, resolved.error);
    }
    outScopeRoot = std::move(resolved.scopeRoot);
    return std::move(resolved.files);
}

std::vector<ModuleIndex> ModuleLoader::resolveModules(std::string_view moduleId, MapperRange range) {
    if (const std::optional<ModulePath> parsed = ModulePath::parse(moduleId); parsed.has_value() && parsed->isAnchored()) {
        if (parsed->anchor == ModulePath::Anchor::SelfFile) {
            reportFatal(range, "Relative anchor '" + std::string(moduleId) + "' denotes the current file and cannot be imported");
        }
        if (parsed->components.empty()) {
            reportFatal(range, "Relative anchor '" + std::string(moduleId) + "' is a path prefix, not an import target");
        }
    }
    const SpellingLookup lookup = lookupSpelling(moduleId);
    if (lookup.kind == SpellingLookup::Kind::ModuleAlias) {
        // Spelling уникален в модуле-владельце: алиас не может адресовать и импорт того же
        // написания (обратное направление проверяет `registerModuleAlias`).
        reportFatal(range, "Module spelling '" + std::string(moduleId) + "' is already bound as a module alias in this module; rename one of them");
    }
    if (lookup.kind == SpellingLookup::Kind::Import && allImportsLoaded(lookup.indices)) {
        return lookup.indices;
    }
    return ensureLoadedGroup(moduleId, range);
}

std::vector<ModuleIndex> ModuleLoader::ensureLoadedGroup(std::string_view moduleId, MapperRange range) {
    checkImportCycle(moduleId, range);
    const std::optional<ModulePath> spelling = ModulePath::parse(moduleId);
    if (!spelling.has_value()) {
        reportFatal(range, "Invalid module identifier '" + std::string(moduleId) + "'");
    }
    if (!spelling->isAnchored() && !isValidModuleLogicalName(moduleId)) {
        reportFatal(range, "Invalid module identifier '" + std::string(moduleId) + "'");
    }

    const ModuleIndex caller = currentModuleIndex();
    const ModuleTarget target = resolveModuleTarget(caller, moduleId);
    fs::path scope_root;
    std::string package_name;
    const std::vector<fs::path> files = resolveTargetFiles(target, moduleId, range, scope_root, package_name);

    inheritPackageNameOfCaller(caller, scope_root, package_name);

    if (files.empty()) {
        reportModuleNotFound(moduleId, range, "group contains no .src files");
    }

    std::vector<ModuleIndex> indices;
    indices.reserve(files.size());
    for (const fs::path& file : files) {
        const fs::path canonical = module_path::canonicalPath(file);
        if (auto existing = registry().findIndex(canonical.string()); existing) {
            if (registry().isLoaded(*existing)) {
                indices.push_back(*existing);
                continue;
            }
            // Запись есть, но тело ещё не сконвертировано: файл в процессе загрузки. Это цикл,
            // найденный по КАНОНИЧЕСКОМУ пути (spelling мог ещё не попасть в карту импорта).
            reportImportCycle(moduleId, range);
        }
        MapperFile src = m_ctx.source().load_file(canonical.string());
        indices.push_back(parseSourceModule(canonical.string(), src, range, scope_root, package_name, /*allowNoHeader=*/false));
    }
    // Групповой/пакетный импорт загружает файлы, но spelling каждого файла тоже принадлежит
    // владельцу: `\\grp\file::name` и `\m :- \\grp\file` должны резолвиться без отдельного импорта.
    auto& spellings = m_importSpellings[caller];
    for (const ModuleIndex idx : indices) {
        const std::string& logical = registry().logicalName(idx);
        if (logical.empty() || logical.front() != '\\') {
            continue; // headerless root не является импортируемым модулем
        }
        const auto [it, inserted] = spellings.emplace(logical, std::vector<ModuleIndex>{idx});
        if (!inserted && std::find(it->second.begin(), it->second.end(), idx) == it->second.end()) {
            reportFatal(range, "Module spelling '" + logical + "' is already bound to another file in this module");
        }
    }
    spellings[std::string(moduleId)] = indices;
    return indices;
}

// ----------------------------------------------------------------------------
// checkImportCycle / importChain - единственная точка диагностики цикла
// ----------------------------------------------------------------------------

void ModuleLoader::reportImportCycle(std::string_view moduleId, MapperRange range) const {
    reportFatal(range, "Cyclic module dependency detected: " + importChain(moduleId));
}

void ModuleLoader::checkImportCycle(std::string_view moduleId, MapperRange range) const {
    // Запись уже есть, но тело ещё не сконвертировано ⇒ модуль в процессе загрузки.
    // Spelling-домен (алиас/сайт импорта) — основной; канонический путь нужен, когда загрузчик
    // сам вызывает parseSourceModule(путь) и spelling ещё не зарегистрирован.
    std::vector<ModuleIndex> candidates;
    const SpellingLookup lookup = lookupSpelling(moduleId);
    if (lookup.found()) {
        candidates = lookup.indices;
    } else if (const auto byPath = registry().findIndex(moduleId)) {
        candidates = {*byPath};
    }
    for (const ModuleIndex idx : candidates) {
        if (!registry().isLoaded(idx)) {
            reportImportCycle(moduleId, range);
        }
    }
}

std::string ModuleLoader::importChain(std::string_view closingModuleId) const {
    std::string chain;
    for (const ModuleIndex idx : m_moduleStack) {
        chain += registry().modulePath(idx);
        chain += " -> ";
    }
    chain += closingModuleId;
    return chain;
}

// ----------------------------------------------------------------------------
// isLoaded - по индексу
// ----------------------------------------------------------------------------

bool ModuleLoader::isLoaded(ModuleIndex index) const {
    return registry().isLoaded(index);
}

std::string_view ModuleLoader::modulePath(ModuleIndex index) const {
    return registry().modulePath(index);
}

void ModuleLoader::setInterfaceFacts(ModuleIndex index, ModuleInterfaceFacts facts) {
    registry().setInterfaceFacts(index, std::move(facts));
}

const ModuleInterfaceFacts& ModuleLoader::interfaceFacts(ModuleIndex index) {
    return registry().interfaceFacts(index);
}

const std::vector<ExportEntry>& ModuleLoader::exportEntries(ModuleIndex index) {
    return interfaceFacts(index).entries;
}

std::shared_ptr<const SiteProjection> ModuleLoader::siteProjection(ModuleIndex index, const utils::ImportMasks& masks) {
    const auto key = std::make_pair(index, masks.normalizedKey());
    const auto existing = m_siteProjections.find(key);
    if (existing != m_siteProjections.end()) {
        return existing->second;
    }
    const ModuleInterfaceFacts& facts = interfaceFacts(index);
    auto projection = std::make_shared<SiteProjection>();
    std::vector<AstNodePtr> nodes = filterExportEntries(facts.entries, facts.namespaceAliases, masks, &projection->unmatchedMasks);
    projection->exports = std::make_shared<const std::vector<AstNodePtr>>(std::move(nodes));
    std::vector<ModuleNamespaceAlias> aliases;
    for (const ModuleNamespaceAlias& alias : facts.namespaceAliases) {
        if (masks.matches(alias.alias)) {
            aliases.push_back(alias);
        }
    }
    projection->namespaceAliases = std::make_shared<const std::vector<ModuleNamespaceAlias>>(std::move(aliases));
    // protected-набор: нужен только факт «есть доступные имена» + несовпавшие маски. Список
    // узлов НЕ строится (он никому не передаётся): имена берутся views на qualified-имена facts.
    std::vector<std::string_view> protectedNames;
    protectedNames.reserve(facts.protectedEntries.size());
    for (const ExportEntry& entry : facts.protectedEntries) {
        protectedNames.push_back(entry.qualifiedName);
        projection->hasProtectedExports = projection->hasProtectedExports || masks.matches(entry.qualifiedName);
    }
    projection->protectedUnmatchedMasks = masks.unmatchedIn(protectedNames);
    return m_siteProjections.emplace(key, std::move(projection)).first->second;
}

const std::vector<AstNodePtr>& ModuleLoader::bodyAst(ModuleIndex index) const {
    return registry().bodyAst(index);
}

std::shared_ptr<std::vector<AstNodePtr>> ModuleLoader::bodyAstShared(ModuleIndex index) const {
    return registry().bodyAstShared(index);
}

// ----------------------------------------------------------------------------
// ModuleResolver (узкий интерфейс для парсера)
// ----------------------------------------------------------------------------

std::optional<ModuleIndex> ModuleLoader::findLoaded(std::string_view moduleId) const {
    const SpellingLookup lookup = lookupSpelling(moduleId);
    if (lookup.kind == SpellingLookup::Kind::ModuleAlias) {
        return registry().isLoaded(lookup.indices.front()) ? std::optional<ModuleIndex>(lookup.indices.front()) : std::nullopt;
    }
    if (lookup.kind != SpellingLookup::Kind::Import) {
        return std::nullopt;
    }
    // Ссылка адресует ОДИН файл: групповой spelling (`\\group`) не квалифицируем.
    if (lookup.indices.size() != 1) {
        return std::nullopt;
    }
    return registry().isLoaded(lookup.indices.front()) ? std::optional<ModuleIndex>(lookup.indices.front()) : std::nullopt;
}

bool ModuleLoader::isModuleLoaded(std::string_view moduleId) const {
    const SpellingLookup lookup = lookupSpelling(moduleId);
    if (lookup.kind == SpellingLookup::Kind::ModuleAlias) {
        return registry().isLoaded(lookup.indices.front());
    }
    return lookup.kind == SpellingLookup::Kind::Import && allImportsLoaded(lookup.indices);
}

ModuleIndex ModuleLoader::activeModuleIndex() const noexcept {
    return m_moduleStack.empty() ? ModuleIndex::invalid() : m_moduleStack.back();
}

ModuleLoader::SpellingLookup ModuleLoader::lookupSpelling(std::string_view spelling) const {
    if (const std::optional<ModulePath> parsed = ModulePath::parse(spelling); parsed.has_value() && parsed->isAnchored()) {
        const ModuleIndex caller = activeModuleIndex();
        SpellingLookup anchored;
        if (!caller.isValid()) {
            return anchored;
        }
        if (parsed->anchor == ModulePath::Anchor::SelfFile) {
            anchored.kind = SpellingLookup::Kind::Import;
            anchored.indices = {caller};
            return anchored;
        }
        const std::optional<ModulePath> resolved = resolveAnchorPath(*parsed, caller);
        if (!resolved.has_value()) {
            return anchored;
        }
        return lookupSpelling(resolved->toString());
    }
    SpellingLookup lookup;
    if (const auto alias = findModuleAlias(activeModuleIndex(), spelling)) {
        lookup.kind = SpellingLookup::Kind::ModuleAlias;
        lookup.indices = {*alias};
        return lookup;
    }
    const ModuleIndex caller = activeModuleIndex();
    const std::vector<ModuleIndex>* cached = caller.isValid() ? findImportSpellingOfModule(caller, spelling) : findImportSpellingUnique(spelling);
    if (cached != nullptr) {
        lookup.kind = SpellingLookup::Kind::Import;
        lookup.indices = *cached;
    }
    return lookup;
}

bool ModuleLoader::allImportsLoaded(const std::vector<ModuleIndex>& indices) const {
    if (indices.empty()) {
        return false;
    }
    for (const ModuleIndex idx : indices) {
        if (!registry().isLoaded(idx)) {
            return false;
        }
    }
    return true;
}

std::string ModuleLoader::packageNameOf(std::string_view logicalName) {
    // Логическое имя — тот же spelling-домен (`ModulePath`): пакетный компонент есть только у
    // пакетного пути (`\\\pkg\...`); остальные (файл/группа/голый stem) пакета не имеют.
    const std::optional<ModulePath> parsed = ModulePath::parse(logicalName);
    return parsed.has_value() && parsed->kind == ModulePath::Kind::Package ? parsed->package : std::string{};
}

void ModuleLoader::inheritPackageNameOfCaller(ModuleIndex caller, const std::filesystem::path& foundScope, std::string& packageName) const {
    if (!packageName.empty() || !caller.isValid()) {
        return;
    }
    const std::string callerPackage = packageNameOf(registry().logicalName(caller));
    if (callerPackage.empty()) {
        return;
    }
    std::error_code caller_ec;
    std::error_code found_ec;
    const std::filesystem::path callerScope = std::filesystem::weakly_canonical(registry().scopeRoot(caller), caller_ec);
    const std::filesystem::path resolvedScope = std::filesystem::weakly_canonical(foundScope, found_ec);
    if (!caller_ec && !found_ec && callerScope == resolvedScope) {
        packageName = callerPackage;
    }
}

MapperRange ModuleLoader::moduleHeaderRange(MapperFile src, const ModuleHeaderScan& header) const {
    const uint32_t begin = static_cast<uint32_t>(header.begin);
    const uint32_t end = static_cast<uint32_t>(std::max(header.end, header.begin + 1));
    return m_ctx.source().makeRange(m_ctx.source().makeLoc(src, begin), m_ctx.source().makeLoc(src, end));
}

void ModuleLoader::reportModuleNotFound(std::string_view moduleId, MapperRange range, const std::string& message) const {
    reportFatal(range, "Module '" + std::string(moduleId) + "' not found: " + message);
}

void ModuleLoader::reportFatal(MapperRange range, std::string message) const {
    m_ctx.diag().report(Severity::Fatal, range, "{}", message);
    // report(Fatal) бросает FatalError: управление сюда не возвращается.
    std::unreachable();
}

const std::vector<ModuleIndex>* ModuleLoader::findImportSpellingOfModule(ModuleIndex owner, std::string_view spelling) const {
    const auto owned = m_importSpellings.find(owner);
    if (owned == m_importSpellings.end()) {
        return nullptr;
    }
    const auto it = owned->second.find(spelling);
    return it == owned->second.end() ? nullptr : &it->second;
}

const std::vector<ModuleIndex>* ModuleLoader::findImportSpellingUnique(std::string_view spelling) const {
    const std::vector<ModuleIndex>* found = nullptr;
    for (const auto& [owner, spellings] : m_importSpellings) {
        (void)owner;
        const auto it = spellings.find(spelling);
        if (it == spellings.end()) {
            continue;
        }
        if (found == nullptr) {
            found = &it->second;
            continue;
        }
        // Один spelling у разных владельцев с РАЗНЫМИ файлами неоднозначен: без активного модуля
        // выбрать владельца нельзя (запрет «первого победителя»).
        if (*found != it->second) {
            return nullptr;
        }
    }
    return found;
}

bool ModuleLoader::hasImportSpelling(ModuleIndex owner, std::string_view spelling) const {
    return findImportSpellingOfModule(owner, spelling) != nullptr;
}

std::optional<ModuleIndex> ModuleLoader::findModuleAlias(ModuleIndex owner, std::string_view alias) const {
    if (!owner.isValid()) {
        return std::nullopt;
    }
    const auto byOwner = m_moduleAliases.find(owner);
    if (byOwner == m_moduleAliases.end()) {
        return std::nullopt;
    }
    const auto it = byOwner->second.find(alias);
    if (it == byOwner->second.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::pair<std::string, ModuleIndex>> ModuleLoader::moduleAliases(ModuleIndex owner) const {
    std::vector<std::pair<std::string, ModuleIndex>> out;
    const auto byOwner = m_moduleAliases.find(owner);
    if (byOwner == m_moduleAliases.end()) {
        return out;
    }
    out.reserve(byOwner->second.size());
    for (const auto& [spelling, target] : byOwner->second) {
        out.emplace_back(spelling, target);
    }
    std::sort(out.begin(), out.end(), lessModuleAlias);
    return out;
}

bool ModuleLoader::registerModuleAlias(std::string_view alias, ModuleIndex target, ModuleIndex owner) {
    if (alias.empty() || !isValidModuleLogicalName(alias) || !target.isValid() || !registry().isLoaded(target) || !owner.isValid() ||
        owner.value >= registry().count()) {
        return false;
    }
    if (hasImportSpelling(owner, alias)) {
        return false; // spelling уже адресует импорт ЭТОГО владельца (уникальность — в модуле)
    }
    auto& owned = m_moduleAliases[owner];
    const auto [it, inserted] = owned.emplace(std::string(alias), target);
    return inserted || it->second == target;
}

bool ModuleLoader::hasModuleAlias(ModuleIndex owner, std::string_view alias, ModuleIndex target) const {
    const std::optional<ModuleIndex> bound = findModuleAlias(owner, alias);
    return bound.has_value() && *bound == target;
}

bool ModuleLoader::canAccessProtected(ModuleIndex importer, ModuleIndex owner) const {
    if (!importer.isValid() || !owner.isValid() || importer.value >= registry().count() || owner.value >= registry().count()) {
        return false;
    }
    if (importer == owner) {
        return true;
    }
    const std::optional<ModulePath> ownerPath = registry().identity(owner);
    const std::optional<ModulePath> importerPath = registry().identity(importer);
    if (!ownerPath.has_value() || !importerPath.has_value()) {
        return false;
    }
    const std::string ownerScope = module_path::canonicalPath(registry().scopeRoot(owner)).string();
    const std::string importerScope = module_path::canonicalPath(registry().scopeRoot(importer)).string();
    if (ownerPath->kind == ModulePath::Kind::Package || importerPath->kind == ModulePath::Kind::Package) {
        return ownerPath->kind == ModulePath::Kind::Package && importerPath->kind == ModulePath::Kind::Package && ownerPath->package == importerPath->package &&
               ownerScope == importerScope;
    }
    return module_path::moduleGroupKey(*ownerPath) == module_path::moduleGroupKey(*importerPath) && ownerScope == importerScope;
}

std::string_view ModuleLoader::moduleLogicalName(ModuleIndex index) const {
    EXPECT(index.value < registry().count() && "moduleLogicalName: module index is not registered");
    return registry().logicalName(index);
}

// ----------------------------------------------------------------------------
// currentModuleIndex
// ----------------------------------------------------------------------------

ModuleIndex ModuleLoader::currentModuleIndex() const {
    if (m_moduleStack.empty()) {
        FAULT("ModuleLoader::currentModuleIndex() called on empty stack");
    }
    return m_moduleStack.back();
}

} // namespace trust
