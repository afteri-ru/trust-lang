#pragma once

// include/types/call/overload_resolve.hpp
// Разрешение ПЕРЕГРУЗКИ: выбор сигнатуры из НАБОРА по типам аргументов.
// ЕДИНЫЙ путь для всех вызовов (функции/методы/операторы): OverloadCandidate + resolveCall.
// Имя с одной сигнатурой - вырожденный случай того же алгоритма (набор из 1), а НЕ отдельный
// fallback-API.
//
// Ранжирование (C++-модель):
//   exact (каноническое C++-равенство) > numeric promotion > numeric conversion > int->float >
//   Any-подобный параметр/аргумент (низший приоритет). Нежизнеспособные кандидаты (нечисловое
//   несовпадение, AP -> машинный) отбрасываются. Лучший - покомпонентно не хуже остальных и хотя
//   бы по одному аргументу строго лучше; при равенстве конверсий не-вариадик предпочтительнее
//   вариадика (C++), несколько несравнимых лучших - ambiguous.
//
// C++-ИДЕНТИЧНОСТЬ сигнатуры (дубли/суффикс/exact) - ЕДИНСТВЕННАЯ реализация:
// types/fn_identity.hpp (FunctionIdentity). Локальных канонизаторов здесь нет.

#include "types/type_read.hpp"
#include "types/func/fn_identity.hpp"
#include "types/subst/template_subst.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "types/call/template_unify.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include "types/names/type_names.hpp"
#include "types/typekind.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Единая шкала рангов неявной конверсии аргумента. Тир — обычное упорядоченное перечисление
/// (`Tier`, порядок объявления = приоритет); числовой ранг вычисляется как
/// `rank = tier * kTierStride + width`, где width — вторичный ключ (ширина цели). Any-подобные —
/// последний тир: они остаются жизнеспособными, но никогда не побеждают конкретную конверсию.
namespace conversion_rank {

/// Тир неявной конверсии, от лучшего к худшему. Числовой порядок задаётся ПОРЯДКОМ объявления
/// (C++-модель; trust-специфичные Representation/AnyLike - в конце).
enum class Tier : uint8_t {
    kExact,          ///< точное совпадение Trust-типов
    kPromotion,      ///< integral/floating promotion (8/16 -> 32, float -> double)
    kWidening,       ///< расширение/смена знаковости к более широкому; derived -> base; AP widen
    kCrossNumeric,   ///< int -> float; machine number -> arbitrary precision
    kNarrowing,      ///< сужение / равная ширина со сменой знаковости / float -> int
    kRepresentation, ///< номинальный алиас <-> база для НЕчисловых типов (числовые - по underlying выше)
    kAnyLike,        ///< параметр/аргумент неизвестен или `:Any` (всегда последний)
};

/// Шаг между тирами: оставляет место для вторичного ключа (ширина цели, 0..255).
constexpr int kTierStride = 1000;
/// Конверсия невозможна (кандидат отбрасывается).
constexpr int kNotViable = -1;

/// Ранг = tier * stride + width (меньше - лучше). Ширина - вторичный ключ («более узкая цель
/// предпочтительнее», как в C++).
[[nodiscard]] constexpr int rank(Tier tier, int width = 0) noexcept {
    return static_cast<int>(tier) * kTierStride + width;
}
static_assert(kTierStride > 255, "tier stride must exceed the width secondary key");

} // namespace conversion_rank

