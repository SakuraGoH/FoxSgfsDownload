#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace util {

std::string trim(const std::string& s);
std::string lowerAscii(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, const std::string& sep);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
std::string join(const std::vector<std::string>& parts, const std::string& sep);

std::wstring utf8ToWide(const std::string& s);
std::string wideToUtf8(const std::wstring& s);
std::string urlEncode(const std::string& s);
std::string htmlUnescape(const std::string& s);
std::string stripTags(const std::string& s);

std::string formatDouble(double v, int maxDecimals = 2);
std::string normalizeSgf(const std::string& sgf);
std::string ellipsizeUtf8(const std::string& s, size_t maxChars);

bool readFile(const std::string& path, std::string& out);
bool writeFile(const std::string& path, const std::string& data,
               std::string* error = nullptr);
bool fileExists(const std::string& path);
std::string exeDirectory();
std::string joinPath(const std::string& a, const std::string& b);

} // namespace util
