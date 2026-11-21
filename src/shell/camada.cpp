// Chromium/Electron apps (Spotify, 21/11/2026): every now and then a window opens with its GPU layer
// ("Intermediate D3D Window", the child where Chromium draws through DirectComposition) placed at the wrong spot,
// -8,-31 px from the window: the frame insets of a standard Windows title bar. The app then shows its top bar cut off
// and an empty strip at the bottom, and stays like that (resizing, maximizing and restoring do not fix it; only
// reopening the app did). Seen once out of ~10 launches, not reproducible on demand.
// This watcher puts that layer back on the window's client area: every 1.5 s it checks the visible Chromium
// windows; a layer of the client's size that sits away from the client origin twice in a row (not a resize in
// progress) is moved back. Setting chromium_camada=0 turns it off.
#include "shell.h"
#include <dwmapi.h>
#include <atomic>
#include <map>
#include <thread>

namespace {
std::atomic<bool> g_on{ false };

struct Seen { POINT off; int times; };

void Check(std::map<HWND, Seen>& seen) {
    struct Ctx { std::map<HWND, Seen>* seen; std::map<HWND, Seen> next; } ctx{ &seen, {} };
    EnumWindows([](HWND h, LPARAM l) -> BOOL {
        auto* c = (Ctx*)l;
        wchar_t cls[32] = {};
        if (!GetClassNameW(h, cls, 32) || wcscmp(cls, L"Chrome_WidgetWin_1")) return TRUE;
        if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        HWND d = FindWindowExW(h, nullptr, L"Intermediate D3D Window", nullptr);
        if (!d || !IsWindowVisible(d)) return TRUE;
        RECT cr; GetClientRect(h, &cr);
        POINT o{ 0, 0 }; ClientToScreen(h, &o);
        RECT dr; GetWindowRect(d, &dr);
        int cw = cr.right, ch = cr.bottom, dw = dr.right - dr.left, dh = dr.bottom - dr.top;
        POINT off{ dr.left - o.x, dr.top - o.y };
        // only the stuck case: same size as the client, a small offset (a frame's worth), not during a move/resize
        if ((off.x == 0 && off.y == 0) || dw != cw || dh != ch || std::abs(off.x) > 64 || std::abs(off.y) > 64 || cw < 200 || ch < 150) return TRUE;
        GUITHREADINFO gi{ sizeof(gi) };
        if (GetGUIThreadInfo(GetWindowThreadProcessId(h, nullptr), &gi) && (gi.flags & GUI_INMOVESIZE)) return TRUE;
        auto it = c->seen->find(h);
        Seen s{ off, (it != c->seen->end() && it->second.off.x == off.x && it->second.off.y == off.y) ? it->second.times + 1 : 1 };
        if (s.times >= 2) {
            SetWindowPos(d, nullptr, 0, 0, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            wchar_t t[80] = {}; GetWindowTextW(h, t, 80);
            DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
            Log(L"chromium: camada da GPU fora do lugar (%ld,%ld) em \"%ls\" (pid %lu): devolvida", off.x, off.y, t, pid);
            s.times = 0;
        }
        c->next[h] = s;
        return TRUE;
    }, (LPARAM)&ctx);
    seen.swap(ctx.next);
}
}  // namespace

void camada::Start() {
    if (g_on.exchange(true)) return;
    std::thread([] {
        std::map<HWND, Seen> seen;
        while (g_on) {
            Sleep(1500);
            if (shell::Setting(L"chromium_camada", L"1") == L"1") Check(seen);
        }
    }).detach();
}

void camada::Stop() { g_on = false; }
