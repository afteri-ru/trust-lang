// include/stdlib/ast_matcher.hpp
// AST Matcher — компонент для поиска и сопоставления узлов AST
// Использует Clang AST Matchers для анализа C++ кода

#ifndef STDLIB_AST_MATCHER_HPP
#define STDLIB_AST_MATCHER_HPP

#include "stdlib/analyzer.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace trust {

// DiagnosticConsumer для отслеживания синтаксических ошибок
class ErrorTracker : public clang::DiagnosticConsumer {
  public:
    bool has_errors = false;
    unsigned error_count = 0;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic &) override;
};

// AST Match callback — делегирует анализ в MethodAnalyzer
class AstMatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    using ResultsCallback = std::function<void(MethodInfo &&)>;

    explicit AstMatchHandler(ResultsCallback callback);
    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;

  private:
    ResultsCallback callback_;
    void collect_methods_from_class(const clang::CXXRecordDecl *record);
};

// Кастомный ASTFrontendAction — подключает MatchFinder и ErrorTracker
class MatchFrontendAction : public clang::ASTFrontendAction {
  public:
    MatchFrontendAction(clang::ast_matchers::MatchFinder &finder, ErrorTracker &tracker);
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &ci, clang::StringRef) override;

  private:
    clang::ast_matchers::MatchFinder &m_finder;
    ErrorTracker &m_tracker;
};

// FrontendActionFactory для MatchFrontendAction
class MatchFrontendActionFactory : public clang::tooling::FrontendActionFactory {
  public:
    MatchFrontendActionFactory(clang::ast_matchers::MatchFinder &finder, ErrorTracker &tracker);
    std::unique_ptr<clang::FrontendAction> create() override;

  private:
    clang::ast_matchers::MatchFinder &m_finder;
    ErrorTracker &m_tracker;
};

// Запуск анализа для одного набора флагов (одного стандарта)
// Возвращает false при синтаксических ошибках.
// Заполняет results записями для указанной версии.
bool run_clang_analysis(const std::string &source_file, const std::vector<std::string> &args, std::vector<MethodInfo> &results);

} // namespace trust

#endif // STDLIB_AST_MATCHER_HPP