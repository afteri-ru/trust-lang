#pragma once

// include/semantic/nativeref.hpp
// NativeRefHook: анализ нативных (сырых) C++-ссылок + отслеживание инвалидации ссылок
// (условный атрибут @[reftrace@], инвалидация зависимых).
//
// Модель (см. .tasklog/1788277644296.md):
//   - @[reftrace@] ставится на КЛАСС/ТИП или на МЕТОД (НЕ на переменную):
//       - класс/тип (в т.ч. forward нативного C++: span, base_iterator): тип всегда внутри
//         держит ссылку/указатель на чужие данные -> ЛЮБАЯ переменная такого типа
//         автоматически зависимая (без указания в коде);
//       - метод: возвращает ссылку во внутренние данные -> результат зависим от объекта.
//   - АВТОМАТИЧЕСКИ (без атрибута), независимо от класса/метода:
//       - переменная чисто ссылочного типа: kRef/kRref/kPtr/kPtrPtr (умные shared/weak/unique/
//         locker НЕ отслеживаются автоматически);
//       - индексный доступ obj[i] (возвращает ссылку на данные) -> зависимая;
//       - любой метод, возвращающий чисто ссылочный тип -> результат зависим.
//   - Источник (main variable) = объект, из данных которого получена зависимая. Инвалидация:
//     ЛЮБАЯ мутация источника (присваивание нового значения ИЛИ вызов не-const метода)
//     инвалидирует зависимые, рождённые ДО мутации.
//   - Поведение управляется severity-опцией -Wreftrace=ignore|warning|error (DiagId::RefTrace).
//
// Реализация - InlineAnalysisHook (подключается параллельно к ядру NameResolutionPass):
//   - onNode: VarDecl (порождение зависимой), ClassDecl/FuncDecl (помеченные @[reftrace@]),
//     AssignOp (мутация источника), MemberAccess+CallExpr (мутация источника не-const методом);
//   - onResolve(Ident): использование зависимой переменной после мутации источника -> диагностика.
//
// Ограничение v1: решение о «зависимой» принимается структурно по инициализатору/аннотации типа
// (типы выражений к моменту onNode ещё не выведены). Транзитивность (зависимая из зависимой)
// сводится к корневому источнику. Умные указатели исключены из авто-трекинга.

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "semantic/diag.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "location/location.hpp"
#include "types/typekind.hpp"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

class NativeRefHook : public InlineAnalysisHook {
  public:
    explicit NativeRefHook(AnalysisContext& actx);

    void enterScope() override;
    void exitScope() override;
    bool onNode(AstNodePtr& node) override;
    void onDeclare(const Symbol& sym) override;
    void onResolve(const AstNodeBase& node, const Symbol* sym) override;

  private:
    /// Один фрейм лексического скоупа (вход/выход синхронны с SymbolTable ядра).
    struct Frame {
        // dependent_var -> (root_source_var, born_epoch источника на момент рождения)
        std::map<std::string, std::pair<std::string, int64_t>> dependent;
        // bare-имя нативной переменной -> (глубина скоупа декларации, вид ссылки).
        // Нативные переменные видны в текущем и внутренних скоупах (поиск сверху вниз).
        std::map<std::string, std::pair<size_t, RefType>> nativeVars;
    };

    // root_source_var -> текущая эпоха мутаций (глобально по имени переменной).
    std::map<std::string, int64_t> m_epoch;
    // root_source_var -> место последней мутации (для note).
    std::map<std::string, MapperRange> m_changeSite;
    std::vector<Frame> m_frames;

    // trust-имена классов и методов, помеченных @[reftrace@].
    std::set<std::string> m_markedClasses;
    std::set<std::string> m_markedMethods;

    AnalysisContext& m_actx;

    bool reftraceEnabled() const;
    void recordMutation(const std::string& name, const MapperRange& range);
    void recordBirth(const VarDecl& var);
    /// Сводит имя (возможно зависимой переменной) к корневому источнику по цепочке зависимостей.
    std::string resolveRootSource(const std::string& name) const;
    /// Находит запись зависимой по имени (фреймы сверху вниз). nullptr - не зависимая.
    const Frame* findFrameWithDependent(const std::string& name, std::pair<std::string, int64_t>* out = nullptr) const;
    bool hasDependents(const std::string& name) const;
    /// Имя корневого источника из выражения (левый объект index/method, цель &/*, арг. конструктора).
    std::string sourceOf(const AstNodeBase* node) const;
    /// Является ли объявление переменной отслеживаемой зависимой (по инициализатору/типу/атрибуту).
    bool varIsTracked(const VarDecl& var, const std::string& source) const;
    bool varIsMarkedClassType(const VarDecl& var) const;
    static std::string normalizeMethodName(std::string_view m);

    // -- Диагностики нативных (сырых) ссылок (D3..D7) --
    /// Вид нативной ссылки из аннотации типа (структурно: текст маркера `%&`/`%*`). kValue - не нативная.
    static RefType nativeKindOfTypeNode(const AstNodeBase* typeNode);
    /// Регистрирует нативную переменную в текущем фрейме + D4 (static/global native -> error).
    void registerNativeVar(const Symbol& sym, RefType kind);
    /// D3+D7: нативные маркеры в сигнатуре функции (возврат/параметры) -> error.
    void checkFuncSignature(const FuncDecl& fn);
    /// D5: сохранение нативной ссылки в переменную внешнего скоупа -> error.
    void checkAssignIntoOuterNative(const Binary& b);
    /// D6: swap нативных ссылок на разных уровнях скоупов -> error.
    void checkSwapNativeAcrossScopes(const Binary& b);
    /// Находит нативную переменную по bare-имени (фреймы сверху вниз). nullptr - не нативная.
    const std::pair<size_t, RefType>* findNativeVar(const std::string& bare) const;
    /// Производит ли выражение нативную ссылку (`%& expr` ИЛИ копию нативной переменной).
    bool exprProducesNativeRef(const AstNodeBase* e) const;
};

} // namespace trust
