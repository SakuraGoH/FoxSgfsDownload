#include "fox.h"
#include "util.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>

using Clock = std::chrono::steady_clock;

static long long msSince(const Clock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now() - start)
        .count();
}

int main(int argc, char** argv) {
    std::string query = "柯洁";
    std::string uid = "6757425";
    int delayMs = 0;
    int runs = 3;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--query" && i + 1 < argc) query = argv[++i];
        else if (arg == "--uid" && i + 1 < argc) uid = argv[++i];
        else if (arg == "--delay" && i + 1 < argc) delayMs = std::atoi(argv[++i]);
        else if (arg == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
    }

    for (int run = 1; run <= runs; ++run) {
        FoxClient fox;
        std::string err;
        auto totalStart = Clock::now();

        auto resolveStart = Clock::now();
        auto user = fox.queryUser(query, err);
        long long resolveMs = msSince(resolveStart);
        if (!user) {
            std::cout << "{\"run\":" << run << ",\"error\":\""
                      << err << "\"}\n";
            continue;
        }
        std::string targetUid = user->uid;
        if (!uid.empty()) targetUid = uid;

        auto h5Start = Clock::now();
        auto latest = fox.fetchLatest(targetUid, err);
        long long h5Ms = msSince(h5Start);

        auto searchStart = Clock::now();
        std::vector<Game> searchItems;
        std::unordered_set<std::string> seen;
        for (const Game& g : latest) seen.insert(g.chessid);
        int searchPages = 0;
        for (int page = 1; page <= 500; ++page) {
            auto data = fox.fetchQipuSearch(user->name, page, err);
            if (data.games.empty()) break;
            ++searchPages;
            for (Game& g : data.games) {
                if (g.chessid.empty() || seen.count(g.chessid)) continue;
                seen.insert(g.chessid);
                searchItems.push_back(std::move(g));
            }
            if (delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }
        long long searchMs = msSince(searchStart);

        auto metaStart = Clock::now();
        std::vector<Game> meta;
        int metaPages = 0;
        for (int type : {1, 6}) {
            std::string last = "0";
            for (int page = 1; page <= 500; ++page) {
                auto data = fox.fetchQipuMeta(targetUid, type, last, err);
                if (data.games.empty()) break;
                ++metaPages;
                meta.insert(meta.end(), data.games.begin(), data.games.end());
                last = data.games.back().chessid;
                if (delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }
            }
        }
        long long metaMs = msSince(metaStart);

        auto mergeStart = Clock::now();
        auto merged = FoxClient::mergeHistory(searchItems, meta);
        long long mergeMs = msSince(mergeStart);
        long long totalMs = msSince(totalStart);

        std::cout << "{"
                  << "\"run\":" << run
                  << ",\"resolve_ms\":" << resolveMs
                  << ",\"h5_ms\":" << h5Ms
                  << ",\"h5_count\":" << latest.size()
                  << ",\"search_ms\":" << searchMs
                  << ",\"search_pages\":" << searchPages
                  << ",\"search_count\":" << searchItems.size()
                  << ",\"meta_ms\":" << metaMs
                  << ",\"meta_pages\":" << metaPages
                  << ",\"meta_count\":" << meta.size()
                  << ",\"merge_ms\":" << mergeMs
                  << ",\"total_ms\":" << totalMs
                  << ",\"total_count\":" << (latest.size() + searchItems.size())
                  << ",\"merged_count\":" << merged.size()
                  << "}\n";
        std::cout.flush();
    }
    return 0;
}
