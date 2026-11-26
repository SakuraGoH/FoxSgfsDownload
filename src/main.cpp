#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "fox.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// D3D11
// ---------------------------------------------------------------------------
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_width = 0;
static UINT g_height = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &sd,
        &g_swapChain, &g_device, &got, &g_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
            static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &sd,
            &g_swapChain, &g_device, &got, &g_context);
    }
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// ---------------------------------------------------------------------------
// Shared state / events
// ---------------------------------------------------------------------------
enum class EvType {
    Status,
    UserInfo,
    OpponentInfo,
    AddGames,
    MetaReady,
    SearchDone,
    SearchError,
    DownloadProgress,
    DownloadDone,
};

struct Event {
    EvType type = EvType::Status;
    std::string text;
    std::string uid;
    std::string chessid;
    std::vector<Game> games;
    bool success = false;
    int done = 0;
    int failed = 0;
    int skipped = 0;
    int total = 0;
};

struct Shared {
    std::mutex mutex;
    std::deque<Event> queue;
    std::atomic<bool> searchStop{false};
    std::atomic<bool> downloadStop{false};
    std::atomic<bool> searching{false};
    std::atomic<bool> downloading{false};
};

static void pushEvent(const std::shared_ptr<Shared>& shared, Event e) {
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->queue.push_back(std::move(e));
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
struct Settings {
    int mode = 0;
    std::string query;
    std::string opponent;
    bool fullHistory = true;
    int pageSize = 25;
    int concurrency = 3;
    int intervalMs = 500;
    std::string outputDir;
};

static std::string settingsPath() {
    return util::joinPath(util::exeDirectory(), "foxsgfsdownload_max.json");
}

static std::string defaultOutputDir() {
    return util::joinPath(util::exeDirectory(), "downloads");
}

static void loadSettings(Settings& s) {
    s.outputDir = defaultOutputDir();
    std::string data;
    if (!util::readFile(settingsPath(), data)) return;
    json j = json::parse(data, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    s.mode = j.value("mode", s.mode);
    s.query = j.value("query", s.query);
    s.opponent = j.value("opponent", s.opponent);
    s.fullHistory = j.value("fullHistory", s.fullHistory);
    s.pageSize = j.value("pageSize", s.pageSize);
    s.concurrency = j.value("concurrency", s.concurrency);
    s.intervalMs = j.value("intervalMs", s.intervalMs);
    s.outputDir = j.value("outputDir", s.outputDir);
}

static void saveSettings(const Settings& s) {
    json j;
    j["mode"] = s.mode;
    j["query"] = s.query;
    j["opponent"] = s.opponent;
    j["fullHistory"] = s.fullHistory;
    j["pageSize"] = s.pageSize;
    j["concurrency"] = s.concurrency;
    j["intervalMs"] = s.intervalMs;
    j["outputDir"] = s.outputDir;
    util::writeFile(settingsPath(), j.dump(2));
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
static const ImU32 kBgTop = IM_COL32(9, 6, 20, 255);
static const ImU32 kBgBottom = IM_COL32(5, 3, 13, 255);
static const ImU32 kPanel = IM_COL32(24, 18, 42, 245);
static const ImU32 kPanelSoft = IM_COL32(31, 23, 54, 245);
static const ImU32 kBorder = IM_COL32(84, 64, 126, 120);
static const ImU32 kGold = IM_COL32(246, 198, 72, 255);
static const ImU32 kGoldSoft = IM_COL32(246, 198, 72, 170);
static const ImU32 kCyan = IM_COL32(92, 227, 255, 255);
static const ImU32 kPurple = IM_COL32(164, 116, 255, 255);
static const ImU32 kText = IM_COL32(238, 234, 250, 255);
static const ImU32 kMuted = IM_COL32(154, 146, 178, 255);
static const ImU32 kGreen = IM_COL32(93, 224, 154, 255);
static const ImU32 kRed = IM_COL32(255, 112, 125, 255);

static ImVec4 V4(ImU32 c) {
    return ImVec4(((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                  ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                  ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                  ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

static ImU32 WithAlpha(ImU32 c, int alpha) {
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

static void applyTheme(float dpi) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(12, 7);
    style.CellPadding = ImVec2(9, 6);
    style.ItemSpacing = ImVec2(9, 8);
    style.ItemInnerSpacing = ImVec2(7, 5);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 20;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 10;

    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.TabBorderSize = 0;

    style.WindowRounding = 0;
    style.ChildRounding = 16;
    style.FrameRounding = 10;
    style.PopupRounding = 14;
    style.ScrollbarRounding = 12;
    style.GrabRounding = 9;
    style.TabRounding = 9;

    style.Colors[ImGuiCol_Text] = V4(kText);
    style.Colors[ImGuiCol_TextDisabled] = V4(kMuted);
    style.Colors[ImGuiCol_WindowBg] = V4(kBgTop);
    style.Colors[ImGuiCol_ChildBg] = V4(kPanel);
    style.Colors[ImGuiCol_PopupBg] = V4(IM_COL32(20, 15, 36, 252));
    style.Colors[ImGuiCol_Border] = V4(kBorder);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_FrameBg] = V4(IM_COL32(38, 28, 64, 255));
    style.Colors[ImGuiCol_FrameBgHovered] = V4(IM_COL32(52, 38, 84, 255));
    style.Colors[ImGuiCol_FrameBgActive] = V4(IM_COL32(64, 46, 102, 255));
    style.Colors[ImGuiCol_TitleBg] = V4(kPanel);
    style.Colors[ImGuiCol_TitleBgActive] = V4(kPanelSoft);
    style.Colors[ImGuiCol_TitleBgCollapsed] = V4(kPanel);
    style.Colors[ImGuiCol_MenuBarBg] = V4(kPanel);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ScrollbarGrab] = V4(IM_COL32(106, 82, 150, 255));
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = V4(IM_COL32(136, 106, 190, 255));
    style.Colors[ImGuiCol_ScrollbarGrabActive] = V4(kPurple);
    style.Colors[ImGuiCol_CheckMark] = V4(kGold);
    style.Colors[ImGuiCol_SliderGrab] = V4(kGold);
    style.Colors[ImGuiCol_SliderGrabActive] = V4(IM_COL32(255, 225, 140, 255));
    style.Colors[ImGuiCol_Button] = V4(IM_COL32(50, 38, 80, 255));
    style.Colors[ImGuiCol_ButtonHovered] = V4(IM_COL32(70, 52, 110, 255));
    style.Colors[ImGuiCol_ButtonActive] = V4(IM_COL32(92, 68, 142, 255));
    style.Colors[ImGuiCol_Header] = V4(IM_COL32(54, 40, 88, 255));
    style.Colors[ImGuiCol_HeaderHovered] = V4(IM_COL32(70, 52, 112, 255));
    style.Colors[ImGuiCol_HeaderActive] = V4(IM_COL32(92, 68, 142, 255));
    style.Colors[ImGuiCol_Separator] = V4(IM_COL32(86, 66, 126, 140));
    style.Colors[ImGuiCol_SeparatorHovered] = V4(kGoldSoft);
    style.Colors[ImGuiCol_SeparatorActive] = V4(kGold);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Tab] = V4(IM_COL32(42, 32, 70, 255));
    style.Colors[ImGuiCol_TabHovered] = V4(IM_COL32(70, 52, 110, 255));
    style.Colors[ImGuiCol_TabSelected] = V4(IM_COL32(60, 46, 96, 255));
    style.Colors[ImGuiCol_PlotLines] = V4(kCyan);
    style.Colors[ImGuiCol_PlotHistogram] = V4(kGold);
    style.Colors[ImGuiCol_TableHeaderBg] = V4(IM_COL32(33, 24, 56, 255));
    style.Colors[ImGuiCol_TableBorderStrong] = V4(IM_COL32(88, 68, 128, 130));
    style.Colors[ImGuiCol_TableBorderLight] = V4(IM_COL32(64, 48, 96, 90));
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_TableRowBgAlt] = V4(IM_COL32(255, 255, 255, 5));
    style.Colors[ImGuiCol_TextSelectedBg] = V4(IM_COL32(246, 198, 72, 70));
    style.Colors[ImGuiCol_NavCursor] = V4(kGold);
    style.ScaleAllSizes(dpi);
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static ImFont* g_fontBody = nullptr;
static ImFont* g_fontBold = nullptr;
static ImFont* g_fontTitle = nullptr;
static ImFont* g_fontSmall = nullptr;

static ImFont* tryLoadFont(const char* path, float size, const ImWchar* ranges,
                           bool bold) {
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    cfg.RasterizerMultiply = bold ? 1.08f : 1.0f;
    cfg.FontNo = 0;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &cfg, ranges);
}

static void loadFonts(float dpi) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    float s = std::min(dpi, 1.15f);
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();
    const char* paths[] = {
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    for (const char* p : paths) {
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) continue;
        g_fontBody = tryLoadFont(p, 18.0f * s, ranges, false);
        if (g_fontBody) break;
    }
    const char* titlePaths[] = {
        "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\msyh.ttc",
    };
    for (const char* p : titlePaths) {
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) continue;
        g_fontTitle = tryLoadFont(p, 24.0f * s, ranges, true);
        if (g_fontTitle) break;
    }
    if (!g_fontBody) g_fontBody = io.Fonts->AddFontDefault();
    if (!g_fontTitle) g_fontTitle = g_fontBody;
    g_fontBold = g_fontBody;
    g_fontSmall = g_fontBody;
    io.FontDefault = g_fontBody;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static ImVec2 polar(ImVec2 c, float r, float deg) {
    float a = deg * 3.14159265358979323846f / 180.0f;
    return ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a));
}

