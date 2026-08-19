#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include "lucide_icons.h"
#include "separation_engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {

constexpr wchar_t kClassName[] = L"UARMusic.FriendlyWindow";
constexpr wchar_t kTitle[] = L"UAR Music";

enum : int {
    IDC_INPUT = 2001,
    IDC_OUTPUT,
    IDC_MODELS,
    IDC_SUFFIX,
    IDC_SEGMENT,
    IDC_OVERLAP,
    IDC_BATCH,
    IDC_LOG,
    IDC_STATUS,
    IDC_PROGRESS,
    IDC_BROWSE_INPUT = 2101,
    IDC_BROWSE_OUTPUT,
    IDC_BROWSE_MODELS,
    IDC_START = 2201,
    IDC_THEME,
};

COLORREF kBackground = RGB(246, 246, 239);
COLORREF kTopbar = RGB(246, 246, 239);
COLORREF kCard = RGB(254, 254, 248);
COLORREF kSurface = RGB(241, 241, 233);
COLORREF kSurfaceHover = RGB(227, 228, 218);
COLORREF kLogSurface = RGB(249, 249, 243);
COLORREF kBorder = RGB(190, 192, 179);
COLORREF kSeparator = RGB(215, 216, 205);
COLORREF kText = RGB(15, 16, 13);
COLORREF kSecondary = RGB(73, 75, 68);
COLORREF kTertiary = RGB(112, 114, 104);
COLORREF kAccent = RGB(91, 108, 0);
COLORREF kAccentDown = RGB(48, 49, 44);
COLORREF kGreen = RGB(0, 112, 126);
COLORREF kOrange = RGB(204, 54, 34);
COLORREF kRed = RGB(185, 42, 48);

struct UiLayout {
    RECT client{};
    RECT topbar{};
    RECT content{};
    RECT header{};
    RECT heroWave{};
    RECT heroMeta{};
    RECT themeButton{};
    RECT statusPill{};
    RECT statusText{};
    RECT sourceCard{};
    RECT engineCard{};
    RECT resultBand{};
    RECT queueCard{};
    RECT footer{};
    std::array<RECT, 2> pathFields{};
    std::array<RECT, 2> browseButtons{};
    RECT logSurface{};
    RECT logEdit{};
    RECT progressText{};
    RECT progressTrack{};
    RECT startButton{};
};

struct AppState {
    HINSTANCE instance{};
    HWND window{};
    HWND input{};
    HWND output{};
    HWND models{};
    HWND suffix{};
    HWND segment{};
    HWND overlap{};
    HWND batch{};
    HWND log{};
    HWND status{};
    HWND progressText{};
    HWND start{};
    HWND theme{};
    std::array<HWND, 3> browse{};
    HFONT regular{};
    HFONT medium{};
    HFONT semibold{};
    HFONT small{};
    HFONT title{};
    HFONT display{};
    HFONT hero{};
    HFONT eyebrow{};
    HFONT mono{};
    HBRUSH surfaceBrush{};
    HBRUSH logBrush{};
    HICON appIcon{};
    std::unique_ptr<uar::SeparationEngine> engine;
    std::thread worker;
    std::atomic_bool cancel{false};
    std::atomic_bool busy{false};
    bool logStarted = false;
    UINT dpi = 96;
    double uiScale = 1.0;
    double progress = 0.0;
    COLORREF statusColor = kAccent;
    std::wstring statusValue = L"等待输入";
    std::wstring progressValue = L"选择输入与输出目录";
    std::array<double, 72> wave{};
    std::array<double, 72> waveVelocity{};
    double animationTime = 0.0;
    POINT pointer{-1000, -1000};
    bool pointerInside = false;
    bool animationsEnabled = true;
    bool lightTheme = true;
};

AppState g;

int Width(const RECT& rect);
int Height(const RECT& rect);
void RecreateFonts();

int ScaleAtDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

int S(int value) {
    return static_cast<int>(std::lround(static_cast<double>(value) * g.uiScale));
}

double ComputeUiScale(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (Width(client) <= 0 || Height(client) <= 0) return g.uiScale;
    const double dpiScale = static_cast<double>(g.dpi) / 96.0;
    const double widthScale = static_cast<double>(Width(client)) / 1260.0;
    const double heightScale = static_cast<double>(Height(client)) / 820.0;
    return std::clamp(std::min({dpiScale, widthScale, heightScale}), 0.74, 2.0);
}

void UpdateUiScale(HWND hwnd) {
    const double next = ComputeUiScale(hwnd);
    if (std::abs(next - g.uiScale) > 0.015) {
        g.uiScale = next;
        RecreateFonts();
    }
}

UINT SystemDpi() {
    using GetDpiForSystemFn = UINT(WINAPI*)();
    FARPROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
    GetDpiForSystemFn function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    if (function) return std::max<UINT>(96, function());
    HDC screen = GetDC(nullptr);
    const int fallback = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);
    return static_cast<UINT>(std::max(96, fallback));
}

void EnablePerMonitorDpi() {
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    FARPROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    SetProcessDpiAwarenessContextFn function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    if (function) function(reinterpret_cast<HANDLE>(-4));
}

UINT WindowDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    FARPROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    GetDpiForWindowFn function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function ? function(hwnd) : SystemDpi();
}

std::wstring ExecutableDirectory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    std::wstring value(buffer.data(), length);
    const size_t slash = value.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

std::string PathText(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

RECT R(int left, int top, int right, int bottom) {
    return RECT{left, top, right, bottom};
}

int Width(const RECT& rect) {
    return rect.right - rect.left;
}

int Height(const RECT& rect) {
    return rect.bottom - rect.top;
}

RECT Inset(RECT rect, int x, int y) {
    rect.left += x;
    rect.right -= x;
    rect.top += y;
    rect.bottom -= y;
    return rect;
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, width), color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void RoundBox(HDC dc, const RECT& rect, int radius, COLORREF fill,
              COLORREF border = CLR_INVALID, int borderWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, borderWidth), border == CLR_INVALID ? fill : border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void Text(HDC dc, const wchar_t* value, RECT rect, UINT flags, HFONT font, COLORREF color) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

HWND Static(HWND parent, int id, const wchar_t* value) {
    HWND control = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", value,
                                   WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   0, 0, 0, 0, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g.instance, nullptr);
    return control;
}

HWND Edit(HWND parent, int id, const wchar_t* value, DWORD style = 0) {
    HWND control = CreateWindowExW(0, L"EDIT", value,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | style,
                                   0, 0, 0, 0, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g.instance, nullptr);
    return control;
}

HWND Button(HWND parent, int id, const wchar_t* value) {
    HWND control = CreateWindowExW(0, L"BUTTON", value,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   0, 0, 0, 0, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g.instance, nullptr);
    return control;
}

