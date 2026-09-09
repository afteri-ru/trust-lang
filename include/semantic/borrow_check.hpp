#pragma once

// include/semantic/borrow_check.hpp
// BorrowCheckHook: статический анализатор заимствования (borrow checker) для УМНЫХ ссылок
// (`unique`/`shared`/`weak`). ВСЕГДА подключён к ядру (гарантия языка / модель памяти):
// флага включения/выключения нет, настраиваются только уровни диагностик (`-Wborrow-<name>=<sev>`).
//
// Модель (согласована с include/semantic/BORROW_CHECKER.md и types/REFType.md §13-§14):
//   * Регион (граница валидности) выводится из места хранения (Storage):
//       Local -> scope, ThreadLocal -> thread, Static/Global -> program.
//     Для heap-объекта за handle регион = регион САМОГО handle. `@[lifetime]` на владеющих
//     видах НЕ применяется (контракт не расширяется).
//   * Заём (borrow) создаётся оператором `*ref` (guard) и невладеющим view (`& x` - weak).
//     Обе формы адресуют anchor; область жизни view обязана не превышать anchor.
//   * Per-frame эпоха мутаций (общий механизм FrameEpoch) - единый для native и smart.
//
// Правила (v1, внутри одной функции):
//   R1 (region)         - view/заём не должен переживать anchor (Storage->region).
//   R4 (move)           - move/reset/swap владельца при живом займе -> BorrowOwnerMoved.
//   R6 (mutation)       - мутация ЭКСКЛЮЗИВНОГО владельца (unique) при живом займе -> BorrowOwnerMutated
//                         (отчёт на месте операции). Для shared мутация допустима (разделяемые данные).
//                         Заём у монопольного `unique` запрещён на уровне семантики (`& u` - ошибка),
//                         поэтому R6 практически недостижим; правило сохранено для формы weak-view.
//
// Ограничение v1: структурная детекция (по инициализатору/аннотации; типы выражений к
// моменту onNode ядром ещё не выведены). Межпроцедурный анализ не выполняется.

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "semantic/diag.hpp"
#include "semantic/frame_epoch.hpp"
#include "ast/ast_nodes.hpp"
#include "types/typekind.hpp"
#include "location/location.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace trust {

/// Регион времени жизни значения/владельца. Упорядочен: Scope < Thread < Program.
enum class BorrowRegion : uint8_t {
    Scope = 0,   ///< локальная/временная (стек), текущий блок/вызов
    Thread = 1,  ///< TLS-сегмент (`@[thread_local]`)
    Program = 2, ///< программная длительность (глобал/статик/модуль)
};

/// Человекочитаемое имя региона (для диагностик).
[[nodiscard]] constexpr const char* borrowRegionName(BorrowRegion r) noexcept {
    switch (r) {
    case BorrowRegion::Scope:
        return "scope";
    case BorrowRegion::Thread:
        return "thread";
    case BorrowRegion::Program:
        return "program";
    }
    return "scope";
}

class BorrowCheckHook : public InlineAnalysisHook {
  public:
    explicit BorrowCheckHook(AnalysisContext& actx);

    void enterScope() override;
    void exitScope() override;
    bool onNode(AstNodePtr& node) override;

    /// Инференс региона по месту хранения (Storage) - единая точка (types/REFType.md §14.3).
    [[nodiscard]] static BorrowRegion regionOfStorage(Storage storage) noexcept;

  private:
    /// Сведения об умной ссылке, объявленной в скоупе.
    struct SmartInfo {
        RefType kind = RefType::kValue; ///< вид ссылки (unique/shared/weak/locker/native)
        bool owner = false;             ///< владелец (unique/shared) vs невладеющий view
        BorrowRegion region = BorrowRegion::Scope;
    };

    /// Один лексический фрейм (синхронен со скоуп-стеком ядра).
    struct Frame {
        std::map<std::string, SmartInfo> smart;                         ///< bare-имя -> сведения
        std::map<std::string, std::pair<std::string, int64_t>> borrows; ///< dep -> (anchor, born_epoch)
    };

    AnalysisContext& m_actx;
    FrameEpoch m_epoch; ///< per-frame эпоха мутаций anchor'ов (общий механизм)
    std::vector<Frame> m_frames;

    /// Регион переменной по её объявлению (Storage + @[thread_local] + область имён).
    [[nodiscard]] BorrowRegion regionOfVar(const VarDecl& var) const;
    /// Обрабатывает объявление переменной: вывод региона/вида и создание займа.
    void analyzeVar(const VarDecl& var);
    /// Обрабатывает присваивание/swap: мутация anchor и проверка move под займом.
    void analyzeAssign(const Binary& b);
    /// Есть ли живой заём на anchor (поиск по фреймам).
    [[nodiscard]] bool hasBorrows(const std::string& anchor) const;
    /// Фрейм, где объявлена заёмная переменная; при находке пишет (anchor, born).
    [[nodiscard]] const Frame* frameWithBorrow(const std::string& dep, std::pair<std::string, int64_t>* out) const;
    /// Сведения об умной ссылке по bare-имени (поиск по фреймам).
    [[nodiscard]] const SmartInfo* findSmart(const std::string& bare) const;
    /// Сводит имя к bare-форме (без ведущего сигила локальности '$').
    [[nodiscard]] static std::string bareName(std::string_view name);
    /// Имя корневого источника (левый объект index/member, цель `&`/`*`).
    [[nodiscard]] static std::string sourceOf(const AstNodeBase* node);
};

} // namespace trust
