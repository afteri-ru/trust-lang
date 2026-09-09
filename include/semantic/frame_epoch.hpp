#pragma once

// include/semantic/frame_epoch.hpp
// Общая per-frame модель «эпох мутаций» для анализаторов инвалидации ссылок.
//
// Единый механизм используется ОБОИМИ анализаторами (native borrowed и borrow-checker):
// эпоха мутации переменной-ИСТОЧНИКА адресуется по фрейму (лексическому скоупу), в котором
// переменная объявлена. Это устраняет прежнюю глобальную (по bare-имени) адресацию, из-за
// которой одноимённые переменные в разных скоупах делили эпоху (ложные срабатывания).
//
// Диагностика «использование зависимой после мутации источника» требует лишь сохранённого
// диапазона мутации (changeSite): при обращении к зависимой выводится сохранённый ранее
// range мутации. Зависимости от pop()/exitScope нет - состояние живёт во фрейме объявления.
//
// Потокобезопасность не требуется: анализатор однопоточный, стек фреймов синхронен
// со скоуп-стеком ядра (push/pop из enterScope/exitScope хуков).

#include "location/location.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace trust {

/// Стек лексических фреймов для per-frame учёта мутаций источников.
class FrameEpoch {
  public:
    FrameEpoch() {
        m_frames.emplace_back(); // глобальный фрейм (depth >= 1)
    }

    /// Вход во вложенный скоуп (синхронно с enterScope ядра).
    void push() { m_frames.emplace_back(); }

    /// Выход из вложенного скоупа (глобальный не удаляется).
    void pop() {
        if (m_frames.size() > 1) {
            m_frames.pop_back();
        }
    }

    /// Регистрирует имя как объявленное в текущем фрейме (адресация эпохи по скоупу).
    void declare(const std::string& bare) {
        if (bare.empty() || bare == "_") {
            return;
        }
        m_frames.back().declared.insert(bare);
    }

    /// Регистрирует мутацию источника: эпоха инкрементируется в фрейме ОБЪЯВЛЕНИЯ
    /// (поиск сверху вниз; иначе - глобальный фрейм), там же сохраняется range мутации.
    void mutate(const std::string& name, const MapperRange& range) {
        if (name.empty()) {
            return;
        }
        Frame* f = frameOf(name);
        if (f == nullptr) {
            return;
        }
        f->epoch[name]++;
        f->changeSite[name] = range;
    }

    /// Текущая эпоха мутаций источника (0 - не мутировал / не найден).
    [[nodiscard]] int64_t epochOf(const std::string& name) const {
        const Frame* f = frameOf(name);
        if (f == nullptr) {
            return 0;
        }
        auto it = f->epoch.find(name);
        return (it == f->epoch.end()) ? 0 : it->second;
    }

    /// Диапазон последней мутации источника; nullptr - нет.
    [[nodiscard]] const MapperRange* changeSiteOf(const std::string& name) const {
        const Frame* f = frameOf(name);
        if (f == nullptr) {
            return nullptr;
        }
        auto it = f->changeSite.find(name);
        return (it == f->changeSite.end()) ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t depth() const noexcept { return m_frames.size(); }

  private:
    struct Frame {
        std::set<std::string> declared;
        std::map<std::string, int64_t> epoch;
        std::map<std::string, MapperRange> changeSite;
    };

    Frame* frameOf(const std::string& name) {
        for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
            if (it->declared.count(name) != 0) {
                return &*it;
            }
        }
        // Источник не объявлен через declare() (напр. параметр/catch-биндинг): относим
        // эпоху к глобальному фрейму - детерминированно и без потери уникальности.
        return m_frames.empty() ? nullptr : &m_frames.front();
    }

    const Frame* frameOf(const std::string& name) const {
        for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
            if (it->declared.count(name) != 0) {
                return &*it;
            }
        }
        return m_frames.empty() ? nullptr : &m_frames.front();
    }

    std::vector<Frame> m_frames;
};

} // namespace trust