static void drawMagicCircle(ImDrawList* dl, ImVec2 c, float r, float angle,
                            int alpha, bool stones) {
    int a1 = alpha;
    int a2 = static_cast<int>(alpha * 0.75f);
    ImU32 gold = WithAlpha(kGold, a1);
    ImU32 cyan = WithAlpha(kCyan, a2);
    ImU32 violet = WithAlpha(kPurple, a2);

    dl->AddCircle(c, r, gold, 96, std::max(1.0f, r * 0.035f));
    dl->AddCircle(c, r * 0.86f, cyan, 96, std::max(1.0f, r * 0.012f));
    dl->AddCircle(c, r * 0.79f, violet, 96, std::max(1.0f, r * 0.01f));

    // 符文圈
    for (int i = 0; i < 48; ++i) {
        float deg = i * 7.5f + angle;
        ImVec2 p1 = polar(c, r * 0.90f, deg);
        ImVec2 p2 = polar(c, r * 0.94f, deg);
        dl->AddLine(p1, p2, (i % 3) ? gold : cyan, std::max(1.0f, r * 0.008f));
    }

    // 八角星
    ImVec2 oct[17];
    for (int i = 0; i < 16; ++i) {
        float rr = (i % 2 == 0) ? r * 0.74f : r * 0.54f;
        oct[i] = polar(c, rr, -90.0f + i * 22.5f + angle);
    }
    oct[16] = oct[0];
    dl->AddPolyline(oct, 17, gold, ImDrawFlags_None, std::max(1.0f, r * 0.018f));

    // 五芒星
    ImVec2 pent[6];
    for (int i = 0; i < 5; ++i) {
        pent[i] = polar(c, r * 0.60f, -90.0f + i * 144.0f + angle * 0.8f);
    }
    pent[5] = pent[0];
    dl->AddPolyline(pent, 6, violet, ImDrawFlags_None, std::max(1.0f, r * 0.012f));

    if (stones) {
        float stoneR = r * 0.115f;
        ImVec2 blackC(c.x - r * 0.09f, c.y + r * 0.045f);
        ImVec2 whiteC(c.x + r * 0.09f, c.y - r * 0.045f);
        dl->AddCircleFilled(blackC, stoneR, IM_COL32(8, 8, 18, a1), 32);
        dl->AddCircle(blackC, stoneR, gold, 32, std::max(1.0f, r * 0.012f));
        dl->AddCircleFilled(whiteC, stoneR, IM_COL32(247, 233, 199, a1), 32);
        dl->AddCircle(whiteC, stoneR, gold, 32, std::max(1.0f, r * 0.012f));
        dl->AddCircleFilled(ImVec2(whiteC.x - stoneR * 0.3f, whiteC.y - stoneR * 0.35f),
                            stoneR * 0.18f, IM_COL32(255, 255, 255, a1), 16);
    }
}

static void drawBackground(ImDrawList* dl, ImVec2 size, double time) {
    dl->AddRectFilledMultiColor(
        ImVec2(0, 0), size,
        kBgTop, IM_COL32(14, 9, 30, 255),
        IM_COL32(8, 5, 18, 255), kBgBottom);
    // 顶部紫色辉光
    for (int i = 12; i >= 1; --i) {
        float t = i / 12.0f;
        dl->AddCircleFilled(ImVec2(size.x * 0.78f, size.y * 0.05f),
                            size.x * 0.28f * t,
                            IM_COL32(94, 56, 200, static_cast<int>(4 + 4 * (1 - t))),
                            64);
    }
    // 左下青色辉光
    for (int i = 10; i >= 1; --i) {
        float t = i / 10.0f;
        dl->AddCircleFilled(ImVec2(size.x * 0.02f, size.y * 0.92f),
                            size.x * 0.22f * t,
                            IM_COL32(40, 120, 180, static_cast<int>(2 + 3 * (1 - t))),
                            64);
    }
    drawMagicCircle(dl, ImVec2(size.x * 0.86f, size.y * 0.52f),
                    size.x * 0.205f, static_cast<float>(time * 4.0), 16, false);
    drawMagicCircle(dl, ImVec2(size.x * 0.10f, size.y * 0.18f),
                    size.x * 0.085f, static_cast<float>(-time * 3.0), 10, true);
}

