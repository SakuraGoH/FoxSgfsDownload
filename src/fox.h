#pragma once

#include "model.h"

#include <optional>
#include <string>
#include <vector>

struct QipuPage {
    std::vector<Game> games;
    bool ended = false;
};

class FoxClient {
public:
    std::optional<UserInfo> queryUser(const std::string& nickname, std::string& error);
    std::vector<Game> fetchLatest(const std::string& uid, std::string& error);
    QipuPage fetchQipuSearch(const std::string& keyword, int page, std::string& error);
    QipuPage fetchQipuMeta(const std::string& uid, int type,
                           const std::string& lastCode, std::string& error);
    std::string fetchSgf(const std::string& chessid, std::string& error);

    static std::vector<Game> mergeHistory(const std::vector<Game>& search,
                                          const std::vector<Game>& meta);
};
