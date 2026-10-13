// test/unit/ir/analysis_manager_test.cpp
// AnalysisManager: порядок по зависимостям (не по регистрации), неизвестная зависимость и
// цикл — внутренние ошибки (FAULT), а не тихий пропуск.
#include "ir/analysis_manager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace trust::flow {
namespace {

class RecordingAnalysis : public Analysis {
  public:
    RecordingAnalysis(std::string name, std::vector<std::string_view> dependencies, std::vector<std::string>& log)
    : m_name(std::move(name))
    , m_dependencies(std::move(dependencies))
    , m_log(log) {}

    [[nodiscard]] std::string_view name() const override { return m_name; }
    [[nodiscard]] std::span<const std::string_view> dependencies() const override { return m_dependencies; }
    void run(const MirModule& /*mir*/) override { m_log.push_back(m_name); }

  private:
    std::string m_name;
    std::vector<std::string_view> m_dependencies;
    std::vector<std::string>& m_log;
};

} // namespace

TEST(AnalysisManagerTest, RunsDependenciesInTopologicalOrder) {
    MirModule mir;
    std::vector<std::string> log;
    AnalysisManager manager;
    // Регистрация в «неправильном» порядке: зависимый раньше зависимости.
    manager.add(std::make_unique<RecordingAnalysis>("borrow", std::vector<std::string_view>{"definite-assignment"}, log));
    manager.add(std::make_unique<RecordingAnalysis>("definite-assignment", std::vector<std::string_view>{}, log));
    manager.add(std::make_unique<RecordingAnalysis>("independent", std::vector<std::string_view>{}, log));

    manager.run(mir);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "definite-assignment");
    EXPECT_EQ(log[1], "borrow");
    EXPECT_EQ(log[2], "independent");
}

TEST(AnalysisManagerTest, UnknownDependencyFails) {
    MirModule mir;
    std::vector<std::string> log;
    AnalysisManager manager;
    manager.add(std::make_unique<RecordingAnalysis>("a", std::vector<std::string_view>{"missing"}, log));
    EXPECT_THROW(manager.run(mir), std::runtime_error);
}

TEST(AnalysisManagerTest, DependencyCycleFails) {
    MirModule mir;
    std::vector<std::string> log;
    AnalysisManager manager;
    manager.add(std::make_unique<RecordingAnalysis>("a", std::vector<std::string_view>{"b"}, log));
    manager.add(std::make_unique<RecordingAnalysis>("b", std::vector<std::string_view>{"a"}, log));
    EXPECT_THROW(manager.run(mir), std::runtime_error);
}

} // namespace trust::flow
