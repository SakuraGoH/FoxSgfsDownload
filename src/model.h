#pragma once

#include <string>
#include "util.h"

struct Game {
    std::string chessid;
    std::string qipuId;
    std::string title;
    std::string starttime;
    std::string endtime;
    std::string blackUid;
    std::string whiteUid;
    std::string blackName;
    std::string whiteName;
    std::string blackEnName;
    std::string whiteEnName;
    int blackDan = 0;
    int whiteDan = 0;
    int winner = 0;
    int point = 0;
    int rule = 0;
    int moves = 0;

    bool selected = false;
    bool downloaded = false;
    std::string status;

    bool isHistory() const { return !qipuId.empty(); }

    bool isWin(const std::string& myUid) const {
        if (myUid.empty()) return false;
        if (winner == 1) return blackUid == myUid;
        if (winner == 2) return whiteUid == myUid;
        return false;
    }

    std::string resultText() const {
        if (winner != 1 && winner != 2) return "其他";
        std::string who = winner == 1 ? "黑" : "白";
        if (point < 0) {
            if (point == -1) return who + "中盘胜";
            if (point == -2) return who + "超时胜";
            return who + "胜";
        }
        double value = point / 100.0;
        std::string unit = rule == 1 ? "子" : "目";
        return who + "胜" + formatPoint(value) + unit;
    }

    static std::string formatPoint(double v) { return util::formatDouble(v, 2); }

    static bool hasHangul(const std::string& s) {
        for (size_t i = 0; i + 2 < s.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 0xEA && c <= 0xED &&
                (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80) {
                return true;
            }
        }
        return false;
    }

    std::string displayBlackName() const {
        if (blackName.empty() || blackName == "???") {
            return blackEnName.empty() ? blackName : blackEnName;
        }
        if (hasHangul(blackName) && !blackEnName.empty()) return blackEnName;
        return blackName;
    }

    std::string displayWhiteName() const {
        if (whiteName.empty() || whiteName == "???" ) {
            return whiteEnName.empty() ? whiteName : whiteEnName;
        }
        if (hasHangul(whiteName) && !whiteEnName.empty()) return whiteEnName;
        return whiteName;
    }

    static std::string rankText(int dan) {
        if (dan >= 100) return "职业" + std::to_string(dan - 99) + "段";
        int rank = dan - 17;
        if (rank > 0) return std::to_string(rank) + "段";
        return std::to_string(-rank + 1) + "级";
    }
};

struct UserInfo {
    std::string uid;
    std::string name;
    std::string englishName;
    std::string gameUin;
};
