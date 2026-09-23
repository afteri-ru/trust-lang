#pragma once

// include/types/overload_resolve.hpp
// Разрешение ПЕРЕГРУЗКИ: выбор сигнатуры из НАБОРА по типам аргументов.
// ЕДИНЫЙ путь для всех вызовов (функции/методы/операторы): имя с одной сигнатурой - вырожденный
// случай того же алгоритма (набор из 1), а НЕ отдельный fallback-API.
//
// Ранжирование (минимальный, безопасный набор правил):
//   exact (канон. равенство) > numeric widening (расширение) > Any-параметр.
// НЕжизнеспособные кандидаты (сужение, Any-аргумент в конкретный параметр, нечисловое
// несовпадение) отбрасываются. Лучший - покомпонентно не хуже остальных и хотя бы по одному
// аргументу строго лучше (как отношение «better than» в C++); несколько несравнимых лучших -
// ambiguous (явная диагностика у вызывающего, без молчаливого выбора).

#include "types/group.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "types/typekind.hpp"

#include <cctype>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// C++-суффикс имени перегрузки, ДЕТЕРМИНИРОВАННЫЙ по СИГНАТУРЕ (имена типов параметров,
/// стабильные между единицами трансляции - не зависят от registry_index). Одинаков для
/// объявления (FuncDecl::m_overloadSuffix) и сайта вызова (CallExpr::resolvedCalleeSuffix),
/// поэтому перегруженные C++-функции различаются по имени и C++-разрешение не участвует.
inline std::string overloadCppSuffix(const TypeRegistry& reg, TypeId sig) {
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(structuralType(sig));
    if (fd == nullptr) {
        return {};
    }
    std::string s = "_";
    const auto sanitize = [](std::string_view n) {
        std::string out;
        out.reserve(n.size());
        for (const char c : n) {
            out += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
        }
        return out;
    };
    for (const TypeId p : fd->paramTypes) {
        s += sanitize(reg.getFullTypeName(p));
        s += "_";
    }
    if (fd->variadicType != INVALID_TYPE_ID) {
        s += "vararg_";
    }
    return s;
}

/// Ранг конверсии аргумента (from) в параметр (to). -1 = кандидат нежизнеспособен; меньше - лучше.
/// Модель близка к C++: exact (0) < integral/floating promotion (tier 1) < numeric conversion
/// (tier 2) < int→float (tier 3) < Any-параметр (tier 4). Ранг = tier*1000 + ширина-цели
/// (вторичный ключ: «ближайший» (более узкий) тип-цель предпочтительнее - как выбирает C++).
inline int overloadConversionRank(const TypeRegistry& reg, TypeId from, TypeId to) {
    const TypeId anyId = reg.getCanonicalTypeId(reg.getType(type_generic::Any));
    // Нетипизированный параметр (`INVALID` в сигнатуре) кодоген трактует как `Any`; неизвестный
    // тип аргумента также считаем Any-подобным. Any-подобные не блокируют кандидата (низший ранг).
    const auto anyLike = [&](TypeId raw) {
        return raw == INVALID_TYPE_ID || reg.getCanonicalTypeId(structuralType(raw)) == anyId;
    };
    if (anyLike(to) || anyLike(from)) {
        return 5000;
    }
    const TypeId fc = reg.getCanonicalTypeId(structuralType(from));
    const TypeId tc = reg.getCanonicalTypeId(structuralType(to));
    if (fc == tc) {
        return 0;
    }
    const Group fg = getGroup(getKindFromId(fc));
    const Group tg = getGroup(getKindFromId(tc));
    const uint8_t fw = getData(getKindFromId(fc));
    const uint8_t tw = getData(getKindFromId(tc));
    const bool fInt = (fg == Group::kIntegers || fg == Group::kUnsigned);
    const bool tInt = (tg == Group::kIntegers || tg == Group::kUnsigned);
    // Произвольная точность (BigInteger/Rational): машинное число → AP (конструктор, C++-неявно);
    // BigInteger→Rational - расширение, Rational→BigInteger - сужение.
    const bool fAP = (fg == Group::kArbitraryPrecision);
    const bool tAP = (tg == Group::kArbitraryPrecision);
    if (tAP) {
        const TypeId rat = reg.getCanonicalTypeId(reg.getType(type::Rational));
        if (fAP) {
            const bool fRat = (fc == rat);
            const bool tRat = (tc == rat);
            if (fRat && !tRat) {
                return 4001; // Rational → BigInteger (сужение)
            }
            if (!fRat && tRat) {
                return 1001; // BigInteger → Rational (расширение)
            }
            return 2000;
        }
        if (fInt || fg == Group::kNumbers || fg == Group::kLogical) {
            return 3000 + fw; // машинное число → BigInteger/Rational
        }
        return -1;
    }
    if (fAP) {
        return -1; // BigInteger/Rational → конкретный машинный тип: неявной конверсии нет
    }
    // Числовые конверсии: модель ЗЕРКАЛИТ допустимые C++ неявные конверсии (включая сужение и
    // смену знаковости) - анализатор РЕШАЕТ, но не запрещает их. Ранг: exact < промоция <
    // расширение < int→float < сужение/знаковость. Меньше - лучше.
    if (fInt && tInt) {
        if (fg == tg && tw > fw) {
            const int tier = (fw < 32 && tw >= 32) ? 1 : 2;
            return tier * 1000 + tw;
        }
        if (fg != tg && tw > fw) {
            return 2000 + tw; // к более широкому (смена знаковости)
        }
        return 4000 + fw; // сужение или равная ширина
    }
    if (fg == Group::kNumbers && tg == Group::kNumbers) {
        return (tw > fw) ? (1000 + tw) : (4000 + fw);
    }
    if (fInt && tg == Group::kNumbers) {
        return (tw >= fw) ? (3000 + tw) : (4000 + fw);
    }
    if (fg == Group::kNumbers && tInt) {
        return 4000 + fw; // float → int (C++-совместимо)
    }
    // Производный Record-класс → базовый (C++-совместимое неявное преобразование).
    if (reg.isRecordType(fc)) {
        std::vector<TypeId> pending = reg.baseClasses(fc);
        while (!pending.empty()) {
            const TypeId b = reg.getCanonicalTypeId(pending.back());
            pending.pop_back();
            if (b == tc) {
                return 2000;
            }
            for (const TypeId bb : reg.baseClasses(b)) {
                pending.push_back(bb);
            }
        }
    }
    return -1; // несовместимые категории
}

