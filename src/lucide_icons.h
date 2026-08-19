#pragma once

#include <windows.h>

#include <algorithm>

namespace uar::ui {

// Geometry follows the Lucide icon set (ISC License).
enum class LucideIcon {
    Activity,
    ArrowUpRight,
    AudioWaveform,
    CheckCircle2,
    FolderDown,
    FolderOpen,
    LockKeyhole,
    MicVocal,
    Music2,
    Pause,
    Play,
    RotateCcw,
    ShieldCheck,
    Square,
    Sun,
    Moon,
};

inline HPEN CreateLucidePen(COLORREF color, int strokeWidth) {
    LOGBRUSH brush{};
    brush.lbStyle = BS_SOLID;
    brush.lbColor = color;
    return ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                        static_cast<DWORD>(std::max(1, strokeWidth)), &brush, 0, nullptr);
}

inline void DrawLucideIcon(HDC dc, LucideIcon icon, const RECT& bounds,
                           COLORREF color, int strokeWidth = 2) {
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

    const int saved = SaveDC(dc);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, 24, 24, nullptr);
    SetViewportExtEx(dc, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr);
    SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);

    HPEN pen = CreateLucidePen(color, strokeWidth);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    auto line = [&](int x1, int y1, int x2, int y2) {
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
    };
    auto bezier = [&](int x1, int y1, int x2, int y2, int x3, int y3) {
        POINT points[3]{{x1, y1}, {x2, y2}, {x3, y3}};
        PolyBezierTo(dc, points, 3);
    };
    auto polyline = [&](const POINT* points, int count, bool close = false) {
        if (count < 2) return;
        BeginPath(dc);
        MoveToEx(dc, points[0].x, points[0].y, nullptr);
        for (int i = 1; i < count; ++i) LineTo(dc, points[i].x, points[i].y);
        if (close) CloseFigure(dc);
        EndPath(dc);
        StrokePath(dc);
    };
    auto check = [&] {
        const POINT points[]{{8, 12}, {11, 15}, {16, 9}};
        polyline(points, 3);
    };

    switch (icon) {
    case LucideIcon::Activity: {
        const POINT points[]{{3, 12}, {7, 12}, {10, 4}, {14, 20}, {17, 12}, {21, 12}};
        polyline(points, 6);
        break;
    }
    case LucideIcon::ArrowUpRight: {
        line(7, 17, 17, 7);
        const POINT points[]{{7, 7}, {17, 7}, {17, 17}};
        polyline(points, 3);
        break;
    }
    case LucideIcon::AudioWaveform:
        line(2, 10, 2, 14);
        line(6, 6, 6, 18);
        line(10, 3, 10, 21);
        line(14, 8, 14, 16);
        line(18, 5, 18, 19);
        line(22, 10, 22, 14);
        break;
    case LucideIcon::CheckCircle2:
        Ellipse(dc, 3, 3, 21, 21);
        check();
        break;
    case LucideIcon::FolderDown: {
        BeginPath(dc);
        MoveToEx(dc, 3, 19, nullptr);
        LineTo(dc, 3, 6);
        bezier(3, 5, 4, 4, 5, 4);
        LineTo(dc, 9, 4);
        LineTo(dc, 11, 7);
        LineTo(dc, 19, 7);
        bezier(20, 7, 21, 8, 21, 9);
        LineTo(dc, 21, 19);
        bezier(21, 20, 20, 21, 19, 21);
        LineTo(dc, 5, 21);
        bezier(4, 21, 3, 20, 3, 19);
        EndPath(dc);
        StrokePath(dc);
        line(12, 10, 12, 17);
        const POINT arrow[]{{9, 14}, {12, 17}, {15, 14}};
        polyline(arrow, 3);
        break;
    }
    case LucideIcon::FolderOpen: {
        BeginPath(dc);
        MoveToEx(dc, 3, 19, nullptr);
        LineTo(dc, 3, 6);
        bezier(3, 5, 4, 4, 5, 4);
        LineTo(dc, 9, 4);
        LineTo(dc, 11, 7);
        LineTo(dc, 19, 7);
        bezier(20, 7, 21, 8, 21, 9);
        LineTo(dc, 10, 9);
        bezier(9, 9, 8, 10, 8, 11);
        LineTo(dc, 5, 20);
        bezier(5, 21, 4, 21, 3, 20);
        EndPath(dc);
        StrokePath(dc);
        BeginPath(dc);
        MoveToEx(dc, 5, 20, nullptr);
        LineTo(dc, 19, 20);
        bezier(20, 20, 21, 19, 21, 18);
        LineTo(dc, 23, 11);
        bezier(23, 10, 22, 9, 21, 9);
        LineTo(dc, 8, 9);
        EndPath(dc);
        StrokePath(dc);
        break;
    }
    case LucideIcon::LockKeyhole:
        RoundRect(dc, 4, 10, 20, 22, 3, 3);
        Arc(dc, 7, 2, 17, 14, 7, 10, 17, 10);
        line(12, 15, 12, 18);
        break;
    case LucideIcon::MicVocal:
        RoundRect(dc, 8, 2, 16, 14, 8, 8);
        Arc(dc, 5, 7, 19, 19, 5, 11, 19, 11);
        line(12, 19, 12, 22);
        line(8, 22, 16, 22);
        line(12, 6, 15, 6);
        line(12, 10, 15, 10);
        break;
    case LucideIcon::Music2:
        line(9, 18, 9, 5);
        line(9, 5, 20, 3);
        line(20, 3, 20, 16);
        Ellipse(dc, 3, 15, 9, 21);
        Ellipse(dc, 14, 13, 20, 19);
        break;
    case LucideIcon::Pause:
        line(9, 5, 9, 19);
        line(15, 5, 15, 19);
        break;
    case LucideIcon::Play: {
        const POINT points[]{{7, 4}, {20, 12}, {7, 20}};
        polyline(points, 3, true);
        break;
    }
    case LucideIcon::RotateCcw:
        Arc(dc, 4, 4, 21, 21, 5, 8, 18, 18);
        line(4, 4, 4, 10);
        line(4, 10, 10, 10);
        break;
    case LucideIcon::ShieldCheck: {
        const POINT shield[]{{12, 3}, {20, 6}, {20, 12}, {18, 17}, {12, 21}, {6, 17}, {4, 12}, {4, 6}};
        polyline(shield, 8, true);
        check();
        break;
    }
    case LucideIcon::Square:
        RoundRect(dc, 6, 6, 18, 18, 2, 2);
        break;
    case LucideIcon::Sun:
        Ellipse(dc, 8, 8, 16, 16);
        line(12, 2, 12, 4);
        line(12, 20, 12, 22);
        line(4, 12, 2, 12);
        line(22, 12, 20, 12);
        line(5, 5, 6, 6);
        line(18, 18, 19, 19);
        line(19, 5, 18, 6);
        line(6, 18, 5, 19);
        break;
    case LucideIcon::Moon:
        BeginPath(dc);
        MoveToEx(dc, 20, 15, nullptr);
        bezier(18, 16, 17, 16, 15, 16);
        bezier(11, 16, 8, 13, 8, 8);
        bezier(8, 7, 8, 5, 9, 4);
        bezier(5, 5, 3, 9, 3, 12);
        bezier(3, 17, 7, 21, 12, 21);
        bezier(15, 21, 19, 19, 20, 15);
        CloseFigure(dc);
        EndPath(dc);
        StrokePath(dc);
        break;
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    RestoreDC(dc, saved);
}

} // namespace uar::ui
