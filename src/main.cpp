// 抽学号 Ultra Version - 原生 Win32 随机点名程序
// 特点：UTF-8 全支持(全程 W API) / 范围+手动+排除 学号 / 滚动抽取动画 / 音效 /
//       历史记录 / 图片背景(0731.png 高清 + 高透明白蒙版) + 半透明白色大卡片 / 高 DPI / 全屏 / 自动保存状态 / 单文件静态链接
//
// 编译(Release, 单文件 exe):
//   g++.exe -std=c++17 -O2 -mwindows -static -static-libgcc -static-libstdc++ -o 抽学号Ultra.exe main.cpp app.rc -lcomctl32 -lcomdlg32 -lwinmm -lshell32 -lgdiplus -lole32
//
// 单元测试(控制台):
//   g++.exe -std=c++17 -O2 -DUNIT_TEST -mconsole -o test.exe main.cpp

#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <random>
#include <ctime>

// ============================== 全局句柄 ==============================
static HINSTANCE g_hInst = NULL;
static HWND g_hwnd = NULL;
static HWND g_hDisplay = NULL, g_hStats = NULL, g_hLblHist = NULL, g_hHistory = NULL;
static HWND g_hBtnPick = NULL, g_hBtnReset = NULL, g_hBtnUndo = NULL, g_hBtnSettings = NULL;
static HWND g_hBtnExport = NULL, g_hBtnClear = NULL, g_hBtnFull = NULL;
static HWND g_hLblN = NULL, g_hEdN = NULL, g_hChkUnique = NULL, g_hLblRecent = NULL, g_hEdRecent = NULL;
static HWND g_hStatus = NULL;
static HWND g_hSignature = NULL; // 日志框底部的署名条（头像 + by notxcm，点击跳转 GitHub）
static HWND g_hChkSupMain = NULL; // 主界面 Surprise 按钮旁的一级复选框（打勾必选，不打勾正常）
// 设置面板（已平铺到主窗口）
static HWND g_hLblRanges = NULL, g_hEdRanges = NULL, g_hLblManual = NULL, g_hEdManual = NULL;
static HWND g_hLblExclude = NULL, g_hEdExclude = NULL, g_hLblAnim = NULL, g_hEdAnim = NULL;
static HWND g_hLblScale = NULL, g_hEdScale = NULL, g_hChkSound = NULL, g_hChkUnique2 = NULL, g_hBtnApply = NULL;

static HFONT g_hFontUI = NULL, g_hFontBtn = NULL, g_hFontStats = NULL;
static HBRUSH g_hBrushPanel = NULL, g_hBrushWhite = NULL;
static HBITMAP g_bgBmp = NULL;
static HDC g_bgDC = NULL;
static int g_bgW = 0, g_bgH = 0;
// 大数字卡片双缓冲缓存（背景+半透明卡片预渲染，动画帧只复制，杜绝闪烁）
static HDC g_cardDC = NULL;
static HBITMAP g_cardBmp = NULL;
static int g_cardW = 0, g_cardH = 0;
static double g_scale = 1.0, g_userScale = 1.0;
static bool g_full = false;
static LONG g_savedStyle = 0;
static RECT g_savedRect = {0, 0, 0, 0};

static std::wstring g_displayText = L"?";
static std::wstring g_displaySub = L"点击大卡片 或 按 空格 开始抽取";
static bool g_animActive = false;
static ULONGLONG g_animStart = 0;
static int g_animDur = 1200;
static std::string g_animResult;
static std::vector<std::string> g_animList;
static std::vector<std::string> g_pendingPool, g_pendingRecent, g_pendingMust;

static std::mt19937 g_rng(std::random_device{}());

// ============================== 配色（纯白底黑字，无花哨配色） ==============================
static const COLORREF C_TEXT   = RGB(24, 24, 24);    // 主文字（近黑）
static const COLORREF C_SUB    = RGB(96, 96, 96);    // 副文字（深灰）
static const COLORREF C_BORDER = RGB(205, 205, 205); // 浅灰边框

// ============================== GDI+ 模糊背景 ==============================
static ULONG_PTR g_gdipToken = 0;
static Gdiplus::Bitmap* g_bgImage = NULL;
static Gdiplus::Bitmap* g_avatarImage = NULL;
static bool LoadBgImage();
static void LoadAvatarImage();

// ============================== UTF-8 辅助 ==============================
static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w; w.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s; s.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring GetWndTextW(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s; s.resize(n + 1);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}

// ============================== 文件 UTF-8 ==============================
static std::string read_file_utf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    DWORD sz = GetFileSize(h, nullptr);
    std::string buf; buf.resize(sz > 0 ? sz : 0);
    DWORD rd = 0;
    if (sz > 0) ReadFile(h, buf.data(), sz, &rd, nullptr);
    CloseHandle(h);
    if (buf.size() >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF)
        buf = buf.substr(3);
    return buf;
}
static bool write_file_utf8(const std::wstring& path, const std::string& content, bool bom) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    if (bom) { const char b[] = { (char)0xEF, (char)0xBB, (char)0xBF }; WriteFile(h, b, 3, &wr, nullptr); }
    WriteFile(h, content.data(), (DWORD)content.size(), &wr, nullptr);
    CloseHandle(h);
    return true;
}