std::wstring Value(HWND control) {
    const int length = static_cast<int>(SendMessageW(control, WM_GETTEXTLENGTH, 0, 0));
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(control, WM_GETTEXT, static_cast<WPARAM>(length + 1),
                 reinterpret_cast<LPARAM>(value.data()));
    value.resize(static_cast<size_t>(length));
    return value;
}

void SetValue(HWND control, const std::wstring& value) {
    SetWindowTextW(control, value.c_str());
}

void DestroyFonts() {
    const std::array<HFONT*, 9> fonts{
        &g.regular, &g.medium, &g.semibold, &g.small, &g.title,
        &g.display, &g.hero, &g.eyebrow, &g.mono
    };
    for (HFONT* font : fonts) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
}

HFONT MakeFont(int logicalHeight, int weight, const wchar_t* face) {
    return CreateFontW(-S(logicalHeight), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, face);
}

void RecreateFonts() {
    DestroyFonts();
    g.regular = MakeFont(15, FW_NORMAL, L"Microsoft YaHei UI");
    g.medium = MakeFont(15, FW_MEDIUM, L"Microsoft YaHei UI");
    g.semibold = MakeFont(18, FW_SEMIBOLD, L"Microsoft YaHei UI");
    g.small = MakeFont(13, FW_NORMAL, L"Microsoft YaHei UI");
    g.title = MakeFont(18, FW_SEMIBOLD, L"Segoe UI");
    g.display = MakeFont(30, FW_SEMIBOLD, L"Microsoft YaHei UI");
    g.hero = MakeFont(44, FW_BOLD, L"Microsoft YaHei UI");
    g.eyebrow = MakeFont(12, FW_SEMIBOLD, L"Segoe UI");
    g.mono = MakeFont(13, FW_NORMAL, L"Microsoft YaHei UI");

    const std::array<HWND, 13> controls{
        g.input, g.output, g.models, g.suffix, g.segment, g.overlap, g.batch,
        g.log, g.status, g.progressText, g.browse[0], g.start, g.theme
    };
    for (HWND control : controls) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.regular), TRUE);
    }
    if (g.browse[1]) SendMessageW(g.browse[1], WM_SETFONT, reinterpret_cast<WPARAM>(g.medium), TRUE);
    if (g.browse[2]) SendMessageW(g.browse[2], WM_SETFONT, reinterpret_cast<WPARAM>(g.medium), TRUE);
    if (g.browse[0]) SendMessageW(g.browse[0], WM_SETFONT, reinterpret_cast<WPARAM>(g.medium), TRUE);
    if (g.start) SendMessageW(g.start, WM_SETFONT, reinterpret_cast<WPARAM>(g.semibold), TRUE);
    if (g.status) SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.medium), TRUE);
    if (g.progressText) SendMessageW(g.progressText, WM_SETFONT, reinterpret_cast<WPARAM>(g.medium), TRUE);
    if (g.log) SendMessageW(g.log, WM_SETFONT, reinterpret_cast<WPARAM>(g.mono), TRUE);
}

UiLayout Measure(HWND hwnd) {
    UiLayout layout;
    GetClientRect(hwnd, &layout.client);
    const int width = Width(layout.client);
    const int height = Height(layout.client);
    const int minMargin = S(34);
    const int idealWidth = S(1160);
    const int contentWidth = std::min(idealWidth, std::max(1, width - minMargin * 2));
    const int contentLeft = (width - contentWidth) / 2;
    const int contentRight = contentLeft + contentWidth;
    const int gap = S(16);

    layout.topbar = R(0, 0, width, S(64));
    layout.content = R(contentLeft, layout.topbar.bottom, contentRight, height);
    layout.header = R(contentLeft, S(82), contentRight, S(286));
    const int heroSplit = contentLeft + contentWidth * 53 / 100;
    layout.heroWave = R(heroSplit + S(26), S(118), contentRight, S(244));
    layout.heroMeta = R(heroSplit + S(26), S(250), contentRight, S(278));
    layout.themeButton = R(contentRight - S(236), S(15),
                           contentRight - S(202), S(49));
    const int statusWidth = S(166);
    layout.statusPill = R(contentRight - statusWidth, S(82), contentRight, S(112));
    layout.statusText = R(layout.statusPill.left + S(33), layout.statusPill.top,
                          layout.statusPill.right - S(12), layout.statusPill.bottom);

    const int cardsTop = S(306);
    const int cardsHeight = S(204);
    const int totalCardsWidth = contentRight - contentLeft;
    const int cardWidth = (totalCardsWidth - gap) / 2;
    layout.sourceCard = R(contentLeft, cardsTop, contentLeft + cardWidth, cardsTop + cardsHeight);
    layout.engineCard = R(layout.sourceCard.right + gap, cardsTop, contentRight, cardsTop + cardsHeight);

    const int footerHeight = S(64);
    const int footerBottom = height - S(16);
    layout.footer = R(contentLeft, footerBottom - footerHeight, contentRight, footerBottom);
    const int lowerTop = layout.sourceCard.bottom + S(16);
    const int lowerBottom = layout.footer.top - S(14);
    const int queueWidth = totalCardsWidth * 63 / 100;
    layout.queueCard = R(contentLeft, lowerTop, contentLeft + queueWidth, lowerBottom);
    layout.resultBand = R(layout.queueCard.right + gap, lowerTop, contentRight, lowerBottom);

    const int cardInset = S(20);
    layout.browseButtons[0] = R(layout.sourceCard.left + cardInset, layout.sourceCard.top + S(87),
                                layout.sourceCard.right - cardInset, layout.sourceCard.top + S(139));
    layout.pathFields[0] = R(layout.sourceCard.left + cardInset, layout.sourceCard.top + S(151),
                             layout.sourceCard.right - cardInset, layout.sourceCard.top + S(187));
    layout.browseButtons[1] = R(layout.engineCard.left + cardInset, layout.engineCard.top + S(87),
                                layout.engineCard.right - cardInset, layout.engineCard.top + S(139));
    layout.pathFields[1] = R(layout.engineCard.left + cardInset, layout.engineCard.top + S(151),
                             layout.engineCard.right - cardInset, layout.engineCard.top + S(187));

    layout.logSurface = R(layout.queueCard.left + S(20), layout.queueCard.top + S(54),
                          layout.queueCard.right - S(20), layout.queueCard.bottom - S(16));
    layout.logEdit = Inset(layout.logSurface, S(12), S(8));

    const int startWidth = S(232);
    layout.startButton = R(layout.footer.right - startWidth, layout.footer.top + S(5),
                           layout.footer.right, layout.footer.bottom - S(5));
    layout.progressText = R(layout.footer.left, layout.footer.top + S(7),
                            layout.startButton.left - S(24), layout.footer.top + S(31));
    layout.progressTrack = R(layout.footer.left, layout.footer.top + S(43),
                             layout.startButton.left - S(24), layout.footer.top + S(47));
    return layout;
}

