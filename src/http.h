#pragma once

#include <string>

namespace http {

struct Result {
    int status = 0;
    std::string body;
    std::string error;

    bool ok() const { return error.empty() && status >= 200 && status < 400; }
};

Result get(const std::string& url, const std::string& userAgent,
           int timeoutMs = 25000);
Result postForm(const std::string& url, const std::string& formBody,
                const std::string& userAgent, int timeoutMs = 25000);

const char* mobileUserAgent();
const char* desktopUserAgent();

} // namespace http
