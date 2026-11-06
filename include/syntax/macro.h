#ifndef TRUST_SYNTAX_MACRO_H_
#define TRUST_SYNTAX_MACRO_H_

#include "syntax/parser_services.hpp"

#include "syntax/term/term.h"
#include "utils/strings.hpp"

#include <functional>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "syntax/util/warning_push.h"
#include "parser.yy.h"
#include "syntax/util/warning_pop.h"

namespace trust {

/**
 * Тип для хранения аргументов макроса - отображение имени аргумента на буфер.
 * MacroBuffer позволяет хранить как Lexem, так и TermPtr.
 */
using MacroArgsType = std::map<std::string, SequenceType>;

// Классификация макроопределения для форматтера (вычисляется при регистрации/удалении макроса).
enum class MacroKind : uint8_t {
    None = 0,
    NoParen = 1,  // определён БЕЗ скобок в сигнатуре (return/break/...)
    Contract = 2, // тело — контракт (@{...@} / вызов trust_contract)
};
inline bool hasKind(MacroKind v, MacroKind k) {
    return (static_cast<int>(v) & static_cast<int>(k)) != 0;
}

// Запись макроса в скоупе: группа макроопределений (несколько арностей одного имени) + класс
// для форматтера. Класс хранится вместе с определением (а не в параллельной карте), поэтому
// живёт, пока существует запись, и пропадает при полном удалении группы (имени).
struct MacroEntry {
    SequenceType defs;                // группа макроопределений с одним первым именем
    MacroKind kind = MacroKind::None; // классификация группы (no-paren/contract)
};

// Информация о ПЕРВОМ рассинхроне при сопоставлении вызова макроса с сигнатурой (MatchMacro).
// Заполняется в MatchMacro на каждом `return 0`; используется ExpandTermMacro для детальной
// диагностики «имя макроса есть, но ни один шаблон не подошёл».
struct MacroMismatch {
    size_t matched_terms = 0; // сколько термов сигнатуры совпало до провала (метрика «ближайшего»)
    size_t buffer_pos = 0;    // позиция токена-рассинхрона во входном буфере (для fix-it)
    std::string expected;     // ожидаемый терм сигнатуры (или "@@" = конец сигнатуры/EOF)
    std::string found;        // найденный терм входного буфера
};

/**
 * MacroScope - внутренний класс: хранит ровно один набор макросов одного модуля.
 * Не содержит логики поиска/раскрытия - только низкоуровневые операции над map.
 *
 * Все макросы стека живут строго в одной области видимости: имя макроса уникально
 * на весь стек, затенение не допускается. Скоупы служат только для группировки
 * макросов по модулям, чтобы при выходе из модуля (PopScope) можно было удалить
 * все макросы, созданные в нём.
 */
class MacroScope : public std::map<std::string, MacroEntry, std::less<>> {
  public:
    using std::map<std::string, MacroEntry, std::less<>>::map;
};

class Macro : public std::enable_shared_from_this<Macro> {
  public:
    /**
     * Идентификатор макроса - последовательность термов; первый терм - ключ ГРУППЫ.
     * В группе допустимы разные арности (`break`, `break $label`); при раскрытии выбирается
     * самый длинный (longest-match по числу потреблённых термов буфера). Сигнатура с скобками
     * и без скобок - РАЗНЫЕ сигнатуры; дубликат диагностируется только при полном совпадении
     * всех термов сигнатуры.
     * Параметры (`$name` в идентификаторе, `\$name` в теле) - произвольные последовательности
     * термов через запятую; учитывается только верхний уровень вложенности скобок. Скобочная
     * группа аргументов допустима не более чем у одного терма идентификатора. Контроль типов
     * аргументов не выполняется (в языке нет перегрузки по типам).
     */

    /// Порт по значению: Macro живёт дольше одного разбора (хранится в Context), поэтому
    /// порт-снимок (ссылки на владельцев + sink'и) не должен зависеть от времени жизни точки сборки.
    explicit Macro(trust::ParserServices ctx);

    template <class Ctx>
        requires(!std::is_same_v<std::remove_cvref_t<Ctx>, trust::ParserServices>)
    explicit Macro(Ctx& ctx)
    : Macro(trust::makeParserServices(ctx)) {}

    virtual ~Macro() {}

    trust::ParserServices m_ctx;

    // Нижний элемент = базовый скоуп (dsl), верхний = текущий модуль.
    /// Добавить пустой скоуп перед загрузкой нового модуля.
    void PushScope();
    /// Удалить верхний скоуп после завершения обработки модуля.
    void PopScope();
    /// Количество скоупов в стеке.
    size_t ScopeCount() const { return m_scopes.size(); }
    /// Пуст ли весь стек макросов (нет ни одного макроса ни в одном скоупе).
    bool isEmpty() const;
    /// Количество макросов в конкретном скоупе стека (0 - базовый, ScopeCount()-1 - верхний).
    size_t CountInScope(size_t scopeIdx) const;

    /// Собрать имена всех макросов по всему стеку скоупов (ключи групп - первый терм сигнатуры).
    std::vector<std::string> MacroNames() const;

    /// Поиск списка макросов по ключу по всему стеку (сверху вниз).
    /// Возвращает указатель на список макросов в первом найденном скоупе или nullptr.
    SequenceType* FindMacroList(std::string_view key);
    const SequenceType* FindMacroList(std::string_view key) const;

