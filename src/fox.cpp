#include "fox.h"

#include "http.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>

using json = nlohmann::json;

namespace {

std::string jstr(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    if (it->is_number_unsigned()) return std::to_string(it->get<unsigned long long>());
    if (it->is_number_float()) return util::formatDouble(it->get<double>(), 2);
    if (it->is_boolean()) return it->get<bool>() ? "1" : "0";
    return {};
}

int jint(const json& j, const char* key, int def = 0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_string()) {
        try { return std::stoi(it->get<std::string>()); } catch (...) { return def; }
    }
    return def;
}

std::string qipuDateFromId(const std::string& id) {
    if (id.size() < 8) return {};
    return id.substr(0, 4) + "-" + id.substr(4, 2) + "-" + id.substr(6, 2);
}

std::string qipuStartTime(const std::string& id, const std::string& endtime) {
    std::string date = qipuDateFromId(id);
    std::string time;
    if (endtime.size() >= 19) time = endtime.substr(11, 8);
    if (!date.empty() && !time.empty()) return date + " " + time;
    return endtime.empty() ? date : endtime;
}

std::string datePart(const std::string& t) {
    return t.size() >= 10 ? t.substr(0, 10) : t;
}

int dateOrdinal(const std::string& t) {
    int y = 0, m = 0, d = 0;
    if (t.size() >= 10) {
        y = std::atoi(t.substr(0, 4).c_str());
        m = std::atoi(t.substr(5, 2).c_str());
        d = std::atoi(t.substr(8, 2).c_str());
    }
    return y * 372 + (m - 1) * 31 + d;
}

std::string titleKey(const std::string& title) {
    std::string s;
    for (char c : title) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s.push_back(c);
    }
    if (s.size() > 24) s.resize(24);
    return s;
}

bool titleMentionsPlayers(const std::string& title, const Game& meta) {
    std::string t = util::lowerAscii(title);
    if (t.empty()) return false;
    int hits = 0;
    for (const std::string& name : {meta.blackName, meta.blackEnName}) {
        if (name.size() >= 2 && t.find(util::lowerAscii(name)) != std::string::npos) {
            ++hits;
            break;
        }
    }
    for (const std::string& name : {meta.whiteName, meta.whiteEnName}) {
        if (name.size() >= 2 && t.find(util::lowerAscii(name)) != std::string::npos) {
            ++hits;
            break;
        }
    }
    return hits >= 2;
}

int playerHits(const std::string& title, const Game& meta) {
    std::string t = util::lowerAscii(title);
    if (t.empty()) return 0;
    int hits = 0;
    auto hit = [&](const std::string& a, const std::string& b) {
        if (a.size() >= 2 && t.find(util::lowerAscii(a)) != std::string::npos) return true;
        if (b.size() >= 2 && t.find(util::lowerAscii(b)) != std::string::npos) return true;
        return false;
    };
    if (hit(meta.blackName, meta.blackEnName)) ++hits;
    if (hit(meta.whiteName, meta.whiteEnName)) ++hits;
    return hits;
}

void parseTitlePlayers(Game& g) {
    const std::string& title = g.title;
    size_t blackPos = title.find("执黑");
    size_t whitePos = title.find("执白");
    bool black = blackPos != std::string::npos;
    size_t pos = black ? blackPos : whitePos;
    if (pos == std::string::npos) return;

    size_t start = pos;
    while (start > 0) {
        char c = title[start - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>') break;
        --start;
    }
    std::string first = util::trim(title.substr(start, pos - start));
    size_t winPos = title.find("胜", pos);
    if (winPos == std::string::npos) return;
    std::string second = util::trim(title.substr(winPos + 3));
    if (first.empty() || second.empty()) return;
    if (black) {
        g.blackName = first;
        g.whiteName = second;
    } else {
        g.whiteName = first;
        g.blackName = second;
    }
}

std::string extractDigitsAfter(const std::string& text, const std::string& marker) {
    size_t pos = text.find(marker);
    if (pos == std::string::npos) return {};
    pos += marker.size();
    size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    return text.substr(start, pos - start);
}

} // namespace

std::optional<UserInfo> FoxClient::queryUser(const std::string& nickname,
                                             std::string& error) {
    std::string url =
        "https://newframe.foxwq.com/cgi/QueryUserInfoPanel?srcuid=0&username=" +
        util::urlEncode(nickname);
    auto r = http::get(url, http::mobileUserAgent(), 20000);
    if (!r.ok()) {
        error = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
        return std::nullopt;
    }
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded()) {
        error = "用户查询接口返回了非 JSON 数据";
        return std::nullopt;
    }
    UserInfo info;
    info.uid = jstr(j, "uid");
    if (info.uid == "0") info.uid.clear();
    info.name = jstr(j, "username");
    info.englishName = jstr(j, "englishname");
    info.gameUin = jstr(j, "gameuin");
    if (info.uid.empty()) {
        error = jstr(j, "resultstr");
        if (error.empty()) error = jstr(j, "errmsg");
        if (error.empty()) error = "找不到这个野狐账号";
        return std::nullopt;
    }
    return info;
}

