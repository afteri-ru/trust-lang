// src/stdlib/analyzer.cpp
// Реализация анализатора методов и функций

#include "stdlib/analyzer.hpp"
#include "stdlib/name_utils.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Type.h"
#include "llvm/Support/raw_ostream.h"

namespace trust {

// ─────────────────────────────────────────────────────────────
// Проверка внутреннего имени — делегирует в name_utils
// ─────────────────────────────────────────────────────────────
bool MethodAnalyzer::is_internal_name(const std::string &qualified_name) {
    return trust::is_internal_name(qualified_name);
}

// ─────────────────────────────────────────────────────────────
// Классификация объявления
// ─────────────────────────────────────────────────────────────
DeclCategory MethodAnalyzer::classify(const clang::NamedDecl *decl) {
    if (!decl)
        return DeclCategory::Unknown;

    if (llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
        return DeclCategory::ClassTemplate;

    if (const auto *ftd = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl)) {
        // Пропускаем deduction guides — они не являются обычными функциями
        if (const auto *templated = ftd->getTemplatedDecl()) {
            if (llvm::isa<clang::CXXDeductionGuideDecl>(templated))
                return DeclCategory::Unknown;
        }
        return DeclCategory::FunctionTemplate;
    }

    if (const auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
        // Пропускаем конструкторы, деструкторы и deduction guides — они не являются обычными методами
        if (llvm::isa<clang::CXXConstructorDecl>(md))
            return DeclCategory::Unknown;
        if (llvm::isa<clang::CXXDestructorDecl>(md))
            return DeclCategory::Unknown;
        if (llvm::isa<clang::CXXDeductionGuideDecl>(md))
            return DeclCategory::Unknown;
        return DeclCategory::Method;
    }

    if (llvm::isa<clang::FunctionDecl>(decl)) {
        if (!llvm::isa<clang::CXXMethodDecl>(decl))
            return DeclCategory::Function;
    }

    if (llvm::dyn_cast<clang::TypeAliasTemplateDecl>(decl))
        return DeclCategory::TypeAlias;

    if (llvm::dyn_cast<clang::TypeAliasDecl>(decl))
        return DeclCategory::TypeAlias;

    return DeclCategory::Unknown;
}

// ─────────────────────────────────────────────────────────────
// Получить тип возвращаемого значения
// ─────────────────────────────────────────────────────────────
std::string MethodAnalyzer::get_return_type(const clang::FunctionDecl *fd) {
    if (!fd)
        return {};

    clang::QualType retType = fd->getReturnType();
    if (retType.isNull())
        return {};
    std::string buf;
    llvm::raw_string_ostream os(buf);
    retType.print(os, clang::PrintingPolicy(fd->getLangOpts()));
    return os.str();
}

// ─────────────────────────────────────────────────────────────
// Получить типы параметров
// ─────────────────────────────────────────────────────────────
std::vector<std::string> MethodAnalyzer::get_param_types(const clang::FunctionDecl *fd) {
    if (!fd)
        return {};

    std::vector<std::string> types;
    auto printingPolicy = clang::PrintingPolicy(fd->getLangOpts());

    for (const auto *param : fd->parameters()) {
        if (param) {
            clang::QualType paramType = param->getType();
            std::string buf;
            llvm::raw_string_ostream os(buf);
            paramType.print(os, printingPolicy);
            types.push_back(os.str());
        }
    }

    return types;
}

// ─────────────────────────────────────────────────────────────
// Построить нормализованную сигнатуру для сравнения
// ─────────────────────────────────────────────────────────────
std::string MethodAnalyzer::build_normalized_signature(const MethodInfo &info) {
    std::string sig = info.return_type + "(";
    for (size_t i = 0; i < info.param_types.size(); ++i) {
        if (i > 0)
            sig += ", ";
        sig += info.param_types[i];
    }
    sig += ")";

    if (info.is_const)
        sig += " const";

    return sig;
}

// ─────────────────────────────────────────────────────────────
// Основной метод анализа
// ─────────────────────────────────────────────────────────────
std::optional<MethodInfo> MethodAnalyzer::analyze(const clang::NamedDecl *decl) {
    if (!decl)
        return std::nullopt;

    MethodInfo info;

    // Qualified name
    std::string buf;
    llvm::raw_string_ostream qname_os(buf);
    decl->printQualifiedName(qname_os);
    info.qualified_name = buf;

    // Фильтрация внутренних имён
    if (is_internal_name(info.qualified_name))
        return std::nullopt;

    // Классификация
    info.category = classify(decl);

    // Извлечение информации для функций и методов
    if (const auto *fd = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
        info.return_type = get_return_type(fd);
        info.param_types = get_param_types(fd);
        info.is_template = llvm::isa<clang::FunctionTemplateDecl>(decl) || fd->isTemplateDecl();
    }

    // Специфика для методов
    if (const auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
        info.is_const = md->isConst();
        info.is_static = md->isStatic();
        info.is_template = md->isTemplateDecl();
    }

    // Для шаблонов классов
    if (llvm::dyn_cast<clang::ClassTemplateDecl>(decl)) {
        info.is_template = true;
    }

    // Для TypeAlias извлекаем underlying type
    if (const auto *tad = llvm::dyn_cast<clang::TypeAliasDecl>(decl)) {
        clang::QualType underlyingType = tad->getUnderlyingType();
        if (!underlyingType.isNull()) {
            std::string buf;
            llvm::raw_string_ostream os(buf);
            underlyingType.print(os, clang::PrintingPolicy(decl->getLangOpts()));
            info.return_type = os.str();
        }
    }

    // Для TypeAliasTemplate (using template) извлекаем underlying type из шаблонного объявления
    if (const auto *tatd = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(decl)) {
        if (const auto *tad = tatd->getTemplatedDecl()) {
            clang::QualType underlyingType = tad->getUnderlyingType();
            if (!underlyingType.isNull()) {
                std::string buf;
                llvm::raw_string_ostream os(buf);
                underlyingType.print(os, clang::PrintingPolicy(decl->getLangOpts()));
                info.return_type = os.str();
            }
        }
        info.is_template = true;
    }

    info.normalized_signature = build_normalized_signature(info);

    return info;
}

} // namespace trust
