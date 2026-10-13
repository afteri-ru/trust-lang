#include "symbols/import/import_sites.hpp"

#include "gtest/gtest.h"

#include <stdexcept>

namespace trust {
namespace {

ImportSite makeSite(ModuleIndex importer, ModuleIndex imported, std::string_view masks, int scope_line) {
    ImportSite site;
    site.importer = importer;
    site.imported = imported;
    site.masks = utils::ImportMasks::parse(masks);
    // Позиция скоупа кодируется началом диапазона (единственная идентичность скоупа).
    site.scopeRange.begin = MapperLocation::makeLoc(MapperFile::fromRaw(0), static_cast<std::size_t>(scope_line));
    return site;
}

// -- ImportSiteRegistry: одна точка записи стека и журнала ---------------------------------

TEST(ImportSiteRegistry, PushFillsStackAndJournal) {
    ImportSiteRegistry registry;
    EXPECT_TRUE(registry.empty());
    EXPECT_EQ(registry.depth(), 0u);

    registry.push(makeSite(ModuleIndex{1}, ModuleIndex{2}, "a", 10));
    EXPECT_FALSE(registry.empty());
    EXPECT_EQ(registry.depth(), 1u);
    EXPECT_EQ(registry.top().imported.value, 2u);
    ASSERT_EQ(registry.journal().size(), 1u);
    EXPECT_EQ(registry.journal().front().imported.value, 2u);
}

TEST(ImportSiteRegistry, JournalDeduplicatesBySiteIdentity) {
    ImportSiteRegistry registry;
    const ImportSite site = makeSite(ModuleIndex{1}, ModuleIndex{2}, "a", 10);
    registry.record(site);
    registry.record(site);
    ASSERT_EQ(registry.journal().size(), 1u);

    // Другая позиция скоупа — отдельный сайт (импорт в разных блоках).
    registry.record(makeSite(ModuleIndex{1}, ModuleIndex{2}, "a", 20));
    EXPECT_EQ(registry.journal().size(), 2u);
    // Другие маски — тоже отдельный сайт.
    registry.record(makeSite(ModuleIndex{1}, ModuleIndex{2}, "b", 10));
    EXPECT_EQ(registry.journal().size(), 3u);
    EXPECT_EQ(registry.depth(), 0u); // журнал не делает сайт активным
}

TEST(ImportSiteRegistry, PopRemovesOnlyActiveSite) {
    ImportSiteRegistry registry;
    registry.push(makeSite(ModuleIndex{1}, ModuleIndex{2}, "", 1));
    registry.push(makeSite(ModuleIndex{1}, ModuleIndex{3}, "", 2));
    EXPECT_EQ(registry.depth(), 2u);
    EXPECT_EQ(registry.top().imported.value, 3u);

    registry.pop();
    EXPECT_EQ(registry.depth(), 1u);
    EXPECT_EQ(registry.top().imported.value, 2u);
    EXPECT_EQ(registry.journal().size(), 2u);

    registry.pop();
    EXPECT_TRUE(registry.empty());
    registry.pop(); // лишний pop безопасен
    EXPECT_TRUE(registry.empty());
}

TEST(ImportSiteRegistry, GuardActivatesSiteAndJournalsIt) {
    ImportSiteRegistry registry;

    EXPECT_EQ(registry.depth(), 0u);
    try {
        ImportSiteRegistry::Guard guard(registry, makeSite(ModuleIndex{1}, ModuleIndex{2}, "", 5));
        EXPECT_EQ(registry.depth(), 1u);
        EXPECT_EQ(registry.top().imported.value, 2u);
        // Активный стек хранит индекс журнала: сайт записан ОДИН раз.
        EXPECT_EQ(registry.journal().size(), 1u);
        throw std::runtime_error("analysis failure");
    } catch (const std::runtime_error&) {
        // RAII обязан снять активный сайт даже при исключении.
    }
    EXPECT_EQ(registry.depth(), 0u);
    EXPECT_EQ(registry.journal().size(), 1u); // журнал переживает снятие активного сайта
}

} // namespace
} // namespace trust