std::vector<Game> FoxClient::fetchLatest(const std::string& uid, std::string& error) {
    std::string url =
        "https://h5.foxwq.com/yehuDiamond/chessbook_local/YHWQFetchChessList"
        "?srcuid=0&dstuid=" + util::urlEncode(uid) +
        "&type=1&lastcode=0&searchkey=&uin=" + util::urlEncode(uid);
    auto r = http::get(url, http::mobileUserAgent(), 20000);
    if (!r.ok()) {
        error = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
        return {};
    }
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded()) {
        error = "棋谱列表接口返回了非 JSON 数据";
        return {};
    }
    std::vector<Game> out;
    auto it = j.find("chesslist");
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& row : *it) {
        Game g;
        g.chessid = jstr(row, "chessid");
        if (g.chessid.empty()) continue;
        g.blackUid = jstr(row, "blackuid");
        g.whiteUid = jstr(row, "whiteuid");
        g.blackName = jstr(row, "blacknick");
        g.blackEnName = jstr(row, "blackenname");
        if (g.blackName.empty()) g.blackName = g.blackEnName;
        g.whiteName = jstr(row, "whitenick");
        g.whiteEnName = jstr(row, "whiteenname");
        if (g.whiteName.empty()) g.whiteName = g.whiteEnName;
        g.blackDan = jint(row, "blackdan");
        g.whiteDan = jint(row, "whitedan");
        g.winner = jint(row, "winner");
        g.point = jint(row, "point");
        g.rule = jint(row, "rule");
        g.moves = jint(row, "movenum");
        g.starttime = jstr(row, "starttime");
        g.endtime = jstr(row, "endtime");
        g.title = jstr(row, "title");
        parseTitlePlayers(g);
        out.push_back(std::move(g));
    }
    return out;
}

QipuPage FoxClient::fetchQipuSearch(const std::string& keyword, int page,
                                    std::string& error) {
    QipuPage result;
    std::string url = "https://www.foxwq.com/qipu.html?keyword=" +
                      util::urlEncode(keyword) + "&p=" + std::to_string(page);
    auto r = http::get(url, http::desktopUserAgent(), 20000);
    if (!r.ok()) {
        error = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
        return result;
    }
    const std::string& html = r.body;
    size_t pos = 0;
    size_t rows = 0;
    while ((pos = html.find("<h4 class=\"qipu-title\">", pos)) != std::string::npos) {
        ++rows;
        size_t a = html.find("<a ", pos);
        if (a == std::string::npos) break;
        size_t hrefKey = html.find("href=\"", a);
        if (hrefKey == std::string::npos) break;
        size_t hrefStart = hrefKey + 6;
        size_t hrefEnd = html.find('"', hrefStart);
        if (hrefEnd == std::string::npos) break;
        std::string href = html.substr(hrefStart, hrefEnd - hrefStart);

        size_t tagEnd = html.find('>', hrefEnd);
        if (tagEnd == std::string::npos) break;
        size_t closeA = html.find("</a>", tagEnd + 1);
        if (closeA == std::string::npos) break;
        std::string title = html.substr(tagEnd + 1, closeA - tagEnd - 1);

        size_t datePos = html.find("qipu-time", closeA);
        std::string date;
        if (datePos != std::string::npos) {
            size_t gt = html.find('>', datePos);
            size_t lt = (gt == std::string::npos) ? std::string::npos
                                                  : html.find('<', gt + 1);
            if (gt != std::string::npos && lt != std::string::npos) {
                date = util::trim(html.substr(gt + 1, lt - gt - 1));
            }
        }

        std::string chessid = extractDigitsAfter(href, "chessid=");
        if (!chessid.empty()) {
            Game g;
            g.chessid = chessid;
            g.title = util::htmlUnescape(util::stripTags(title));
            parseTitlePlayers(g);
            g.starttime = date.size() == 16 ? date + ":00" : date;
            g.endtime = g.starttime;
            result.games.push_back(std::move(g));
        }
        pos = (datePos == std::string::npos) ? closeA : datePos;
    }
    result.ended = rows == 0;
    return result;
}