// ============================== 字符串工具 ==============================
template<class T> static T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> r; size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find('\n', i);
        if (j == std::string::npos) { r.push_back(trim(s.substr(i))); break; }
        r.push_back(trim(s.substr(i, j - i))); i = j + 1;
    }
    if (r.size() == 1 && r[0].empty()) r.clear();
    return r;
}
static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> r; size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) { r.push_back(trim(s.substr(i))); break; }
        r.push_back(trim(s.substr(i, j - i))); i = j + 1;
    }
    return r;
}
static std::string nowStr() {
    SYSTEMTIME st; GetLocalTime(&st);
    char b[64]; snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d",
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

// ============================== 数据模型 ==============================
struct Range { long long start, end, step; int width; };
struct App {
    std::vector<Range> ranges;
    std::vector<std::string> manual;
    std::set<std::string> excluded;
    std::vector<std::string> pool;
    std::vector<std::pair<std::string, std::string>> history; // id, time
    std::vector<std::string> recent;
    std::vector<std::string> mustPick;    // Surprise 必抽榜（活跃列表：抽中即出榜，重置时从 mustPickCfg 恢复）
    std::vector<std::string> mustPickCfg; // Surprise 必抽榜配置（永久保存：重启/重置不丢失）
    bool surpriseOn = false;              // Surprise 开关：打勾才生效，不打勾正常抽取
    int animMs = 1200;
    double scaleUI = 1.0;
    int theme = 0;
    bool sound = true;
    bool unique = true;
    int nPick = 1;
    int avoidRecent = 0;
};
static App g_app;

static void buildPool(App& a) {
    std::set<std::string> seen;
    a.pool.clear();
    auto add = [&](const std::string& id) {
        if (id.empty()) return;
        if (a.excluded.count(id)) return;
        if (seen.count(id)) return;
        seen.insert(id); a.pool.push_back(id);
    };
    for (auto& r : a.ranges) {
        long long st = r.start, en = r.end;
        long long step = r.step; if (step == 0) step = 1;
        if (st > en) std::swap(st, en);
        for (long long i = st; ; i += step) {
            if (i > en) break;
            std::string id;
            if (r.width > 0) { char buf[64]; snprintf(buf, sizeof buf, "%0*lld", r.width, i); id = buf; }
            else id = std::to_string(i);
            add(id);
            if (step <= 0) break;
        }
    }
    for (auto& m : a.manual) add(trim(m));
}

static void parseRanges(const std::string& text, std::vector<Range>& out) {
    out.clear();
    for (auto& ln : split_lines(text)) {
        if (ln.empty()) continue;
        auto p = split_csv(ln);
        Range r;
        r.start = p.size() >= 1 ? _atoi64(p[0].c_str()) : 1;
        r.end   = p.size() >= 2 ? _atoi64(p[1].c_str()) : 1;
        r.step  = p.size() >= 3 && !p[2].empty() ? _atoi64(p[2].c_str()) : 1;
        r.width = p.size() >= 4 && !p[3].empty() ? (int)_atoi64(p[3].c_str()) : 0;
        if (r.step == 0) r.step = 1;
        out.push_back(r);
    }
}

static void defaultConfig() {
    g_app = App();
    g_app.ranges.push_back({ 1, 50, 1, 0 });
    g_app.animMs = 1200; g_app.scaleUI = 1.0; g_app.theme = 0;
    g_app.sound = true; g_app.unique = true; g_app.nPick = 1; g_app.avoidRecent = 0; g_app.surpriseOn = false;
    buildPool(g_app);
}

// ============================== 配置存取 ==============================
static void saveConfig(const std::wstring& path) {
    std::string s;
    s += "[抽学号Ultra配置]\n版本=1\n";
    for (auto& r : g_app.ranges) {
        char b[128]; snprintf(b, sizeof b, "范围=%lld,%lld,%lld,%d\n", r.start, r.end, r.step, r.width);
        s += b;
    }
    for (auto& m : g_app.manual) s += "手动=" + m + "\n";
    for (auto& e : g_app.excluded) s += "排除=" + e + "\n";
    char b[256];
    snprintf(b, sizeof b, "动画毫秒=%d\n界面缩放=%.3f\n主题=%d\n声音=%d\n不重复=%d\n数量=%d\n避免最近=%d\n",
             g_app.animMs, g_app.scaleUI, g_app.theme, g_app.sound ? 1 : 0,
             g_app.unique ? 1 : 0, g_app.nPick, g_app.avoidRecent);
    s += b;
    for (auto& h : g_app.history) s += "记录=" + h.first + "|" + h.second + "\n";
    for (auto& m : g_app.mustPickCfg) s += "必抽榜=" + m + "\n";
    s += std::string("惊喜开关=") + (g_app.surpriseOn ? "1" : "0") + "\n";
    write_file_utf8(path, s, false);
}

static bool loadConfig(const std::wstring& path) {
    std::string s = read_file_utf8(path);
    if (s.empty()) return false;
    App tmp = g_app;
    tmp.ranges.clear(); tmp.manual.clear(); tmp.excluded.clear();
    tmp.history.clear(); tmp.recent.clear(); tmp.mustPick.clear(); tmp.mustPickCfg.clear();
    for (auto& ln : split_lines(s)) {
        if (ln.empty()) continue;
        size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(ln.substr(0, eq));
        std::string val = trim(ln.substr(eq + 1));
        if (key == "范围") {
            auto p = split_csv(val);
            if (p.size() >= 2) {
                Range r; r.start = _atoi64(p[0].c_str()); r.end = _atoi64(p[1].c_str());
                r.step = p.size() >= 3 && !p[2].empty() ? _atoi64(p[2].c_str()) : 1;
                r.width = p.size() >= 4 && !p[3].empty() ? (int)_atoi64(p[3].c_str()) : 0;
                if (r.step == 0) r.step = 1;
                tmp.ranges.push_back(r);
            }
        } else if (key == "手动") { tmp.manual.push_back(val); }
        else if (key == "排除") { if (!val.empty()) tmp.excluded.insert(val); }
        else if (key == "动画毫秒") { tmp.animMs = clampv((int)_atoi64(val.c_str()), 100, 10000); }
        else if (key == "界面缩放") { tmp.scaleUI = clampv(atof(val.c_str()), 0.7, 2.0); }
        else if (key == "主题") { tmp.theme = clampv((int)_atoi64(val.c_str()), 0, 2); }
        else if (key == "声音") { tmp.sound = (val == "1" || val == "true" || val == "开" || val == "yes" || val == "是"); }
        else if (key == "不重复") { tmp.unique = (val == "1" || val == "true" || val == "开" || val == "yes" || val == "是"); }
        else if (key == "数量") { tmp.nPick = clampv((int)_atoi64(val.c_str()), 1, 9999); }
        else if (key == "避免最近") { tmp.avoidRecent = clampv((int)_atoi64(val.c_str()), 0, 9999); }
        else if (key == "记录") {
            std::string id = val, tm = "";
            size_t bar = val.find('|');
            if (bar != std::string::npos) { id = trim(val.substr(0, bar)); tm = trim(val.substr(bar + 1)); }
            if (!id.empty()) tmp.history.push_back({ id, tm });
        }
        else if (key == "必抽榜") { if (!val.empty()) { tmp.mustPick.push_back(val); tmp.mustPickCfg.push_back(val); } }
        else if (key == "惊喜开关") { tmp.surpriseOn = (val == "1" || val == "true" || val == "开" || val == "yes" || val == "是"); }
    }
    g_app = tmp;
    buildPool(g_app);
    return true;
}

static std::wstring statePath() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p = buf;
    size_t sl = p.find_last_of(L"\\/");
    p = p.substr(0, sl + 1) + L"抽学号Ultra.state.txt";
    return p;
}
static void saveState() { saveConfig(statePath()); }

// ============================== 音效 (内存 WAV) ==============================
static std::vector<BYTE> g_wavChime, g_wavTick;
static void synthWav(std::vector<BYTE>& out, double f1, double f2, double dur, double amp) {
    int sr = 44100; int n = (int)(sr * dur);
    int dataBytes = n * 2;
    out.resize(44 + dataBytes);
    memcpy(&out[0], "RIFF", 4);
    int riff = 36 + dataBytes; memcpy(&out[4], &riff, 4);
    memcpy(&out[8], "WAVE", 4);
    memcpy(&out[12], "fmt ", 4);
    int fmt = 16; memcpy(&out[16], &fmt, 4);
    short fmt1 = 1; memcpy(&out[20], &fmt1, 2);
    short ch = 1; memcpy(&out[22], &ch, 2);
    int sr4 = sr; memcpy(&out[24], &sr4, 4);
    int br = sr * 2; memcpy(&out[28], &br, 4);
    short bps = 2; memcpy(&out[32], &bps, 2);
    short bits = 16; memcpy(&out[34], &bits, 2);
    memcpy(&out[36], "data", 4);
    memcpy(&out[40], &dataBytes, 4);
    for (int i = 0; i < n; i++) {
        double t = (double)i / sr;
        double env = amp * (1.0 - t / dur);
        double v = (t < dur * 0.5) ? std::sin(2 * 3.1415926 * f1 * t)
                                   : std::sin(2 * 3.1415926 * f2 * t);
        short s = (short)(v * env * 32767);
        memcpy(&out[44 + i * 2], &s, 2);
    }
}
static void playChime() {
    if (!g_app.sound || g_wavChime.empty()) return;
    PlaySoundW((LPCWSTR)g_wavChime.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// ============================== DPI ==============================
static double GetScale() {
    double s = 1.0;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        typedef UINT(WINAPI * GPW)(HWND);
        auto f = (GPW)GetProcAddress(u, "GetDpiForWindow");
        if (f && g_hwnd) { int d = f(g_hwnd); if (d > 0) s = d / 96.0; }
    }
    if (s <= 0) s = 1.0;
    return s * g_userScale;
}
static void InitDPI() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    typedef BOOL(WINAPI * SPDAC)(HANDLE);
    auto f = (SPDAC)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (f) f((HANDLE)-4); // PER_MONITOR_AWARE_V2
    else {
        typedef BOOL(WINAPI * SPA)(void);
        auto f2 = (SPA)GetProcAddress(u, "SetProcessDPIAware");
        if (f2) f2();
    }
}

