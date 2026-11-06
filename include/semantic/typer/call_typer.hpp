#pragma once

// Типизатор вызовов функций: формат-строки, выбор сигнатуры (перегрузки/шаблоны),
// привязка аргументов и многоточие. Выделен из ExprTyper как отдельная зона ответственности.

#include "semantic/typer/typer_base.hpp"

namespace trust {

class CallTyper : public TyperBase {
  public:
    CallTyper(AnalysisContext& actx, NameResolutionPass& core)
    : TyperBase(actx, core) {}

    void typeCallNode(CallExpr& call);
    /// Печатаемое C++-имя ВЫБРАННОЙ перегрузки: если объявление живёт в namespace-области,
    /// текст ссылки нормализуется на `path::name` (+ `moduleRef` для чужого модуля). Нужно для
    /// слияния `using` из разных областей и для алиасов: spelling ссылки не содержит пути.
    void materializeQualifiedCallee(CallExpr& call, const Symbol& symbol, std::size_t chosenIndex);

  private:
    /// Единый отказ вызова: тип результата INVALID (диагностика уже выдана вызывающим).
    void failCall(CallExpr& call) const;
    /// Вызов TrustLang-функции по имени: набор перегрузок, named/default binding, многоточие,
    /// generic-унификация и метаданные узла. true - вызов обработан (успех или диагностика),
    /// вызывающему нужно вернуться; false - это не вызов TrustLang-функции.
    bool typeTrustFunctionCall(CallExpr& call, const Symbol& symbol, const FuncDecl* decl);
    /// Вызов значения record/native-класса с объявленным `()` (арность/многоточие как у метода).
    /// true - обработка прервана диагностикой; false - оператора нет либо типизация успешна
    /// (общие проверки аргументов выполняются ниже).
    bool typeCallOperator(CallExpr& call);
    /// Вызов статического члена нативного класса: именованные аргументы запрещены,
    /// перегрузка/арность/типы проверяются анализатором, C++-имя не манглируется.
    void typeStaticMemberCall(CallExpr& call, const Symbol& s);
    /// Вызов нативной функции: именованные/default-аргументы запрещены, перегрузки
    /// разрешаются для типа результата (имя символа остаётся нативным).
    /// true — вызов обработан (или диагностирован); false — нет перегрузки, вызывающий
    /// продолжает общий путь типизации.
    bool typeNativeCall(CallExpr& call, const Symbol& s, const FuncDecl* fdecl);
    void checkFormatArgs(CallExpr& call);
    void checkFormatStringArgs(CallExpr& call);
    void analyzeCallFilling(CallExpr& call);
};

} // namespace trust
