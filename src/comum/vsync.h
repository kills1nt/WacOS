// Animation timer in step with the display: WM_TIMER(id) once per refresh (DwmFlush), so animations run at the
// monitor's rate (144/165 Hz) instead of the ~64 per second a plain SetTimer gives (its clock ticks every 15.6 ms).
//   vsync::Set(h, id, ms)  same as SetTimer(h, id, ms) (kept as the fallback) plus one tick per display refresh
//   vsync::Kill(h, id)     KillTimer and stop the display ticks
// The ticks are sent (SendMessageTimeout), so a slow frame never piles messages up in the queue. A tick stream
// that nobody stops ends by itself after 4 s; the plain timer goes on, so a long animation just drops to ~64/s.
// Setting fps_animacao=60 (ajustes.txt) turns the display ticks off.
#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#pragma comment(lib, "dwmapi.lib")

namespace vsync {

struct Stream { HWND h; UINT_PTR id; std::atomic<bool> on{ false }; std::atomic<ULONGLONG> until{ 0 }; HANDLE wake = nullptr; };

inline std::mutex& Mx() { static std::mutex m; return m; }
inline std::vector<Stream*>& All() { static std::vector<Stream*> v; return v; }

inline bool Enabled() {
    static int on = -1;
    if (on < 0) {
        on = 1;
        wchar_t path[MAX_PATH];
        if (DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH); n && n < MAX_PATH - 20) {
            wcscat_s(path, L"\\Shell\\ajustes.txt");
            if (HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr); f != INVALID_HANDLE_VALUE) {
                char buf[16384]; DWORD got = 0;
                if (ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr)) { buf[got] = 0; if (strstr(buf, "fps_animacao=60")) on = 0; }
                CloseHandle(f);
            }
        }
    }
    return on == 1;
}

inline void Run(Stream* s) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    for (;;) {
        if (!s->on) { WaitForSingleObject(s->wake, INFINITE); continue; }
        if (FAILED(DwmFlush())) Sleep(4);
        if (!s->on) continue;
        if (!IsWindow(s->h) || GetTickCount64() > s->until) { s->on = false; continue; }
        DWORD_PTR r;
        SendMessageTimeoutW(s->h, WM_TIMER, s->id, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, 100, &r);
    }
}

inline void Set(HWND h, UINT_PTR id, UINT ms) {
    SetTimer(h, id, ms, nullptr);
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lk(Mx());
    for (auto* s : All())
        if (s->h == h && s->id == id) {
            s->until = GetTickCount64() + 4000;
            if (!s->on.exchange(true)) SetEvent(s->wake);
            return;
        }
    auto* s = new Stream;
    s->h = h; s->id = id; s->until = GetTickCount64() + 4000; s->on = true;
    s->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    All().push_back(s);
    std::thread(Run, s).detach();
}

inline void Kill(HWND h, UINT_PTR id) {
    KillTimer(h, id);
    std::lock_guard<std::mutex> lk(Mx());
    for (auto* s : All()) if (s->h == h && s->id == id) s->on = false;
}

// the display's refresh period in ms (6.06 at 165 Hz); 16.7 when unknown
inline double FrameMs() {
    DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.rateRefresh.uiNumerator && ti.rateRefresh.uiDenominator) {
        double hz = (double)ti.rateRefresh.uiNumerator / ti.rateRefresh.uiDenominator;
        if (hz >= 30 && hz <= 500) return 1000.0 / hz;
    }
    return 1000.0 / 60;
}

}  // namespace vsync