// ============================== 字体/画刷 ==============================
static HFONT makeFont(int px, bool bold) {
    return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
}
static void UpdateFonts() {
    g_scale = GetScale();
    if (g_hFontUI) DeleteObject(g_hFontUI);
    if (g_hFontBtn) DeleteObject(g_hFontBtn);
    if (g_hFontStats) DeleteObject(g_hFontStats);
    int base = (int)(15 * g_scale);
    g_hFontUI = makeFont(base, false);
    g_hFontBtn = makeFont((int)(16 * g_scale), true);
    g_hFontStats = makeFont((int)(14 * g_scale), false);
    HWND ctrls[] = { g_hStats, g_hLblHist, g_hHistory, g_hBtnPick, g_hBtnReset, g_hBtnUndo,
                     g_hBtnSettings, g_hBtnExport, g_hBtnClear, g_hBtnFull, g_hLblN, g_hEdN,
                     g_hChkUnique, g_hLblRecent, g_hEdRecent, g_hStatus, g_hChkSupMain,
                     g_hLblRanges, g_hEdRanges, g_hLblManual, g_hEdManual, g_hLblExclude, g_hEdExclude,
                     g_hLblAnim, g_hEdAnim, g_hLblScale, g_hEdScale, g_hChkSound, g_hChkUnique2, g_hBtnApply };
    for (HWND c : ctrls) if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    if (g_hBtnPick) SendMessageW(g_hBtnPick, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);
}
static void applyTheme() {
    if (g_hBrushPanel) { DeleteObject(g_hBrushPanel); g_hBrushPanel = NULL; }
    g_hBrushPanel = CreateSolidBrush(RGB(255, 255, 255));
    if (g_hBrushWhite) { DeleteObject(g_hBrushWhite); g_hBrushWhite = NULL; }
    g_hBrushWhite = CreateSolidBrush(RGB(255, 255, 255));
}

// ============================== 模糊背景（嵌入 PNG，铺满 + 白色蒙版） ==============================
static void BuildBg(int w, int h) {
    if (g_bgDC) { DeleteDC(g_bgDC); g_bgDC = NULL; }
    if (g_bgBmp) { DeleteObject(g_bgBmp); g_bgBmp = NULL; }
    if (w <= 0 || h <= 0) return;
    HDC hdc = GetDC(g_hwnd);
    g_bgDC = CreateCompatibleDC(hdc);
    g_bgBmp = CreateCompatibleBitmap(hdc, w, h);
    ReleaseDC(g_hwnd, hdc);
    // 位图保持选中在专用背景 DC 中（不要再选回旧对象，否则后续 BitBlt 会从 1x1 默认位图复制导致背景空白）
    SelectObject(g_bgDC, g_bgBmp);
    if (g_bgImage && g_bgImage->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics g(g_bgDC);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        // 先铺白底，避免透明区域发灰
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
        g.FillRectangle(&white, 0, 0, w, h);
        // 以 cover 方式铺满窗口
        int iw = g_bgImage->GetWidth(), ih = g_bgImage->GetHeight();
        double s = (double)w / iw; if ((double)h / ih > s) s = (double)h / ih;
        int dw = (int)(iw * s), dh = (int)(ih * s);
        int dx = (w - dw) / 2, dy = (h - dh) / 2;
        g.DrawImage(g_bgImage, dx, dy, dw, dh);
        // 白色蒙版：图片本身偏亮（0731.png 平均亮度 ~172），蒙版透明度调高让图片清晰可见，
        // 同时保证深色文字可读（避免花哨配色，保持黑白灰体系）
        Gdiplus::SolidBrush veil(Gdiplus::Color(140, 255, 255, 255));
        g.FillRectangle(&veil, 0, 0, w, h);
    } else {
        RECT rc = { 0, 0, w, h }; FillRect(g_bgDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
    g_bgW = w; g_bgH = h;
}

// ============================== 从资源加载图片（PNG / JPEG，按资源类型） ==============================
static Gdiplus::Bitmap* LoadResourceImage(LPCWSTR type, int id) {
    HRSRC hrs = FindResourceW(g_hInst, MAKEINTRESOURCEW(id), type);
    if (!hrs) return NULL;
    HGLOBAL hg = LoadResource(g_hInst, hrs);
    if (!hg) return NULL;
    DWORD sz = SizeofResource(g_hInst, hrs);
    void* p = LockResource(hg);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hmem) return NULL;
    void* pm = GlobalLock(hmem);
    memcpy(pm, p, sz);
    GlobalUnlock(hmem);
    IStream* stream = NULL;
    if (FAILED(CreateStreamOnHGlobal(hmem, TRUE, &stream))) { GlobalFree(hmem); return NULL; }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
    stream->Release(); // 释放后 hmem 由流自动回收
    if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return NULL; }
    return bmp;
}
static bool LoadBgImage() {
    g_bgImage = LoadResourceImage(L"PNG", IDR_BG);
    return g_bgImage != NULL;
}
static void LoadAvatarImage() {
    g_avatarImage = LoadResourceImage((LPCWSTR)RT_RCDATA, IDR_AVATAR);
}

// ============================== 布局（左：统计/日志/按钮；中：设置面板；右：抽取卡片） ==============================
static void Layout() {
    if (!g_hwnd) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * g_scale);
    int gap = (int)(8 * g_scale);
    int colW = (int)clampv((double)(W - 4 * pad - 2 * gap) / 3, 250.0 * g_scale, 400.0 * g_scale);
    int x0 = pad;
    int x1 = x0 + colW + gap; // 设置列
    int x2 = x1 + colW + gap; // 右列
    int rw = W - x2 - pad;
    int statH = (int)(64 * g_scale);
    int lblH = (int)(20 * g_scale);
    int btnH = (int)(30 * g_scale);
    int btnRowH = btnH + gap;
    int bottomRows = 2;
    int leftBottom = bottomRows * btnRowH + pad;

    // 左栏：统计 / 记录 / 日志框 / 署名条 / 按钮
    SetWindowPos(g_hStats, NULL, x0, pad, colW, statH, SWP_NOZORDER);
    int yLbl = pad + statH + (int)(6 * g_scale);
    SetWindowPos(g_hLblHist, NULL, x0, yLbl, colW, lblH, SWP_NOZORDER);
    int yList = yLbl + lblH + (int)(4 * g_scale);
    int sigH = (int)(32 * g_scale);
    int listH = H - pad - leftBottom - yList - sigH - (int)(6 * g_scale);
    if (listH < 40) listH = 40;
    SetWindowPos(g_hHistory, NULL, x0, yList, colW, listH, SWP_NOZORDER);
    int sigY = yList + listH + (int)(4 * g_scale);
    SetWindowPos(g_hSignature, NULL, x0, sigY, colW, sigH, SWP_NOZORDER);

    int by = H - pad - leftBottom + (int)(2 * g_scale);
    int bw = (colW - 2 * gap) / 3;
    HWND rowBtns[6] = { g_hBtnReset, g_hBtnUndo, g_hBtnSettings, g_hBtnExport, g_hBtnClear, g_hBtnFull };
    for (int i = 0; i < 6; i++) {
        int col = i % 3, row = i / 3;
        int bx = x0 + col * (bw + gap);
        int bY = by + row * btnRowH;
        if (i == 2 && g_hChkSupMain) {
            // Surprise 按钮 + 一级复选框并排（打勾必选，不打勾正常）
            int chkW = (int)(62 * g_scale);
            SetWindowPos(rowBtns[i], NULL, bx, bY, bw - chkW, btnH, SWP_NOZORDER);
            SetWindowPos(g_hChkSupMain, NULL, bx + bw - chkW, bY, chkW, btnH, SWP_NOZORDER);
        } else {
            SetWindowPos(rowBtns[i], NULL, bx, bY, bw, btnH, SWP_NOZORDER);
        }
    }

    // 中栏：设置面板（范围 / 手动 / 排除 / 动画·缩放 / 音效·不重复 / 应用）
    int sy = pad;
    SetWindowPos(g_hLblRanges, NULL, x1, sy, colW, lblH, SWP_NOZORDER); sy += lblH + (int)(4 * g_scale);
    SetWindowPos(g_hEdRanges, NULL, x1, sy, colW, (int)(104 * g_scale), SWP_NOZORDER); sy += (int)(104 * g_scale) + (int)(8 * g_scale);
    SetWindowPos(g_hLblManual, NULL, x1, sy, colW, lblH, SWP_NOZORDER); sy += lblH + (int)(4 * g_scale);
    SetWindowPos(g_hEdManual, NULL, x1, sy, colW, (int)(80 * g_scale), SWP_NOZORDER); sy += (int)(80 * g_scale) + (int)(8 * g_scale);
    SetWindowPos(g_hLblExclude, NULL, x1, sy, colW, lblH, SWP_NOZORDER); sy += lblH + (int)(4 * g_scale);
    SetWindowPos(g_hEdExclude, NULL, x1, sy, colW, (int)(74 * g_scale), SWP_NOZORDER); sy += (int)(74 * g_scale) + (int)(8 * g_scale);
    int halfW = (colW - gap) / 2;
    SetWindowPos(g_hLblAnim, NULL, x1, sy, (int)(58 * g_scale), lblH, SWP_NOZORDER);
    SetWindowPos(g_hEdAnim, NULL, x1 + (int)(60 * g_scale), sy, halfW - (int)(60 * g_scale), btnH, SWP_NOZORDER);
    SetWindowPos(g_hLblScale, NULL, x1 + halfW + gap, sy, (int)(58 * g_scale), lblH, SWP_NOZORDER);
    SetWindowPos(g_hEdScale, NULL, x1 + halfW + gap + (int)(60 * g_scale), sy, halfW - (int)(60 * g_scale), btnH, SWP_NOZORDER);
    sy += btnH + (int)(8 * g_scale);
    SetWindowPos(g_hChkSound, NULL, x1, sy, (int)(106 * g_scale), btnH, SWP_NOZORDER);
    SetWindowPos(g_hChkUnique2, NULL, x1 + (int)(112 * g_scale), sy, (int)(140 * g_scale), btnH, SWP_NOZORDER);
    sy += btnH + (int)(10 * g_scale);
    SetWindowPos(g_hBtnApply, NULL, x1, sy, colW, btnH, SWP_NOZORDER);

    // 右栏：快速设置行 / 大卡片 / 抽号按钮 / 状态栏
    int topY = pad;
    SetWindowPos(g_hLblN, NULL, x2, topY, (int)(72 * g_scale), lblH, SWP_NOZORDER);
    SetWindowPos(g_hEdN, NULL, x2 + (int)(74 * g_scale), topY, (int)(46 * g_scale), btnH, SWP_NOZORDER);
    SetWindowPos(g_hChkUnique, NULL, x2 + (int)(128 * g_scale), topY, (int)(112 * g_scale), btnH, SWP_NOZORDER);
    SetWindowPos(g_hLblRecent, NULL, x2 + (int)(244 * g_scale), topY, (int)(88 * g_scale), lblH, SWP_NOZORDER);
    SetWindowPos(g_hEdRecent, NULL, x2 + (int)(334 * g_scale), topY, (int)(46 * g_scale), btnH, SWP_NOZORDER);

    int statusH = (int)(26 * g_scale);
    int pickH = (int)(46 * g_scale);
    int dispTop = topY + btnH + (int)(18 * g_scale);
    int dispBottom = H - pad - statusH - pickH - (int)(18 * g_scale);
    int dispH = dispBottom - dispTop;
    if (dispH < 80) dispH = 80;
    SetWindowPos(g_hDisplay, NULL, x2, dispTop, rw, dispH, SWP_NOZORDER);
    int pickY = dispBottom + (int)(10 * g_scale);
    SetWindowPos(g_hBtnPick, NULL, x2 + (rw - (int)(200 * g_scale)) / 2, pickY, (int)(200 * g_scale), pickH, SWP_NOZORDER);
    SetWindowPos(g_hStatus, NULL, x0, H - statusH - (int)(4 * g_scale), W - 2 * pad, statusH, SWP_NOZORDER);
}