    /// Поиск скоупа, содержащего указанный ключ, по всему стеку (сверху вниз).
    MacroScope* FindScope(std::string_view key);
    const MacroScope* FindScope(std::string_view key) const;

    /// Доступ к стеку скоупов (для перечисления классификации в helper-ах макро).
    const std::vector<MacroScope>& scopes() const { return m_scopes; }

    std::string toMacroHash(TermPtr& term);

    /// Возвращает TermID framing-лексемы (MACRO_LEXEME/MACRO_STR_LEXEME/MACRO_DEL_LEXEME),
    /// если имя (с начальным `@` или без него) привязано к маркерному макросу, чьё тело
    /// равно ровно одной такой лексеме; иначе TermID::END. Используется в
    /// Parser::GetNextToken для распознавания маркеров по имени без раскрытия макроса.
    TermID MarkerToken(std::string_view key) const;

    std::string toMacroHashName(std::string_view str) {
        if (utils::isLocalName(str)) {
            return "$";
        } else if (utils::isMacroName(str)) {
            return std::string(str.substr(1));
        }
        return std::string(str);
    }

    std::string GetMacroMaping(const std::string str, const char* separator = ", ");

    SequenceType GetMacroId(TermPtr& term);

    SequenceType MakeMacroId(const SequenceType& seq);

    TermPtr EvalOpMacros(TermPtr& term);
    bool CheckMacro(const TermPtr& term);
    bool RemoveMacro(TermPtr& term);

    TermPtr GetMacroById(const SequenceType block);
    TermPtr GetMacro(std::vector<std::string> list);

    bool IdentityMacro(const SequenceType& buffer, TermPtr& term);
    /// Возвращает число потреблённых терминов входного буфера, если сигнатура макроса
    /// сопоставлена (prefix/longest-match), иначе 0. identity-проверка - это MatchMacro != 0.
    /// При несовпадении (возврат 0) в `mismatch` (если задан) фиксируется первый рассинхрон.
    size_t MatchMacro(const SequenceType& buffer, TermPtr& macro, MacroMismatch* mismatch = nullptr);
    bool CompareMacroName(std::string_view term_name, std::string_view macro_name);

    size_t ExtractArgs(SequenceType& buffer, TermPtr& term, MacroArgsType& args);
    void InsertArg_(MacroArgsType& args, std::string name, SequenceType& buffer, size_t pos = static_cast<size_t>(-1));
    SequenceType SymbolSeparateArg_(const SequenceType& buffer, size_t pos, std::vector<std::string> name, std::string& error);

    SequenceType ExpandMacros(const TermPtr& macro, MacroArgsType& args, Parser& parser, MapperRange callRange);
    std::string ExpandString(const TermPtr& macro, MacroArgsType& args);

    std::string Dump();
    std::string Dump(const MacroArgsType& var);
    std::string Dump(const SequenceType& arr);
    std::string DumpText(const SequenceType& arr);

    bool TestName(std::string_view name);
    std::string CreateFullName(std::string_view name);

    /// Класс макроса по имени (с ведущим '@' или без). Изменяется при регистрации/удалении.
    MacroKind macroKind(std::string_view name) const;
    /// Ключ группы `key` зарегистрирован в БАЗОВОМ скоупе (DSL/`--dsl`): такой макрос не зависит от
    /// контекста импорта, поэтому блок интерфейса файла может его использовать. Проверка по ключу
    /// точна, пока базовый скоуп неизменяем из файлов (`isDslScopeGroup`).
    bool isBaseScopeGroup(std::string_view key) const;

    /// Изменение/удаление адресовано группе БАЗОВОГО скоупа из файлового скоупа: базовый скоуп (DSL)
    /// неизменяем после загрузки — поверхность языка задаётся `--dsl`/конфигом, а не файлом.
    /// false — изменений нет либо действие выполняется внутри самого базового скоупа (загрузка DSL).
    [[nodiscard]] bool isDslScopeGroup(const TermPtr& definition);

    /// Макрос определён без скобок в сигнатуре (return/break/...) — пробел после имени.
    bool isNoParenMacro(std::string_view name) const;
    /// Макрос является контрактом (тело = @{...@} или вызов trust_contract).
    bool isContractMacro(std::string_view name) const;
    /// Снимок текущей классификации (имя без '@' → класс), собирается из живых скоупов.
    /// Используется потребителем (напр. форматтером) как перечисление базовых макросов
    /// перед подпиской на on_macro_kind.
    std::unordered_map<std::string, MacroKind> macroKinds() const;

    /// Коллбек: макрос добавлен/переопределён (removed=false) или удалён (removed=true).
    /// Вызывается в реальном времени при регистрации/удалении — неважно, где макрос загружен
    /// (DSL/исходник/модуль). position — 1-based offset события в источнике. Потребитель
    /// (напр. форматтер) строит по событиям timeline классификации, корректную на позицию токена.
    std::function<void(std::string_view name, MacroKind kind, bool removed, size_t position)> on_macro_kind;

  private:
    /// Рекурсивно проверяет, является ли терм (или его тело) контрактом:
    /// содержит @{ (TRUST_BEGIN) или ссылку на уже зарегистрированный Contract-макрос.
    bool bodyHasContract(const TermPtr& t) const;
    /// Вычисляет класс макроса по его определению (m_sequence/m_right).
    MacroKind classifyMacro(const TermPtr& macro) const;

    std::vector<MacroScope> m_scopes;
};

} // namespace trust

#endif // TRUST_SYNTAX_MACRO_H_