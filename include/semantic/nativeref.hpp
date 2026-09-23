#pragma once

// include/semantic/nativeref.hpp
// NativeRefHook: анализ нативных (сырых) C++-ссылок + отслеживание инвалидации зависимых
// (атрибут @[borrowed], инвалидация зависимых).
//
// Модель (ось НЕЗАВИСИМА от вида ссылки):
//   - @[borrowed] ставится на КЛАСС/ТИП, на МЕТОД или на ПЕРЕМЕННУЮ:
//       - класс/тип (в т.ч. forward нативного C++: span, base_iterator): тип всегда внутри
//         держит ссылку/указатель на чужие данные -> ЛЮБАЯ переменная такого типа
//         автоматически зависимая (без указания в коде);
//       - метод: возвращает ссылку во внутренние данные -> результат зависим от объекта;
//       - переменная: явно объявляет переменную зависимой.
//   - АВТОМАТИЧЕСКИ (без атрибута) ничего не отслеживается: значение-копии (в т.ч. `obj[i]` —
//     копия значения) и умные ссылки/наблюдатели (shared/weak/unique/locker) НЕ являются
//     зависимыми. Для умных ссылок мутация владельца под займом — зона borrow-checker
//     (`-Wborrow-*`), а не borrowed. Отслеживаются только сущности с контрактом @[borrowed].
//   - КОПИРОВАНИЕ АТРИБУТА: переменная, инициализированная/присвоенная из зависимой, тоже
//     становится зависимой от того же корневого источника; переприсваивание независимым
//     значением снимает зависимость (release).
//   - Источник (main variable) = объект, из данных которого получена зависимая. Инвалидация:
//     ЛЮБАЯ мутация источника (присваивание нового значения ИЛИ вызов не-const метода)
//     инвалидирует зависимые, рождённые ДО мутации.
//   - Поведение управляется severity-опцией -Wborrowed=ignore|warning|error (DiagId::Borrowed).
//
// Реализация - InlineAnalysisHook (подключается параллельно к ядру NameResolutionPass):
//   - onNode: VarDecl (порождение зависимой), ClassDecl/FuncDecl (помеченные @[borrowed]),
//     AssignOp (мутация источника + копирование/снятие зависимости), MemberAccess+CallExpr
//     (мутация источника не-const методом);
//   - onResolve(Ident): использование зависимой переменной после мутации источника -> диагностика.
//
// Ограничение v1: решение о «зависимой» принимается структурно по инициализатору/аннотации типа
// (типы выражений к моменту onNode ещё не выведены). Транзитивность (зависимая из зависимой)
// сводится к корневому источнику. Умные указатели исключены из авто-трекинга.

#include "semantic/inline_hook.hpp"
#include "semantic/frame_epoch.hpp"
#include "semantic/pass.hpp"
#include "semantic/diag.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
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

    // Per-frame эпоха мутаций источников (общий механизм с BorrowCheckHook). Push/pop
    // синхронны с m_frames в enterScope/exitScope.
    FrameEpoch m_epoch;
    std::vector<Frame> m_frames;

    // trust-имена классов и методов, помеченных @[borrowed].
    std::set<std::string> m_markedClasses;
    std::set<std::string> m_markedMethods;

    AnalysisContext& m_actx;

    bool borrowedEnabled() const;
    void recordMutation(const std::string& name, const MapperRange& range);
    void recordBirth(const VarDecl& var);
    /// Копирование/снятие атрибута @[borrowed] при присваивании (AssignOp).
    void propagateBorrowOnAssign(const Binary& b);
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
    /// Явный атрибут @[borrowed] на переменной или в её аннотации типа.
    bool varHasBorrowedAttr(const VarDecl& var) const;
    /// Выражение-инициализатор, порождающее зависимые данные (метод/конструктор @[borrowed]-контракта).
    bool exprIsBorrowed(const AstNodeBase* init) const;
    static std::string normalizeMethodName(std::string_view m);

    // -- Диагностики нативных (сырых) ссылок (D3..D7) --
    /// Вид нативной ссылки из аннотации типа (`@[reftype("ptr"|"ref")]`). kValue - не нативная.
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
    /// Производит ли выражение нативную ссылку (копию нативной переменной / результат нативной функции).
    bool exprProducesNativeRef(const AstNodeBase* e) const;
};

} // namespace trust
