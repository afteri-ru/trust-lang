// src/stdlib/ast_matcher.cpp
// Реализация AST Matcher — компонента для поиска и сопоставления узлов AST

#include "stdlib/ast_matcher.hpp"
#include "stdlib/api_comparator.hpp"

#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/raw_ostream.h"

namespace trust {

// ─────────────────────────────────────────────────────────────
// ErrorTracker
// ─────────────────────────────────────────────────────────────
void ErrorTracker::HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) {
    if (level >= clang::DiagnosticsEngine::Error) {
        has_errors = true;
        // Логируем первую ошибку для диагностики
        if (error_count == 0) {
            llvm::SmallString<64> msg;
            info.FormatDiagnostic(msg);
            llvm::errs() << "Clang diagnostic error: " << msg << "\n";
        }
        error_count++;
    }
}

// ─────────────────────────────────────────────────────────────
// AstMatchHandler
// ─────────────────────────────────────────────────────────────
AstMatchHandler::AstMatchHandler(ResultsCallback callback)
: callback_(std::move(callback)) {
}

void AstMatchHandler::run(const clang::ast_matchers::MatchFinder::MatchResult& result) {
    const clang::NamedDecl* decl = nullptr;

    // Class template declaration — также собираем все методы
    if (const auto* ctd = result.Nodes.getNodeAs<clang::ClassTemplateDecl>("ctd")) {
        decl = ctd;
        if (auto info = MethodAnalyzer::analyze(decl)) {
            callback_(std::move(*info));
        }
        // Собираем методы из класса-шаблона
        if (const auto* record = ctd->getTemplatedDecl()) {
            collect_methods_from_class(record);
        }
        return;
    }

    // Class template specialization — пропускаем
    if (result.Nodes.getNodeAs<clang::ClassTemplateSpecializationDecl>("ctsd")) {
        return;
    }

    // Function template declaration
    if (const auto* ftd = result.Nodes.getNodeAs<clang::FunctionTemplateDecl>("ftd")) {
        // Пропускаем deduction guides — они не являются обычными функциями
        if (const auto* templated = ftd->getTemplatedDecl()) {
            if (llvm::isa<clang::CXXDeductionGuideDecl>(templated))
                return;
        }
        decl = ftd;
        if (auto info = MethodAnalyzer::analyze(decl)) {
            callback_(std::move(*info));
        }
        return;
    }

    // Function declaration (non-member functions only)
    if (const auto* fd = result.Nodes.getNodeAs<clang::FunctionDecl>("fd")) {
        if (llvm::isa<clang::CXXMethodDecl>(fd))
            return;
        decl = fd;
        if (auto info = MethodAnalyzer::analyze(decl)) {
            callback_(std::move(*info));
        }
        return;
    }

    // Type alias template declaration
    if (const auto* tatd = result.Nodes.getNodeAs<clang::TypeAliasTemplateDecl>("tatd")) {
        decl = tatd;
        if (auto info = MethodAnalyzer::analyze(decl)) {
            callback_(std::move(*info));
        }
        return;
    }

    // Method of a class (member function)
    if (const auto* md = result.Nodes.getNodeAs<clang::CXXMethodDecl>("cxxmd")) {
        decl = md;
        if (auto info = MethodAnalyzer::analyze(decl)) {
            callback_(std::move(*info));
        }
        return;
    }

    // Type alias declaration (non-template using)
    if (const auto* tad = result.Nodes.getNodeAs<clang::TypeAliasDecl>("tad")) {
        decl = tad;
        if (auto info = MethodAnalyzer::analyze(decl)) {
            callback_(std::move(*info));
        }
        return;
    }
}

void AstMatchHandler::collect_methods_from_class(const clang::CXXRecordDecl* record) {
    if (!record)
        return;

    for (const auto* method : record->methods()) {
        if (auto info = MethodAnalyzer::analyze(method)) {
            callback_(std::move(*info));
        }
    }
}

// ─────────────────────────────────────────────────────────────
// MatchFrontendAction
// ─────────────────────────────────────────────────────────────
MatchFrontendAction::MatchFrontendAction(clang::ast_matchers::MatchFinder& finder, ErrorTracker& tracker)
: m_finder(finder)
, m_tracker(tracker) {
}

std::unique_ptr<clang::ASTConsumer> MatchFrontendAction::CreateASTConsumer(clang::CompilerInstance& ci, clang::StringRef) {
    m_tracker.has_errors = false;
    ci.getDiagnostics().setClient(&m_tracker, false);
    return m_finder.newASTConsumer();
}

// ─────────────────────────────────────────────────────────────
// MatchFrontendActionFactory
// ─────────────────────────────────────────────────────────────
MatchFrontendActionFactory::MatchFrontendActionFactory(clang::ast_matchers::MatchFinder& finder, ErrorTracker& tracker)
: m_finder(finder)
, m_tracker(tracker) {
}

std::unique_ptr<clang::FrontendAction> MatchFrontendActionFactory::create() {
    return std::make_unique<MatchFrontendAction>(m_finder, m_tracker);
}

// ─────────────────────────────────────────────────────────────
// run_clang_analysis
// ─────────────────────────────────────────────────────────────
bool run_clang_analysis(const std::string& source_file, const std::vector<std::string>& args, std::vector<MethodInfo>& results) {
    // Добавляем resource-dir для Clang, чтобы он мог найти системные заголовки
    std::vector<std::string> enriched_args = args;
    enriched_args.push_back("-resource-dir=/usr/lib/llvm-22/lib/clang/22");
    clang::tooling::FixedCompilationDatabase comp_db(".", enriched_args);
    clang::tooling::ClangTool tool(comp_db, {source_file});

    clang::ast_matchers::MatchFinder finder;
    results.clear();

    // Collect results into vector
    std::vector<MethodInfo> collected;

    AstMatchHandler handler([&collected](MethodInfo&& info) {
        // Фильтрация по паттерну
        std::string matched = ApiComparator::match_pattern(info.qualified_name);
        if (!matched.empty()) {
            collected.push_back(std::move(info));
        }
    });

    finder.addMatcher(clang::ast_matchers::classTemplateDecl(clang::ast_matchers::isExpansionInSystemHeader()).bind("ctd"), &handler);
    finder.addMatcher(clang::ast_matchers::functionTemplateDecl(clang::ast_matchers::isExpansionInSystemHeader()).bind("ftd"), &handler);
    finder.addMatcher(clang::ast_matchers::functionDecl(clang::ast_matchers::allOf(clang::ast_matchers::isExpansionInSystemHeader(),
                                                                                   clang::ast_matchers::unless(clang::ast_matchers::cxxDeductionGuideDecl())))
                          .bind("fd"),
                      &handler);
    finder.addMatcher(clang::ast_matchers::cxxMethodDecl(clang::ast_matchers::allOf(clang::ast_matchers::isExpansionInSystemHeader(),
                                                                                    clang::ast_matchers::unless(clang::ast_matchers::cxxDeductionGuideDecl())))
                          .bind("cxxmd"),
                      &handler);
    finder.addMatcher(clang::ast_matchers::typeAliasTemplateDecl(clang::ast_matchers::isExpansionInSystemHeader()).bind("tatd"), &handler);
    finder.addMatcher(clang::ast_matchers::typeAliasDecl(clang::ast_matchers::isExpansionInSystemHeader()).bind("tad"), &handler);

    ErrorTracker tracker;
    MatchFrontendActionFactory factory(finder, tracker);
    auto result = tool.run(&factory);

    if (result != 0 || tracker.has_errors)
        return false;

    results = std::move(collected);
    return true;
}

} // namespace trust