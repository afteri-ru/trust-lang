// src/types/ref_type.cpp
// Единый источник C++-имени ВИДА ССЫЛКИ (RefType) - см. include/types/ref_type.hpp.
#include "types/ref_type.hpp"

namespace trust {

std::string refTypeDisplayName(RefType kind, std::string_view pointeeName, std::string_view deleterName) {
    if (kind == RefType::kValue || kind == RefType::kMptr) {
        return std::string(pointeeName);
    }
    std::string args(pointeeName);
    if (kind == RefType::kUnique && !deleterName.empty()) {
        args += ", " + std::string(deleterName);
    }
    return std::string(refTypeName(kind)) + "<" + args + ">";
}

std::string refTypeCppName(RefType kind, std::string_view pointeeCpp, std::string_view deleterCpp) {
    const std::string base(pointeeCpp);
    switch (kind) {
    case RefType::kValue:
        return base;
    case RefType::kPtr:
        return base + "*";
    case RefType::kPtrPtr:
        return base + "**";
    case RefType::kRef:
        return base + "&";
    case RefType::kRref:
        return base + "&&";
    case RefType::kShared:
        return std::string(refTypeCppTemplateName(RefType::kShared)) + "<" + base + ">";
    case RefType::kWeak:
        // Слабая ссылка - trust::Weak поверх СИЛЬНОЙ (trust::Shared<pointee>); sync-вариант
        // (Weak<AccessShared<T,Policy>>) композируется в getCppTypeName до вызова этой функции.
        return std::string(refTypeCppTemplateName(RefType::kWeak)) + "<" + std::string(refTypeCppTemplateName(RefType::kShared)) + "<" + base + ">>";
    case RefType::kUnique:
        // Эксклюзивное владение; D (`@[deleter(D)]`) - ЧАСТЬ типа: trust::Unique<T[, D]>.
        if (!deleterCpp.empty()) {
            return std::string(refTypeCppTemplateName(RefType::kUnique)) + "<" + base + ", " + std::string(deleterCpp) + ">";
        }
        return std::string(refTypeCppTemplateName(RefType::kUnique)) + "<" + base + ">";
    case RefType::kLocker:
        // Охраняемый доступ к reference-wrapper (результат lock()/lock_const()).
        return std::string(refTypeCppTemplateName(RefType::kLocker)) + "<" + base + ">";
    case RefType::kMptr:
        break; // указатель на член не комбинируется с ref-видами
    }
    return base;
}

std::vector<std::string_view> refTypeRuntimeIncludes(RefType kind, bool sync) {
    // Единственное место выбора рантайм-заголовка по виду/backend'у. Sync-обёртка определена в
    // отдельном заголовке, при этом базовые типы (Locker) идут из основного - оба обязательны.
    std::vector<std::string_view> includes;
    if (kind != RefType::kShared && kind != RefType::kWeak && kind != RefType::kUnique) {
        return includes;
    }
    includes.push_back("@trust/trusted-cpp.hpp");
    if (sync) {
        includes.push_back("@trust/trusted-cpp-sync.hpp");
    }
    return includes;
}

} // namespace trust
