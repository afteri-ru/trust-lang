// src/pm/protocol.cpp
#include "pm/protocol.hpp"

namespace trust::pm {

PmException::PmException(PmError error)
: m_error(std::move(error))
, m_what(m_error.code + ": " + m_error.message) {
}

const char* PmException::what() const noexcept {
    return m_what.c_str();
}

const PmError& PmException::error() const noexcept {
    return m_error;
}

nlohmann::json okEnvelope(nlohmann::json data) {
    nlohmann::json envelope;
    envelope["protocol"] = kProtocolVersion;
    envelope["ok"] = true;
    envelope["data"] = std::move(data);
    envelope["errors"] = nlohmann::json::array();
    return envelope;
}

nlohmann::json errorEnvelope(const PmError& error) {
    return errorEnvelope(std::vector<PmError>{error});
}

nlohmann::json errorEnvelope(const std::vector<PmError>& errors) {
    nlohmann::json list = nlohmann::json::array();
    for (const PmError& e : errors) {
        list.push_back(nlohmann::json{{"code", e.code}, {"message", e.message}, {"context", e.context}});
    }
    nlohmann::json envelope;
    envelope["protocol"] = kProtocolVersion;
    envelope["ok"] = false;
    envelope["data"] = nullptr;
    envelope["errors"] = std::move(list);
    return envelope;
}

} // namespace trust::pm