static void textSmall(const char* text, ImU32 color = kMuted) {
    ImGui::PushFont(g_fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, V4(color));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static void textBody(const std::string& text, ImU32 color = kText) {
    ImGui::PushStyleColor(ImGuiCol_Text, V4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

static void textBold(const std::string& text, float size = 0, ImU32 color = kText) {
    ImGui::PushFont(size > 24 ? g_fontTitle : g_fontBold);
    ImGui::PushStyleColor(ImGuiCol_Text, V4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static bool accentButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, V4(kGold));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V4(IM_COL32(255, 224, 138, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V4(IM_COL32(220, 170, 50, 255)));
    ImGui::PushStyleColor(ImGuiCol_Text, V4(IM_COL32(20, 14, 8, 255)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

static bool ghostButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.03f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.09f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Border, V4(IM_COL32(120, 96, 168, 150)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return clicked;
}

static bool beginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, V4(kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    return ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_NoScrollbar);
}

static void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
static void dbg(const char* msg);

class App {
public:
    explicit App(HWND hwnd) : hwnd_(hwnd), shared_(std::make_shared<Shared>()) {
        dbg("ctor start");
        loadSettings(settings_);
        dbg("ctor settings");
        mode_ = settings_.mode;
        fullHistory_ = settings_.fullHistory;
        pageSize_ = settings_.pageSize;
        concurrency_ = settings_.concurrency;
        intervalMs_ = settings_.intervalMs;
        outputDir_ = settings_.outputDir;
        dbg("ctor output");
        dbg(("ctor output: " + outputDir_).c_str());
        std::snprintf(query_, sizeof(query_), "%s", settings_.query.c_str());
        std::snprintf(opponent_, sizeof(opponent_), "%s", settings_.opponent.c_str());
        dbg("ctor query");
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(util::utf8ToWide(outputDir_)), ec);
        dbg(ec ? ("ctor mkdir error: " + ec.message()).c_str() : "ctor mkdir ok");
        status_ = "就绪 · 输入昵称或 UID 开始查询";
        dbg("ctor done");
    }

    ~App() { saveSettings(settings_); }

    void frame();
    void saveNow() { saveSettings(settings_); }
    void setAutoQuery(const std::string& query, bool uid, bool download = false,
                      const std::string& opponent = "") {
        std::snprintf(query_, sizeof(query_), "%s", query.c_str());
        std::snprintf(opponent_, sizeof(opponent_), "%s", opponent.c_str());
        mode_ = uid ? 1 : 0;
        autoSearchPending_ = true;
        autoDownload_ = download;
    }
    void setAutoTree(bool download = false) {
        mode_ = 2;
        autoSearchPending_ = true;
        autoDownload_ = download;
    }

private:
    HWND hwnd_ = nullptr;
    std::shared_ptr<Shared> shared_;

    Settings settings_;
    std::vector<Game> games_;
    std::unordered_map<std::string, size_t> index_;
    std::vector<size_t> filtered_;

    std::string currentUser_;
    std::string currentUid_;
    std::string opponentUid_;
    std::string opponentName_;
    std::string opponentEnglish_;
    size_t matchCount_ = 0;
    std::string status_;
    std::string toast_;
    double toastUntil_ = 0.0;
    std::string errorText_;
    bool showError_ = false;
    bool showHelp_ = false;
    bool focusQuery_ = true;
    bool autoSearchPending_ = false;
    bool autoDownload_ = false;

    int mode_ = 0;
    char query_[256] = {};
    char opponent_[256] = {};
    char keyword_[256] = {};
    char jump_[16] = {};
    int sortMode_ = 0;
    int page_ = 1;
    int pageSize_ = 25;
    int searchEpoch_ = 0;
    int treeDepth_ = 1;
    int lastDrawnPage_ = -1;
    int lastDrawnEpoch_ = -1;
    size_t lastFilteredSize_ = 0;
    bool resetScroll_ = true;
    bool fullHistory_ = true;

    int historyCount_ = 0;
    std::string timeRange_ = "—";
    int downloadTotal_ = 0;
    int downloadDone_ = 0;
    int downloadFailed_ = 0;
    int downloadSkipped_ = 0;
    int concurrency_ = 3;
    int intervalMs_ = 500;
    std::string outputDir_;

    void pollEvents();
    void startSearch();
    void startTreeSearch();
    void startDownload(std::vector<Game> list);
    void downloadOne(const Game& game);
    bool opponentFilterActive() const;
    bool gameMatchesOpponent(const Game& g) const;
    void fillKnownNames(Game& g) const;
    void rebuildFiltered(bool keepPage);
    void resetForSearch();
    void applyMeta(const std::vector<Game>& meta);
    void toast(const std::string& text);
    void chooseOutputDir();

    void drawHeader();
    void drawSearchCard();
    void drawStats();
    void drawFilterBar();
    void drawTable();
    void drawBottomBar();
    void drawOverlays();

    int pageCount() const;
    bool pageAllSelected() const;
    void setPageSelected(bool value);
    std::vector<Game> collectSelected() const;
    std::vector<Game> collectFiltered() const;
    void refreshStats();
};

int App::pageCount() const {
    if (filtered_.empty()) return 1;
    return static_cast<int>((filtered_.size() + pageSize_ - 1) / pageSize_);
}

bool App::opponentFilterActive() const {
    return !opponentUid_.empty() || !opponentName_.empty();
}

void App::fillKnownNames(Game& g) const {
    const std::string oppDisplay =
        opponentEnglish_.empty() ? opponentName_ : opponentEnglish_;
    auto fill = [&](std::string& name, const std::string& uid) {
        if (!name.empty() && name != "???") return;
        if (!currentUid_.empty() && uid == currentUid_ &&
            !currentUser_.empty() && currentUser_ != currentUid_) {
            name = currentUser_;
        } else if (!opponentUid_.empty() && uid == opponentUid_ &&
                   !oppDisplay.empty()) {
            name = oppDisplay;
        }
    };
    fill(g.blackName, g.blackUid);
    fill(g.whiteName, g.whiteUid);
}

bool App::gameMatchesOpponent(const Game& g) const {
    if (!opponentFilterActive()) return true;
    if (!opponentUid_.empty() &&
        (g.blackUid == opponentUid_ || g.whiteUid == opponentUid_)) {
        return true;
    }
    auto nameEquals = [&](const std::string& name) {
        if (name.empty()) return false;
        if (!opponentName_.empty() && name == opponentName_) return true;
        if (!opponentEnglish_.empty() &&
            util::lowerAscii(name) == util::lowerAscii(opponentEnglish_)) {
            return true;
        }
        return false;
    };
    if (nameEquals(g.blackName) || nameEquals(g.whiteName)) return true;
    if (nameEquals(g.displayBlackName()) || nameEquals(g.displayWhiteName())) {
        return true;
    }

    // 棋谱库列表项只有标题，标题里通常包含“A执黑胜B”这样的双方名字。
    std::string title = util::lowerAscii(g.title);
    if (!opponentName_.empty() &&
        title.find(util::lowerAscii(opponentName_)) != std::string::npos) {
        return true;
    }
    if (!opponentEnglish_.empty() &&
        title.find(util::lowerAscii(opponentEnglish_)) != std::string::npos) {
        return true;
    }
    return false;
}

void App::rebuildFiltered(bool keepPage) {
    std::string key = util::lowerAscii(util::trim(keyword_));
    filtered_.clear();
    filtered_.reserve(games_.size());
    for (size_t i = 0; i < games_.size(); ++i) {
        const Game& g = games_[i];
        if (!gameMatchesOpponent(g)) continue;
        if (!key.empty()) {
            std::string hay = util::lowerAscii(
                g.displayBlackName() + " " + g.displayWhiteName() + " " +
                g.chessid + " " + g.title);
            if (hay.find(key) == std::string::npos) continue;
        }
        filtered_.push_back(i);
    }
    std::stable_sort(filtered_.begin(), filtered_.end(), [&](size_t a, size_t b) {
        const Game& ga = games_[a];
        const Game& gb = games_[b];
        if (ga.starttime == gb.starttime) return ga.chessid > gb.chessid;
        return sortMode_ == 0 ? ga.starttime > gb.starttime
                              : ga.starttime < gb.starttime;
    });
    if (!keepPage) page_ = 1;
    int pc = pageCount();
    if (page_ > pc) page_ = pc;
    if (page_ < 1) page_ = 1;
    if (filtered_.size() < lastFilteredSize_) resetScroll_ = true;
    lastFilteredSize_ = filtered_.size();
    refreshStats();
}

void App::refreshStats() {
    historyCount_ = 0;
    matchCount_ = 0;
    std::string minTime, maxTime;
    for (const Game& g : games_) {
        if (!gameMatchesOpponent(g)) continue;
        ++matchCount_;
        if (g.isHistory()) ++historyCount_;
        if (!g.starttime.empty()) {
            if (minTime.empty() || g.starttime < minTime) minTime = g.starttime;
            if (maxTime.empty() || g.starttime > maxTime) maxTime = g.starttime;
        }
    }
    if (minTime.empty()) {
        timeRange_ = "—";
    } else {
        timeRange_ = minTime.substr(0, 10) + " ~ " + maxTime.substr(0, 10);
    }
}

bool App::pageAllSelected() const {
    if (filtered_.empty()) return false;
    int start = (page_ - 1) * pageSize_;
    int end = std::min<int>(static_cast<int>(filtered_.size()), start + pageSize_);
    if (start >= end) return false;
    for (int i = start; i < end; ++i) {
        if (!games_[filtered_[i]].selected) return false;
    }
    return true;
}

void App::setPageSelected(bool value) {
    int start = (page_ - 1) * pageSize_;
    int end = std::min<int>(static_cast<int>(filtered_.size()), start + pageSize_);
    for (int i = start; i < end; ++i) games_[filtered_[i]].selected = value;
}

std::vector<Game> App::collectSelected() const {
    std::vector<Game> out;
    for (const Game& g : games_) if (g.selected) out.push_back(g);
    return out;
}

std::vector<Game> App::collectFiltered() const {
    std::vector<Game> out;
    out.reserve(filtered_.size());
    for (size_t i : filtered_) out.push_back(games_[i]);
    return out;
}

void App::toast(const std::string& text) {
    toast_ = text;
    toastUntil_ = ImGui::GetTime() + 4.0;
}

void App::pollEvents() {
    std::deque<Event> local;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        local.swap(shared_->queue);
    }
    bool changed = false;
    for (Event& e : local) {
        switch (e.type) {
        case EvType::Status:
            status_ = e.text;
            break;
        case EvType::UserInfo:
            currentUser_ = e.text;
            currentUid_ = e.uid;
            break;
        case EvType::OpponentInfo:
            opponentUid_ = e.uid;
            opponentName_ = e.text;
            opponentEnglish_ = e.chessid;
            changed = true;
            break;
        case EvType::AddGames: {
            for (Game& g : e.games) {
                if (g.chessid.empty() || index_.count(g.chessid)) continue;
                fillKnownNames(g);
                index_[g.chessid] = games_.size();
                games_.push_back(std::move(g));
                changed = true;
            }
            status_ = "已获取 " + std::to_string(games_.size()) + " 局 · 正在继续拉取…";
            break;
        }
        case EvType::MetaReady:
            applyMeta(e.games);
            changed = true;
            status_ = "历史棋谱信息已补全 · 共 " + std::to_string(games_.size()) + " 局";
            break;
        case EvType::SearchDone:
            status_ = e.text;
            toast(e.text);
            rebuildFiltered(true);
            if (opponentFilterActive()) {
                status_ += " · " + util::trim(opponent_) + " 命中 " +
                           std::to_string(matchCount_) + " 局";
            }
            if (autoDownload_) {
                autoDownload_ = false;
                auto jobs = collectFiltered();
                if (!jobs.empty()) startDownload(jobs);
            }
            break;
        case EvType::SearchError:
            status_ = e.text;
            errorText_ = e.text;
            showError_ = true;
            break;
        case EvType::DownloadProgress: {
            auto it = index_.find(e.chessid);
            if (it != index_.end()) {
                Game& g = games_[it->second];
                g.downloaded = e.success;
                g.status = e.success ? "已下载" : "失败";
                changed = true;
            }
            downloadDone_ = e.done;
            downloadFailed_ = e.failed;
            downloadSkipped_ = e.skipped;
            downloadTotal_ = e.total;
            status_ = e.text;
            break;
        }
        case EvType::DownloadDone:
            downloadDone_ = e.done;
            downloadFailed_ = e.failed;
            downloadSkipped_ = e.skipped;
            downloadTotal_ = e.total;
            status_ = "下载完成：成功 " + std::to_string(e.done) + "，失败 " +
                      std::to_string(e.failed) + "，跳过 " +
                      std::to_string(e.skipped);
            toast(status_);
            break;
        }
    }
    if (changed) rebuildFiltered(true);
}

void App::applyMeta(const std::vector<Game>& meta) {
    for (const Game& m : meta) {
        auto it = index_.find(m.chessid);
        if (it == index_.end()) continue;
        Game& g = games_[it->second];
        bool selected = g.selected;
        bool downloaded = g.downloaded;
        std::string status = g.status;
        g = m;
        fillKnownNames(g);
        g.selected = selected;
        g.downloaded = downloaded;
        g.status = status;
    }
}

void App::resetForSearch() {
    ++searchEpoch_;
    games_.clear();
    index_.clear();
    filtered_.clear();
    lastFilteredSize_ = 0;
    resetScroll_ = true;
    currentUser_.clear();
    currentUid_.clear();
    opponentUid_.clear();
    opponentName_.clear();
    opponentEnglish_.clear();
    matchCount_ = 0;
    historyCount_ = 0;
    timeRange_ = "—";
    downloadTotal_ = downloadDone_ = downloadFailed_ = downloadSkipped_ = 0;
    page_ = 1;
    status_ = "正在连接野狐…";
}

void App::startSearch() {
    if (shared_->searching.load()) return;
    if (mode_ == 2) {
        startTreeSearch();
        return;
    }
    std::string query = util::trim(query_);
    if (query.empty()) {
        toast("请输入昵称或 UID");
        return;
    }
    settings_.query = query;
    settings_.opponent = util::trim(opponent_);
    settings_.mode = mode_;
    settings_.fullHistory = fullHistory_;
    saveSettings(settings_);

    resetForSearch();
    shared_->searchStop.store(false);
    shared_->searching.store(true);
    focusQuery_ = false;

    auto shared = shared_;
    int mode = mode_;
    bool fullHistory = fullHistory_;
    std::string uidHint = mode == 1 ? query : std::string();
    std::string opponentText = util::trim(opponent_);

    std::thread([shared, mode, fullHistory, query, uidHint, opponentText]() {
        FoxClient fox;
        std::string err;
        UserInfo user;
        std::string uid = uidHint;
        if (mode == 0) {
            auto maybe = fox.queryUser(query, err);
            if (!maybe) {
                pushEvent(shared, Event{EvType::SearchError, "查询失败：" + err});
                shared->searching.store(false);
                return;
            }
            user = *maybe;
            uid = user.uid;
            pushEvent(shared, Event{EvType::UserInfo, user.name, user.uid});
            pushEvent(shared, Event{EvType::Status,
                                    "正在拉取最新对局 · " + user.name});
        } else {
            pushEvent(shared, Event{EvType::UserInfo, uid, uid});
            pushEvent(shared, Event{EvType::Status, "正在拉取最新对局…"});
        }

        auto latest = fox.fetchLatest(uid, err);
        std::unordered_set<std::string> seen;
        if (!latest.empty() && user.name.empty()) {
            for (const Game& g : latest) {
                if (g.blackUid == uid && !g.blackName.empty()) {
                    user.name = g.displayBlackName();
                    break;
                }
                if (g.whiteUid == uid && !g.whiteName.empty()) {
                    user.name = g.displayWhiteName();
                    break;
                }
            }
            if (!user.name.empty()) {
                pushEvent(shared, Event{EvType::UserInfo, user.name, uid});
            }
        }

        if (!opponentText.empty()) {
            auto isUid = [](const std::string& s) {
                return !s.empty() &&
                       std::all_of(s.begin(), s.end(), [](unsigned char c) {
                           return std::isdigit(c);
                       });
            };
            std::string oppUid;
            std::string oppName;
            std::string oppEnglish;
            if (isUid(opponentText)) {
                oppUid = opponentText;
                // UID 无法直接反查昵称，先从 A 的最新对局里找 B 的名字。
                for (const Game& g : latest) {
                    if (g.blackUid == oppUid && !g.blackName.empty()) {
                        oppName = g.displayBlackName();
                        oppEnglish = g.blackEnName.empty() ? oppName : g.blackEnName;
                        break;
                    }
                    if (g.whiteUid == oppUid && !g.whiteName.empty()) {
                        oppName = g.displayWhiteName();
                        oppEnglish = g.whiteEnName.empty() ? oppName : g.whiteEnName;
                        break;
                    }
                }
            } else {
                auto opp = fox.queryUser(opponentText, err);
                if (!opp) {
                    pushEvent(shared, Event{EvType::SearchError,
                                            "对手查询失败：" + err});
                    shared->searching.store(false);
                    return;
                }
                oppUid = opp->uid;
                oppName = opp->name;
                oppEnglish = opp->englishName;
            }
            if (oppUid == uid) {
                pushEvent(shared, Event{EvType::SearchError,
                                        "对手不能和查询对象是同一个人"});
                shared->searching.store(false);
                return;
            }
            pushEvent(shared, Event{EvType::OpponentInfo, oppName, oppUid,
                                    oppEnglish});
            pushEvent(shared, Event{EvType::Status,
                                    "对手：" + (oppName.empty() ? oppUid : oppName) +
                                        " · 正在拉取最新对局…"});
        }

        if (!latest.empty()) {
            pushEvent(shared, Event{EvType::AddGames, "", "", "", latest});
            for (const Game& g : latest) seen.insert(g.chessid);
        }

        std::string keyword = user.name;
        if (keyword.empty() && mode == 0) keyword = query;
        if (keyword.empty()) {
            pushEvent(shared, Event{EvType::SearchError,
                                    "没有找到可用的昵称，无法检索棋谱库"});
            shared->searching.store(false);
            return;
        }

        std::vector<std::string> aliases;
        if (!user.name.empty()) aliases.push_back(user.name);
        aliases.push_back(query);
        aliases.push_back(user.englishName);
        // 去重
        {
            std::vector<std::string> tmp;
            for (auto& s : aliases) {
                if (s.empty()) continue;
                if (std::find(tmp.begin(), tmp.end(), s) == tmp.end()) tmp.push_back(s);
            }
            aliases.swap(tmp);
        }

        // 棋谱库详情与列表并行：详情按 type=1/6 两条游标链并发，
        // 列表页之间没有游标依赖，用少量 worker 并发翻页。
        std::vector<Game> meta;
        std::mutex metaMutex;
        auto metaWorker = [&](int type) {
            FoxClient client;
            std::string last = "0";
            for (int page = 1; page <= 500 && !shared->searchStop.load(); ++page) {
                std::string e;
                auto data = client.fetchQipuMeta(uid, type, last, e);
                if (data.games.empty()) break;
                std::string nextLast = data.games.back().chessid;
                size_t metaSize = 0;
                {
                    std::lock_guard<std::mutex> lock(metaMutex);
                    meta.insert(meta.end(), data.games.begin(), data.games.end());
                    metaSize = meta.size();
                }
                last = nextLast;
                if (page % 10 == 0) {
                    pushEvent(shared, Event{EvType::Status,
                                            "棋谱库详情 · 已补全 " +
                                                std::to_string(metaSize) + " 局"});
                }
            }
        };
        std::thread metaA(metaWorker, 1);
        std::thread metaB(metaWorker, 6);

        auto runSearchPages = [&](const std::string& kw) {
            constexpr int kWorkers = 4;
            std::atomic<int> nextPage{1};
            std::atomic<bool> ended{false};
            std::atomic<int> donePages{0};
            std::mutex mutex;
            std::unordered_set<std::string> seenLocal = seen;
            std::vector<Game> out;

            auto worker = [&]() {
                FoxClient client;
                while (!shared->searchStop.load() && !ended.load()) {
                    int page = nextPage.fetch_add(1);
                    if (page > 500) break;
                    std::string e;
                    auto data = client.fetchQipuSearch(kw, page, e);
                    if (data.games.empty()) {
                        ended.store(true);
                        break;
                    }
                    std::vector<Game> batch;
                    size_t total = 0;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        for (const Game& g : data.games) {
                            if (g.chessid.empty() ||
                                !seenLocal.insert(g.chessid).second) {
                                continue;
                            }
                            batch.push_back(g);
                        }
                        out.insert(out.end(), batch.begin(), batch.end());
                        total = out.size();
                    }
                    if (!batch.empty()) {
                        Event ev;
                        ev.type = EvType::AddGames;
                        ev.games = batch;
                        pushEvent(shared, ev);
                    }
                    int pages = ++donePages;
                    if (pages % 4 == 0) {
                        pushEvent(shared, Event{
                            EvType::Status,
                            "棋谱库列表 · 已完成 " + std::to_string(pages) +
                                " 页 · 已获取 " +
                                std::to_string(total + latest.size()) + " 局"});
                    }
                    if (data.ended) ended.store(true);
                }
            };

            std::vector<std::thread> workers;
            for (int i = 0; i < kWorkers; ++i) workers.emplace_back(worker);
            for (auto& t : workers) t.join();
            return out;
        };

        std::vector<Game> searchItems;
        bool got = false;
        for (const std::string& alias : aliases) {
            if (shared->searchStop.load()) break;
            auto out = runSearchPages(alias);
            if (!out.empty()) {
                searchItems = std::move(out);
                keyword = alias;
                got = true;
                break;
            }
        }
        metaA.join();
        metaB.join();
        if (!got) {
            pushEvent(shared, Event{EvType::SearchDone,
                                    "查询完成：共 " +
                                        std::to_string(latest.size()) +
                                        " 局（没有更多棋谱库记录）"});
            shared->searching.store(false);
            return;
        }

        if (!shared->searchStop.load() && !meta.empty()) {
            auto merged = FoxClient::mergeHistory(searchItems, meta);
            Event ev;
            ev.type = EvType::MetaReady;
            ev.games = std::move(merged);
            pushEvent(shared, ev);
        }

        int total = static_cast<int>(latest.size() + searchItems.size());
        std::string msg = shared->searchStop.load()
                              ? "已停止 · 共加载 " + std::to_string(total) + " 局"
                              : "查询完成 · 共 " + std::to_string(total) + " 局";
        pushEvent(shared, Event{EvType::SearchDone, msg});
        shared->searching.store(false);
    }).detach();
}

void App::startTreeSearch() {
    if (shared_->searching.load()) return;
    std::string content;
    std::string rootPath = util::joinPath(util::exeDirectory(), "root.txt");
    if (!util::readFile(rootPath, content)) {
        if (!util::readFile("root.txt", content)) {
            errorText_ = "未找到 root.txt。请把 root.txt 放在 exe 同目录，每行一个昵称或 UID。";
            showError_ = true;
            return;
        }
    }
    if (util::startsWith(content, "\xEF\xBB\xBF")) content.erase(0, 3);
    std::vector<std::string> seeds;
    for (const std::string& raw : util::split(content, "\n")) {
        std::string line = util::trim(raw);
        if (line.empty() || line[0] == '#') continue;
        seeds.push_back(line);
    }
    if (seeds.empty()) {
        errorText_ = "root.txt 中没有可用的昵称或 UID。";
        showError_ = true;
        return;
    }

    resetForSearch();
    currentUser_ = "树搜索";
    settings_.mode = 2;
    saveSettings(settings_);
    shared_->searchStop.store(false);
    shared_->searching.store(true);
    int depthLimit = std::clamp(treeDepth_, 1, 8);

    auto shared = shared_;
    std::thread([shared, seeds, depthLimit]() {
        FoxClient fox;
        std::deque<std::pair<std::string, int>> queue;
        for (const std::string& seed : seeds) queue.push_back({seed, 0});
        std::unordered_set<std::string> resolved;
        std::unordered_set<std::string> seen;
        int expanded = 0;
        int skipped = 0;

        auto isUid = [](const std::string& s) {
            return !s.empty() && std::all_of(s.begin(), s.end(),
                                             [](unsigned char c) { return std::isdigit(c); });
        };

        while (!queue.empty() && !shared->searchStop.load()) {
            auto item = queue.front();
            queue.pop_front();
            const std::string text = item.first;
            int depth = item.second;
            std::string uid = isUid(text) ? text : std::string();
            if (uid.empty()) {
                std::string err;
                auto maybe = fox.queryUser(text, err);
                if (!maybe) {
                    ++skipped;
                    pushEvent(shared, Event{EvType::Status,
                                            "树搜索：跳过 " + text + "（" + err + "）"});
                    continue;
                }
                uid = maybe->uid;
            }
            if (uid.empty() || resolved.count(uid)) continue;
            resolved.insert(uid);

            std::string err;
            auto latest = fox.fetchLatest(uid, err);
            std::vector<Game> fresh;
            for (Game& g : latest) {
                if (g.chessid.empty() || seen.count(g.chessid)) continue;
                seen.insert(g.chessid);
                fresh.push_back(std::move(g));
            }
            if (!fresh.empty()) {
                Event ev;
                ev.type = EvType::AddGames;
                ev.games = fresh;
                pushEvent(shared, ev);
            }
            ++expanded;
            pushEvent(shared, Event{
                EvType::Status,
                "树搜索 · 深度 " + std::to_string(depth) + " · 已展开 " +
                    std::to_string(expanded) + " 人 · 新增 " +
                    std::to_string(fresh.size()) + " · 总 " +
                    std::to_string(seen.size()) + " 局"});

            if (depth + 1 < depthLimit) {
                for (const Game& g : fresh) {
                    const std::string& opp = g.blackUid == uid ? g.whiteUid : g.blackUid;
                    if (!opp.empty() && !resolved.count(opp)) {
                        queue.push_back({opp, depth + 1});
                    }
                }
            }
        }
        std::string msg = shared->searchStop.load()
                              ? "树搜索已停止 · 展开 " + std::to_string(expanded) +
                                    " 人 · 共 " + std::to_string(seen.size()) + " 局"
                              : "树搜索完成 · 展开 " + std::to_string(expanded) +
                                    " 人 · 跳过 " + std::to_string(skipped) +
                                    " · 共 " + std::to_string(seen.size()) + " 局";
        pushEvent(shared, Event{EvType::SearchDone, msg});
        shared->searching.store(false);
    }).detach();
}

void App::startDownload(std::vector<Game> list) {
    if (shared_->downloading.load()) return;
    // 去重
    std::unordered_set<std::string> seen;
    std::vector<Game> pending;
    for (Game& g : list) {
        if (g.chessid.empty() || !seen.insert(g.chessid).second) continue;
        pending.push_back(g);
    }
    if (pending.empty()) {
        toast("没有可下载的对局");
        return;
    }
    std::error_code dirError;
    std::filesystem::create_directories(
        std::filesystem::path(util::utf8ToWide(outputDir_)), dirError);
    if (dirError) {
        errorText_ = "无法创建输出目录：" + outputDir_ + "\n" + dirError.message();
        showError_ = true;
        return;
    }
    shared_->downloadStop.store(false);
    shared_->downloading.store(true);
    downloadTotal_ = static_cast<int>(pending.size());
    downloadDone_ = downloadFailed_ = downloadSkipped_ = 0;
    saveSettings(settings_);

    auto shared = shared_;
    std::string dir = outputDir_;
    int concurrency = std::max(1, concurrency_);
    int interval = std::max(0, intervalMs_);
    std::thread([shared, pending, dir, concurrency, interval]() {
        std::atomic<size_t> next{0};
        std::atomic<int> done{0}, failed{0}, skipped{0};
        int total = static_cast<int>(pending.size());

        auto worker = [&]() {
            FoxClient fox;
            while (!shared->downloadStop.load()) {
                size_t i = next.fetch_add(1);
                if (i >= pending.size()) break;
                const Game g = pending[i];
                std::filesystem::path path(
                    std::filesystem::path(util::utf8ToWide(dir)) /
                    std::filesystem::path(util::utf8ToWide(g.chessid + ".sgf")));
                std::error_code ec;
                if (std::filesystem::exists(path, ec)) {
                    ++skipped;
                    Event ev;
                    ev.type = EvType::DownloadProgress;
                    ev.chessid = g.chessid;
                    ev.success = true;
                    ev.done = done.load();
                    ev.failed = failed.load();
                    ev.skipped = skipped.load();
                    ev.total = total;
                    ev.text = "跳过已存在 · " + g.chessid;
                    pushEvent(shared, ev);
                    continue;
                }
                try {
                    std::string err;
                    std::string sgf = fox.fetchSgf(g.chessid, err);
                    if (sgf.empty()) throw std::runtime_error(err.empty() ? "空棋谱" : err);
                    std::string outPath = util::wideToUtf8(path.wstring());
                    dbg(("download out: " + outPath).c_str());
                    std::string writeError;
                    if (!util::writeFile(outPath, sgf, &writeError)) {
                        dbg(("download write fail: " + writeError).c_str());
                        throw std::runtime_error("保存失败: " + writeError);
                    }
                    dbg("download write ok");
                    ++done;
                    Event ev;
                    ev.type = EvType::DownloadProgress;
                    ev.chessid = g.chessid;
                    ev.success = true;
                    ev.done = done.load();
                    ev.failed = failed.load();
                    ev.skipped = skipped.load();
                    ev.total = total;
                    ev.text = "正在下载 " + std::to_string(done.load() + failed.load() +
                                                          skipped.load()) +
                              "/" + std::to_string(total);
                    pushEvent(shared, ev);
                } catch (const std::exception& ex) {
                    ++failed;
                    Event ev;
                    ev.type = EvType::DownloadProgress;
                    ev.chessid = g.chessid;
                    ev.success = false;
                    ev.done = done.load();
                    ev.failed = failed.load();
                    ev.skipped = skipped.load();
                    ev.total = total;
                    ev.text = "下载失败 " + g.chessid + " · " + ex.what();
                    pushEvent(shared, ev);
                }
                if (interval > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                }
            }
        };

        std::vector<std::thread> workers;
        for (int i = 0; i < concurrency; ++i) workers.emplace_back(worker);
        for (auto& t : workers) t.join();

        Event doneEv;
        doneEv.type = EvType::DownloadDone;
        doneEv.done = done.load();
        doneEv.failed = failed.load();
        doneEv.skipped = skipped.load();
        doneEv.total = total;
        pushEvent(shared, doneEv);
        shared->downloading.store(false);
    }).detach();
}

void App::downloadOne(const Game& game) {
    startDownload({game});
}

void App::chooseOutputDir() {
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"选择保存目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(pidl, path)) {
        outputDir_ = util::wideToUtf8(path);
        settings_.outputDir = outputDir_;
        saveSettings(settings_);
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(util::utf8ToWide(outputDir_)), ec);
        if (ec) {
            errorText_ = "无法创建输出目录：" + outputDir_ + "\n" + ec.message();
            showError_ = true;
        }
    }
    CoTaskMemFree(pidl);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void App::drawHeader() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 0));
    ImGui::BeginChild("##header", ImVec2(0, 78), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    drawMagicCircle(dl, ImVec2(p.x + 30, p.y + 39), 27.0f,
                    static_cast<float>(ImGui::GetTime() * 12.0), 255, true);

    ImGui::Dummy(ImVec2(66, 66));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    textBold("FoxSgfsDownload-Max", 26.0f, kText);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
    textSmall("野狐棋谱库 · 全量历史检索与批量下载", kMuted);
    ImGui::EndGroup();

    // 右侧状态胶囊
    std::string pill;
    ImU32 dot = kGreen;
    if (shared_->downloading.load()) {
        pill = "下载中";
        dot = kCyan;
    } else if (shared_->searching.load()) {
        pill = "搜索中";
        dot = kGold;
    } else {
        pill = "就绪";
        dot = kGreen;
    }
    ImGui::PushFont(g_fontSmall);
    ImVec2 textSize = ImGui::CalcTextSize(pill.c_str());
    ImGui::PopFont();
    float pillW = textSize.x + 38.0f;
    float rightX = ImGui::GetWindowWidth() - pillW - 18.0f;
    ImGui::SetCursorPos(ImVec2(rightX, 24.0f));
    ImVec2 pillPos = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(pillPos, ImVec2(pillPos.x + pillW, pillPos.y + 30),
                      IM_COL32(255, 255, 255, 8), 15.0f);
    dl->AddRect(pillPos, ImVec2(pillPos.x + pillW, pillPos.y + 30),
                WithAlpha(dot, 90), 15.0f, 0, 1.0f);
    dl->AddCircleFilled(ImVec2(pillPos.x + 15, pillPos.y + 15), 4.0f, dot, 16);
    ImGui::PushFont(g_fontSmall);
    ImGui::SetCursorScreenPos(ImVec2(pillPos.x + 25, pillPos.y + 7));
    ImGui::TextUnformatted(pill.c_str());
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(rightX - 64, 24.0f));
    if (ghostButton("帮助", ImVec2(56, 30))) showHelp_ = true;

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void App::drawSearchCard() {
    beginCard("##searchcard", ImVec2(0, 116));
    ImGui::SetCursorPos(ImVec2(16, 12));
    textSmall("查询对象");
    ImGui::SetCursorPos(ImVec2(16, 30));
    ImGui::SetNextItemWidth(92);
    const char* modes[] = {"昵称", "UID", "树搜索"};
    ImGui::Combo("##mode", &mode_, modes, 3);
    ImGui::SameLine(0, 8);
    bool enter = false;
    if (mode_ == 2) {
        textSmall("读取 exe 同目录 root.txt", kMuted);
        ImGui::SameLine(0, 12);
        textSmall("深度");
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(56);
        ImGui::DragInt("##treedepth", &treeDepth_, 0.2f, 1, 8, "%d",
                       ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("1 = 只查 root.txt 里的人；2 = 再查他们的对手。\n"
                              "深度越高，人数和请求量增长越快。");
        }
    } else {
        ImGui::SetNextItemWidth(360);
        if (focusQuery_) {
            ImGui::SetKeyboardFocusHere();
            focusQuery_ = false;
        }
        enter = ImGui::InputTextWithHint(
            "##query", "昵称或 UID", query_, sizeof(query_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("例如：柯洁 / 6757425。回车开始查询。");
        }
        ImGui::SameLine(0, 10);
        textSmall("对手");
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(190);
        enter = ImGui::InputTextWithHint(
                    "##opponent", "可选：昵称 / UID", opponent_,
                    sizeof(opponent_), ImGuiInputTextFlags_EnterReturnsTrue) ||
                enter;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("例如：申真谞 / 137345。\n"
                              "只保留 A 与 B 互为黑白双方的对局，留空则不限对手。");
        }
        ImGui::SameLine(0, 12);
        ImGui::Checkbox("抓取全部历史", &fullHistory_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("从官方棋谱库翻页拉取 2013 年以来的历史对局；\n"
                              "搜索过程中每 20 局就会追加显示，可先翻页查看。");
        }
    }
    ImGui::SameLine(0, 12);
    ImGui::BeginDisabled(shared_->searching.load());
    if (accentButton("开始查询", ImVec2(108, 34)) || enter) startSearch();
    ImGui::EndDisabled();
    ImGui::SameLine(0, 8);
    ImGui::BeginDisabled(!shared_->searching.load());
    if (ghostButton("停止", ImVec2(62, 34))) shared_->searchStop.store(true);
    ImGui::EndDisabled();

    ImGui::SetCursorPos(ImVec2(16, 76));
    if (shared_->searching.load()) {
        int dots = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
        std::string s = status_ + std::string(dots, '.');
        textSmall(s.c_str(), kGold);
    } else {
        textSmall(status_.c_str(), kMuted);
    }
    endCard();
}