QipuPage FoxClient::fetchQipuMeta(const std::string& uid, int type,
                                  const std::string& lastCode,
                                  std::string& error) {
    QipuPage result;
    std::string url =
        "https://www.foxwq.com/newqipu/requestQp?type=" + std::to_string(type) +
        "&lastCode=" + util::urlEncode(lastCode) +
        "&GradeLow=0&GradeHigh=0&FindUserName=&uid=" + util::urlEncode(uid);
    auto r = http::get(url, http::desktopUserAgent(), 20000);
    if (!r.ok()) {
        error = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
        return result;
    }
    if (r.body.find("[foxwq_qp]") == std::string::npos) {
        error = "棋谱库详情接口返回格式异常";
        return result;
    }
    std::string body = r.body.substr(r.body.find("[foxwq_qp]") + 10);
    size_t endPos = body.find("[foxwq_end]");
    if (endPos != std::string::npos) body.erase(endPos);

    for (const std::string& chunk : util::split(body, "[foxwq_game_detail_list]")) {
        auto fields = util::split(chunk, "[foxwq_game_detail]");
        if (fields.size() < 21 || util::trim(fields[0]).empty()) continue;
        Game g;
        g.chessid = util::trim(fields[0]);
        g.qipuId = g.chessid;
        g.blackUid = util::trim(fields[1]);
        g.blackDan = std::atoi(fields[2].c_str());
        g.blackName = util::trim(fields[3]);
        g.blackEnName = util::trim(fields[4]);
        g.whiteUid = util::trim(fields[6]);
        g.whiteDan = std::atoi(fields[7].c_str());
        g.whiteName = util::trim(fields[8]);
        g.whiteEnName = util::trim(fields[9]);
        g.winner = std::atoi(fields[11].c_str());
        g.point = std::atoi(fields[12].c_str());
        g.moves = std::atoi(fields[14].c_str());
        g.endtime = util::trim(fields[20]);
        g.starttime = qipuStartTime(g.chessid, g.endtime);
        if (fields.size() > 21) g.title = util::trim(fields[21]);
        result.games.push_back(std::move(g));
    }
    result.ended = result.games.empty();
    return result;
}

std::string FoxClient::fetchSgf(const std::string& chessid, std::string& error) {
    std::string form = "chessid=" + util::urlEncode(chessid);
    const char* cgiUrls[] = {
        "http://happyapp.huanle.qq.com/cgi-bin/CommonMobileCGI/TXWQFetchChess",
        "http://cgi.foxwq.com/cgi-bin/CommonMobileCGI/TXWQFetchChess",
    };
    std::string lastError;
    for (const char* url : cgiUrls) {
        auto r = http::postForm(url, form, "okhttp/3.12.12", 20000);
        if (!r.ok()) {
            lastError = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
            continue;
        }
        json j = json::parse(r.body, nullptr, false);
        if (j.is_discarded()) {
            lastError = "棋谱接口返回了非 JSON 数据";
            continue;
        }
        if (jint(j, "result", -1) == 0) {
            std::string chess = jstr(j, "chess");
            if (!chess.empty()) return util::normalizeSgf(chess);
        }
        lastError = jstr(j, "resultstr");
    }

    std::string h5 =
        "https://h5.foxwq.com/yehuDiamond/chessbook_local/YHWQFetchChess?chessid=" +
        util::urlEncode(chessid);
    auto r = http::get(h5, http::mobileUserAgent(), 20000);
    if (r.ok()) {
        json j = json::parse(r.body, nullptr, false);
        if (!j.is_discarded() && jint(j, "result", -1) == 0) {
            std::string chess = jstr(j, "chess");
            if (!chess.empty()) return util::normalizeSgf(chess);
        }
        if (!j.is_discarded()) lastError = jstr(j, "resultstr");
    } else if (!r.error.empty()) {
        lastError = r.error;
    }
    error = lastError.empty() ? "棋谱获取失败" : lastError;
    return {};
}

std::vector<Game> FoxClient::mergeHistory(const std::vector<Game>& search,
                                          const std::vector<Game>& metaItems) {
    std::unordered_map<std::string, std::vector<size_t>> byTitle;
    for (size_t i = 0; i < metaItems.size(); ++i) {
        std::string key = titleKey(metaItems[i].title);
        if (!key.empty()) {
            key.resize(std::min<size_t>(key.size(), 16));
            byTitle[key].push_back(i);
        }
    }

    std::vector<Game> merged;
    merged.reserve(search.size());
    for (const Game& item : search) {
        const Game* found = nullptr;
        std::string key = titleKey(item.title);
        if (key.size() > 16) key.resize(16);
        auto it = byTitle.find(key);
        if (it != byTitle.end()) {
            int itemDay = dateOrdinal(item.starttime);
            int bestHits = -1;
            int bestDiff = 9999;
            for (size_t idx : it->second) {
                int metaDay = dateOrdinal(metaItems[idx].starttime);
                int diff = std::abs(itemDay - metaDay);
                if (diff > 2) continue;
                int hits = playerHits(item.title, metaItems[idx]);
                if (hits > bestHits || (hits == bestHits && diff < bestDiff)) {
                    bestHits = hits;
                    bestDiff = diff;
                    found = &metaItems[idx];
                }
            }
        }
        if (!found) {
            int itemDay = dateOrdinal(item.starttime);
            for (const Game& m : metaItems) {
                int metaDay = dateOrdinal(m.starttime);
                if (std::abs(itemDay - metaDay) > 2) continue;
                if (titleMentionsPlayers(item.title, m)) {
                    found = &m;
                    break;
                }
            }
        }
        if (found) {
            Game g = *found;
            g.chessid = item.chessid;
            g.qipuId = found->chessid;
            g.title = item.title.empty() ? found->title : item.title;
            g.starttime = item.starttime.empty() ? found->starttime : item.starttime;
            g.endtime = item.starttime.empty() ? found->endtime : item.starttime;
            if (!item.blackName.empty() && !item.whiteName.empty()) {
                g.blackName = item.blackName;
                g.whiteName = item.whiteName;
            }
            merged.push_back(std::move(g));
        } else {
            merged.push_back(item);
        }
    }
    return merged;
}