/// Ранг конверсии аргумента (from) в параметр (to). `conversion_rank::kNotViable` = кандидат
/// нежизнеспособен; меньше - лучше. Exact сравнивается по НОМИНАЛЬНОЙ Trust-identity; числовые
/// тиры классифицируются по underlying (см. `conversion_rank`).
inline int overloadConversionRank(const TypeReadModel& reg, TypeId from, TypeId to) {
    // Неизвестный тип аргумента (ошибка уже диагностирована выше) не должен превращаться в
    // каскад: он остаётся жизнеспособным с низшим рангом. Нетипизированный параметр (`INVALID`
    // в сигнатуре) кодоген трактует как `:Any`, поэтому он также принимает всё.
    if (from == INVALID_TYPE_ID || to == INVALID_TYPE_ID) {
        return conversion_rank::rank(conversion_rank::Tier::kAnyLike);
    }
    const TypeId fc = reg.store().identityOf(from, TypeIdentityScope::kCanonical);
    const TypeId tc = reg.store().identityOf(to, TypeIdentityScope::kCanonical);
    // Exact match — НОМИНАЛЬНАЯ Trust-идентичность (пользовательский алиас отличен от базы;
    // встроенные representation-алиасы раскрываются). `:Any` — КОНКРЕТНЫЙ тип, поэтому
    // `Any -> Any` exact. Канонические fc/tc ниже используются и для классификации числовых
    // конверсий. Проверка `:Any`-правил идёт ПОСЛЕ exact.
    const FunctionIdentity& identity = reg.identity();
    if (identity.sameSemanticType(from, to)) {
        return conversion_rank::rank(conversion_rank::Tier::kExact);
    }
    if (isAnyType(to, reg)) {
        return conversion_rank::rank(conversion_rank::Tier::kAnyLike); // параметр `:Any` принимает любой тип
    }
    // `:Any` — конкретный тип (`std::any`) БЕЗ неявной конверсии в другие типы: передача
    // Any-значения в конкретный параметр нежизнеспособна (явная конверсия на call-site -
    // отдельная функциональность, а не silent fallback).
    if (isAnyType(from, reg)) {
        return conversion_rank::kNotViable;
    }
    const Group fg = getGroup(getTypeKind(fc));
    const Group tg = getGroup(getTypeKind(tc));
    const uint8_t fw = getData(getTypeKind(fc));
    const uint8_t tw = getData(getTypeKind(tc));
    const bool fInt = (fg == Group::kIntegers || fg == Group::kUnsigned);
    const bool tInt = (tg == Group::kIntegers || tg == Group::kUnsigned);
    // Произвольная точность (BigInteger/Rational): машинное число → AP (конструктор, C++-неявно);
    // BigInteger→Rational - расширение, Rational→BigInteger - сужение.
    const bool fAP = (fg == Group::kArbitraryPrecision);
    const bool tAP = (tg == Group::kArbitraryPrecision);
    if (tAP) {
        const TypeId rat = reg.store().desugaredTypeId(reg.getType(type::Rational));
        if (fAP) {
            const bool fRat = (fc == rat);
            const bool tRat = (tc == rat);
            if (fRat && !tRat) {
                return conversion_rank::rank(conversion_rank::Tier::kNarrowing); // Rational -> BigInteger (сужение)
            }
            if (!fRat && tRat) {
                return conversion_rank::rank(conversion_rank::Tier::kWidening); // BigInteger -> Rational (расширение)
            }
            return conversion_rank::rank(conversion_rank::Tier::kWidening);
        }
        if (fInt || fg == Group::kNumbers || fg == Group::kLogical) {
            return conversion_rank::rank(conversion_rank::Tier::kCrossNumeric, fw); // машинное число -> BigInteger/Rational
        }
        return conversion_rank::kNotViable;
    }
    if (fAP) {
        return conversion_rank::kNotViable; // BigInteger/Rational -> конкретный машинный тип
    }
    // Числовые конверсии: модель ЗЕРКАЛИТ допустимые C++ неявные конверсии (включая сужение и
    // смену знаковости) - анализатор РЕШАЕТ, но не запрещает их. Ранг: exact < промоция <
    // расширение < int→float < сужение/знаковость. Меньше - лучше.
    if (fInt && tInt) {
        if (fg == tg && tw > fw) {
            const auto tier = (fw < 32 && tw >= 32) ? conversion_rank::Tier::kPromotion : conversion_rank::Tier::kWidening;
            return conversion_rank::rank(tier, tw);
        }
        if (fg != tg && tw > fw) {
            return conversion_rank::rank(conversion_rank::Tier::kWidening, tw); // к более широкому (смена знаковости)
        }
        return conversion_rank::rank(conversion_rank::Tier::kNarrowing, fw); // сужение или равная ширина
    }
    if (fg == Group::kNumbers && tg == Group::kNumbers) {
        return (tw > fw) ? conversion_rank::rank(conversion_rank::Tier::kPromotion, tw) : conversion_rank::rank(conversion_rank::Tier::kNarrowing, fw);
    }
    if (fInt && tg == Group::kNumbers) {
        return (tw >= fw) ? conversion_rank::rank(conversion_rank::Tier::kCrossNumeric, tw) : conversion_rank::rank(conversion_rank::Tier::kNarrowing, fw);
    }
    if (fg == Group::kNumbers && tInt) {
        return conversion_rank::rank(conversion_rank::Tier::kNarrowing, fw); // float -> int (C++-совместимо)
    }
    // Производный Record-класс → базовый (C++-совместимое неявное преобразование).
    if (reg.store().isRecordType(fc)) {
        std::vector<TypeId> pending = reg.store().baseClasses(fc);
        // visited: наследование может быть ромбовидным (и в пределе циклическим) - без защиты
        // обход повторяет базовые классы экспоненциально/зацикливается.
        std::vector<TypeId> visited;
        while (!pending.empty()) {
            const TypeId b = reg.store().desugaredTypeId(pending.back());
            pending.pop_back();
            bool seen = false;
            for (const TypeId v : visited) {
                if (v == b) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            visited.push_back(b);
            if (b == tc) {
                return conversion_rank::rank(conversion_rank::Tier::kWidening); // derived -> base
            }
            for (const TypeId bb : reg.store().baseClasses(b)) {
                pending.push_back(bb);
            }
        }
    }
    // Номинальный алиас и его представление совпадают (fc == tc), но Trust-типы различны:
    // конверсия представления (record/прочие нечисловые алиасы); хуже любой числовой.
    if (fc == tc) {
        return conversion_rank::rank(conversion_rank::Tier::kRepresentation);
    }
    return conversion_rank::kNotViable; // несовместимые категории
}

/// Результат разрешения перегрузки (один тип для всех форм вызова).
struct OverloadResolution {
    TypeId chosen = INVALID_TYPE_ID; ///< Выбранная ОБЪЯВЛЕННАЯ сигнатура; INVALID - выбора нет.
    /// Индекс выбранного кандидата во ВХОДНОМ наборе (порядок `sigs`, переданных вызывающим).
    /// Нужен потому, что два разных объявления могут делить один `TypeId` (позиционная
    /// идентичность: `<T,U> f(T,U)` и `<U,T> f(T,U)`); суффикс имени выбирается по индексу.
    std::size_t chosenIndex = 0;
    /// ЭФФЕКТИВНАЯ (инстанцированная) сигнатура вызова: подставленные параметры/возврат, пак
    /// развёрнут. Для non-generic совпадает с `chosen`. INVALID — инстанциация невозможна.
    TypeId effectiveSig = INVALID_TYPE_ID;
    TemplateBindings bindings;
    std::vector<TypeId> ambiguous; ///< Несравнимые ЛУЧШИЕ кандидаты (непусто ⟺ ambiguous).
    enum class Failure {
        None,
        NoMatch,
        Conflict,
        Unbound,
        /// A candidate has a type-pack slot but the caller did not enable template-aware
        /// resolution (a pack signature cannot be ranked without unification).
        PackWithoutTemplate,
        /// The return type contains a type-pack in a single-type position: an effective call
        /// signature cannot be built.
        ReturnPack
    } failure = Failure::None;
    TypeId unboundParam = INVALID_TYPE_ID;
};

/// Один кандидат вызова. Форма вызова (свободная функция/метод/оператор) влияет только на
/// СПОСОБ заполнения `argTypes` (позиционно или после named/default binding) и на флаг
/// `templateAware`; сам выбор — один алгоритм для всех форм.
struct OverloadCandidate {
    TypeId declaredSig = INVALID_TYPE_ID;
    /// Типы аргументов В ПОРЯДКЕ ПАРАМЕТРОВ (после binding, если он применялся).
    std::vector<TypeId> argTypes;
    /// Минимум явно переданных позиций (для defaults; 0 = полная арность).
    std::size_t requiredCount = 0;
    /// Унифицировать типовые параметры и инстанцировать эффективную сигнатуру.
    bool templateAware = false;
    /// Разрешить хвостовые default-параметры (bound-путь).
    bool allowDefaults = false;
    /// Тип, объявивший member-оператор: `argTypes[0]` - получатель, неявный object-параметр
    /// имеет тип `objectOwner` (объявленные параметры выровнены с `argTypes[1..]`).
    /// INVALID - кандидат без неявного объекта (free-форма). Позволяет ранжировать member и
    /// free формы в ОДНОМ наборе (C++ [over.match.oper]).
    TypeId objectOwner = INVALID_TYPE_ID;
};

/// Кандидат с уже вычисленными рангами аргументов (общая часть всех путей).
struct OverloadRankedCandidate {
    TypeId sig;
    std::vector<int> ranks;
    /// C++ tie-break: at equal conversion sequences a more specialized (fixed-arity) candidate
    /// wins. Non-zero for a C-variadic slot AND for a type-pack slot - C++ partial ordering
    /// prefers `f(T)` over `f(T...)` / `f(T, ...)` when the conversion sequences are equal.
    int nonFixedPenalty = 0;
    /// C++ rule: at equal rank a non-template function is preferred over a template.
    bool generic = false;
    /// Index of the candidate in the caller's declared set (suffix/param metadata are per index,
    /// because two declarations may share one `TypeId`).
    std::size_t declIndex = 0;
};

/// Выбрать лучший кандидат: покомпонентное «не хуже» по рангам, затем variadic tie-break,
/// затем C++-правило «не-шаблонная перегрузка предпочтительнее шаблонной».
inline OverloadResolution chooseBestOverload(std::vector<OverloadRankedCandidate> cands) {
    OverloadResolution res;
    if (cands.empty()) {
        return res;
    }
    // Инвариант: все кандидаты одного вызова подготовлены по ОДНОМУ набору фактических
    // аргументов (binding даёт одинаковую длину). Разные длины нельзя «тихо» считать
    // несравнимыми (это маскировало бы баг будущих форм: materialization defaults и т.п.).
    const std::size_t rankCount = cands.front().ranks.size();
    for (const auto& c : cands) {
        EXPECT(c.ranks.size() == rankCount && "chooseBestOverload: all candidates must have the same number of rank components");
    }
    // Лексикографический порядок: СНАЧАЛА ранги по аргументам (strict better ANYWHERE wins),
    // non-fixed penalty применяется ТОЛЬКО при полном равенстве рангов. Иначе точное совпадение
    // с вариадиком/паком ошибочно проигрывало бы/становилось ambiguous относительно худшей конверсии.
    const auto notWorse = [](const OverloadRankedCandidate& a, const OverloadRankedCandidate& b) {
        bool anyBetter = false;
        for (std::size_t i = 0; i < a.ranks.size(); ++i) {
            if (a.ranks[i] > b.ranks[i]) {
                return false;
            }
            if (a.ranks[i] < b.ranks[i]) {
                anyBetter = true;
            }
        }
        if (anyBetter) {
            return true;
        }
        return a.nonFixedPenalty <= b.nonFixedPenalty;
    };
    std::vector<std::size_t> best;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < cands.size() && !dominated; ++j) {
            if (i == j) {
                continue;
            }
            const bool jNum = notWorse(cands[j], cands[i]);
            const bool iNum = notWorse(cands[i], cands[j]);
            if (jNum && !iNum) {
                dominated = true; // j строго лучше i
            }
        }
        if (!dominated) {
            best.push_back(i);
        }
    }
    // C++-правило: не-шаблонная перегрузка предпочтительнее шаблонной, НО только если её
    // последовательности конверсий не хуже КАЖДОГО из лучших кандидатов (C++ [over.match.best]).
    // Если не-шаблон и шаблон несравнимы (лучше по разным аргументам) — ambiguous, а не тихое
    // предпочтение не-шаблона.
    std::vector<std::size_t> nonGeneric;
    for (const std::size_t i : best) {
        if (!cands[i].generic) {
            nonGeneric.push_back(i);
        }
    }
    std::vector<std::size_t> finalists = best;
    if (!nonGeneric.empty()) {
        bool allNotWorse = true;
        for (const std::size_t g : nonGeneric) {
            for (const std::size_t b : best) {
                if (!notWorse(cands[g], cands[b])) {
                    allNotWorse = false;
                    break;
                }
            }
            if (!allNotWorse) {
                break;
            }
        }
        if (allNotWorse) {
            finalists = nonGeneric;
        }
    }
    if (finalists.size() == 1) {
        res.chosen = cands[finalists.front()].sig;
        res.chosenIndex = cands[finalists.front()].declIndex;
        return res;
    }
    res.ambiguous.reserve(finalists.size());
    for (const std::size_t i : finalists) {
        res.ambiguous.push_back(cands[i].sig);
    }
    return res;
}