void App::drawStats() {
    float avail = ImGui::GetContentRegionAvail().x;
    float gap = 12.0f;
    float w = (avail - gap * 3) / 4.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
    auto card = [&](const char* id, const char* label, const std::string& value,
                    const std::string& sub, ImU32 valueColor) {
        if (beginCard(id, ImVec2(w, 82))) {
            ImGui::SetCursorPos(ImVec2(14, 10));
            textSmall(label);
            ImGui::SetCursorPos(ImVec2(14, 30));
            textBold(util::ellipsizeUtf8(value, 26), 21.0f, valueColor);
            ImGui::SetCursorPos(ImVec2(14, 56));
            textSmall(util::ellipsizeUtf8(sub, 30).c_str(), kMuted);
        }
        endCard();
    };

    std::string user = currentUser_.empty() ? "未选择" : currentUser_;
    if (!currentUid_.empty() && currentUser_ != currentUid_) {
        user += " · " + currentUid_;
    }
    card("##stat_user", "当前用户", user, "野狐账号", kText);
    ImGui::SameLine();
    bool opp = opponentFilterActive();
    std::string totalLabel = opp ? "对手匹配" : "总对局";
    std::string totalSub = opp ? ("vs " + util::trim(opponent_)) : "含最新对局";
    card("##stat_total", totalLabel.c_str(),
         std::to_string(opp ? matchCount_ : games_.size()), totalSub, kGold);
    ImGui::SameLine();
    card("##stat_hist", "历史棋谱", std::to_string(historyCount_), "官方棋谱库", kCyan);
    ImGui::SameLine();
    card("##stat_range", "时间跨度", timeRange_,
         shared_->searching.load() ? "正在持续加载…" : "按对局时间", kPurple);
    ImGui::PopStyleVar();
}

