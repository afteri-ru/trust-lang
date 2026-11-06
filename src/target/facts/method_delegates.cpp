// Реализация хранилища делегатов нативных методов (см. заголовок).
#include "target/method_delegates.hpp"

#include <utility>

namespace trust {

void MethodDelegates::set(TypeId owner, std::string method, std::string delegate) {
    m_byOwner[owner][std::move(method)] = std::move(delegate);
}

const std::string* MethodDelegates::findByOwner(TypeId owner, std::string_view method) const noexcept {
    const auto it = m_byOwner.find(owner);
    if (it == m_byOwner.end()) {
        return nullptr;
    }
    const auto mit = it->second.find(std::string(method));
    return mit == it->second.end() ? nullptr : &mit->second;
}

} // namespace trust