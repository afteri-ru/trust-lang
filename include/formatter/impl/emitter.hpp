#pragma once

// Класс Emitter — один проход по токенам: вывод зазоров/комментариев, правила перевода строки
// (computeNewline) и пробела (computeSpace), отступы. Реализация — src/formatter/impl/emitter.cpp.
// Внутренний заголовок зоны форматтера: включается только собственной реализацией.

#include "formatter/format.hpp"
#include "formatter/impl/format_model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust::formatter {

class Emitter {
  public:
    Emitter(std::vector<Token> tokens, const FormatOptions& opts);

    void setSource(std::string_view src);

    std::string run();

  private:
    std::string_view gapText(int a, int b) const;
    static int countNewlines(const std::string& s);
    void appendCommentNewlines(int pendingNl);
    void emitGapComments(const Gap& gap);
    void appendNewlines(int inGap);
    std::string indentString() const;
    void emitIndent();
    bool computeNewline(int prev, int i);
    bool isKeywordText(const std::string& t) const;
    bool isKeywordLike(const std::string& t) const;
    std::string keywordText(const Token& tok) const;
    bool isNoParenKeyword(int idx) const;
    bool isContractToken(int i) const;
    bool computeSpace(int prev, int i);
    int groupOf(int idx) const;
    bool groupMultilineAt(int idx) const;
    void precomputeGroups();
    bool containsForcedMultiline(int o, int c) const;
    bool groupFitsInline(int o, int c) const;

    std::vector<Token> toks_;
    const FormatOptions& opts_;
    std::string_view sourceView_;
    std::vector<int> match_;
    std::vector<bool> multiline_;
    std::vector<int> openStack_;
    std::vector<bool> inParen_;
    std::string out_;
    int indent_ = 0;
    int contractIndent_ = 0;
    bool lineStart_ = true;
};

} // namespace trust::formatter