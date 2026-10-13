// test/unit/types/type_registry_fixture.hpp
// Общая фикстура юнит-тестов типовой системы: Context + TypeSystem, внедрённый в контекст.
// Владение реестром — у теста (Context держит невладеющий указатель).
#pragma once

#include "session/context.hpp"
#include "types/type_system.hpp"

#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace test {

class TypeSystemFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    [[nodiscard]] TypeSystem& types() noexcept { return *m_types; }
    /// READ-ONLY view над зонами (для проверок чтения).
    [[nodiscard]] TypeReadModel model() const noexcept { return m_types->read(); }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

} // namespace test
} // namespace trust
