#include "util.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace util {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    if (sep.empty()) {
        out.push_back(s);
        return out;
    }
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + sep.size();
    }
    return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

static void appendUtf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string htmlUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back('&');
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos" || ent == "#39") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned cp = 0;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                cp = static_cast<unsigned>(std::strtoul(ent.c_str() + 2, nullptr, 16));
            } else {
                cp = static_cast<unsigned>(std::strtoul(ent.c_str() + 1, nullptr, 10));
            }
            if (cp > 0) appendUtf8(out, cp);
            else out.push_back('&');
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

std::string stripTags(const std::string& s) {
    std::string out;
    bool inTag = false;
    for (char c : s) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out.push_back(c);
    }
    return out;
}

std::string formatDouble(double v, int maxDecimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", maxDecimals, v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::string normalizeSgf(const std::string& sgf) {
    std::string text = trim(sgf);
    if (startsWith(text, "\xEF\xBB\xBF")) text.erase(0, 3);
    size_t start = text.find("(;");
    if (start == std::string::npos) return text;

    size_t rootStart = start + 2;
    size_t rootEnd = rootStart;
    while (rootEnd < text.size() && text[rootEnd] != ';' &&
           text[rootEnd] != '(' && text[rootEnd] != ')') {
        ++rootEnd;
    }
    std::string root = text.substr(rootStart, rootEnd - rootStart);

    int depth = 0;
    size_t end = text.size();
    for (size_t i = start; i < text.size(); ++i) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }

    std::string body = text.substr(start + 1, end > start ? end - start - 1 : 0);
    std::string main;
    depth = 0;
    for (char c : body) {
        if (c == '(' && depth == 0) break;
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        main.push_back(c);
    }

    std::string moves;
    for (size_t i = 0; i < main.size(); ++i) {
        if (main[i] != ';') continue;
        size_t j = i + 1;
        while (j < main.size() && std::isspace(static_cast<unsigned char>(main[j]))) ++j;
        if (j >= main.size() || (main[j] != 'B' && main[j] != 'W')) continue;
        char color = main[j++];
        if (j >= main.size() || main[j] != '[') continue;
        ++j;
        std::string coord;
        while (j < main.size() && main[j] != ']' && coord.size() < 2) {
            char c = main[j++];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) coord.push_back(c);
        }
        while (j < main.size() && main[j] != ']') ++j;
        moves += ';';
        moves += color;
        moves += '[';
        moves += coord;
        moves += ']';
    }
    return "(;" + root + moves + ")";
}

std::string ellipsizeUtf8(const std::string& s, size_t maxChars) {
    size_t chars = 0;
    size_t pos = 0;
    while (pos < s.size() && chars < maxChars) {
        unsigned char c = static_cast<unsigned char>(s[pos]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (pos + len > s.size()) break;
        pos += len;
        ++chars;
    }
    if (pos >= s.size()) return s;
    return s.substr(0, pos) + "\xE2\x80\xA6";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(std::filesystem::path(utf8ToWide(path)), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, const std::string& data,
               std::string* error) {
    std::wstring wide = utf8ToWide(path);
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = "CreateFileW " + std::to_string(GetLastError());
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(handle, data.data(), static_cast<DWORD>(data.size()),
                        &written, nullptr);
    DWORD last = GetLastError();
    CloseHandle(handle);
    if (!ok || written != data.size()) {
        if (error) *error = "WriteFile " + std::to_string(last);
        return false;
    }
    return true;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(utf8ToWide(path)), ec);
}

std::string exeDirectory() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) return ".";
    std::wstring dir(buf, n);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) dir.erase(pos);
    return wideToUtf8(dir);
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

} // namespace util