void PlaceEdit(HWND control, const RECT& field, bool log = false) {
    const int insetX = log ? 0 : S(11);
    const int insetY = log ? 0 : S(8);
    MoveWindow(control, field.left + insetX, field.top + insetY,
               std::max(1, Width(field) - insetX * 2),
               std::max(1, Height(field) - insetY * 2), TRUE);
}

void Layout(HWND hwnd) {
    UpdateUiScale(hwnd);
    const UiLayout layout = Measure(hwnd);
    PlaceEdit(g.input, layout.pathFields[0]);
    PlaceEdit(g.output, layout.pathFields[1]);
    for (size_t i = 0; i < 2; ++i) {
        const RECT& rect = layout.browseButtons[i];
        MoveWindow(g.browse[i], rect.left, rect.top, Width(rect), Height(rect), TRUE);
    }
    PlaceEdit(g.log, layout.logEdit, true);
    MoveWindow(g.status, layout.statusText.left, layout.statusText.top,
               Width(layout.statusText), Height(layout.statusText), TRUE);
    MoveWindow(g.progressText, layout.progressText.left, layout.progressText.top,
               Width(layout.progressText), Height(layout.progressText), TRUE);
    MoveWindow(g.start, layout.startButton.left, layout.startButton.top,
               Width(layout.startButton), Height(layout.startButton), TRUE);
    MoveWindow(g.theme, layout.themeButton.left, layout.themeButton.top,
               Width(layout.themeButton), Height(layout.themeButton), TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Gradient(HDC dc, const RECT& rect, COLORREF start, COLORREF end, bool vertical = false) {
    TRIVERTEX vertices[2]{
        {rect.left, rect.top, static_cast<COLOR16>(GetRValue(start) << 8),
         static_cast<COLOR16>(GetGValue(start) << 8), static_cast<COLOR16>(GetBValue(start) << 8), 0},
        {rect.right, rect.bottom, static_cast<COLOR16>(GetRValue(end) << 8),
         static_cast<COLOR16>(GetGValue(end) << 8), static_cast<COLOR16>(GetBValue(end) << 8), 0},
    };
    GRADIENT_RECT gradient{0, 1};
    GradientFill(dc, vertices, 2, &gradient, 1,
                 vertical ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
}

void Panel(HDC dc, const RECT& rect, COLORREF accent) {
    RoundBox(dc, rect, S(6), kCard, kBorder);
    Line(dc, rect.left + S(20), rect.top, rect.left + S(82), rect.top,
         accent, std::max(1, S(2)));
}

void DrawTopbar(HDC dc, const UiLayout& layout) {
    Fill(dc, layout.topbar, kTopbar);
    Line(dc, 0, layout.topbar.bottom - 1, layout.topbar.right,
         layout.topbar.bottom - 1, kSeparator, S(1));

    const RECT logo = R(layout.content.left, S(15), layout.content.left + S(34), S(49));
    RoundBox(dc, logo, S(4), kAccent, kAccent);
    uar::ui::DrawLucideIcon(dc, uar::ui::LucideIcon::AudioWaveform,
                            Inset(logo, S(8), S(8)), kBackground, 2);
    Text(dc, L"UAR / MUSIC", R(logo.right + S(12), S(13), logo.right + S(184), S(39)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.title, kText);
    Text(dc, L"STEM LAB  01.01", R(logo.right + S(13), S(36), logo.right + S(184), S(54)),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, kTertiary);

    const int privacyLeft = layout.content.right - S(190);
    uar::ui::DrawLucideIcon(dc, uar::ui::LucideIcon::ShieldCheck,
                            R(privacyLeft, S(22), privacyLeft + S(18), S(40)), kGreen, 2);
    Text(dc, L"LOCAL ONLY / 不上传", R(privacyLeft + S(28), S(14), layout.content.right, S(50)),
         DT_RIGHT | DT_SINGLELINE | DT_VCENTER, g.eyebrow, kSecondary);
}

void DrawWaveform(HDC dc, const RECT& rect) {
    if (Width(rect) <= 0 || Height(rect) <= 0) return;
    const COLORREF fineGrid = g.lightTheme ? RGB(224, 225, 215) : RGB(27, 29, 25);
    const COLORREF centerGrid = g.lightTheme ? RGB(204, 206, 193) : RGB(38, 43, 34);
    Line(dc, rect.left, rect.top, rect.right, rect.top, kBorder, S(1));
    Line(dc, rect.left, rect.bottom, rect.right, rect.bottom, kBorder, S(1));
    const int centerY = rect.top + Height(rect) / 2;
    Line(dc, rect.left, centerY, rect.right, centerY, centerGrid, S(1));
    for (int i = 1; i < 8; ++i) {
        const int x = rect.left + Width(rect) * i / 8;
        Line(dc, x, rect.top, x, rect.bottom, fineGrid, S(1));
    }

    std::array<POINT, 72> primary{};
    std::array<POINT, 72> secondary{};
    for (size_t i = 0; i < primary.size(); ++i) {
        const double ratio = static_cast<double>(i) / static_cast<double>(primary.size() - 1);
        const int x = rect.left + static_cast<int>(ratio * Width(rect));
        const double envelope = 0.35 + 0.65 * std::sin(ratio * 3.141592653589793);
        primary[i] = POINT{x, centerY + static_cast<int>(g.wave[i] * Height(rect) * 0.34 * envelope)};
        const double trace = std::sin(g.animationTime * 0.86 + ratio * 19.0) *
                             std::sin(ratio * 3.141592653589793) * 0.16;
        secondary[i] = POINT{x, centerY + static_cast<int>(trace * Height(rect))};
    }
    HPEN secondaryPen = CreatePen(PS_SOLID, std::max(1, S(1)), kOrange);
    HGDIOBJ oldPen = SelectObject(dc, secondaryPen);
    Polyline(dc, secondary.data(), static_cast<int>(secondary.size()));
    SelectObject(dc, oldPen);
    DeleteObject(secondaryPen);

    HPEN primaryPen = CreatePen(PS_SOLID, std::max(1, S(2)), kAccent);
    oldPen = SelectObject(dc, primaryPen);
    Polyline(dc, primary.data(), static_cast<int>(primary.size()));
    SelectObject(dc, oldPen);
    DeleteObject(primaryPen);
}

void DrawHeader(HDC dc, const UiLayout& layout) {
    Text(dc, L"NEURAL STEM SEPARATION / 02 CHANNELS",
         R(layout.header.left, layout.header.top, layout.header.right, layout.header.top + S(22)),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, kAccent);
    Text(dc, L"拆开一首歌",
         R(layout.header.left, layout.header.top + S(26), layout.heroWave.left - S(28),
           layout.header.top + S(82)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.hero, kText);
    Text(dc, L"听见两种可能",
         R(layout.header.left, layout.header.top + S(84), layout.heroWave.left - S(28),
           layout.header.top + S(144)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.hero, kText);
    Text(dc, L"VOCAL", R(layout.header.left, layout.header.top + S(159),
         layout.header.left + S(74), layout.header.bottom),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, kOrange);
    Text(dc, L"/", R(layout.header.left + S(76), layout.header.top + S(157),
         layout.header.left + S(92), layout.header.bottom),
         DT_CENTER | DT_SINGLELINE, g.eyebrow, kTertiary);
    Text(dc, L"INSTRUMENTAL", R(layout.header.left + S(96), layout.header.top + S(159),
         layout.header.left + S(216), layout.header.bottom),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, kGreen);

    RoundBox(dc, layout.statusPill, S(15), kCard, kBorder);
    const auto statusIcon = g.statusColor == kGreen
        ? uar::ui::LucideIcon::CheckCircle2 : uar::ui::LucideIcon::Activity;
    uar::ui::DrawLucideIcon(dc, statusIcon,
                            R(layout.statusPill.left + S(10), layout.statusPill.top + S(7),
                              layout.statusPill.left + S(26), layout.statusPill.bottom - S(7)),
                            g.statusColor, 2);
    Text(dc, g.statusValue.c_str(), layout.statusText,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.small, g.statusColor);

    DrawWaveform(dc, layout.heroWave);
    Text(dc, L"LIVE SIGNAL", layout.heroMeta,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.eyebrow, kTertiary);
    Text(dc, L"44.1 kHz  /  DUAL MODEL  /  OFFLINE",
         layout.heroMeta, DT_RIGHT | DT_SINGLELINE | DT_VCENTER, g.eyebrow, kSecondary);
}

void DrawField(HDC dc, const RECT& field, HWND control) {
    const bool focused = GetFocus() == control;
    RoundBox(dc, field, S(4), kSurface, focused ? kAccent : kBorder,
             focused ? S(2) : S(1));
}

void DrawStepCard(HDC dc, const RECT& card, const wchar_t* number,
                  const wchar_t* eyebrow, const wchar_t* title,
                  uar::ui::LucideIcon icon, COLORREF color,
                  const RECT& field, HWND control) {
    Panel(dc, card, color);
    uar::ui::DrawLucideIcon(dc, icon,
                            R(card.left + S(20), card.top + S(20),
                              card.left + S(44), card.top + S(44)), color, 2);
    Text(dc, eyebrow, R(card.left + S(56), card.top + S(17),
         card.right - S(86), card.top + S(36)),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, color);
    Text(dc, title, R(card.left + S(56), card.top + S(35),
         card.right - S(76), card.top + S(68)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.semibold, kText);
    Text(dc, number, R(card.right - S(76), card.top + S(10),
         card.right - S(20), card.top + S(62)),
         DT_RIGHT | DT_SINGLELINE | DT_VCENTER, g.display,
         g.lightTheme ? RGB(218, 219, 207) : RGB(56, 59, 50));
    DrawField(dc, field, control);
}

void DrawSourceCard(HDC dc, const UiLayout& layout) {
    DrawStepCard(dc, layout.sourceCard, L"01", L"SOURCE / INPUT",
                 L"输入歌曲目录", uar::ui::LucideIcon::FolderOpen, kAccent,
                 layout.pathFields[0], g.input);
}

void DrawEngineCard(HDC dc, const UiLayout& layout) {
    DrawStepCard(dc, layout.engineCard, L"02", L"DESTINATION / OUTPUT",
                 L"分轨保存目录", uar::ui::LucideIcon::FolderDown, kGreen,
                 layout.pathFields[1], g.output);
}

void DrawResults(HDC dc, const UiLayout& layout) {
    const RECT panel = layout.resultBand;
    Panel(dc, panel, kOrange);
    Text(dc, L"OUTPUT PAIR", R(panel.left + S(20), panel.top + S(16),
         panel.right - S(20), panel.top + S(36)),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, kOrange);
    Text(dc, L"双轨输出", R(panel.left + S(20), panel.top + S(35),
         panel.right - S(20), panel.top + S(66)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.semibold, kText);

    const int firstTop = panel.top + S(74);
    const int rowHeight = std::max(S(42),
        static_cast<int>((panel.bottom - firstTop - S(12)) / 2));
    const std::array<uar::ui::LucideIcon, 2> icons{
        uar::ui::LucideIcon::MicVocal, uar::ui::LucideIcon::Music2
    };
    const std::array<const wchar_t*, 2> labels{L"VOCAL", L"INSTRUMENTAL"};
    const std::array<const wchar_t*, 2> titles{L"人声分轨", L"伴奏分轨"};
    const std::array<COLORREF, 2> colors{kAccent, kGreen};
    for (int i = 0; i < 2; ++i) {
        const int top = firstTop + rowHeight * i;
        if (i > 0) Line(dc, panel.left + S(20), top,
                        panel.right - S(20), top, kSeparator, S(1));
        uar::ui::DrawLucideIcon(dc, icons[static_cast<size_t>(i)],
                                R(panel.left + S(20), top + S(12),
                                  panel.left + S(43), top + S(35)),
                                colors[static_cast<size_t>(i)], 2);
        Text(dc, labels[static_cast<size_t>(i)],
             R(panel.left + S(58), top + S(7), panel.right - S(20), top + S(24)),
             DT_LEFT | DT_SINGLELINE, g.eyebrow, colors[static_cast<size_t>(i)]);
        Text(dc, titles[static_cast<size_t>(i)],
             R(panel.left + S(58), top + S(22), panel.right - S(20), top + S(46)),
             DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.medium, kText);
    }
}

void DrawQueue(HDC dc, const UiLayout& layout) {
    Panel(dc, layout.queueCard, g.statusColor);
    uar::ui::DrawLucideIcon(dc, uar::ui::LucideIcon::Activity,
                            R(layout.queueCard.left + S(20), layout.queueCard.top + S(18),
                              layout.queueCard.left + S(40), layout.queueCard.top + S(38)),
                            g.statusColor, 2);
    Text(dc, L"PROCESS MONITOR", R(layout.queueCard.left + S(52), layout.queueCard.top + S(13),
         layout.queueCard.right - S(20), layout.queueCard.top + S(31)),
         DT_LEFT | DT_SINGLELINE, g.eyebrow, g.statusColor);
    Text(dc, L"处理进度", R(layout.queueCard.left + S(52), layout.queueCard.top + S(28),
         layout.queueCard.right - S(20), layout.queueCard.top + S(50)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.medium, kText);

    if (Height(layout.logSurface) <= S(16)) return;
    RoundBox(dc, layout.logSurface, S(4), kLogSurface, kSeparator);
    if (!g.logStarted) {
        uar::ui::DrawLucideIcon(dc, uar::ui::LucideIcon::AudioWaveform,
                                R(layout.logSurface.left + S(14), layout.logSurface.top + S(14),
                                  layout.logSurface.left + S(36), layout.logSurface.top + S(36)),
                                kTertiary, 2);
        Text(dc, L"WAITING FOR SOURCE", R(layout.logSurface.left + S(48), layout.logSurface.top + S(10),
             layout.logSurface.right - S(14), layout.logSurface.top + S(30)),
             DT_LEFT | DT_SINGLELINE, g.eyebrow, kTertiary);
        Text(dc, L"当前无处理任务", R(layout.logSurface.left + S(48), layout.logSurface.top + S(28),
             layout.logSurface.right - S(14), layout.logSurface.bottom - S(8)),
             DT_LEFT | DT_SINGLELINE, g.small, kSecondary);
    }
}

void DrawFooter(HDC dc, const UiLayout& layout) {
    Line(dc, layout.footer.left, layout.footer.top, layout.footer.right, layout.footer.top,
         kSeparator, S(1));
    Text(dc, g.progressValue.c_str(), layout.progressText,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.medium, kSecondary);
    RoundBox(dc, layout.progressTrack, S(2), RGB(39, 42, 35), RGB(39, 42, 35));
    const int filled = static_cast<int>(Width(layout.progressTrack) * std::clamp(g.progress, 0.0, 1.0));
    if (filled > 0) {
        RECT bar = layout.progressTrack;
        bar.right = bar.left + filled;
        Gradient(dc, bar, kAccent, kGreen);
    }
}

void DrawCanvasTexture(HDC dc, const UiLayout& layout) {
    Fill(dc, layout.client, kBackground);
    Gradient(dc, R(0, layout.topbar.bottom, Width(layout.client), S(300)),
             g.lightTheme ? RGB(235, 236, 225) : RGB(17, 18, 14),
             kBackground, true);
    for (int i = 0; i < 14; ++i) {
        const int x = layout.content.left + Width(layout.content) * i / 13;
        Line(dc, x, layout.topbar.bottom, x, layout.header.bottom,
             g.lightTheme ? RGB(231, 232, 222) : RGB(19, 21, 17), S(1));
    }
    Line(dc, layout.content.left - S(14), layout.header.top,
         layout.content.left - S(14), layout.footer.bottom, kAccent, S(2));
}

void Paint(HWND hwnd, HDC target) {
    const UiLayout layout = Measure(hwnd);
    const int width = Width(layout.client);
    const int height = Height(layout.client);
    if (width <= 0 || height <= 0) return;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    DrawCanvasTexture(dc, layout);
    DrawTopbar(dc, layout);
    DrawHeader(dc, layout);
    DrawSourceCard(dc, layout);
    DrawEngineCard(dc, layout);
    DrawResults(dc, layout);
    DrawQueue(dc, layout);
    DrawFooter(dc, layout);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void StepAnimation(HWND hwnd) {
    const UiLayout layout = Measure(hwnd);
    const RECT stage = layout.heroWave;
    const bool pointerOnStage = g.pointerInside && PtInRect(&stage, g.pointer);
    const double pointerIndex = pointerOnStage
        ? static_cast<double>(g.pointer.x - stage.left) /
              std::max(1, Width(stage)) * static_cast<double>(g.wave.size() - 1)
        : -1000.0;
    const double pointerY = pointerOnStage
        ? std::clamp(static_cast<double>(g.pointer.y - (stage.top + Height(stage) / 2)) /
                         std::max(1, Height(stage) / 2), -1.0, 1.0)
        : 0.0;
    const double energy = g.busy.load() ? 1.0 : 0.58;
    for (size_t i = 0; i < g.wave.size(); ++i) {
        const double position = static_cast<double>(i);
        double target = (std::sin(g.animationTime * 1.55 + position * 0.43) * 0.34 +
                         std::sin(g.animationTime * 0.77 - position * 0.19) * 0.18) * energy;
        if (pointerOnStage) {
            const double distance = position - pointerIndex;
            target += pointerY * std::exp(-(distance * distance) / 72.0) * 0.82;
        }
        g.waveVelocity[i] += (target - g.wave[i]) * 0.085;
        g.waveVelocity[i] *= 0.84;
        g.wave[i] += g.waveVelocity[i];
    }
    g.animationTime += g.animationsEnabled ? 0.075 : 0.006;
    RECT dirty = layout.header;
    dirty.top = layout.statusPill.top;
    InvalidateRect(hwnd, &dirty, FALSE);
}

void Log(const std::wstring& line) {
    if (!g.log) return;
    if (!g.logStarted) {
        g.logStarted = true;
        ShowWindow(g.log, SW_SHOW);
    }
    SendMessageW(g.log, EM_SETREADONLY, FALSE, 0);
    SendMessageW(g.log, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    const std::wstring text = line + L"\r\n";
    SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g.log, EM_SETREADONLY, TRUE, 0);
    SendMessageW(g.log, EM_SCROLLCARET, 0, 0);
}

void SetStatus(const wchar_t* value, COLORREF color) {
    g.statusColor = color;
    g.statusValue = value;
    SetWindowTextW(g.status, value);
    InvalidateRect(g.window, nullptr, FALSE);
}

void SetProgressText(const std::wstring& value) {
    g.progressValue = value;
    SetWindowTextW(g.progressText, value.c_str());
    InvalidateRect(g.window, nullptr, FALSE);
}

void UpdateReadyState() {
    if (g.busy.load()) return;
    const bool hasSongs = !Value(g.input).empty();
    const bool hasOutput = !Value(g.output).empty();
    g.progress = 0.0;
    if (hasSongs && hasOutput) {
        SetStatus(L"就绪", kGreen);
        SetProgressText(L"目录已就绪，可以开始分轨");
    } else if (hasSongs || hasOutput) {
        SetStatus(L"等待目录", kOrange);
        SetProgressText(hasSongs ? L"还需选择输出目录" : L"还需选择歌曲目录");
    } else {
        SetStatus(L"等待输入", kAccent);
        SetProgressText(L"选择输入与输出目录");
    }
}

void BrowseFolder(HWND owner, HWND target) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetValue(target, path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

void SetBusyControls(bool busy) {
    for (HWND button : g.browse) EnableWindow(button, !busy);
    EnableWindow(g.input, !busy);
    EnableWindow(g.output, !busy);
    EnableWindow(g.models, !busy);
    EnableWindow(g.suffix, !busy);
    EnableWindow(g.segment, !busy);
    EnableWindow(g.overlap, !busy);
    EnableWindow(g.batch, !busy);
}

void FinishJob(const wchar_t* status, bool success) {
    g.busy.store(false);
    SetBusyControls(false);
    SetWindowTextW(g.start, success ? L"再次分轨" : L"重新开始");
    if (success) g.progress = 1.0;
    if (success) SetProgressText(L"处理完成，双轨文件已写入输出目录");
    else if (g.cancel.load()) SetProgressText(L"任务已停止，可以重新开始");
    else SetProgressText(L"处理未完成，请查看进度记录");
    SetStatus(status, success ? kGreen : (g.cancel.load() ? kTertiary : kRed));
    InvalidateRect(g.window, nullptr, FALSE);
}

void RunJob(std::wstring inputText, std::wstring outputText, std::wstring modelText,
            std::wstring suffixText, int segment, int overlap, int batch) {
    auto post = [](std::function<void()> callback) {
        auto* heap = new std::function<void()>(std::move(callback));
        PostMessageW(g.window, WM_APP + 1, 0, reinterpret_cast<LPARAM>(heap));
    };
    auto log = [&](const std::string& value) { post([value] { Log(Utf8ToWide(value)); }); };
    auto progress = [&](double value) {
        post([value] {
            g.progress = value;
            std::wostringstream text;
            text << L"正在分轨 / " << static_cast<int>(value * 100.0) << L"%";
            SetProgressText(text.str());
            InvalidateRect(g.window, nullptr, FALSE);
        });
    };

    std::string error;
    try {
        const std::filesystem::path input(inputText);
        const std::filesystem::path output(outputText);
        const std::filesystem::path modelDir = modelText.empty()
            ? std::filesystem::path(ExecutableDirectory()) / L"models"
            : std::filesystem::path(modelText);
        uar::SeparationConfig config;
        config.bsModel = modelDir / L"bs_roformer_quantized.onnx";
        config.melModel = modelDir / L"mel_band_roformer_vocals.onnx";
        config.outputSuffix = std::move(suffixText);
        config.segmentSize = segment;
        config.overlap = overlap;
        config.batchSize = batch;
        if (!g.engine->ValidateModels(config, error)) {
            log("程序文件不完整，请重新打开或重新安装。");
            post([] { FinishJob(L"引擎异常", false); });
            return;
        }

        const auto files = uar::ScanAudioFiles(input);
        if (files.empty()) {
            log("这个文件夹里没有找到歌曲，请换一个文件夹。");
            post([] { FinishJob(L"未发现音频", false); });
            return;
        }

        log("找到 " + std::to_string(files.size()) + " 首歌曲，开始分轨。");
        const std::filesystem::path temp = output / L".ncm_decoded";
        size_t completed = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            if (g.cancel.load()) break;
            std::filesystem::path source = files[i];
            std::wstring extension = source.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
            if (extension == L".ncm") {
                log("正在打开：" + PathText(source.filename()));
                if (!uar::DecryptNcm(source, temp, source, error, &g.cancel)) {
                    log("这首歌没有打开成功，可以换一首再试。");
                    continue;
                }
            }
            log("正在制作：" + PathText(source.filename()));
            const bool ok = g.engine->ProcessFile(
                source, output, config,
                {log, [&, i](double value) {
                    progress((static_cast<double>(i) + value) / files.size());
                }},
                g.cancel, error);
            if (!ok) {
                if (error == "歌曲太长")
                    log("这首歌太长了，请换一首短一些的歌曲。");
                else
                    log("这首歌没有制作成功，可以换一首再试。");
                if (g.cancel.load()) break;
                continue;
            }
            ++completed;
        }

        if (g.cancel.load()) {
            log("已经停下来。");
            post([] { FinishJob(L"任务已停止", false); });
        } else if (completed == files.size()) {
            log("全部歌曲都做好了！");
            post([] { FinishJob(L"处理完成", true); });
        } else {
            log("有几首歌曲没有成功，请看看上面的消息。");
            post([] { FinishJob(L"部分未完成", false); });
        }
    } catch (const std::exception& exception) {
        (void)exception;
        log("制作时遇到了问题，请再试一次。");
        post([] { FinishJob(L"处理失败", false); });
    }
}

void StartJob() {
    if (g.busy.load()) {
        g.cancel.store(true);
        SetStatus(L"正在停止", kOrange);
        Log(L"正在停止任务，请稍候。");
        return;
    }
    const std::wstring inputText = Value(g.input);
    const std::wstring outputText = Value(g.output);
    if (inputText.empty() || outputText.empty()) {
        Log(L"请先完成上面的第 1 步和第 2 步。");
        SetStatus(L"等待目录", kOrange);
        return;
    }

    g.cancel.store(false);
    g.busy.store(true);
    g.progress = 0.0;
    SetWindowTextW(g.log, L"");
    SetBusyControls(true);
    SetWindowTextW(g.start, L"停止任务");
    SetProgressText(L"正在初始化双模型引擎");
    SetStatus(L"正在分轨", kOrange);
    Log(L"分轨任务已开始。");
    const int segment = std::max(1, _wtoi(Value(g.segment).c_str()));
    const int overlap = std::clamp(_wtoi(Value(g.overlap).c_str()), 0, 90);
    const int batch = std::max(1, _wtoi(Value(g.batch).c_str()));
    if (g.worker.joinable()) g.worker.join();
    g.worker = std::thread(RunJob, inputText, outputText, Value(g.models),
                           Value(g.suffix), segment, overlap, batch);
}

void DrawButton(const DRAWITEMSTRUCT* item) {
    const bool primary = item->CtlID == IDC_START;
    const bool chooseSongs = item->CtlID == IDC_BROWSE_INPUT;
    const bool chooseOutput = item->CtlID == IDC_BROWSE_OUTPUT;
    const bool theme = item->CtlID == IDC_THEME;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
    COLORREF fill = pressed ? kSurfaceHover : (hot ? kSurfaceHover : kSurface);
    COLORREF border = focused ? kAccent : kBorder;
    COLORREF textColor = kText;
    if (primary) {
        fill = g.lightTheme ? (pressed ? RGB(48, 49, 44) : kText)
                            : (pressed ? kAccentDown : kAccent);
        border = fill;
        textColor = kBackground;
    } else if (chooseSongs) {
        fill = g.lightTheme
            ? (pressed ? RGB(224, 228, 202) : RGB(240, 242, 222))
            : (pressed ? RGB(37, 42, 24) : RGB(25, 28, 20));
        border = focused ? kAccent
                         : (g.lightTheme ? RGB(153, 165, 96) : RGB(74, 83, 42));
        textColor = kAccent;
    } else if (chooseOutput) {
        fill = g.lightTheme
            ? (pressed ? RGB(214, 233, 232) : RGB(232, 244, 241))
            : (pressed ? RGB(22, 38, 39) : RGB(19, 29, 29));
        border = focused ? kGreen
                         : (g.lightTheme ? RGB(110, 166, 169) : RGB(48, 77, 78));
        textColor = kGreen;
    } else if (theme) {
        fill = pressed ? kSurfaceHover : kTopbar;
        border = focused ? kAccent : kBorder;
        textColor = kText;
    }
    if (disabled) {
        fill = g.lightTheme ? RGB(230, 230, 222) : RGB(27, 28, 25);
        border = kSeparator;
        textColor = kTertiary;
    }
    Fill(item->hDC, item->rcItem,
         theme ? kTopbar : (primary ? kBackground : kCard));
    const RECT button = Inset(item->rcItem, S(1), S(1));
    RoundBox(item->hDC, button, S(4), fill, border, focused ? S(2) : S(1));
    if (!theme) {
        wchar_t value[128]{};
        GetWindowTextW(item->hwndItem, value, static_cast<int>(std::size(value)));
        RECT label = button;
        label.left += S(49);
        label.right -= primary ? S(48) : S(18);
        Text(item->hDC, value, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER,
             primary ? g.semibold : g.medium, textColor);
    }
    uar::ui::LucideIcon icon = uar::ui::LucideIcon::Play;
    if (primary && g.busy.load()) icon = uar::ui::LucideIcon::Square;
    else if (primary && g.logStarted) icon = uar::ui::LucideIcon::RotateCcw;
    else if (chooseSongs) icon = uar::ui::LucideIcon::FolderOpen;
    else if (chooseOutput) icon = uar::ui::LucideIcon::FolderDown;
    else if (theme) icon = g.lightTheme ? uar::ui::LucideIcon::Moon
                                        : uar::ui::LucideIcon::Sun;
    const int iconSize = primary ? S(20) : S(19);
    const int centerY = button.top + Height(button) / 2;
    const int iconLeft = theme ? button.left + (Width(button) - iconSize) / 2
                               : button.left + S(20);
    uar::ui::DrawLucideIcon(item->hDC, icon,
                            R(iconLeft, centerY - iconSize / 2,
                              iconLeft + iconSize, centerY + iconSize / 2),
                            textColor, 2);
    if (primary) {
        uar::ui::DrawLucideIcon(item->hDC, uar::ui::LucideIcon::ArrowUpRight,
                                R(button.right - S(34), centerY - S(8),
                                  button.right - S(18), centerY + S(8)),
                                textColor, 2);
    }
}

void SetCue(HWND control, const wchar_t* text) {
    constexpr UINT kSetCueBanner = 0x1501;
    SendMessageW(control, kSetCueBanner, TRUE, reinterpret_cast<LPARAM>(text));
}

void CreateUi(HWND hwnd) {
    g.dpi = WindowDpi(hwnd);
    g.uiScale = 1.0;
    g.surfaceBrush = CreateSolidBrush(kSurface);
    g.logBrush = CreateSolidBrush(kLogSurface);
    g.engine = std::make_unique<uar::SeparationEngine>();

    g.input = Edit(hwnd, IDC_INPUT, L"", ES_AUTOHSCROLL);
    g.output = Edit(hwnd, IDC_OUTPUT, L"", ES_AUTOHSCROLL);
    const std::wstring defaultModels = ExecutableDirectory() + L"\\models";
    g.models = Edit(hwnd, IDC_MODELS, defaultModels.c_str(), ES_AUTOHSCROLL);
    g.suffix = Edit(hwnd, IDC_SUFFIX, L"", ES_AUTOHSCROLL);
    g.segment = Edit(hwnd, IDC_SEGMENT, L"256", ES_NUMBER | ES_CENTER);
    g.overlap = Edit(hwnd, IDC_OVERLAP, L"8", ES_NUMBER | ES_CENTER);
    g.batch = Edit(hwnd, IDC_BATCH, L"1", ES_NUMBER | ES_CENTER);
    g.log = Edit(hwnd, IDC_LOG, L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL);
    const wchar_t* controlTheme = g.lightTheme ? L"Explorer" : L"DarkMode_Explorer";
    SetWindowTheme(g.input, controlTheme, nullptr);
    SetWindowTheme(g.output, controlTheme, nullptr);
    SetWindowTheme(g.log, controlTheme, nullptr);
    ShowWindow(g.log, SW_HIDE);
    g.status = Static(hwnd, IDC_STATUS, L"等待输入");
    g.progressText = Static(hwnd, IDC_PROGRESS, L"选择输入与输出目录");
    ShowWindow(g.status, SW_HIDE);
    ShowWindow(g.progressText, SW_HIDE);
    g.browse[0] = Button(hwnd, IDC_BROWSE_INPUT, L"选择歌曲目录");
    g.browse[1] = Button(hwnd, IDC_BROWSE_OUTPUT, L"选择输出目录");
    g.browse[2] = Button(hwnd, IDC_BROWSE_MODELS, L"选择");
    g.start = Button(hwnd, IDC_START, L"开始分轨");
    g.theme = Button(hwnd, IDC_THEME, g.lightTheme ? L"切换至暗色主题" : L"切换至亮色主题");

    ShowWindow(g.models, SW_HIDE);
    ShowWindow(g.suffix, SW_HIDE);
    ShowWindow(g.segment, SW_HIDE);
    ShowWindow(g.overlap, SW_HIDE);
    ShowWindow(g.batch, SW_HIDE);
    ShowWindow(g.browse[2], SW_HIDE);

    SetCue(g.input, L"尚未选择歌曲目录");
    SetCue(g.output, L"尚未选择输出目录");
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) {
        g.animationsEnabled = animations != FALSE;
    }
    RecreateFonts();
    Layout(hwnd);
    SetTimer(hwnd, 1, g.animationsEnabled ? 33 : 180, nullptr);
}

void ApplyWindowAppearance(HWND hwnd) {
    BOOL dark = g.lightTheme ? FALSE : TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const int roundedCorners = 2;
    DwmSetWindowAttribute(hwnd, 33, &roundedCorners, sizeof(roundedCorners));
    const COLORREF caption = kTopbar;
    const COLORREF captionText = kText;
    const COLORREF border = kBackground;
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
}

void ApplyTheme(HWND hwnd, bool light) {
    enum class StatusTone { Accent, Green, Orange, Red, Tertiary };
    StatusTone tone = StatusTone::Accent;
    if (g.statusColor == kGreen) tone = StatusTone::Green;
    else if (g.statusColor == kOrange) tone = StatusTone::Orange;
    else if (g.statusColor == kRed) tone = StatusTone::Red;
    else if (g.statusColor == kTertiary) tone = StatusTone::Tertiary;

    g.lightTheme = light;
    if (light) {
        kBackground = RGB(246, 246, 239);
        kTopbar = RGB(246, 246, 239);
        kCard = RGB(254, 254, 248);
        kSurface = RGB(241, 241, 233);
        kSurfaceHover = RGB(227, 228, 218);
        kLogSurface = RGB(249, 249, 243);
        kBorder = RGB(190, 192, 179);
        kSeparator = RGB(215, 216, 205);
        kText = RGB(15, 16, 13);
        kSecondary = RGB(73, 75, 68);
        kTertiary = RGB(112, 114, 104);
        kAccent = RGB(91, 108, 0);
        kAccentDown = RGB(48, 49, 44);
        kGreen = RGB(0, 112, 126);
        kOrange = RGB(204, 54, 34);
        kRed = RGB(185, 42, 48);
    } else {
        kBackground = RGB(11, 12, 10);
        kTopbar = RGB(11, 12, 10);
        kCard = RGB(20, 21, 18);
        kSurface = RGB(14, 15, 13);
        kSurfaceHover = RGB(29, 31, 26);
        kLogSurface = RGB(16, 17, 15);
        kBorder = RGB(48, 51, 43);
        kSeparator = RGB(40, 42, 37);
        kText = RGB(244, 244, 237);
        kSecondary = RGB(173, 175, 164);
        kTertiary = RGB(112, 115, 105);
        kAccent = RGB(210, 244, 62);
        kAccentDown = RGB(174, 208, 28);
        kGreen = RGB(80, 208, 225);
        kOrange = RGB(255, 101, 76);
        kRed = RGB(255, 86, 91);
    }

    switch (tone) {
    case StatusTone::Accent: g.statusColor = kAccent; break;
    case StatusTone::Green: g.statusColor = kGreen; break;
    case StatusTone::Orange: g.statusColor = kOrange; break;
    case StatusTone::Red: g.statusColor = kRed; break;
    case StatusTone::Tertiary: g.statusColor = kTertiary; break;
    }

    if (g.surfaceBrush) DeleteObject(g.surfaceBrush);
    if (g.logBrush) DeleteObject(g.logBrush);
    g.surfaceBrush = CreateSolidBrush(kSurface);
    g.logBrush = CreateSolidBrush(kLogSurface);
    const wchar_t* controlTheme = light ? L"Explorer" : L"DarkMode_Explorer";
    SetWindowTheme(g.input, controlTheme, nullptr);
    SetWindowTheme(g.output, controlTheme, nullptr);
    SetWindowTheme(g.log, controlTheme, nullptr);
    SetWindowTextW(g.theme, light ? L"切换至暗色主题" : L"切换至亮色主题");
    ApplyWindowAppearance(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

HICON CreateAppIcon() {
    constexpr int size = 32;
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    HDC colorDc = CreateCompatibleDC(screen);
    HDC maskDc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, size, size);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ReleaseDC(nullptr, screen);
    if (!colorDc || !maskDc || !color || !mask) {
        if (colorDc) DeleteDC(colorDc);
        if (maskDc) DeleteDC(maskDc);
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        return nullptr;
    }

    HGDIOBJ oldColor = SelectObject(colorDc, color);
    HGDIOBJ oldMask = SelectObject(maskDc, mask);
    RECT iconRect{0, 0, size, size};
    Fill(colorDc, iconRect, kBackground);
    PatBlt(maskDc, 0, 0, size, size, BLACKNESS);
    uar::ui::DrawLucideIcon(colorDc, uar::ui::LucideIcon::AudioWaveform,
                            R(4, 4, size - 4, size - 4), kAccent, 2);
    SelectObject(colorDc, oldColor);
    SelectObject(maskDc, oldMask);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(colorDc);
    DeleteDC(maskDc);
    return icon;
}

void ResizeForCurrentMonitor(HWND hwnd) {
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return;
    const int availableWidth = Width(monitorInfo.rcWork);
    const int availableHeight = Height(monitorInfo.rcWork);
    const UINT dpi = WindowDpi(hwnd);
    const int width = std::min(ScaleAtDpi(1260, dpi), availableWidth * 9 / 10);
    const int height = std::min(ScaleAtDpi(820, dpi), availableHeight * 9 / 10);
    const int x = monitorInfo.rcWork.left + (availableWidth - width) / 2;
    const int y = monitorInfo.rcWork.top + (availableHeight - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateUi(hwnd);
        ApplyWindowAppearance(hwnd);
        return 0;
    case WM_SIZE:
        if (g.input) Layout(hwnd);
        return 0;
    case WM_TIMER:
        if (wParam == 1) {
            StepAnimation(hwnd);
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        g.pointer = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!g.pointerInside) {
            g.pointerInside = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.pointerInside = false;
        g.pointer = POINT{-1000, -1000};
        return 0;
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wParam);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     Width(*suggested), Height(*suggested),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RecreateFonts();
        Layout(hwnd);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = std::max(960, ScaleAtDpi(1000, g.dpi));
        info->ptMinTrackSize.y = std::max(680, ScaleAtDpi(720, g.dpi));
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code == BN_CLICKED) {
            if (id == IDC_BROWSE_INPUT) BrowseFolder(hwnd, g.input);
            else if (id == IDC_BROWSE_OUTPUT) BrowseFolder(hwnd, g.output);
            else if (id == IDC_BROWSE_MODELS) BrowseFolder(hwnd, g.models);
            else if (id == IDC_START) StartJob();
            else if (id == IDC_THEME) ApplyTheme(hwnd, !g.lightTheme);
            return 0;
        }
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (code == EN_CHANGE && (id == IDC_INPUT || id == IDC_OUTPUT)) {
            UpdateReadyState();
            return 0;
        }
        break;
    }
    case WM_APP + 1: {
        auto* callback = reinterpret_cast<std::function<void()>*>(lParam);
        if (callback) {
            (*callback)();
            delete callback;
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == g.log) {
            SetTextColor(dc, kSecondary);
            SetBkColor(dc, kLogSurface);
            return reinterpret_cast<LRESULT>(g.logBrush);
        }
        SetTextColor(dc, kText);
        SetBkColor(dc, kSurface);
        return reinterpret_cast<LRESULT>(g.surfaceBrush);
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == g.log) {
            SetTextColor(dc, kSecondary);
            SetBkColor(dc, kLogSurface);
            return reinterpret_cast<LRESULT>(g.logBrush);
        }
        SetTextColor(dc, control == g.status ? g.statusColor : kSecondary);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        Paint(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        if (g.busy.load()) {
            g.cancel.store(true);
            SetStatus(L"正在停止", kOrange);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        g.cancel.store(true);
        if (g.worker.joinable()) g.worker.join();
        g.engine.reset();
        DestroyFonts();
        if (g.surfaceBrush) DeleteObject(g.surfaceBrush);
        if (g.logBrush) DeleteObject(g.logBrush);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    EnablePerMonitorDpi();
    g.instance = instance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    g.appIcon = CreateAppIcon();
    windowClass.hIcon = g.appIcon;
    windowClass.hIconSm = g.appIcon;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const UINT dpi = SystemDpi();
    const int availableWidth = Width(work);
    const int availableHeight = Height(work);
    const int width = std::min(ScaleAtDpi(1260, dpi), availableWidth * 9 / 10);
    const int height = std::min(ScaleAtDpi(820, dpi), availableHeight * 9 / 10);
    const int x = work.left + (availableWidth - width) / 2;
    const int y = work.top + (availableHeight - height) / 2;

    g.window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, kTitle,
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               x, y, width, height,
                               nullptr, nullptr, instance, nullptr);
    if (!g.window) {
        CoUninitialize();
        return 1;
    }

    ResizeForCurrentMonitor(g.window);
    ShowWindow(g.window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    UpdateWindow(g.window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g.appIcon) DestroyIcon(g.appIcon);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
