#pragma once

// Компонент семантики: DeclAnalyzer — фасад анализа объявлений. Объявления переменных,
// функций и деструктуризации делегируются профильным анализаторам; типы (typedef/enum/
// variant/record/class) остаются зоной ответственности фасада и его type-файлов.

#include "semantic/core/pass.hpp"
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class NameResolutionPass;
class VarDeclAnalyzer;
class FuncDeclAnalyzer;
class DestructureAnalyzer;

class DeclAnalyzer {
  public:
    explicit DeclAnalyzer(AnalysisContext& actx, NameResolutionPass& core);
    ~DeclAnalyzer();

    void analyzeVarDecl(VarDecl& var_node);
    void analyzeTypeDecl(Binary& binary_node);
    /// Определение ранее объявленного типа `Name = %cpp { ... };` (TermID::TYPE_DEFINE).
    void analyzeTypeDefine(Binary& binary_node);
    void analyzeEnumDecl(Binary& binary_node);
    void analyzeVariantDecl(Binary& binary_node);
    void analyzeRecordDecl(Binary& binary_node);
    void analyzeFuncDecl(FuncDecl& func_node);
    void validateFuncDefaults(const FuncDecl& func_node);
    // Функции объявляются обычным путём; нативный шаблон-ТИП — оператор типа (`<T> Name ::= %cpp { };`).
    /// `ownerModule` — модуль-владелец объявления: для ГЕНЕРИРУЕМОГО класса (`::=`-обёртка) он
    /// публикует интерфейс, поэтому on-use include типа — заголовок этого модуля.
    void analyzeClassDecl(ClassDecl& ncd, ModuleIndex ownerModule);
    /// Проверка БАЗИСОВ генерируемого класса (`Name = %cpp, %std::string {...}` / `... , :Base {...}`):
    /// нативное имя — готовый C++-базис, обычное — тип-класс (резолвится). ЕДИНАЯ точка для
    /// `=`-реализаций builtin и `::=`-объявлений; ошибки НЕ прерывают анализ (материализация членов
    /// нужна HIR verify).
    void validateClassBases(ClassDecl& ncd, std::string_view typeName);
    void declareFuncParams(FuncDecl& func_node);
    void analyzeLambdaCaptures(FuncDecl& func_node);
    void analyzeDestructure(DestructureDecl& node);

  private:
    AnalysisContext& m_actx;
    NameResolutionPass& m_core;

    /// RHS-имя типа (`T` / `:T`) → TypeId цели алиаса. ОБЩАЯ точка `::=` (новый номинальный
    /// тип) и `=` (второе имя существующего типа). INVALID_TYPE_ID - RHS не имя типа либо цель
    /// не разрешилась (диагностика выдана). `opSpelling` попадает в текст диагностики.
    TypeId resolveAliasTargetType(const AstNodeBase& right, std::string_view opSpelling);

    std::unique_ptr<VarDeclAnalyzer> m_var;
    std::unique_ptr<FuncDeclAnalyzer> m_func;
    std::unique_ptr<DestructureAnalyzer> m_destructure;
};

} // namespace trust
