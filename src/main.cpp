#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

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
constexpr wchar_t kTitle[] = L"歌曲小助手";

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
};

// 面向低龄用户的明亮配色：高对比文字，蓝、绿、橙分别表示选择、保存与结果。
constexpr COLORREF kBackground = RGB(247, 248, 251);
constexpr COLORREF kTopbar = RGB(255, 255, 255);
constexpr COLORREF kCard = RGB(255, 255, 255);
constexpr COLORREF kSurface = RGB(247, 248, 251);
constexpr COLORREF kSurfaceHover = RGB(237, 242, 250);
constexpr COLORREF kLogSurface = RGB(250, 250, 252);
constexpr COLORREF kBorder = RGB(225, 228, 234);
constexpr COLORREF kSeparator = RGB(231, 233, 238);
constexpr COLORREF kText = RGB(29, 29, 31);
constexpr COLORREF kSecondary = RGB(99, 99, 102);
constexpr COLORREF kTertiary = RGB(112, 112, 117);
constexpr COLORREF kAccent = RGB(0, 122, 255);
constexpr COLORREF kAccentDown = RGB(0, 93, 214);
constexpr COLORREF kGreen = RGB(36, 174, 91);
constexpr COLORREF kOrange = RGB(245, 139, 31);
constexpr COLORREF kRed = RGB(224, 62, 62);