void App::drawFilterBar() {
    beginCard("##filterbar", ImVec2(0, 48));
    ImGui::SetCursorPos(ImVec2(16, 8));
    textSmall("筛选");
    ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(180);
    bool apply = ImGui::InputTextWithHint("##kw", "棋手 / 赛事 / ID",
                                          keyword_, sizeof(keyword_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivatedAfterEdit()) apply = true;
    ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##sort", &sortMode_, "最新在前\0最早在前\0")) {
        rebuildFiltered(false);
    }
    ImGui::SameLine(0, 8);
    int pageSizeIndex = pageSize_ == 50 ? 1 : (pageSize_ == 100 ? 2 : 0);
    ImGui::SetNextItemWidth(72);
    if (ImGui::Combo("##psize", &pageSizeIndex, "25\0" "50\0" "100\0")) {
        pageSize_ = pageSizeIndex == 0 ? 25 : (pageSizeIndex == 1 ? 50 : 100);
        settings_.pageSize = pageSize_;
        saveSettings(settings_);
        rebuildFiltered(false);
    }
    ImGui::SameLine(0, 10);
    if (ghostButton("全选本页", ImVec2(100, 28))) setPageSelected(true);
    ImGui::SameLine(0, 6);
    if (ghostButton("全选筛选", ImVec2(100, 28))) {
        for (size_t i : filtered_) games_[i].selected = true;
    }
    ImGui::SameLine(0, 6);
    if (ghostButton("反选", ImVec2(64, 28))) {
        for (Game& g : games_) g.selected = !g.selected;
    }
    ImGui::SameLine(0, 6);
    if (ghostButton("清空", ImVec2(64, 28))) {
        for (Game& g : games_) g.selected = false;
    }
    int selected = 0;
    for (const Game& g : games_) if (g.selected) ++selected;
    ImGui::SameLine(0, 12);
    textSmall(("已勾选 " + std::to_string(selected) + " 盘").c_str(), kGold);
    if (apply) rebuildFiltered(false);
    endCard();
}

