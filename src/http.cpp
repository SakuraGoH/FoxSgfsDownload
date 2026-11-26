#include "http.h"

#include "util.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace http {

const char* mobileUserAgent() {
    return "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) "
           "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 "
           "Safari/604.1";
}

const char* desktopUserAgent() {
    return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
}

static std::string lastErrorText(const char* where) {
    DWORD code = GetLastError();
    std::string s = where;
    s += " (WinHTTP error ";
    s += std::to_string(code);
    s += ")";
    return s;
}

static Result request(const std::wstring& method, const std::string& url,
                      const std::string& body, const std::string& userAgent,
                      int timeoutMs) {
    Result out;
    std::wstring wurl = util::utf8ToWide(url);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        out.error = lastErrorText("URL 解析失败");
        return out;
    }

    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0) {
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";

    HINTERNET session = WinHttpOpen(
        L"FoxSgfsDownload-Max/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        session = WinHttpOpen(L"FoxSgfsDownload-Max/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        out.error = lastErrorText("WinHTTP 初始化失败");
        return out;
    }

    WinHttpSetTimeouts(session, 10000, 10000, timeoutMs, timeoutMs);

    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
    if (!connect) {
        out.error = lastErrorText("连接服务器失败");
        WinHttpCloseHandle(session);
        return out;
    }

    HINTERNET req = WinHttpOpenRequest(
        connect, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        out.error = lastErrorText("创建请求失败");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }

    std::wstring headers =
        L"Accept: application/json,text/plain,*/*\r\n"
        L"Accept-Encoding: identity\r\n"
        L"Connection: close\r\n"
        L"User-Agent: " + util::utf8ToWide(userAgent) + L"\r\n";
    WinHttpAddRequestHeaders(req, headers.c_str(), static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    if (!method.empty() && method != L"GET") {
        WinHttpAddRequestHeaders(
            req, L"Content-Type: application/x-www-form-urlencoded\r\n",
            static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }

    DWORD bodyLen = static_cast<DWORD>(body.size());
    BOOL sent = WinHttpSendRequest(
        req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        bodyLen, bodyLen, 0);
    if (!sent) {
        out.error = lastErrorText("发送请求失败");
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        out.error = lastErrorText("接收响应失败");
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }

    DWORD status = 0;
    DWORD statusLen = sizeof(status);
    WinHttpQueryHeaders(req,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen,
                        WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int>(status);

    std::string data;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        size_t old = data.size();
        data.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, data.data() + old, avail, &read)) {
            data.resize(old);
            break;
        }
        data.resize(old + read);
        if (read == 0) break;
    }
    out.body = std::move(data);

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return out;
}

Result get(const std::string& url, const std::string& userAgent, int timeoutMs) {
    return request(L"GET", url, "", userAgent, timeoutMs);
}

Result postForm(const std::string& url, const std::string& formBody,
                const std::string& userAgent, int timeoutMs) {
    return request(L"POST", url, formBody, userAgent, timeoutMs);
}

} // namespace http

