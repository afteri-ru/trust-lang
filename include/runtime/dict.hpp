#pragma once

#ifndef TRUST_INCLUDED_DICT_
#define TRUST_INCLUDED_DICT_

#include <cmath>
#include <concepts>
#include <list>
#include <memory>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "diag/error.hpp"

namespace trust {

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// C++23: более читаемые мета-функции через fold-expression.
template <class T, class... Rest>
inline constexpr bool is_all_v = (std::same_as<T, Rest> && ...);

template <class T, class... Rest>
inline constexpr bool is_any_v = (std::same_as<T, Rest> || ...);

template <class T, class... Rest>
struct is_all : std::bool_constant<is_all_v<T, Rest...>> {};

template <class T, class... Rest>
struct is_any : std::bool_constant<is_any_v<T, Rest...>> {};

// C++23: ограничение через concepts.
template <std::floating_point T>
[[nodiscard]] inline bool almost_equal(T x, T y, int ulp) {
    // the machine epsilon has to be scaled to the magnitude of the values used
    // and multiplied by the desired precision in ULPs (units in the last place)
    return std::fabs(x - y) <= std::numeric_limits<T>::epsilon() * std::fabs(x + y) * ulp
           // unless the result is subnormal
           || std::fabs(x - y) < std::numeric_limits<T>::min();
}

#endif

template <typename T>
class Iter;
/*
 * Аргумент по умолчанию может быть литерал или выражение.
 * Если аргумент по умолчанию — это выражение, то оно вычисляется только один раз для всей программы при загрузке модуля.
 * Аргументы по умолчанию парсятся из токенов (создаются или вычисляются) при загрузке модуля, т.е. при создании
 * аругментов по умолчанию, а сами значения хранятся уже как объекты.
 *
 * Аргументы в функции всегда имеют номер, который начинается с единицы в порядке определения в прототипе + некоторые могут иметь наименование.
 * Даже обязательные аргументы (которые не имеют значения по умолчанию в прототипе финкции), могут быть переданы по имени,
 * а сами именованные аргументы могут быть указаны в произвольном порядке. Поэтому в реализации Args для обязательных
 * аргументов так же хранится имя аргумента, но его значение отсутствует.
 *
 * Так как позционные аргументы тоже могут передавать как именованные, то контроль и подстчет кол-ва
 * не именованных аргументов при вызове фунции ничего не определяет.
 * Следовательно, при вызове функции не именованные аргументы записываются по порядку, а именованные по имени.
 * Контроль передаваемых аргументов производится на наличие значения у каждого аргумента.
 * Если при определении функции после всех аргументов стоит многоточие, то разрешается добавлять
 * новые аргументы по мимо тех, которые уже определены в прототипе функции.
 */

namespace detail {
template <class P, class T>
concept smart_ptr_to = requires(P p) {
    // минимальное требование: разыменование даёт T& (или совместимо)
    { *p } -> std::convertible_to<T &>;
};

template <class N>
concept dict_name_arg = std::convertible_to<N, std::string_view> || std::is_pointer_v<std::remove_reference_t<N>>;

template <class I>
concept dict_index_arg = std::integral<I> && (!std::is_pointer_v<std::remove_reference_t<I>>);
} // namespace detail

template <typename T, typename PTR = std::shared_ptr<T>>
    requires detail::smart_ptr_to<PTR, T>
class Dict : public std::list<std::pair<std::string, PTR>> {
  public:
    typedef PTR Type;
    typedef std::pair<std::string, Type> PairType;
    typedef std::list<PairType> ListType;

    friend class Context;

    template <detail::dict_index_arg I>
    inline const PairType &operator[](I index) {
        return at(index);
    }

    template <detail::dict_name_arg N>
    inline const PairType &operator[](N &&name) {
        if constexpr (std::is_pointer_v<std::remove_reference_t<N>>) {
            return at(std::string_view{name});
        } else {
            return at(std::string_view{name});
        }
    }

    inline PairType &push_back(const PairType &p) {
        ListType::push_back(p);
        return *at_index(-1);
    }

    inline PairType &push_back(const Type value, const std::string &name = "") { return push_back(pair(value, name)); }

    //        inline PairType top() const {
    //            if (ListType::empty()) {
    //                LOG_RUNTIME("Empty Index '%ld' not exists!", index);
    //            }
    //            return *ListType::back();
    //        }

    static inline PairType pair(const Type value, std::string name = "") { return PairType(std::move(name), value); }

