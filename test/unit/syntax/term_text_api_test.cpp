// test/unit/syntax/term_text_api_test.cpp
// API текста Term: единственный read-аксессор getText(); Clone не копирует view
// и копирует owned-строку ровно один раз.
#include "syntax/term/term.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace trust {
namespace {

TEST(TermTextApiTest, CloneSharesSourceBackedView) {
    std::string source = "shared_name";
    const TermPtr term = Term::Create(TermID::NAME, parser::token_type::NAME, source.data(), source.size());
    const TermPtr clone = term->Clone();

    ASSERT_EQ(clone->getText(), "shared_name");
    EXPECT_EQ(clone->getText().data(), source.data()) << "source-backed view must stay shared after Clone";
    EXPECT_EQ(term->getText().data(), clone->getText().data());
    EXPECT_EQ(clone->getTermID(), term->getTermID());
}

TEST(TermTextApiTest, CloneCopiesOwnedTextOnce) {
    const TermPtr term = Term::Create(TermID::NAME, std::string("owned_name"));
    const TermPtr clone = term->Clone();

    ASSERT_EQ(clone->getText(), "owned_name");
    EXPECT_NE(clone->getText().data(), term->getText().data()) << "owned text must be copied, not shared";
}

TEST(TermTextApiTest, OwnedMutationReplacesSourceView) {
    std::string source = "view_name";
    const TermPtr term = Term::Create(TermID::NAME, parser::token_type::NAME, source.data(), source.size());
    ASSERT_EQ(term->getText().data(), source.data());

    term->setTextOwned("owned_name");
    EXPECT_EQ(term->getText(), "owned_name");
    EXPECT_NE(term->getText().data(), source.data());

    const TermPtr clone = term->Clone();
    ASSERT_EQ(clone->getText(), "owned_name");
    EXPECT_NE(clone->getText().data(), term->getText().data()) << "owned text must be copied once by Clone";
}

TEST(TermTextApiTest, MutableTextConvertsViewToOwned) {
    std::string source = "a";
    const TermPtr term = Term::Create(TermID::NAME, parser::token_type::NAME, source.data(), source.size());
    term->mutableText().append("bc");

    EXPECT_EQ(term->getText(), "abc");
    EXPECT_NE(term->getText().data(), source.data());
}

TEST(TermTextApiTest, DefaultTextIsEmptyOwned) {
    const TermPtr term = Term::Create(TermID::NAME, std::string{});
    EXPECT_TRUE(term->getText().empty());
}

TEST(TermTextApiTest, MutableTextOnOwnedDoesNotReallocateCopy) {
    const TermPtr term = Term::Create(TermID::NAME, std::string("owned"));
    const char* before = term->getText().data();
    term->mutableText() += "!";
    EXPECT_EQ(term->getText(), "owned!");
    EXPECT_EQ(term->getText().data(), before) << "already-owned text must be mutated in place";
}

TEST(TermTextApiTest, CloneOwnedTextIsIndependentAfterMutation) {
    const TermPtr term = Term::Create(TermID::NAME, std::string("base"));
    const TermPtr clone = term->Clone();
    clone->mutableText() += "_clone";
    EXPECT_EQ(clone->getText(), "base_clone");
    EXPECT_EQ(term->getText(), "base") << "mutating clone must not affect the original owned text";
}

} // namespace
} // namespace trust