// ============================== UI 刷新 ==============================
static void SetStatus(const std::wstring& s) { if (g_hStatus) SetWindowTextW(g_hStatus, s.c_str()); }
static void InvalidateDisplay() { if (g_hDisplay) InvalidateRect(g_hDisplay, NULL, TRUE); }

static void updateStats() {
    int total = (int)g_app.pool.size() + (int)g_app.history.size();
    int picked = (int)g_app.history.size();
    int remain = (int)g_app.pool.size();
    int pct = total > 0 ? picked * 100 / total : 0;
    std::wstring s = L"奖池总数：" + std::to_wstring(total)
        + L"\n已抽取：" + std::to_wstring(picked)
        + L"    剩余：" + std::to_wstring(remain)
        + L"\n抽取进度：" + std::to_wstring(pct) + L"%";
    SetWindowTextW(g_hStats, s.c_str());
    SetStatus(L"就绪  |  当前奖池剩余 " + std::to_wstring(remain) + L" 个学号");
}

static void refreshHistory() {
    SendMessageW(g_hHistory, LB_RESETCONTENT, 0, 0);
    std::wstring line;
    int idx = (int)g_app.history.size();
    for (auto it = g_app.history.rbegin(); it != g_app.history.rend(); ++it) {
        line = std::to_wstring(idx) + L". " + to_wide(it->first)
             + L"  (" + to_wide(it->second) + L")";
        SendMessageW(g_hHistory, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        idx--;
    }
}

static void syncQuickControls() {
    SetWindowTextW(g_hEdN, std::to_wstring(g_app.nPick).c_str());
    SetWindowTextW(g_hEdRecent, std::to_wstring(g_app.avoidRecent).c_str());
    CheckDlgButton(g_hwnd, IDC_CHKUNIQUE, g_app.unique ? BST_CHECKED : BST_UNCHECKED);
    if (g_hChkSupMain)
        CheckDlgButton(g_hwnd, IDC_CHK_SUP, g_app.surpriseOn ? BST_CHECKED : BST_UNCHECKED);
}

// ============================== 抽取逻辑 ==============================
static void readQuickSettings() {
    wchar_t buf[64];
    GetWindowTextW(g_hEdN, buf, 64); g_app.nPick = clampv(_wtoi(buf), 1, 9999);
    GetWindowTextW(g_hEdRecent, buf, 64); g_app.avoidRecent = clampv(_wtoi(buf), 0, 9999);
    g_app.unique = (IsDlgButtonChecked(g_hwnd, IDC_CHKUNIQUE) == BST_CHECKED);
}

static void doPick() {
    if (g_animActive) return;
    readQuickSettings();
    if (g_app.pool.empty()) {
        SetStatus(L"奖池已空，请点「重置」或调整范围/排除。");
        MessageBoxW(g_hwnd, L"当前没有可抽取的学号。\n请检查「设置」面板中的范围、手动添加与排除，或点击「重置」。",
                    L"提示", MB_OK | MB_ICONINFORMATION);
        InvalidateDisplay();
        return;
    }

    // 预先计算本次结果（不影响动画期间显示）
    std::vector<std::string> tmpPool = g_app.pool;
    std::vector<std::string> recent = g_app.recent;
    std::vector<std::string> res;
    std::vector<std::string> remainMust; // 本次抽完后剩余的必抽榜（出榜：抽中的移除，未抽中/不在奖池的保留）
    int maxAvail = (int)tmpPool.size();
    int n = g_app.unique ? clampv(g_app.nPick, 1, maxAvail) : clampv(g_app.nPick, 1, 9999);
    int k = 0;
    // Surprise 必抽榜：仅在打勾开启时优先（按顺序，仅抽仍在奖池中的数字，抽中即出榜）；
    // 不在奖池中的必抽榜数字保留在 remainMust，不打勾则正常随机
    if (g_app.surpriseOn) {
        for (const auto& m : g_app.mustPick) {
            auto it = std::find(tmpPool.begin(), tmpPool.end(), m);
            if (k < n && it != tmpPool.end()) {
                res.push_back(m);
                tmpPool.erase(it);
                if (g_app.avoidRecent > 0) {
                    recent.push_back(m);
                    while ((int)recent.size() > g_app.avoidRecent) recent.erase(recent.begin());
                }
                k++;
            } else {
                remainMust.push_back(m); // 未抽中或名额已满，保留在活跃列表
            }
        }
    }
    // 其余名额照常随机
    for (; k < n; k++) {
        std::vector<std::string> cand;
        if (g_app.unique && g_app.avoidRecent > 0) {
            std::set<std::string> rs(recent.begin(), recent.end());
            for (auto& x : tmpPool) if (!rs.count(x)) cand.push_back(x);
            if (cand.empty()) cand = tmpPool;
        } else cand = tmpPool;
        if (cand.empty()) break;
        std::string c = cand[std::uniform_int_distribution<size_t>(0, cand.size() - 1)(g_rng)];
        res.push_back(c);
        if (g_app.unique) {
            auto it = std::find(tmpPool.begin(), tmpPool.end(), c);
            if (it != tmpPool.end()) tmpPool.erase(it);
        }
        if (g_app.avoidRecent > 0) {
            recent.push_back(c);
            while ((int)recent.size() > g_app.avoidRecent) recent.erase(recent.begin());
        }
    }
    if (res.empty()) { SetStatus(L"没有可抽取的学号。"); return; }

    g_animList = res;
    g_animResult = (res.size() == 1) ? res[0] : (std::to_string(res.size()) + " 个");
    g_animActive = true;
    g_animStart = GetTickCount64();
    g_animDur = clampv(g_app.animMs, 300, 10000);
    g_pendingPool = tmpPool;
    g_pendingRecent = recent;
    g_pendingMust = remainMust;
    SetTimer(g_hwnd, 1, 40, NULL);
    SetStatus(L"抽取中…");
    updateStats();
}

static void finalizePick() {
    g_app.pool = g_pendingPool;
    g_app.recent = g_pendingRecent;
    g_app.mustPick = g_pendingMust; // 出榜：抽中的从活跃列表移除，配置 mustPickCfg 不变
    std::string t = nowStr();
    for (auto& r : g_animList) g_app.history.push_back({ r, t });
    g_displayText = to_wide(g_animResult);
    if (g_animList.size() == 1)
        g_displaySub = L"按 空格 / 回车 再抽一次";
    else
        g_displaySub = L"已抽取 " + std::to_wstring(g_animList.size()) + L" 个，详见左侧记录";
    InvalidateDisplay();
    playChime();
    refreshHistory();
    updateStats();
    saveState();
}

static void resetPool() {
    if (g_animActive) { SetStatus(L"动画进行中，请稍候…"); return; }
    buildPool(g_app);
    g_app.recent.clear();
    g_app.mustPick = g_app.mustPickCfg; // 重置奖池时恢复完整必抽榜
    g_displayText = L"?";
    g_displaySub = L"已重置奖池，点击大卡片或按空格";
    InvalidateDisplay();
    refreshHistory();
    updateStats();
    saveState();
    SetStatus(L"已重置奖池（全部学号回到奖池）。");
}

static void undoLast() {
    if (g_animActive) { SetStatus(L"动画进行中，请稍候…"); return; }
    if (g_app.history.empty()) { SetStatus(L"没有可撤销的记录。"); return; }
    auto last = g_app.history.back();
    g_app.history.pop_back();
    if (g_app.unique) {
        if (std::find(g_app.pool.begin(), g_app.pool.end(), last.first) == g_app.pool.end())
            g_app.pool.push_back(last.first);
    }
    if (!g_app.recent.empty() && g_app.recent.back() == last.first) g_app.recent.pop_back();
    g_displayText = to_wide(last.first);
    g_displaySub = L"已撤销上一次抽取";
    InvalidateDisplay();
    refreshHistory();
    updateStats();
    saveState();
}

static void clearHistory() {
    if (g_animActive) { SetStatus(L"动画进行中，请稍候…"); return; }
    if (g_app.history.empty()) { SetStatus(L"记录已经是空的。"); return; }
    if (MessageBoxW(g_hwnd, L"确定要清空所有抽取记录吗？\n（奖池不受影响）", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_app.history.clear();
        g_app.recent.clear();
        refreshHistory(); updateStats(); saveState();
        SetStatus(L"已清空抽取记录。");
    }
}

static void exportCSV() {
    wchar_t fn[MAX_PATH] = L"抽取记录.csv";
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fn; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string out;
    out += "\xEF\xBB\xBF"; // UTF-8 BOM，便于 Excel 正确识别中文
    out += "序号,学号,时间\n";
    int i = 1;
    for (auto& h : g_app.history) out += std::to_string(i++) + "," + h.first + "," + h.second + "\n";
    if (write_file_utf8(fn, out, false))
        MessageBoxW(g_hwnd, (L"已导出 " + std::to_wstring(g_app.history.size()) + L" 条记录。").c_str(),
                    L"完成", MB_OK | MB_ICONINFORMATION);
}

static void toggleFull() {
    if (!g_full) {
        g_savedStyle = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
        GetWindowRect(g_hwnd, &g_savedRect);
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, g_savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX));
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        g_full = true;
        SetWindowTextW(g_hBtnFull, L"退出全屏");
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, g_savedStyle);
        SetWindowPos(g_hwnd, HWND_TOP, g_savedRect.left, g_savedRect.top,
                     g_savedRect.right - g_savedRect.left, g_savedRect.bottom - g_savedRect.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        g_full = false;
        SetWindowTextW(g_hBtnFull, L"全屏");
    }
}