/// Результат разрешения перегрузки.
struct OverloadResolution {
    TypeId chosen = INVALID_TYPE_ID; ///< Выбранная сигнатура; INVALID - однозначного выбора нет.
    std::vector<TypeId> ambiguous;   ///< Несравнимые ЛУЧШИЕ кандидаты (непусто ⟺ ambiguous).
};

/// Разрешить перегрузку: выбрать сигнатуру из `signatures` по типам аргументов `argTypes`.
/// Сигнатуры - интернированные FunctionTypeData (флаги снимаются structuralType).
inline OverloadResolution resolveOverload(const TypeRegistry& reg, std::span<const TypeId> signatures, std::span<const TypeId> argTypes) {
    const std::size_t nargs = argTypes.size();
    struct Candidate {
        TypeId sig;
        std::vector<int> ranks;
    };
    std::vector<Candidate> cands;
    for (const TypeId rawSig : signatures) {
        const TypeId sig = structuralType(rawSig);
        const auto* fd = reg.getTypeDataAs<FunctionTypeData>(sig);
        if (fd == nullptr) {
            continue;
        }
        const bool variadic = (fd->variadicType != INVALID_TYPE_ID);
        if (!variadic && fd->paramTypes.size() != nargs) {
            continue;
        }
        if (variadic && nargs < fd->paramTypes.size()) {
            continue;
        }
        std::vector<int> r(nargs, 0);
        bool ok = true;
        for (std::size_t i = 0; i < nargs && ok; ++i) {
            const TypeId pt = (i < fd->paramTypes.size()) ? fd->paramTypes[i] : fd->variadicType;
            const int k = overloadConversionRank(reg, argTypes[i], pt);
            if (k < 0) {
                ok = false;
                break;
            }
            r[i] = k;
        }
        if (ok) {
            cands.push_back(Candidate{sig, std::move(r)});
        }
    }

    OverloadResolution res;
    if (cands.empty()) {
        return res;
    }
    // Максимальные элементы частичного порядка «не хуже по всем аргументам».
    const auto notWorse = [](const std::vector<int>& a, const std::vector<int>& b) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] > b[i]) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t i = 0; i < cands.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < cands.size() && !dominated; ++j) {
            if (i == j) {
                continue;
            }
            const bool jNum = notWorse(cands[j].ranks, cands[i].ranks);
            const bool iNum = notWorse(cands[i].ranks, cands[j].ranks);
            if (jNum && !iNum) {
                dominated = true; // j строго лучше i
            }
        }
        if (!dominated) {
            res.ambiguous.push_back(cands[i].sig);
        }
    }
    if (res.ambiguous.size() == 1) {
        res.chosen = res.ambiguous.front();
        res.ambiguous.clear();
    }
    return res;
}

} // namespace trust