    virtual PairType &at(const int64_t index) { return *at_index(index); }

    virtual const PairType &at(const int64_t index) const { return *at_index_const(index); }

    [[nodiscard]] typename ListType::iterator find(std::string_view name) {
        auto iter = ListType::begin();
        while (iter != ListType::end()) {
            if (iter->first == name) {
                return iter;
            }
            iter++;
        }
        return ListType::end();
    }

    //        typename ListType::const_iterator find(const std::string_view name) const {
    //            return find(name);
    //        }

    virtual PairType &at(std::string_view name) {
        auto iter = find(name);
        if (iter != ListType::end()) {
            return *iter;
        }
        FAULT("Property '{}' not found!", std::string(name));
    }

    // Backward-compatible overload.
    virtual PairType &at(const std::string &name) { return at(std::string_view{name}); }

    virtual const std::string &name(const int64_t index) const { return at_index_const(index)->first; }

    virtual int64_t index(std::string_view field_name) {
        typename ListType::iterator found = find(field_name);
        if (found == ListType::end()) {
            return -1;
        }
        return std::distance(ListType::begin(), found);
    }

    virtual void clear_() { ListType::clear(); }

    virtual int64_t resize(int64_t new_size, const Type fill, const std::string &name = "") {
        if (new_size >= 0) {
            // Размер положительный, просто изменить число элементов добавив или удалив последние
            ListType::resize(new_size, std::pair<std::string, Type>(name, fill));
        } else {
            // Если размер отрицательный - добавить или удалить вначале
            new_size = -new_size;
            if (static_cast<int64_t>(ListType::size()) > new_size) {

                ListType::erase(ListType::begin(), at_index(ListType::size() - new_size));

            } else if (static_cast<int64_t>(ListType::size()) < new_size) {
                ListType::insert(ListType::begin(), (ListType::size() - new_size), std::pair<std::string, Type>(name, fill));
            } else {
                ListType::clear();
            }
        }
        return ListType::size();
    }

    typename ListType::iterator at_index(const int64_t index) {
        if (index < 0) {
            if (-index <= static_cast<int64_t>(ListType::size())) {
                int64_t pos = index + 1;
                typename ListType::iterator iter = ListType::end();
                while (iter != ListType::begin()) {
                    iter--;
                    if (pos == 0) {
                        return iter;
                    }
                    pos++;
                }
            }
        } else {
            int64_t pos = 0;
            typename ListType::iterator iter = ListType::begin();
            while (iter != ListType::end()) {
                if (pos == index) {
                    return iter;
                }
                pos++;
                iter++;
            }
        }
        FAULT("Index '{}' not exists!", index);
    }

    typename ListType::const_iterator at_index_const(const int64_t index) const {
        if (index < 0) {
            if (-index < static_cast<int64_t>(ListType::size())) {
                int64_t pos = index + 1;
                typename ListType::const_iterator iter = ListType::end();
                while (iter != ListType::begin()) {
                    iter--;
                    if (pos == 0) {
                        return iter;
                    }
                    pos++;
                }
            }
        } else {
            int64_t pos = 0;
            typename ListType::const_iterator iter = ListType::begin();
            while (iter != ListType::end()) {
                if (pos == index) {
                    return iter;
                }
                pos++;
                iter++;
            }
        }
        FAULT("Index '{}' not exists!", index);
    }

    virtual void erase(const int64_t index) { ListType::erase(at_index_const(index)); }

    virtual void erase(const size_t index_from, const size_t index_to) {
        if (index_from <= index_to) {
            if (index_from == index_to) {
                if (index_from < ListType::size()) {
                    ListType::erase(at_index(index_from));
                }
            } else {
                ListType::erase(at_index(index_from), (index_to < ListType::size() ? at_index(index_to) : ListType::end()));
            }
        } else {
            ASSERT(index_to < index_from);
            ListType::erase(at_index(index_to), ListType::end());
            ListType::erase(ListType::begin(), (index_from < ListType::size() ? at_index(index_from) : ListType::end()));
        }
    }

    virtual ~Dict() {}

    /*
     * Конструкторы для создания списка параметров (при подготовке аргументов перед вызовом функции)
     */
    Dict() {}

    Dict(PairType arg) { push_front(arg.second, arg.first); }

    template <class... A>
    inline Dict(PairType arg, A... rest) : Dict(rest...) {
        push_front(arg.second, arg.first);
    }
};

} // namespace trust

#endif // TRUST_INCLUDED_DICT_