// ============================== 大数字卡片窗口 ==============================
// 预渲染"背景透出 + 半透明圆角卡片"到缓存位图（不含文字），动画期间每帧只复制缓存再画数字，
// 整个画面在内存 DC 中拼好一次 BitBlt 上屏（双缓冲），杜绝闪烁
static void BuildCard(HWND disp, int w, int h) {
    if (g_cardDC) { DeleteDC(g_cardDC); g_cardDC = NULL; }
    if (g_cardBmp) { DeleteObject(g_cardBmp); g_cardBmp = NULL; }
    if (w <= 0 || h <= 0) return;
    HDC hdc = GetDC(g_hwnd);
    g_cardDC = CreateCompatibleDC(hdc);
    g_cardBmp = CreateCompatibleBitmap(hdc, w, h);
    ReleaseDC(g_hwnd, hdc);
    if (!g_cardDC || !g_cardBmp) { if (g_cardDC) { DeleteDC(g_cardDC); g_cardDC = NULL; } if (g_cardBmp) { DeleteObject(g_cardBmp); g_cardBmp = NULL; } return; }
    SelectObject(g_cardDC, g_cardBmp);
    // 1) 透出主窗口合成背景（含白色蒙版的图片）
    if (g_bgDC && g_bgW > 0 && g_bgH > 0) {
        POINT off = { 0, 0 };
        MapWindowPoints(disp, g_hwnd, &off, 1);
        BitBlt(g_cardDC, 0, 0, w, h, g_bgDC, off.x, off.y, SRCCOPY);
    } else {
        RECT rc = { 0, 0, w, h }; FillRect(g_cardDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
    // 2) 半透明白色圆角卡片（GDI+ 抗锯齿；保证数字清晰，同时透出背景）
    int mgn = (int)(14 * g_scale);
    RECT cr = { mgn, mgn, w - mgn, h - mgn };
    {
        float r = (float)(28 * g_scale);
        float wd = (float)(cr.right - cr.left), ht = (float)(cr.bottom - cr.top);
        float x0 = (float)cr.left, y0 = (float)cr.top;
        if (r > wd / 2) r = (float)(wd / 2);
        if (r > ht / 2) r = (float)(ht / 2);
        Gdiplus::Graphics g(g_cardDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        path.AddArc(x0, y0, r * 2, r * 2, 180, 90);
        path.AddArc(x0 + wd - r * 2, y0, r * 2, r * 2, 270, 90);
        path.AddArc(x0 + wd - r * 2, y0 + ht - r * 2, r * 2, r * 2, 0, 90);
        path.AddArc(x0, y0 + ht - r * 2, r * 2, r * 2, 90, 90);
        path.CloseFigure();
        Gdiplus::SolidBrush card(Gdiplus::Color(205, 255, 255, 255));
        g.FillPath(&card, &path);
        Gdiplus::Pen pen(Gdiplus::Color(175, 205, 205, 205), (float)(2 * g_scale));
        g.DrawPath(&pen, &path);
    }
    g_cardW = w; g_cardH = h;
}

static LRESULT CALLBACK DisplayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1; // 背景在 WM_PAINT 中整体重绘，避免闪烁
    if (m == WM_SIZE) { BuildCard(h, LOWORD(l), HIWORD(l)); return 0; }
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        // 缓存尺寸不匹配时重建（背景/卡片部分）
        if (g_cardW != W || g_cardH != H) BuildCard(h, W, H);
        // 内存 DC 双缓冲：全部绘制到内存，最后一次性上屏
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
        HGDIOBJ oldMem = SelectObject(memDC, memBmp);
        if (g_cardDC) BitBlt(memDC, 0, 0, W, H, g_cardDC, 0, 0, SRCCOPY);
        else { FillRect(memDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH)); }
        int mgn = (int)(14 * g_scale);
        RECT cr = { rc.left + mgn, rc.top + mgn, rc.right - mgn, rc.bottom - mgn };
        // 大数字（自适应字号）
        int availW = cr.right - cr.left - (int)(24 * g_scale);
        int availH = cr.bottom - cr.top - (int)(24 * g_scale);
        int fs = (int)(availH * 0.6);
        if (fs < 14) fs = 14;
        HFONT fbest = NULL;
        while (true) {
            HFONT f = makeFont(fs, true);
            HGDIOBJ old = SelectObject(memDC, f);
            SIZE sz; GetTextExtentPoint32W(memDC, g_displayText.c_str(), (int)g_displayText.size(), &sz);
            SelectObject(memDC, old);
            if (sz.cx <= availW || fs <= 14) { fbest = f; break; }
            DeleteObject(f); fs -= 4; if (fs < 14) fs = 14;
        }
        HGDIOBJ oldF = SelectObject(memDC, fbest);
        SetTextColor(memDC, C_TEXT);
        SetBkMode(memDC, TRANSPARENT);
        RECT tr = cr;
        DrawTextW(memDC, g_displayText.c_str(), (int)g_displayText.size(), &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, oldF); DeleteObject(fbest);

        if (!g_displaySub.empty()) {
            HFONT sf = makeFont((int)(15 * g_scale), false);
            HGDIOBJ os = SelectObject(memDC, sf);
            SetTextColor(memDC, C_SUB);
            RECT sr = { cr.left, cr.bottom - (int)(46 * g_scale), cr.right, cr.bottom - (int)(14 * g_scale) };
            DrawTextW(memDC, g_displaySub.c_str(), (int)g_displaySub.size(), &sr, DT_CENTER | DT_SINGLELINE);
            SelectObject(memDC, os); DeleteObject(sf);
        }
        // 一次性上屏
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldMem);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_LBUTTONDOWN) { SendMessageW(g_hwnd, UM_PICK, 0, 0); return 0; }
    if (m == WM_SETCURSOR) { SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND)); return TRUE; }
    return DefWindowProcW(h, m, w, l);
}