void App::drawTable() {
    float availY = ImGui::GetContentRegionAvail().y;
    float bottomH = 92.0f;
    float cardH = std::max(220.0f, availY - bottomH - 12.0f);
    beginCard("##tablecard", ImVec2(0, cardH));

    if (games_.empty()) {
        const char* msg = shared_->searching.load()
                              ? "正在拉取棋谱，请稍候…"
                              : "输入昵称或 UID，开始检索全量历史棋谱";
        ImGui::PushFont(g_fontBody);
        float tw = ImGui::CalcTextSize(msg).x;
        ImGui::PopFont();
        ImGui::SetCursorPos(ImVec2(std::max(0.0f,
                                            (ImGui::GetWindowWidth() - tw) * 0.5f),
                                   42));
        textSmall(msg, kMuted);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        drawMagicCircle(dl, ImVec2(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.64f),
                        ws.y * 0.20f, static_cast<float>(ImGui::GetTime() * 8.0),
                        22, true);
        endCard();
        return;
    }

    float footerH = 44.0f;
    float tableH = ImGui::GetContentRegionAvail().y - footerH;
    ImGuiTableFlags flags = ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_ScrollX |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_NoBordersInBodyUntilResize;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 6));
    std::string tableId = "##games_" + std::to_string(searchEpoch_) + "_" +
                          std::to_string(page_);
    if (ImGui::BeginTable(tableId.c_str(), 12, flags, ImVec2(0, tableH))) {
        if (page_ != lastDrawnPage_ || searchEpoch_ != lastDrawnEpoch_ ||
            resetScroll_) {
            ImGui::SetScrollY(0.0f);
            ImGui::SetScrollX(0.0f);
            lastDrawnPage_ = page_;
            lastDrawnEpoch_ = searchEpoch_;
            resetScroll_ = false;
        }
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("选", ImGuiTableColumnFlags_WidthFixed, 26);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32);
        ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("赛事 / 标题", ImGuiTableColumnFlags_WidthFixed, 280);
        ImGui::TableSetupColumn("黑方", ImGuiTableColumnFlags_WidthFixed, 105);
        ImGui::TableSetupColumn("黑段", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("白方", ImGuiTableColumnFlags_WidthFixed, 105);
        ImGui::TableSetupColumn("白段", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("结果", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("手数", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("chessid", ImGuiTableColumnFlags_WidthFixed, 170);
        ImGui::TableHeadersRow();

        int start = (page_ - 1) * pageSize_;
        int end = std::min<int>(static_cast<int>(filtered_.size()), start + pageSize_);
        for (int i = start; i < end; ++i) {
            size_t gi = filtered_[i];
            Game& g = games_[gi];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 32.0f);
            ImU32 rowBg = (i % 2) ? IM_COL32(255, 255, 255, 4) : IM_COL32(0, 0, 0, 0);
            if (g.selected) rowBg = IM_COL32(246, 198, 72, 18);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowBg);
            ImGui::PushID(static_cast<int>(gi));

            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##sel", &g.selected);

            ImGui::TableSetColumnIndex(1);
            textSmall(std::to_string(i + 1).c_str(), kMuted);

            ImGui::TableSetColumnIndex(2);
            textBody(g.starttime.size() >= 16 ? g.starttime.substr(0, 16) : g.starttime);

            ImGui::TableSetColumnIndex(3);
            {
                std::string title = g.title.empty() ? "（无标题）" : g.title;
                textBody(util::ellipsizeUtf8(title, 52),
                         g.isHistory() ? kText : IM_COL32(220, 214, 238, 255));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(560);
                    ImGui::TextUnformatted(g.title.empty() ? "（无标题）" : g.title.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                    if (ImGui::IsMouseDoubleClicked(0)) downloadOne(g);
                }
            }

            ImGui::TableSetColumnIndex(4);
            textBody(g.displayBlackName().empty() ? "未知" : g.displayBlackName());
            ImGui::TableSetColumnIndex(5);
            textSmall(Game::rankText(g.blackDan).c_str(), kMuted);
            ImGui::TableSetColumnIndex(6);
            textBody(g.displayWhiteName().empty() ? "未知" : g.displayWhiteName());
            ImGui::TableSetColumnIndex(7);
            textSmall(Game::rankText(g.whiteDan).c_str(), kMuted);

            ImGui::TableSetColumnIndex(8);
            {
                ImU32 color = kText;
                if (!currentUid_.empty()) {
                    if (g.isWin(currentUid_)) color = kGreen;
                    else if (g.winner == 1 || g.winner == 2) color = kRed;
                } else if (g.winner == 1 || g.winner == 2) {
                    color = g.winner == 1 ? IM_COL32(210, 220, 255, 255) : kText;
                }
                textBody(g.resultText(), color);
            }

            ImGui::TableSetColumnIndex(9);
            textSmall(std::to_string(g.moves).c_str(), kMuted);

            ImGui::TableSetColumnIndex(10);
            if (g.downloaded) {
                textSmall("已下载", kGreen);
            } else if (g.status == "失败") {
                textSmall("失败", kRed);
            } else if (ImGui::SmallButton("下载")) {
                downloadOne(g);
            }

            ImGui::TableSetColumnIndex(11);
            textSmall(g.chessid.c_str(), IM_COL32(120, 110, 145, 255));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    // 分页栏
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::Separator();
    ImGui::SetCursorPosX(12);
    if (ImGui::Button("|<", ImVec2(30, 26))) page_ = 1;
    ImGui::SameLine(0, 4);
    if (ImGui::Button("<", ImVec2(28, 26))) page_ = std::max(1, page_ - 1);
    ImGui::SameLine(0, 6);
    ImGui::Text("第 %d / %d 页", page_, pageCount());
    ImGui::SameLine(0, 6);
    if (ImGui::Button(">", ImVec2(28, 26))) page_ = std::min(pageCount(), page_ + 1);
    ImGui::SameLine(0, 4);
    if (ImGui::Button(">|", ImVec2(30, 26))) page_ = pageCount();
    ImGui::SameLine(0, 14);
    ImGui::SetNextItemWidth(64);
    ImGui::InputTextWithHint("##jump", "页码", jump_, sizeof(jump_),
                             ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine(0, 4);
    if (ImGui::Button("跳转", ImVec2(48, 26))) {
        int v = std::atoi(jump_);
        if (v >= 1) page_ = std::min(pageCount(), v);
        jump_[0] = 0;
    }
    ImGui::SameLine(0, 14);
    textSmall(("共 " + std::to_string(filtered_.size()) + " 局").c_str(), kMuted);

    endCard();
}

void App::drawBottomBar() {
    beginCard("##bottom", ImVec2(0, 88));
    ImGui::SetCursorPos(ImVec2(16, 12));
    int selected = 0;
    for (const Game& g : games_) if (g.selected) ++selected;
    ImGui::BeginDisabled(shared_->downloading.load() || games_.empty());
    if (accentButton(("下载勾选 (" + std::to_string(selected) + ")").c_str(),
                     ImVec2(132, 34))) {
        startDownload(collectSelected());
    }
    ImGui::SameLine(0, 8);
    if (ghostButton("下载筛选结果", ImVec2(124, 34))) {
        startDownload(collectFiltered());
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 8);
    ImGui::BeginDisabled(!shared_->downloading.load());
    if (ghostButton("停止下载", ImVec2(84, 34))) shared_->downloadStop.store(true);
    ImGui::EndDisabled();

    ImGui::SameLine(0, 20);
    textSmall("并发");
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(58);
    ImGui::DragInt("##conc", &concurrency_, 0.2f, 1, 16, "%d",
                   ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine(0, 10);
    textSmall("间隔");
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(72);
    ImGui::DragInt("##interval", &intervalMs_, 20.0f, 0, 60000, "%d",
                   ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine(0, 6);
    textSmall("ms");
    ImGui::SameLine(0, 14);
    if (ghostButton("输出目录", ImVec2(80, 28))) chooseOutputDir();
    ImGui::SameLine(0, 8);
    textSmall(util::ellipsizeUtf8(outputDir_, 38).c_str(), kMuted);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", outputDir_.c_str());

    ImGui::SetCursorPos(ImVec2(16, 56));
    if (shared_->downloading.load() || downloadTotal_ > 0) {
        int finished = downloadDone_ + downloadFailed_ + downloadSkipped_;
        float frac = downloadTotal_ > 0
                         ? static_cast<float>(finished) / downloadTotal_
                         : 0.0f;
        std::string overlay = "成功 " + std::to_string(downloadDone_) +
                              " · 失败 " + std::to_string(downloadFailed_) +
                              " · 跳过 " + std::to_string(downloadSkipped_) +
                              " / " + std::to_string(downloadTotal_);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V4(kGold));
        ImGui::ProgressBar(frac, ImVec2(ImGui::GetContentRegionAvail().x - 16, 16),
                           overlay.c_str());
        ImGui::PopStyleColor();
    } else {
        textSmall("提示：历史棋谱每 20 局追加显示；双击标题可单独下载。", kMuted);
    }
    endCard();
}

void App::drawOverlays() {
    if (showHelp_) {
        ImGui::OpenPopup("使用说明");
        showHelp_ = false;
    }
    if (ImGui::BeginPopupModal("使用说明", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(560);
        ImGui::TextUnformatted(
            "1. 输入野狐昵称或 UID，勾选“抓取全部历史”。\n"
            "2. 最新约 100 局先显示，随后官方棋谱库每 20 局追加一页，可边搜边翻页。\n"
            "3. 双方段位、胜负、手数会在搜索完成后自动补全。\n"
            "4. 勾选对局或点击“下载筛选结果”，选择保存目录即可批量下载 SGF。\n"
            "5. 双击标题或点行尾“下载”可单独下载一局。\n\n"
            "说明：匿名公开接口只能取到官方棋谱库覆盖的历史；纯日常对局超过最新 100 局的部分官方未公开。\n\n"
            "授权：严格绝对禁止商用，仅限个人学习、复盘、研究使用；下载的 SGF 版权归原作者及野狐平台所有。");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (accentButton("知道了", ImVec2(100, 32))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (showError_) {
        ImGui::OpenPopup("错误");
        showError_ = false;
    }
    if (ImGui::BeginPopupModal("错误", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(480);
        ImGui::TextUnformatted(errorText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (accentButton("确定", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    double now = ImGui::GetTime();
    if (!toast_.empty() && now < toastUntil_) {
        ImGui::SetNextWindowPos(
            ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                   ImGui::GetIO().DisplaySize.y - 36),
            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.96f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, V4(IM_COL32(30, 22, 50, 248)));
        ImGui::PushStyleColor(ImGuiCol_Border, V4(kGoldSoft));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 9));
        ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove);
        ImGui::TextUnformatted(toast_.c_str());
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
}

void App::frame() {
    pollEvents();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    drawBackground(ImGui::GetBackgroundDrawList(), display, ImGui::GetTime());

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    drawHeader();
    drawSearchCard();
    drawStats();
    drawFilterBar();
    drawTable();
    drawBottomBar();
    ImGui::End();
    ImGui::PopStyleVar();
    drawOverlays();
    if (autoSearchPending_) {
        autoSearchPending_ = false;
        startSearch();
    }
}

// ---------------------------------------------------------------------------
// Win32 / D3D11 bootstrap
// ---------------------------------------------------------------------------
static bool g_resizePending = false;
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_resizeWidth = LOWORD(lParam);
            g_resizeHeight = HIWORD(lParam);
            g_resizePending = true;
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_SCREENSAVE || (wParam & 0xfff0) == SC_MONITORPOWER)
            return 0;
        break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 1000;
        info->ptMinTrackSize.y = 700;
        return 0;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void dbg(const char* msg) {
    static int enabled = -1;
    if (enabled < 0) {
        char buf[8]{};
        enabled = GetEnvironmentVariableA("FOXKIFU_DEBUG", buf, sizeof(buf)) ? 1 : 0;
    }
    if (!enabled) return;
    std::ofstream f("cpp_debug.log", std::ios::app);
    f << msg << "\n";
    f.flush();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    dbg("start");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE)) {
        dbg("process dpi unaware");
    } else {
        dbg(("process dpi failed " + std::to_string(GetLastError())).c_str());
    }
    dbg("dpi");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"FoxKifuStudioWindow";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = std::min(1440, sw - 80);
    int wh = std::min(920, sh - 80);
    dbg(("screen " + std::to_string(sw) + "x" + std::to_string(sh) +
         " window " + std::to_string(ww) + "x" + std::to_string(wh)).c_str());
    RECT rc{0, 0, ww, wh};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"FoxSgfsDownload-Max · 野狐棋谱库",
        WS_OVERLAPPEDWINDOW, (sw - (rc.right - rc.left)) / 2,
        (sh - (rc.bottom - rc.top)) / 2, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DWORD corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }
    dbg("device");

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    dbg("window");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    dbg("context");
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (dpi <= 0.0f) dpi = 1.0f;
    float uiScale = 1.0f;
    dbg(("dpi " + std::to_string(dpi)).c_str());
    applyTheme(uiScale);
    dbg("theme");
    loadFonts(uiScale);
    dbg("fonts");
    ImGui_ImplWin32_Init(hwnd);
    dbg("win32 init");
    ImGui_ImplDX11_Init(g_device, g_context);
    dbg("dx11 init");

    App app(hwnd);
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::string autoQuery;
        std::string autoOpponent;
        bool autoUid = false;
        bool autoDownload = false;
        bool autoTree = false;
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                if (arg == L"--query" && i + 1 < argc) {
                    autoQuery = util::wideToUtf8(argv[++i]);
                } else if (arg == L"--opponent" && i + 1 < argc) {
                    autoOpponent = util::wideToUtf8(argv[++i]);
                } else if (arg == L"--uid") {
                    autoUid = true;
                } else if (arg == L"--download") {
                    autoDownload = true;
                } else if (arg == L"--tree") {
                    autoTree = true;
                }
            }
            LocalFree(argv);
        }
        if (autoTree) {
            app.setAutoTree(autoDownload);
        } else if (!autoQuery.empty()) {
            app.setAutoQuery(autoQuery, autoUid, autoDownload, autoOpponent);
        }
    }
    dbg("app");
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_resizePending && g_resizeWidth > 0 && g_resizeHeight > 0) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight,
                                       DXGI_FORMAT_UNKNOWN, 0);
            g_width = g_resizeWidth;
            g_height = g_resizeHeight;
            CreateRenderTarget();
            g_resizePending = false;
        }

        ImGui_ImplDX11_NewFrame();
        dbg("dx11 newframe");
        ImGui_ImplWin32_NewFrame();
        dbg("win32 newframe");
        ImGui::NewFrame();
        dbg("imgui newframe");
        app.frame();
        dbg("frame done");
        ImGui::Render();
        dbg("render");

        const float clear[4] = {0.03f, 0.02f, 0.06f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    app.saveNow();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    CoUninitialize();
    return 0;
}