/// ЕДИНЫЙ путь РАНЖИРОВАНИЯ/ВЫБОРА кандидата для всех форм вызова (free/method/operator/[]/()).
/// Кандидаты отличаются только заполнением `argTypes` (позиционно/после binding), флагом
/// `templateAware` и неявным object-параметром member-формы (`objectOwner`); алгоритм один.
///
/// ⚠ READ-ONLY: унификация и ранги БЕЗ подстановки и БЕЗ интернирования (StructuralInterner сюда
/// не передаётся — DoD). Шаблонная позиция после успешной унификации ранжируется `kExact`;
/// конкретная (без TemplateParam) — напрямую через `overloadConversionRank`. `effectiveSig` здесь
/// НЕ строится; инстанцирование — отдельный шаг (`resolveCall`).
inline OverloadResolution selectCallCandidate(const TypeReadModel& reg, std::span<const OverloadCandidate> candidates) {
    OverloadResolution res;
    struct Prepared {
        OverloadCandidate cand;
        TemplateDeduction ded;
        std::vector<int> ranks;
        bool generic = false;
        std::size_t declIndex = 0;
    };
    std::vector<Prepared> prepared;
    bool sawConflict = false;
    bool sawUnbound = false;
    bool sawPackWithoutTemplate = false;
    bool sawNonPackMismatch = false;
    TypeId firstUnbound = INVALID_TYPE_ID;

    for (std::size_t ci = 0; ci < candidates.size(); ++ci) {
        const OverloadCandidate& cand = candidates[ci];
        const TypeId sig = getUnqualifiedType(cand.declaredSig);
        const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(sig);
        EXPECT(fd != nullptr && "selectCallCandidate: candidate signature must be a function type");
        const bool variadic = (fd->variadicType != INVALID_TYPE_ID);
        const std::size_t nargs = cand.argTypes.size();
        TemplateDeduction ded;
        bool generic = false;
        std::vector<int> ranks;

        if (cand.templateAware) {
            EXPECT(cand.objectOwner == INVALID_TYPE_ID && "resolveCall: template-aware member candidates are not supported");
            if (!unifyTemplateSignature(reg, sig, cand.argTypes, ded)) {
                if (ded.status == TemplateDeduction::Status::Conflict) {
                    sawConflict = true;
                } else if (ded.status == TemplateDeduction::Status::Unbound) {
                    sawUnbound = true;
                    if (firstUnbound == INVALID_TYPE_ID) {
                        firstUnbound = ded.conflictingParam;
                    }
                }
                continue;
            }
            std::vector<TypeId> typeParams;
            collectTemplateParams(reg, sig, typeParams);
            generic = !typeParams.empty();
            ranks.assign(nargs, 0);
            bool ok = true;
            // Read-only: реестр здесь не мутируется, но поля держим в локальных копиях как
            // защиту от будущего интернирования (дескрипторы живут в deque - адреса стабильны).
            const std::vector<TypeId> paramTypes = fd->paramTypes;
            const TypeId variadicType = fd->variadicType;
            const bool hasPack = fd->paramPackType != INVALID_TYPE_ID;
            const std::size_t fixed = paramTypes.size();
            for (std::size_t i = 0; i < nargs && ok; ++i) {
                if (i < fixed) {
                    // ⚠ Read-only: БЕЗ подстановки/интернирования на этапе выбора. Шаблонная
                    // позиция после успешной унификации — exact (подставленный тип канонически
                    // равен аргументу); конкретная позиция ранжируется напрямую.
                    std::vector<TypeId> pparams;
                    collectTemplateParams(reg, paramTypes[i], pparams);
                    const int k = pparams.empty()
                        ? overloadConversionRank(reg, cand.argTypes[i], paramTypes[i])
                        : conversion_rank::rank(conversion_rank::Tier::kExact);
                    if (k < 0) {
                        ok = false;
                        break;
                    }
                    ranks[i] = k;
                } else if (hasPack) {
                    ranks[i] = 0; // pack-позиция: унификация уже гарантировала совместимость
                    continue;
                } else if (variadic) {
                    const int k = overloadConversionRank(reg, cand.argTypes[i], variadicType);
                    if (k < 0) {
                        ok = false;
                        break;
                    }
                    ranks[i] = k;
                } else {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                continue;
            }
        } else {
            if (fd->paramPackType != INVALID_TYPE_ID) {
                // A pack signature is rankable only after unification: report it instead of a
                // silent "no matching overload".
                sawPackWithoutTemplate = true;
                continue;
            }
            // Member-форма: `argTypes[0]` - неявный object-параметр объявившего типа,
            // объявленные параметры выровнены с `argTypes[1..]` (C++ [over.match.oper]).
            const std::size_t implicitObject = (cand.objectOwner != INVALID_TYPE_ID) ? 1 : 0;
            const std::size_t declaredParams = fd->paramTypes.size() + implicitObject;
            bool arityOk = true;
            if (cand.allowDefaults) {
                arityOk = !(nargs < cand.requiredCount || (!variadic && nargs > declaredParams));
            } else if (variadic) {
                arityOk = nargs >= declaredParams;
            } else {
                arityOk = nargs == declaredParams;
            }
            if (!arityOk) {
                sawNonPackMismatch = true;
                continue;
            }
            ranks.assign(nargs, 0);
            bool ok = true;
            for (std::size_t i = 0; i < nargs && ok; ++i) {
                const TypeId pt = (i < implicitObject) ? cand.objectOwner
                                                       : ((i - implicitObject < fd->paramTypes.size()) ? fd->paramTypes[i - implicitObject] : fd->variadicType);
                const int k = overloadConversionRank(reg, cand.argTypes[i], pt);
                if (k < 0) {
                    ok = false;
                    break;
                }
                ranks[i] = k;
            }
            if (!ok) {
                sawNonPackMismatch = true;
                continue;
            }
        }
        prepared.push_back(Prepared{cand, std::move(ded), std::move(ranks), generic, ci});
    }

    if (prepared.empty()) {
        if (sawConflict) {
            res.failure = OverloadResolution::Failure::Conflict;
        } else if (sawUnbound) {
            res.failure = OverloadResolution::Failure::Unbound;
            res.unboundParam = firstUnbound;
        } else if (sawNonPackMismatch) {
            // A regular (non-pack) candidate was considered and rejected: "no matching overload"
            // is the accurate diagnosis, not "pack requires template-aware resolution".
            res.failure = OverloadResolution::Failure::NoMatch;
        } else if (sawPackWithoutTemplate) {
            res.failure = OverloadResolution::Failure::PackWithoutTemplate;
        } else {
            res.failure = OverloadResolution::Failure::NoMatch;
        }
        return res;
    }

    std::vector<OverloadRankedCandidate> ranked;
    ranked.reserve(prepared.size());
    for (const auto& p : prepared) {
        const auto* fd = reg.store().getTypeDataAs<FunctionTypeData>(p.cand.declaredSig);
        EXPECT(fd != nullptr && "selectCallCandidate: candidate signature must be a function type");
        // C++ partial ordering at equal conversion sequences: a signature without non-fixed slots
        // (no C-variadic `...`, no type pack) is more specialized and wins.
        const bool hasNonFixedSlot = fd->variadicType != INVALID_TYPE_ID || fd->paramPackType != INVALID_TYPE_ID;
        ranked.push_back(OverloadRankedCandidate{getUnqualifiedType(p.cand.declaredSig), p.ranks, hasNonFixedSlot ? 1 : 0, p.generic, p.declIndex});
    }
    OverloadResolution best = chooseBestOverload(std::move(ranked));
    res.ambiguous = std::move(best.ambiguous);
    if (best.chosen == INVALID_TYPE_ID) {
        return res;
    }
    for (auto& p : prepared) {
        if (p.declIndex != best.chosenIndex) {
            continue;
        }
        res.chosen = best.chosen;
        res.chosenIndex = p.declIndex;
        res.bindings = std::move(p.ded.bindings);
        // effectiveSig НЕ заполняется здесь — инстанцирование выполняет resolveCall (ниже).
        break;
    }
    return res;
}

/// ЕДИНЫЙ путь выбора и инстанцирования сигнатуры вызова для всех форм (free/method/operator/[]/()).
/// Селекция — read-only (`selectCallCandidate`); инстанцирование эффективной сигнатуры
/// (единственная запись в интернер) выполняется ТОЛЬКО для выбранного template-aware кандидата.
inline OverloadResolution resolveCall(TypeWriteContext ctx, std::span<const OverloadCandidate> candidates) {
    OverloadResolution res = selectCallCandidate(ctx.read, candidates);
    if (res.chosen == INVALID_TYPE_ID) {
        return res; // NoMatch/Conflict/Unbound/ambiguous/pack-no-template диагностированы выше
    }
    EXPECT(res.chosenIndex < candidates.size() && "resolveCall: chosen index must be in-bounds");
    const OverloadCandidate& chosen = candidates[res.chosenIndex];
    if (chosen.templateAware) {
        res.effectiveSig = instantiateCallSignature(ctx, res.chosen, res.bindings, chosen.argTypes);
        if (res.effectiveSig != INVALID_TYPE_ID) {
            std::vector<TypeId> remaining;
            collectTemplateParams(ctx.read, res.effectiveSig, remaining);
            if (!remaining.empty()) {
                res.effectiveSig = INVALID_TYPE_ID;
                res.failure = OverloadResolution::Failure::Unbound;
                res.unboundParam = remaining.front();
            }
        } else {
            // instantiateCallSignature fails only when a type-pack appears in a single-type
            // position (the return type) - that is NOT an undeduced parameter.
            res.failure = OverloadResolution::Failure::ReturnPack;
        }
    } else {
        res.effectiveSig = res.chosen;
    }
    if (res.effectiveSig != INVALID_TYPE_ID) {
        res.failure = OverloadResolution::Failure::None;
    }
    return res;
}

} // namespace trust