// ============================== 抽取记录日志框底部的署名条（头像 + by notxcm，点击跳转 GitHub） ==============================
static LRESULT CALLBACK SignatureProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        // 纯白底，与上方日志框（LISTBOX 白底）连成一体
        FillRect(hdc, &rc, g_hBrushWhite);
        // 顶部浅灰分隔线，作为日志框的内底部分隔
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(225, 225, 225));
        HGDIOBJ op = SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.right, rc.top);
        SelectObject(hdc, op); DeleteObject(pen);
        int pad = (int)(6 * g_scale);
        int curX = pad;
        // 1) 作者头像（缩小为圆形小头像）
        if (g_avatarImage && g_avatarImage->GetLastStatus() == Gdiplus::Ok) {
            int ah = H - (int)(8 * g_scale);
            int iw = g_avatarImage->GetWidth(), ih = g_avatarImage->GetHeight();
            int aw = (ih > 0) ? (int)((double)ah * iw / ih) : ah;
            if (aw > H - 4) aw = H - 4;
            if (aw < 8) aw = 8;
            int ay = (H - ah) / 2;
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath clip;
            clip.AddEllipse((float)curX, (float)ay, (float)aw, (float)ah);
            g.SetClip(&clip);
            g.DrawImage(g_avatarImage, curX, ay, aw, ah);
            g.ResetClip();
            curX += aw + (int)(8 * g_scale);
        }
        // 2) 署名 by notxcm（粗体）
        HFONT sf = makeFont((int)(12 * g_scale), true);
        HGDIOBJ os = SelectObject(hdc, sf);
        SetTextColor(hdc, C_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        RECT tr = { curX, 0, curX + 400, H };
        DrawTextW(hdc, L"by notxcm", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SIZE sz; GetTextExtentPoint32W(hdc, L"by notxcm", (int)wcslen(L"by notxcm"), &sz);
        curX += sz.cx + (int)(6 * g_scale);
        // 3) 灰色小字提示跳转目标
        HFONT sf2 = makeFont((int)(11 * g_scale), false);
        HGDIOBJ os2 = SelectObject(hdc, sf2);
        SetTextColor(hdc, C_SUB);
        RECT hr = { curX, 0, W - pad, H };
        DrawTextW(hdc, L"github.com/notxcm", -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, os2); DeleteObject(sf2);
        SelectObject(hdc, os); DeleteObject(sf);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_LBUTTONUP) {
        // 点击整条署名区，跳转到作者 GitHub 主页
        ShellExecuteW(NULL, L"open", L"https://github.com/notxcm", NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }
    if (m == WM_SETCURSOR) { SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND)); return TRUE; }
    return DefWindowProcW(h, m, w, l);
}

static void RegisterDisplayClass() {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = DisplayProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = L"NumDisplayClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    RegisterClassW(&wc);
    WNDCLASSW ws = { 0 };
    ws.lpfnWndProc = SignatureProc;
    ws.hInstance = g_hInst;
    ws.lpszClassName = L"SignatureClass";
    ws.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    ws.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    RegisterClassW(&ws);
}

// ============================== 设置面板（已平铺到主窗口） ==============================
static void syncSettingsPanel() {
    std::string rtext;
    for (auto& r : g_app.ranges) {
        char b[128]; snprintf(b, sizeof b, "%lld,%lld,%lld,%d\n", r.start, r.end, r.step, r.width);
        rtext += b;
    }
    std::string mtext, etext;
    for (auto& x : g_app.manual) mtext += x + "\n";
    for (auto& x : g_app.excluded) etext += x + "\n";
    SetWindowTextW(g_hEdRanges, to_wide(rtext).c_str());
    SetWindowTextW(g_hEdManual, to_wide(mtext).c_str());
    SetWindowTextW(g_hEdExclude, to_wide(etext).c_str());
    wchar_t b[32];
    swprintf(b, 32, L"%d", g_app.animMs); SetWindowTextW(g_hEdAnim, b);
    swprintf(b, 32, L"%.2f", g_app.scaleUI); SetWindowTextW(g_hEdScale, b);
    CheckDlgButton(g_hwnd, IDC_CHK_SOUND, g_app.sound ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hwnd, IDC_CHK_UNIQUE2, g_app.unique ? BST_CHECKED : BST_UNCHECKED);
}

static void ApplySettings() {
    if (g_animActive) { SetStatus(L"动画进行中，请稍候…"); return; }
    std::string rtext = to_utf8(GetWndTextW(g_hEdRanges));
    std::string mtext = to_utf8(GetWndTextW(g_hEdManual));
    std::string etext = to_utf8(GetWndTextW(g_hEdExclude));
    parseRanges(rtext, g_app.ranges);
    g_app.manual = split_lines(mtext);
    g_app.excluded.clear();
    for (auto& e : split_lines(etext)) if (!e.empty()) g_app.excluded.insert(e);
    wchar_t buf[64];
    GetWindowTextW(g_hEdAnim, buf, 64);
    g_app.animMs = clampv(_wtoi(buf), 100, 10000);
    GetWindowTextW(g_hEdScale, buf, 64);
    g_app.scaleUI = clampv(_wtof(buf), 0.7, 2.0);
    g_app.sound = (IsDlgButtonChecked(g_hwnd, IDC_CHK_SOUND) == BST_CHECKED);
    g_app.unique = (IsDlgButtonChecked(g_hwnd, IDC_CHK_UNIQUE2) == BST_CHECKED);
    buildPool(g_app);
    applyTheme(); BuildBg(g_bgW, g_bgH); UpdateFonts(); Layout();
    InvalidateRect(g_hwnd, NULL, TRUE); InvalidateDisplay();
    refreshHistory(); updateStats(); syncQuickControls(); saveState();
    SetStatus(L"设置已应用。");
}

