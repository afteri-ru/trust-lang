#pragma once

// Анализатор объявлений ФУНКЦИЙ: сигнатура (проверки - в semantic/decl/func_signature_check.hpp),
// параметры, defaults, лямбда-захваты и шаблонные native-импорты (функции-алиасы). Регистрация
// нативного шаблона-ТИПА (`<T> %Cpp() := ...;`) - в зоне типов (DeclAnalyzer::analyzeNativeTemplateDecl).

#include "semantic/core/pass.hpp"

namespace trust {

class NameResolutionPass;

class FuncDeclAnalyzer {
  public:
    FuncDeclAnalyzer(AnalysisContext& actx, NameResolutionPass& core)
    : m_actx(actx)
    , m_core(core) {
    }

    void analyzeFuncDecl(FuncDecl& func_node);
    /// АЛИАС имени (`f :- g;` / `f :- ns::g;`): привязка имени к набору перегрузок цели. Не объявление
    /// функции: сигнатуры/тела нет, в реестр функций не попадает, C++-функция не эмитится.
    void analyzeNameAlias(FuncDecl& func_node);
    /// `a :- ns::;` - привязка ПРЕФИКСА к области имён (LHS — только NAME, цель с `::`): `a::X` -> `ns::X`.
    /// Регистрируется в текущем скоупе; из exported namespace-блока (интерфейса) экспортируется с
    /// квалифицированным префиксом.
    void analyzeNamespaceAlias(FuncDecl& func_node);
    void validateFuncDefaults(const FuncDecl& func_node);
    void declareFuncParams(FuncDecl& func_node);
    void analyzeLambdaCaptures(FuncDecl& func_node);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;

    /// Z16: символ функции в текущем скоупе (набор перегрузок, C++-суффикс идентичности,
    /// declareOrComplete) + уведомление хуков `onDeclare`.
    void registerFuncSymbol(FuncDecl& func_node, TypeId func_type);
};

} // namespace trust
