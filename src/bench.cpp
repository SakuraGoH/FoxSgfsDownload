#include "fox.h"
#include "util.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

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
    bool parallel = false;
    int workers = 4;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--query" && i + 1 < argc) query = argv[++i];
        else if (arg == "--uid" && i + 1 < argc) uid = argv[++i];
        else if (arg == "--delay" && i + 1 < argc) delayMs = std::atoi(argv[++i]);
        else if (arg == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (arg == "--parallel") parallel = true;
        else if (arg == "--workers" && i + 1 < argc) workers = std::max(1, std::atoi(argv[++i]));
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

        long long searchMs = 0;
        long long metaMs = 0;
        int searchPages = 0;
        int metaPages = 0;
        std::vector<Game> searchItems;
        std::vector<Game> meta;
        if (parallel) {
            auto searchStart = Clock::now();
            auto metaStart = Clock::now();
            std::atomic<int> nextPage{1};
            std::atomic<int> doneSearchPages{0};
            std::atomic<int> doneMetaPages{0};
            std::atomic<bool> ended{false};
            std::mutex searchMutex;
            std::mutex metaMutex;
            std::unordered_set<std::string> seen;
            for (const Game& g : latest) seen.insert(g.chessid);

            auto searchWorker = [&]() {
                FoxClient client;
                while (!ended.load()) {
                    int page = nextPage.fetch_add(1);
                    if (page > 500) break;
                    std::string e;
                    auto data = client.fetchQipuSearch(user->name, page, e);
                    if (data.games.empty()) {
                        ended.store(true);
                        break;
                    }
                    {
                        std::lock_guard<std::mutex> lock(searchMutex);
                        for (Game& g : data.games) {
                            if (g.chessid.empty() || seen.count(g.chessid)) continue;
                            seen.insert(g.chessid);
                            searchItems.push_back(std::move(g));
                        }
                    }
                    ++doneSearchPages;
                    if (delayMs > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    }
                }
            };

            auto metaWorker = [&](int type) {
                FoxClient client;
                std::string last = "0";
                for (int page = 1; page <= 500; ++page) {
                    std::string e;
                    auto data = client.fetchQipuMeta(targetUid, type, last, e);
                    if (data.games.empty()) break;
                    {
                        std::lock_guard<std::mutex> lock(metaMutex);
                        meta.insert(meta.end(), data.games.begin(), data.games.end());
                    }
                    last = data.games.back().chessid;
                    ++doneMetaPages;
                    if (delayMs > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    }
                }
            };

            std::vector<std::thread> threads;
            threads.emplace_back(metaWorker, 1);
            threads.emplace_back(metaWorker, 6);
            for (int i = 0; i < workers; ++i) threads.emplace_back(searchWorker);
            for (auto& t : threads) t.join();
            searchPages = doneSearchPages.load();
            metaPages = doneMetaPages.load();
            searchMs = msSince(searchStart);
            metaMs = msSince(metaStart);
        } else {
            auto searchStart = Clock::now();
            std::unordered_set<std::string> seen;
            for (const Game& g : latest) seen.insert(g.chessid);
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
            searchMs = msSince(searchStart);

            auto metaStart = Clock::now();
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
            metaMs = msSince(metaStart);
        }

        auto mergeStart = Clock::now();
        auto merged = FoxClient::mergeHistory(searchItems, meta);
        long long mergeMs = msSince(mergeStart);
        long long totalMs = msSince(totalStart);

        std::cout << "{"
                  << "\"run\":" << run
                  << ",\"mode\":\"" << (parallel ? "parallel" : "sequential") << "\""
                  << ",\"workers\":" << (parallel ? workers : 1)
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