// ============================== Surprise 惊喜窗口 ==============================
// 光明正大放在原「设置」位置：极简小窗，输入框填数字（空格分隔多个）。
// 开关在主界面 Surprise 按钮旁的复选框「必选」控制：打勾则每次抽取优先
// 命中榜内数字（按顺序，仍在奖池中的才抽，抽中即出榜）；必抽榜配置永久保存，
// 重置奖池或重启后恢复完整列表；不打勾则完全正常抽取。
static INT_PTR CALLBACK SurpriseDlg(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INITDIALOG) {
        std::wstring cur;
        for (size_t i = 0; i < g_app.mustPickCfg.size(); i++) {
            if (i) cur += L" ";
            cur += to_wide(g_app.mustPickCfg[i]);
        }
        SetWindowTextW(GetDlgItem(h, IDC_ED_SUP), cur.c_str());
        return TRUE;
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == IDOK) {
            std::wstring txt = GetWndTextW(GetDlgItem(h, IDC_ED_SUP));
            std::string s = to_utf8(txt);
            g_app.mustPick.clear();
            g_app.mustPickCfg.clear();
            for (auto& x : split_csv(s)) {
                std::string t = trim(x);
                if (!t.empty()) { g_app.mustPick.push_back(t); g_app.mustPickCfg.push_back(t); }
            }
            saveState();
            syncQuickControls();
            if (g_app.mustPickCfg.empty()) {
                SetStatus(L"Surprise 已清空。");
            } else if (g_app.surpriseOn) {
                SetStatus(L"Surprise 已开启：" + to_wide(g_app.mustPick[0]) + L" 将优先命中。");
            } else {
                SetStatus(L"数字已保存，勾选「必选」即生效。");
            }
            EndDialog(h, 1);
            return TRUE;
        }
        if (id == IDC_BTN_SUPCLEAR) {
            g_app.mustPick.clear();
            g_app.mustPickCfg.clear();
            saveState();
            SetStatus(L"Surprise 已清空。");
            EndDialog(h, 1);
            return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(h, 0); return TRUE; }
    }
    return FALSE;
}

// ============================== 主窗口过程 ==============================
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        return 0;
    case WM_SIZE: {
        int w = LOWORD(l), hh = HIWORD(l);
        BuildBg(w, hh);
        Layout();
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* r = (RECT*)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g_scale = GetScale();
        UpdateFonts(); Layout(); InvalidateRect(h, NULL, TRUE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        if (g_bgBmp) BitBlt(hdc, 0, 0, g_bgW, g_bgH, g_bgDC, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, C_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)w; SetTextColor(hdc, C_TEXT); SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)g_hBrushWhite;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w; SetTextColor(hdc, C_TEXT); SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)g_hBrushWhite;
    }
    case UM_PICK:
        doPick();
        return 0;
    case WM_TIMER:
        if (w == 1) {
            ULONGLONG el = GetTickCount64() - g_animStart;
            double p = (double)el / g_animDur;
            if (p >= 1.0) {
                KillTimer(h, 1); g_animActive = false; finalizePick();
            } else {
                std::string scr;
                if (!g_app.pool.empty())
                    scr = g_app.pool[std::uniform_int_distribution<size_t>(0, g_app.pool.size() - 1)(g_rng)];
                else if (!g_app.mustPick.empty())
                    scr = g_app.mustPick[0];
                else
                    scr = "—";
                g_displayText = to_wide(scr); g_displaySub = L"";
                InvalidateDisplay();
                UINT interval = (UINT)(30 + p * p * 150);
                SetTimer(h, 1, interval, NULL);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE || w == VK_RETURN) {
            HWND f = GetFocus();
            if (f == g_hEdN || f == g_hEdRecent || f == g_hHistory) return 0;
            doPick(); return 0;
        }
        if (w == 'R' || w == 'r') { resetPool(); return 0; }
        if (w == 'F' || w == 'f') { toggleFull(); return 0; }
        if (w == VK_ESCAPE && g_full) { toggleFull(); return 0; }
        return DefWindowProcW(h, m, w, l);
    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
        case IDC_BTNPICK: doPick(); break;
        case IDC_BTNRESET: resetPool(); break;
        case IDC_BTNUNDO: undoLast(); break;
        case IDC_BTNSETTINGS: DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_SURPRISE), h, SurpriseDlg); break;
        case IDC_BTNEXPORT: exportCSV(); break;
        case IDC_BTNCLEAR: clearHistory(); break;
        case IDC_BTNFULL: toggleFull(); break;
        case IDC_BTN_APPLY: ApplySettings(); break;
        case IDC_CHK_SUP:
            if (HIWORD(w) == BN_CLICKED) {
                // 一级复选框：打勾则 Surprise 必选生效，不打勾则正常抽取（立即保存）
                g_app.surpriseOn = (IsDlgButtonChecked(h, IDC_CHK_SUP) == BST_CHECKED);
                saveState();
                if (g_app.surpriseOn) {
                    if (g_app.mustPick.empty())
                        SetStatus(L"Surprise 已开启，但还没有填写数字，请点按钮填写。");
                    else
                        SetStatus(L"Surprise 已开启：" + to_wide(g_app.mustPick[0]) + L" 将优先命中。");
                } else {
                    SetStatus(L"Surprise 已关闭，正常抽取。");
                }
            }
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        saveState();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_cardDC) { DeleteDC(g_cardDC); g_cardDC = NULL; }
        if (g_cardBmp) { DeleteObject(g_cardBmp); g_cardBmp = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================== 控件创建 ==============================
static void CreateControls() {
    g_hStats = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_STATS, g_hInst, NULL);
    g_hLblHist = CreateWindowW(L"STATIC", L"抽取记录", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBLHIST, g_hInst, NULL);
    g_hHistory = CreateWindowW(L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_HISTORY, g_hInst, NULL);
    g_hBtnPick = CreateWindowW(L"BUTTON", L"🎲 抽号", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNPICK, g_hInst, NULL);
    g_hBtnReset = CreateWindowW(L"BUTTON", L"重置奖池", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNRESET, g_hInst, NULL);
    g_hBtnUndo = CreateWindowW(L"BUTTON", L"撤销", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNUNDO, g_hInst, NULL);
    g_hBtnSettings = CreateWindowW(L"BUTTON", L"Surprise", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNSETTINGS, g_hInst, NULL);
    g_hChkSupMain = CreateWindowW(L"BUTTON", L"必选", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_CHK_SUP, g_hInst, NULL);
    g_hBtnExport = CreateWindowW(L"BUTTON", L"导出", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNEXPORT, g_hInst, NULL);
    g_hBtnClear = CreateWindowW(L"BUTTON", L"清空记录", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNCLEAR, g_hInst, NULL);
    g_hBtnFull = CreateWindowW(L"BUTTON", L"全屏", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTNFULL, g_hInst, NULL);
    g_hLblN = CreateWindowW(L"STATIC", L"一次抽取：", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBLN, g_hInst, NULL);
    g_hEdN = CreateWindowW(L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_EDN, g_hInst, NULL);
    g_hChkUnique = CreateWindowW(L"BUTTON", L"不重复", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_CHKUNIQUE, g_hInst, NULL);
    g_hLblRecent = CreateWindowW(L"STATIC", L"避免最近：", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBLRECENT, g_hInst, NULL);
    g_hEdRecent = CreateWindowW(L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_EDRECENT, g_hInst, NULL);
    g_hStatus = CreateWindowW(L"STATIC", L"就绪", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_STATUS, g_hInst, NULL);
    g_hSignature = CreateWindowW(L"SignatureClass", L"", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_SIG, g_hInst, NULL);

    // 设置面板（平铺在主窗口中间的设置列）
    g_hLblRanges = CreateWindowW(L"STATIC", L"学号范围（每行：起始,结束,步长,宽度）", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBL_RANGES, g_hInst, NULL);
    g_hEdRanges = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_MULTILINE | WS_VSCROLL | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_ED_RANGES, g_hInst, NULL);
    g_hLblManual = CreateWindowW(L"STATIC", L"手动添加（每行一个）", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBL_MANUAL, g_hInst, NULL);
    g_hEdManual = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_MULTILINE | WS_VSCROLL | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_ED_MANUAL, g_hInst, NULL);
    g_hLblExclude = CreateWindowW(L"STATIC", L"排除学号（每行一个）", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBL_EXCLUDE, g_hInst, NULL);
    g_hEdExclude = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_MULTILINE | WS_VSCROLL | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_ED_EXCLUDE, g_hInst, NULL);
    g_hLblAnim = CreateWindowW(L"STATIC", L"动画ms:", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBL_ANIM, g_hInst, NULL);
    g_hEdAnim = CreateWindowW(L"EDIT", L"1200", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_ED_ANIM, g_hInst, NULL);
    g_hLblScale = CreateWindowW(L"STATIC", L"缩放:", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_LBL_SCALE, g_hInst, NULL);
    g_hEdScale = CreateWindowW(L"EDIT", L"1.00", WS_VISIBLE | WS_CHILD | WS_BORDER, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_ED_SCALE, g_hInst, NULL);
    g_hChkSound = CreateWindowW(L"BUTTON", L"开启音效", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_CHK_SOUND, g_hInst, NULL);
    g_hChkUnique2 = CreateWindowW(L"BUTTON", L"默认不重复", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_CHK_UNIQUE2, g_hInst, NULL);
    g_hBtnApply = CreateWindowW(L"BUTTON", L"应用设置", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_BTN_APPLY, g_hInst, NULL);
}