struct UiLayout {
    RECT client{};
    RECT topbar{};
    RECT content{};
    RECT header{};
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
    std::array<HWND, 3> browse{};
    HFONT regular{};
    HFONT medium{};
    HFONT semibold{};
    HFONT small{};
    HFONT title{};
    HFONT display{};
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
    std::wstring statusValue = L"等你选择";
    std::wstring progressValue = L"先选歌曲，再选保存位置";
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

void Circle(HDC dc, int centerX, int centerY, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
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
    const std::array<HFONT*, 6> fonts{&g.regular, &g.medium, &g.semibold, &g.small, &g.title, &g.display};
    for (HFONT* font : fonts) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
}

HFONT MakeFont(int logicalHeight, int weight, const wchar_t* face) {
    return CreateFontW(-S(logicalHeight), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

void RecreateFonts() {
    DestroyFonts();
    g.regular = MakeFont(15, FW_NORMAL, L"Microsoft YaHei UI");
    g.medium = MakeFont(15, FW_MEDIUM, L"Microsoft YaHei UI");
    g.semibold = MakeFont(18, FW_SEMIBOLD, L"Microsoft YaHei UI");
    g.small = MakeFont(13, FW_NORMAL, L"Microsoft YaHei UI");
    g.title = MakeFont(19, FW_SEMIBOLD, L"Microsoft YaHei UI");
    g.display = MakeFont(32, FW_SEMIBOLD, L"Microsoft YaHei UI");

    const std::array<HWND, 12> controls{
        g.input, g.output, g.models, g.suffix, g.segment, g.overlap, g.batch,
        g.log, g.status, g.progressText, g.browse[0], g.start
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
}

UiLayout Measure(HWND hwnd) {
    UiLayout layout;
    GetClientRect(hwnd, &layout.client);
    const int width = Width(layout.client);
    const int height = Height(layout.client);
    const int minMargin = S(36);
    const int idealWidth = S(1120);
    const int contentWidth = std::min(idealWidth, std::max(1, width - minMargin * 2));
    const int contentLeft = (width - contentWidth) / 2;
    const int contentRight = contentLeft + contentWidth;
    const int gap = S(18);

    layout.topbar = R(0, 0, width, S(68));
    layout.content = R(contentLeft, layout.topbar.bottom, contentRight, height);
    layout.header = R(contentLeft, S(86), contentRight, S(170));
    const int statusWidth = S(142);
    layout.statusPill = R(contentRight - statusWidth, S(103), contentRight, S(143));
    layout.statusText = R(layout.statusPill.left + S(30), layout.statusPill.top + S(7),
                          layout.statusPill.right - S(10), layout.statusPill.bottom - S(7));

    const int cardsTop = S(190);
    const int cardsHeight = S(218);
    const int totalCardsWidth = contentRight - contentLeft;
    const int cardWidth = (totalCardsWidth - gap) / 2;
    layout.sourceCard = R(contentLeft, cardsTop, contentLeft + cardWidth, cardsTop + cardsHeight);
    layout.engineCard = R(layout.sourceCard.right + gap, cardsTop, contentRight, cardsTop + cardsHeight);

    layout.resultBand = R(contentLeft, layout.sourceCard.bottom + S(18),
                          contentRight, layout.sourceCard.bottom + S(128));

    const int footerHeight = S(70);
    const int footerBottom = height - S(18);
    layout.footer = R(contentLeft, footerBottom - footerHeight, contentRight, footerBottom);
    layout.queueCard = R(contentLeft, layout.resultBand.bottom + S(16),
                         contentRight, layout.footer.top - S(14));

    const int cardInset = S(22);
    layout.browseButtons[0] = R(layout.sourceCard.left + cardInset, layout.sourceCard.top + S(76),
                                layout.sourceCard.right - cardInset, layout.sourceCard.top + S(134));
    layout.pathFields[0] = R(layout.sourceCard.left + cardInset, layout.sourceCard.top + S(171),
                             layout.sourceCard.right - cardInset, layout.sourceCard.top + S(207));
    layout.browseButtons[1] = R(layout.engineCard.left + cardInset, layout.engineCard.top + S(76),
                                layout.engineCard.right - cardInset, layout.engineCard.top + S(134));
    layout.pathFields[1] = R(layout.engineCard.left + cardInset, layout.engineCard.top + S(171),
                             layout.engineCard.right - cardInset, layout.engineCard.top + S(207));

    layout.logSurface = R(layout.queueCard.left + S(18), layout.queueCard.top + S(48),
                          layout.queueCard.right - S(18), layout.queueCard.bottom - S(14));
    layout.logEdit = Inset(layout.logSurface, S(11), S(7));

    const int startWidth = S(220);
    layout.startButton = R(layout.footer.right - startWidth, layout.footer.top + S(6),
                           layout.footer.right, layout.footer.bottom - S(6));
    layout.progressText = R(layout.footer.left, layout.footer.top + S(8),
                            layout.startButton.left - S(24), layout.footer.top + S(31));
    layout.progressTrack = R(layout.footer.left, layout.footer.top + S(42),
                             layout.startButton.left - S(24), layout.footer.top + S(48));
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
    InvalidateRect(hwnd, nullptr, FALSE);
}

void SoftCard(HDC dc, RECT rect) {
    RECT shadow = rect;
    OffsetRect(&shadow, 0, S(2));
    RoundBox(dc, shadow, S(8), RGB(235, 237, 242));
    RoundBox(dc, rect, S(8), kCard, kBorder);
}

void DrawMusicNoteIcon(HDC dc, const RECT& rect, COLORREF color) {
    const int stemX = rect.left + Width(rect) * 2 / 3;
    const int top = rect.top + Height(rect) / 5;
    const int bottom = rect.bottom - Height(rect) / 4;
    Line(dc, stemX, top, stemX, bottom, color, std::max(2, S(2)));
    Line(dc, stemX - Width(rect) / 4, top + S(2), stemX, top, color, std::max(2, S(2)));
    Circle(dc, stemX - S(4), bottom + S(2), std::max(2, S(4)), color);
}

void DrawFolderIcon(HDC dc, const RECT& rect, COLORREF color) {
    const int tabWidth = Width(rect) * 2 / 5;
    RoundBox(dc, R(rect.left, rect.top + S(4), rect.left + tabWidth, rect.top + S(11)),
             S(3), color);
    RoundBox(dc, R(rect.left, rect.top + S(8), rect.right, rect.bottom), S(4), color);
}

void DrawPlayIcon(HDC dc, const RECT& rect, COLORREF color) {
    POINT points[3]{
        {rect.left, rect.top},
        {rect.right, rect.top + Height(rect) / 2},
        {rect.left, rect.bottom},
    };
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTopbar(HDC dc, const UiLayout& layout) {
    Fill(dc, layout.topbar, kTopbar);
    Line(dc, 0, layout.topbar.bottom - 1, layout.topbar.right,
         layout.topbar.bottom - 1, kSeparator, S(1));

    const RECT logo = R(layout.content.left, S(16), layout.content.left + S(36), S(52));
    RoundBox(dc, logo, S(8), kAccent);
    DrawMusicNoteIcon(dc, Inset(logo, S(8), S(6)), RGB(255, 255, 255));
    Text(dc, L"歌曲小助手", R(logo.right + S(12), S(14), logo.right + S(180), S(54)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.title, kText);

    const RECT privacy = R(layout.content.right - S(190), S(17), layout.content.right, S(51));
    RoundBox(dc, privacy, S(17), RGB(239, 249, 243), RGB(203, 233, 215));
    Circle(dc, privacy.left + S(18), privacy.top + Height(privacy) / 2, S(4), kGreen);
    Text(dc, L"歌曲不会上传", R(privacy.left + S(32), privacy.top,
         privacy.right - S(10), privacy.bottom),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.small, RGB(27, 127, 68));
}

void DrawHeader(HDC dc, const UiLayout& layout) {
    Text(dc, L"把每首歌变成两份", R(layout.header.left, layout.header.top,
         layout.statusPill.left - S(24), layout.header.top + S(40)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.display, kText);
    Text(dc, L"一份是唱歌声，一份是音乐声",
         R(layout.header.left, layout.header.top + S(45), layout.statusPill.left - S(24), layout.header.bottom),
         DT_LEFT | DT_SINGLELINE, g.regular, kSecondary);

    COLORREF pillFill = RGB(232, 248, 239);
    COLORREF pillBorder = RGB(190, 229, 206);
    if (g.statusColor == kAccent) {
        pillFill = RGB(235, 245, 255);
        pillBorder = RGB(190, 220, 250);
    } else if (g.statusColor == kOrange) {
        pillFill = RGB(255, 244, 228);
        pillBorder = RGB(244, 207, 155);
    } else if (g.statusColor == kRed) {
        pillFill = RGB(255, 237, 237);
        pillBorder = RGB(241, 193, 193);
    } else if (g.statusColor == kTertiary) {
        pillFill = RGB(240, 243, 248);
        pillBorder = kBorder;
    }
    RoundBox(dc, layout.statusPill, S(20), pillFill, pillBorder);
    Circle(dc, layout.statusPill.left + S(18), layout.statusPill.top + Height(layout.statusPill) / 2,
           S(4), g.statusColor);
    Text(dc, g.statusValue.c_str(), layout.statusText,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.medium, g.statusColor);
}

void DrawField(HDC dc, const RECT& field, HWND control) {
    const bool focused = GetFocus() == control;
    RoundBox(dc, field, S(8), kSurface, focused ? kAccent : kBorder, focused ? S(2) : S(1));
}

void DrawStepCard(HDC dc, const RECT& card, const wchar_t* number,
                  const wchar_t* title, const wchar_t* detail,
                  COLORREF color, const RECT& field, HWND control) {
    SoftCard(dc, card);
    Circle(dc, card.left + S(35), card.top + S(31), S(15), color);
    Text(dc, number, R(card.left + S(20), card.top + S(16),
         card.left + S(50), card.top + S(46)),
         DT_CENTER | DT_SINGLELINE | DT_VCENTER, g.medium, RGB(255, 255, 255));
    Text(dc, title, R(card.left + S(60), card.top + S(13),
         card.right - S(20), card.top + S(42)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.semibold, kText);
    Text(dc, detail, R(card.left + S(60), card.top + S(43),
         card.right - S(20), card.top + S(66)),
         DT_LEFT | DT_SINGLELINE, g.small, kSecondary);
    Text(dc, L"你选的地方", R(card.left + S(22), card.top + S(145),
         card.right - S(22), card.top + S(166)),
         DT_LEFT | DT_SINGLELINE, g.small, kTertiary);
    DrawField(dc, field, control);
}

void DrawSourceCard(HDC dc, const UiLayout& layout) {
    DrawStepCard(dc, layout.sourceCard, L"1", L"歌曲在哪里？",
                 L"选择装着歌曲的文件夹", kAccent,
                 layout.pathFields[0], g.input);
}

void DrawEngineCard(HDC dc, const UiLayout& layout) {
    DrawStepCard(dc, layout.engineCard, L"2", L"歌曲放哪里？",
                 L"做好后会放在这里", kGreen,
                 layout.pathFields[1], g.output);
}

void DrawResults(HDC dc, const UiLayout& layout) {
    const RECT band = layout.resultBand;
    RoundBox(dc, band, S(8), RGB(255, 252, 245), RGB(244, 231, 207));
    const int centerY = band.top + Height(band) / 2;
    const int introWidth = S(270);

    Circle(dc, band.left + S(38), centerY, S(20), kOrange);
    DrawMusicNoteIcon(dc, R(band.left + S(26), centerY - S(14),
                      band.left + S(50), centerY + S(14)), RGB(255, 255, 255));
    Text(dc, L"做好后会得到", R(band.left + S(70), band.top + S(24),
         band.left + introWidth - S(12), band.top + S(51)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.semibold, kText);
    Text(dc, L"原来的歌曲不会改变", R(band.left + S(70), band.top + S(57),
         band.left + introWidth - S(12), band.bottom - S(18)),
         DT_LEFT | DT_SINGLELINE, g.small, kSecondary);

    Line(dc, band.left + introWidth, band.top + S(20), band.left + introWidth,
         band.bottom - S(20), RGB(237, 224, 201), S(1));
    const int resultLeft = band.left + introWidth + S(22);
    const int resultWidth = band.right - resultLeft - S(18);
    const int groupWidth = resultWidth / 2;
    const std::array<const wchar_t*, 2> markers{L"唱", L"伴"};
    const std::array<const wchar_t*, 2> titles{L"唱歌声", L"音乐声"};
    const std::array<const wchar_t*, 2> details{L"只留下唱歌的声音", L"只留下乐器和背景音乐"};
    const std::array<COLORREF, 2> colors{kAccent, kGreen};
    for (int i = 0; i < 2; ++i) {
        const int left = resultLeft + groupWidth * i;
        Circle(dc, left + S(28), centerY, S(20), colors[static_cast<size_t>(i)]);
        Text(dc, markers[static_cast<size_t>(i)],
             R(left + S(8), centerY - S(20), left + S(48), centerY + S(20)),
             DT_CENTER | DT_SINGLELINE | DT_VCENTER, g.medium, RGB(255, 255, 255));
        Text(dc, titles[static_cast<size_t>(i)],
             R(left + S(62), band.top + S(25), left + groupWidth - S(14), band.top + S(52)),
             DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.semibold, kText);
        Text(dc, details[static_cast<size_t>(i)],
             R(left + S(62), band.top + S(58), left + groupWidth - S(14), band.bottom - S(16)),
             DT_LEFT | DT_SINGLELINE, g.small, kSecondary);
        if (i == 0) {
            Line(dc, left + groupWidth - S(8), band.top + S(28),
                 left + groupWidth - S(8), band.bottom - S(28),
                 RGB(237, 224, 201), S(1));
        }
    }
}

void DrawQueue(HDC dc, const UiLayout& layout) {
    SoftCard(dc, layout.queueCard);
    Circle(dc, layout.queueCard.left + S(27), layout.queueCard.top + S(25), S(4), g.statusColor);
    Text(dc, L"进行情况", R(layout.queueCard.left + S(40), layout.queueCard.top + S(10),
         layout.queueCard.right - S(18), layout.queueCard.top + S(40)),
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.medium, kText);
    if (!g.logStarted && Height(layout.logSurface) > S(16)) {
        Text(dc, L"准备好啦，点击开始后我会告诉你进度", layout.logSurface,
             DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.regular, kTertiary);
    } else if (Height(layout.logSurface) > S(16)) {
        RoundBox(dc, layout.logSurface, S(8), kLogSurface, kSeparator);
    }
}

void DrawFooter(HDC dc, const UiLayout& layout) {
    Line(dc, layout.footer.left, layout.footer.top, layout.footer.right, layout.footer.top,
         kSeparator, S(1));
    Text(dc, g.progressValue.c_str(), layout.progressText,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER, g.medium, kSecondary);
    RoundBox(dc, layout.progressTrack, S(4), RGB(225, 229, 237), RGB(225, 229, 237));
    const int filled = static_cast<int>(Width(layout.progressTrack) * std::clamp(g.progress, 0.0, 1.0));
    if (filled > 0) {
        RECT bar = layout.progressTrack;
        bar.right = bar.left + filled;
        RoundBox(dc, bar, S(4), kAccent, kAccent);
    }
}

void Paint(HWND hwnd, HDC target) {
    const UiLayout layout = Measure(hwnd);
    const int width = Width(layout.client);
    const int height = Height(layout.client);
    if (width <= 0 || height <= 0) return;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    Fill(dc, layout.client, kBackground);
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
        SetStatus(L"可以开始", kGreen);
        SetProgressText(L"都选好了，点击开始吧");
    } else if (hasSongs || hasOutput) {
        SetStatus(L"还差一个", kOrange);
        SetProgressText(hasSongs ? L"再选择保存位置" : L"再选择歌曲文件夹");
    } else {
        SetStatus(L"等你选择", kAccent);
        SetProgressText(L"先选歌曲，再选保存位置");
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
    SetWindowTextW(g.start, success ? L"再做一次" : L"重新开始");
    if (success) g.progress = 1.0;
    if (success) SetProgressText(L"做好啦！去保存位置看看吧");
    else if (g.cancel.load()) SetProgressText(L"已经停下来，可以再试一次");
    else SetProgressText(L"没有完成，请看看下面的进行情况");
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
            text << L"正在制作 · " << static_cast<int>(value * 100.0) << L"%";
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
            post([] { FinishJob(L"程序有问题", false); });
            return;
        }

        const auto files = uar::ScanAudioFiles(input);
        if (files.empty()) {
            log("这个文件夹里没有找到歌曲，请换一个文件夹。");
            post([] { FinishJob(L"没找到歌曲", false); });
            return;
        }

        log("找到 " + std::to_string(files.size()) + " 首歌曲，开始制作。");
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
            post([] { FinishJob(L"已经停下来", false); });
        } else if (completed == files.size()) {
            log("全部歌曲都做好了！");
            post([] { FinishJob(L"完成啦", true); });
        } else {
            log("有几首歌曲没有成功，请看看上面的消息。");
            post([] { FinishJob(L"有歌没成功", false); });
        }
    } catch (const std::exception& exception) {
        (void)exception;
        log("制作时遇到了问题，请再试一次。");
        post([] { FinishJob(L"没有完成", false); });
    }
}

void StartJob() {
    if (g.busy.load()) {
        g.cancel.store(true);
        SetStatus(L"正在停下来", kOrange);
        Log(L"正在停下来，请稍等…");
        return;
    }
    const std::wstring inputText = Value(g.input);
    const std::wstring outputText = Value(g.output);
    if (inputText.empty() || outputText.empty()) {
        Log(L"请先完成上面的第 1 步和第 2 步。");
        SetStatus(L"还差一步", kOrange);
        return;
    }

    g.cancel.store(false);
    g.busy.store(true);
    g.progress = 0.0;
    SetWindowTextW(g.log, L"");
    SetBusyControls(true);
    SetWindowTextW(g.start, L"先停一下");
    SetProgressText(L"正在准备，请稍等…");
    SetStatus(L"正在制作", kOrange);
    Log(L"开始啦。");
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
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    COLORREF fill = pressed ? kSurfaceHover : kSurface;
    COLORREF border = focused ? kAccent : kBorder;
    COLORREF textColor = kText;
    if (primary) {
        fill = pressed ? kAccentDown : kAccent;
        border = fill;
        textColor = RGB(255, 255, 255);
    } else if (chooseSongs) {
        fill = pressed ? RGB(211, 226, 252) : RGB(229, 238, 255);
        border = focused ? kAccent : RGB(190, 211, 249);
        textColor = kAccentDown;
    } else if (chooseOutput) {
        fill = pressed ? RGB(212, 239, 225) : RGB(229, 247, 238);
        border = focused ? kGreen : RGB(183, 226, 204);
        textColor = RGB(23, 132, 77);
    }
    if (disabled) {
        fill = RGB(235, 238, 243);
        border = kSeparator;
        textColor = kTertiary;
    }
    Fill(item->hDC, item->rcItem, primary ? kBackground : kCard);
    const RECT button = Inset(item->rcItem, S(1), S(1));
    RoundBox(item->hDC, button, S(8), fill, border, focused ? S(2) : S(1));
    wchar_t value[128]{};
    GetWindowTextW(item->hwndItem, value, static_cast<int>(std::size(value)));
    Text(item->hDC, value, button, DT_CENTER | DT_SINGLELINE | DT_VCENTER,
         primary ? g.semibold : g.medium,
         textColor);
    const int centerY = button.top + Height(button) / 2;
    if (primary) {
        if (value[0] == L'先') {
            RoundBox(item->hDC, R(button.left + S(24), centerY - S(7),
                     button.left + S(29), centerY + S(7)), S(2), textColor);
            RoundBox(item->hDC, R(button.left + S(32), centerY - S(7),
                     button.left + S(37), centerY + S(7)), S(2), textColor);
        } else {
            DrawPlayIcon(item->hDC,
                         R(button.left + S(24), centerY - S(8),
                           button.left + S(34), centerY + S(8)), textColor);
        }
    } else if (chooseSongs || chooseOutput) {
        DrawFolderIcon(item->hDC,
                       R(button.left + S(20), centerY - S(8),
                         button.left + S(40), centerY + S(9)), textColor);
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
    SetWindowTheme(g.log, L"Explorer", nullptr);
    ShowWindow(g.log, SW_HIDE);
    g.status = Static(hwnd, IDC_STATUS, L"等你选择");
    g.progressText = Static(hwnd, IDC_PROGRESS, L"先选歌曲，再选保存位置");
    ShowWindow(g.status, SW_HIDE);
    ShowWindow(g.progressText, SW_HIDE);
    g.browse[0] = Button(hwnd, IDC_BROWSE_INPUT, L"选择歌曲文件夹");
    g.browse[1] = Button(hwnd, IDC_BROWSE_OUTPUT, L"选择保存位置");
    g.browse[2] = Button(hwnd, IDC_BROWSE_MODELS, L"选择");
    g.start = Button(hwnd, IDC_START, L"开始制作");

    ShowWindow(g.models, SW_HIDE);
    ShowWindow(g.suffix, SW_HIDE);
    ShowWindow(g.segment, SW_HIDE);
    ShowWindow(g.overlap, SW_HIDE);
    ShowWindow(g.batch, SW_HIDE);
    ShowWindow(g.browse[2], SW_HIDE);

    SetCue(g.input, L"还没选歌曲文件夹");
    SetCue(g.output, L"还没选保存位置");
    RecreateFonts();
    Layout(hwnd);
}

void ApplyWindowAppearance(HWND hwnd) {
    BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const int roundedCorners = 2;
    DwmSetWindowAttribute(hwnd, 33, &roundedCorners, sizeof(roundedCorners));
    const COLORREF caption = kTopbar;
    const COLORREF captionText = kText;
    const COLORREF border = kBorder;
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
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
    Fill(colorDc, iconRect, kAccent);
    PatBlt(maskDc, 0, 0, size, size, BLACKNESS);
    HPEN wavePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(colorDc, wavePen);
    const std::array<int, 7> heights{8, 16, 22, 12, 19, 14, 7};
    for (size_t i = 0; i < heights.size(); ++i) {
        const int x = 5 + static_cast<int>(i) * 4;
        const int half = heights[i] / 2;
        MoveToEx(colorDc, x, 16 - half, nullptr);
        LineTo(colorDc, x, 16 + half);
    }
    SelectObject(colorDc, oldPen);
    DeleteObject(wavePen);
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
            SetStatus(L"正在停下来", kOrange);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
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
