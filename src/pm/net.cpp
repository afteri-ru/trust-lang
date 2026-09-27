// src/pm/net.cpp
#include "pm/net.hpp"

#include "pm/cache.hpp"
#include "pm/protocol.hpp"

#include <curl/curl.h>

#include <fstream>

namespace trust::pm {
namespace {

namespace fs = std::filesystem;

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

void ensureCurlInitialized() {
    static CurlGlobal global;
    (void)global;
}

std::size_t writeToString(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

std::size_t writeToStream(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(data, static_cast<std::streamsize>(size * nmemb));
    return out->good() ? size * nmemb : 0;
}

void configure(CURL* handle, const std::string& url, const DownloadOptions& options, void* userdata, curl_write_callback callback) {
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, options.timeout_seconds);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "trust-pm/1");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, userdata);
}

[[noreturn]] void failFetch(const std::string& url, const std::string& reason) {
    throw PmException(PmError{"pm.net.fetch", "failed to fetch '" + url + "': " + reason, nlohmann::json{{"url", url}}});
}

} // namespace

std::string downloadToString(const std::string& url, const DownloadOptions& options) {
    if (options.offline) {
        throw PmException(PmError{"pm.net.offline", "network access is disabled (--offline): " + url, nlohmann::json{{"url", url}}});
    }
    ensureCurlInitialized();
    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        failFetch(url, "cannot initialize libcurl");
    }
    std::string body;
    configure(handle, url, options, &body, writeToString);
    const CURLcode code = curl_easy_perform(handle);
    curl_easy_cleanup(handle);
    if (code != CURLE_OK) {
        failFetch(url, curl_easy_strerror(code));
    }
    return body;
}

std::string downloadToFile(const std::string& url, const fs::path& dest, const DownloadOptions& options) {
    if (options.offline) {
        throw PmException(PmError{"pm.net.offline", "network access is disabled (--offline): " + url, nlohmann::json{{"url", url}}});
    }
    ensureCurlInitialized();
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        throw PmException(PmError{"pm.net.write", "cannot write download to: " + dest.string(), nlohmann::json{{"file", dest.string()}}});
    }
    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        failFetch(url, "cannot initialize libcurl");
    }
    configure(handle, url, options, &out, writeToStream);
    const CURLcode code = curl_easy_perform(handle);
    curl_easy_cleanup(handle);
    out.close();
    if (code != CURLE_OK) {
        fs::remove(dest, ec);
        failFetch(url, curl_easy_strerror(code));
    }
    return sha256File(dest);
}

} // namespace trust::pm