// ============================== 入口 ==============================
#ifdef UNIT_TEST
int main() {
    int fails = 0;
    auto check = [&](bool c, const char* name) { printf("%s: %s\n", c ? "PASS" : "FAIL", name); if (!c) fails++; };

    // 范围 + 排除 + 手动
    defaultConfig();
    g_app.ranges.clear();
    g_app.ranges.push_back({ 1, 10, 1, 0 });
    g_app.ranges.push_back({ 20, 22, 1, 0 });
    g_app.manual.push_back("S99");
    g_app.excluded.insert("3"); g_app.excluded.insert("21");
    buildPool(g_app);
    check(g_app.pool.size() == 10 + 3 + 1 - 2, "buildPool size with exclude+manual");
    check(std::find(g_app.pool.begin(), g_app.pool.end(), std::string("3")) == g_app.pool.end(), "exclude 3 removed");
    check(std::find(g_app.pool.begin(), g_app.pool.end(), std::string("21")) == g_app.pool.end(), "exclude 21 removed");
    check(std::find(g_app.pool.begin(), g_app.pool.end(), std::string("S99")) != g_app.pool.end(), "manual S99 present");

    // 宽度补零
    g_app = App(); g_app.ranges.push_back({ 1, 3, 1, 4 }); buildPool(g_app);
    check(g_app.pool[0] == "0001", "zero-padding width=4");

    // 抽取移除
    g_app = App(); g_app.ranges.push_back({ 1, 5, 1, 0 }); g_app.unique = true; buildPool(g_app);
    size_t before = g_app.pool.size();
    std::string r; bool ok = false;
    for (int i = 0; i < 5; i++) {
        std::vector<std::string> cand = g_app.pool;
        if (cand.empty()) break;
        r = cand[std::uniform_int_distribution<size_t>(0, cand.size() - 1)(g_rng)];
        auto it = std::find(g_app.pool.begin(), g_app.pool.end(), r);
        if (it != g_app.pool.end()) g_app.pool.erase(it); else ok = true;
    }
    check(g_app.pool.size() == 0, "unique pick exhausts pool");
    check(!ok, "picked item was in pool");

    // 配置存取往返
    g_app = App(); g_app.ranges.push_back({ 5, 8, 2, 3 }); g_app.manual.push_back("X1"); g_app.excluded.insert("7");
    g_app.animMs = 900; g_app.scaleUI = 1.25; g_app.theme = 2; g_app.history.push_back({ "005", "2026-01-01 00:00:00" });
    buildPool(g_app);
    std::wstring tp = L"C:\\Users\\imjia\\__cxunit_state.txt";
    saveConfig(tp);
    App saved = g_app;
    g_app = App();
    bool loaded = loadConfig(tp);
    check(loaded, "loadConfig returns true");
    check(g_app.ranges.size() == 1 && g_app.ranges[0].start == 5 && g_app.ranges[0].end == 8, "range round-trip");
    check(g_app.excluded.count("7") == 1, "excluded round-trip");
    check(g_app.animMs == 900 && g_app.theme == 2, "settings round-trip");
    check(g_app.history.size() == 1 && g_app.history[0].first == "005", "history round-trip");
    DeleteFileW(tp.c_str());

    // 必抽榜配置往返（mustPickCfg 永久保存，mustPick 从 mustPickCfg 恢复）
    g_app = App(); g_app.mustPick.push_back("42"); g_app.mustPick.push_back("7"); g_app.mustPickCfg = g_app.mustPick; g_app.surpriseOn = true;
    saveConfig(tp);
    g_app = App();
    loaded = loadConfig(tp);
    check(loaded && g_app.mustPickCfg.size() == 2 && g_app.mustPickCfg[0] == "42" && g_app.mustPickCfg[1] == "7", "mustPickCfg round-trip");
    check(g_app.mustPick.size() == 2 && g_app.mustPick == g_app.mustPickCfg, "mustPick restored from cfg on load");
    check(g_app.surpriseOn, "surpriseOn round-trip");
    DeleteFileW(tp.c_str());

    // 模拟抽取出榜后 mustPick 减少，但 mustPickCfg 不变；保存后重启恢复
    g_app = App(); g_app.ranges.push_back({ 1, 10, 1, 0 }); buildPool(g_app);
    g_app.mustPick.push_back("1"); g_app.mustPick.push_back("2"); g_app.mustPickCfg = g_app.mustPick;
    g_app.surpriseOn = true; g_app.unique = true; g_app.nPick = 1;
    // 模拟抽中 "1"：mustPick 出榜
    g_app.mustPick.erase(g_app.mustPick.begin());
    auto it1 = std::find(g_app.pool.begin(), g_app.pool.end(), std::string("1"));
    if (it1 != g_app.pool.end()) g_app.pool.erase(it1);
    check(g_app.mustPick.size() == 1 && g_app.mustPick[0] == "2", "mustPick consumed after pick");
    check(g_app.mustPickCfg.size() == 2, "mustPickCfg unchanged after pick");
    // 保存后重启
    saveConfig(tp);
    g_app = App();
    loaded = loadConfig(tp);
    check(g_app.mustPickCfg.size() == 2 && g_app.mustPickCfg[0] == "1", "mustPickCfg persists across restart");
    check(g_app.mustPick.size() == 2, "mustPick restored from cfg on restart");
    DeleteFileW(tp.c_str());

    // 音效生成
    synthWav(g_wavChime, 660, 880, 0.5, 0.3);
    check(g_wavChime.size() == 44 + 44100, "wav size correct");
    check(memcmp(g_wavChime.data(), "RIFF", 4) == 0, "wav RIFF header");

    printf("\n%s (%d failures)\n", fails == 0 ? "ALL PASS" : "HAS FAILURES", fails);
    return fails == 0 ? 0 : 1;
}

#else

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    g_hInst = hInst;
    Gdiplus::GdiplusStartupInput gsi; Gdiplus::GdiplusStartup(&g_gdipToken, &gsi, NULL);
    INITCOMMONCONTROLSEX icc; icc.dwSize = sizeof(icc); icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
    InitDPI();
    RegisterDisplayClass();
    synthWav(g_wavChime, 660, 880, 0.5, 0.35);
    synthWav(g_wavTick, 1200, 1200, 0.05, 0.12);
    LoadBgImage(); // 加载嵌入的背景图资源
    LoadAvatarImage(); // 加载嵌入的作者头像资源

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MainWinClass";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON));
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, L"MainWinClass", L"抽学号 Ultra · 随机点名 by notxcm",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1320, 720,
                             NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    g_hDisplay = CreateWindowExW(0, L"NumDisplayClass", L"",
                                 WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, (HMENU)IDC_DISPLAY, hInst, NULL);

    CreateControls();
    applyTheme();
    UpdateFonts();

    // 载入上次状态，否则使用默认
    if (!loadConfig(statePath())) defaultConfig();
    syncSettingsPanel();
    syncQuickControls();
    BuildBg(1320, 720);
    InvalidateRect(g_hwnd, NULL, TRUE);
    refreshHistory();
    updateStats();

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hwnd && IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_bgImage) { delete g_bgImage; g_bgImage = NULL; }
    if (g_avatarImage) { delete g_avatarImage; g_avatarImage = NULL; }
    Gdiplus::GdiplusShutdown(g_gdipToken);
    return (int)msg.wParam;
}
#endif
