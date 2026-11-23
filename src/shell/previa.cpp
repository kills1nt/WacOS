// Prévia das janelas no Dock: com o ponteiro parado sobre um app aberto, aparece acima do ícone um painel com a
// miniatura ao vivo (DWM) de cada janela dele e o título. Clique numa miniatura: a janela vem para a frente
// (minimizada volta); o × no canto fecha a janela; botão do meio também fecha. O painel some quando o ponteiro sai
// do ícone e do painel. Ajuste dock_previa=1 (padrão); dock_previa_ms = espera antes de abrir (padrão 450).
#include "shell.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
const wchar_t* CLS = L"ShellPrevia";
const UINT_PTR T_SHOW = 1, T_POLL = 2;
const float PAD = 10, GAP = 10, TITLE_H = 24, MAX_TW = 230, MAX_TH = 150, MIN_TW = 120;

struct Card { HWND win; std::wstring title; bool minimized; HTHUMBNAIL th = nullptr; D2D1_RECT_F box{}, thumb{}, close{};
              std::vector<BYTE> shot; UINT sw = 0, sh = 0; };  // minimised: a photo of the window (DWM thumbnails are empty then)

HWND g_wnd;
ui::Surface g_s;
std::wstring g_appId, g_wantId;     // mostrando / esperando para mostrar
RECT g_icon{}, g_wantIcon{};         // o ícone no Dock (px de tela)
int g_side = 0, g_wantSide = 0;     // 0 embaixo, 1 esquerda, 2 direita
float g_scale = 1;
std::vector<Card> g_cards;
int g_hot = -1, g_hotClose = -1;
DWORD g_outSince = 0;
IDWriteTextFormat* g_tf = nullptr;
ID2D1SolidColorBrush* g_br = nullptr;

bool Enabled() { return shell::Setting(L"dock_previa", L"1") == L"1"; }

void ClearThumbs() {
    for (auto& c : g_cards) if (c.th) DwmUnregisterThumbnail(c.th);
    g_cards.clear();
}

void HideNow() {
    KillTimer(g_wnd, T_SHOW); KillTimer(g_wnd, T_POLL);
    if (g_wnd) ShowWindow(g_wnd, SW_HIDE);
    ClearThumbs();
    g_appId.clear(); g_wantId.clear(); g_hot = g_hotClose = -1;
}

// the cursor is over the icon, the panel or the gap between them
bool CursorInside() {
    POINT p; GetCursorPos(&p);
    RECT pr; GetWindowRect(g_wnd, &pr);
    RECT u; UnionRect(&u, &pr, &g_icon);
    // only the strip between icon and panel, not the whole bounding box
    RECT strip = g_icon;
    if (g_side == 0) { strip.top = pr.bottom - 2; }
    else if (g_side == 1) { strip.right = pr.left + 2; }
    else { strip.left = pr.right - 2; }
    return PtInRect(&pr, p) || PtInRect(&g_icon, p) || PtInRect(&strip, p);
}

void Paint() {
    if (!g_wnd || g_cards.empty()) return;
    RECT wr; GetWindowRect(g_wnd, &wr);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    g_s.hwnd = g_wnd;
    ID2D1RenderTarget* rt = ui::Begin(g_s, W, H, g_scale);
    if (!rt) return;
    bool dark = ui::Dark();
    // the brush belongs to the render target, which ui::Begin recreates when the size changes: a new one each time
    if (g_br) { g_br->Release(); g_br = nullptr; }
    if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &g_br))) { ui::End(g_s, false); return; }
    if (!g_tf) {
        g_tf = ui::Format(12.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        IDWriteInlineObject* ell = nullptr;
        ui::dw->CreateEllipsisTrimmingSign(g_tf, &ell);
        DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        g_tf->SetTrimming(&tr, ell);
        if (ell) ell->Release();
    }
    D2D1_COLOR_F fg = dark ? D2D1::ColorF(1, 1, 1, 0.92f) : D2D1::ColorF(0, 0, 0, 0.85f);
    for (int i = 0; i < (int)g_cards.size(); i++) {
        auto& c = g_cards[i];
        if (i == g_hot) {
            g_br->SetColor(dark ? D2D1::ColorF(1, 1, 1, 0.12f) : D2D1::ColorF(0, 0, 0, 0.08f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(c.box, 8, 8), g_br);
        }
        float tx = c.box.left + 6 + (i == g_hot ? 20 : 0);
        g_br->SetColor(fg);
        D2D1_RECT_F tr{ tx, c.box.top + 2, c.box.right - 6, c.box.top + TITLE_H };
        rt->DrawText(c.title.c_str(), (UINT32)c.title.size(), g_tf, tr, g_br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (i == g_hot) {  // × para fechar a janela
            D2D1_POINT_2F cc{ (c.close.left + c.close.right) / 2, (c.close.top + c.close.bottom) / 2 };
            g_br->SetColor(i == g_hotClose ? D2D1::ColorF(0.93f, 0.33f, 0.31f, 1) : (dark ? D2D1::ColorF(1, 1, 1, 0.25f) : D2D1::ColorF(0, 0, 0, 0.18f)));
            rt->FillEllipse(D2D1::Ellipse(cc, 7, 7), g_br);
            g_br->SetColor(i == g_hotClose ? D2D1::ColorF(0.3f, 0, 0, 0.9f) : fg);
            float d = 2.8f;
            rt->DrawLine({ cc.x - d, cc.y - d }, { cc.x + d, cc.y + d }, g_br, 1.3f);
            rt->DrawLine({ cc.x - d, cc.y + d }, { cc.x + d, cc.y - d }, g_br, 1.3f);
        }
        if (c.minimized && !c.shot.empty()) {
            if (ID2D1Bitmap* bmp = ui::Bitmap(rt, c.sw, c.sh, c.shot)) {
                rt->DrawBitmap(bmp, c.thumb, 0.92f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                bmp->Release();
            }
        } else
        if (c.minimized || !c.th) {  // sem miniatura (minimizada): um cartão com o texto
            g_br->SetColor(dark ? D2D1::ColorF(1, 1, 1, 0.06f) : D2D1::ColorF(0, 0, 0, 0.05f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(c.thumb, 6, 6), g_br);
            g_br->SetColor(dark ? D2D1::ColorF(1, 1, 1, 0.5f) : D2D1::ColorF(0, 0, 0, 0.45f));
            std::wstring t = Tr(L"Minimizada");
            IDWriteTextFormat* tf2 = ui::Format(12, DWRITE_FONT_WEIGHT_REGULAR, false, DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawText(t.c_str(), (UINT32)t.size(), tf2, c.thumb, g_br);
            tf2->Release();
        }
    }
    ui::End(g_s, false);
    // as miniaturas do DWM por cima do desenho
    for (auto& c : g_cards) {
        if (!c.th) continue;
        DWM_THUMBNAIL_PROPERTIES tp{};
        tp.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        tp.rcDestination = { (LONG)std::lround(c.thumb.left * g_scale), (LONG)std::lround(c.thumb.top * g_scale),
                             (LONG)std::lround(c.thumb.right * g_scale), (LONG)std::lround(c.thumb.bottom * g_scale) };
        tp.fVisible = !c.minimized;
        tp.opacity = 255;
        tp.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(c.th, &tp);
    }
}

bool Build() {
    App* a = apps::FindById(g_wantId);
    if (!a || a->wins.empty() || !g_wnd) return false;
    ClearThumbs();
    g_appId = g_wantId; g_icon = g_wantIcon; g_side = g_wantSide;
    HMONITOR hm = MonitorFromRect(&g_icon, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(hm, &mi);
    UINT dx = 96, dy = 96; GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dx, &dy);
    g_scale = dx / 96.f;
    float monW = (mi.rcWork.right - mi.rcWork.left) / g_scale - 40;
    // cards: the thumbnail keeps the window's shape inside MAX_TW x MAX_TH
    float x = PAD, maxH = 0;
    for (auto& w : a->wins) {
        if (!IsWindow(w.hwnd)) continue;
        Card c{ w.hwnd, w.title, w.minimized || IsIconic(w.hwnd) };
        if (c.title.empty()) c.title = a->name;
        float tw = MAX_TW, th = MAX_TH * 0.62f;
        if (c.minimized) {  // the photo of the minimised window, at its restored size
            WINDOWPLACEMENT wp{ sizeof(wp) };
            if (GetWindowPlacement(w.hwnd, &wp)) {
                RECT src{ 0, 0, wp.rcNormalPosition.right - wp.rcNormalPosition.left, wp.rcNormalPosition.bottom - wp.rcNormalPosition.top };
                if (src.right > 0 && src.bottom > 0 && foto::Window(w.hwnd, src, 0, c.shot, c.sw, c.sh) && c.sw && c.sh) {
                    float k = std::min(MAX_TW / c.sw, MAX_TH / c.sh);
                    tw = std::max(MIN_TW, c.sw * k); th = c.sh * k;
                } else c.shot.clear();
            }
        }
        if (SUCCEEDED(DwmRegisterThumbnail(g_wnd, w.hwnd, &c.th))) {
            SIZE sz{};
            if (!c.minimized && SUCCEEDED(DwmQueryThumbnailSourceSize(c.th, &sz)) && sz.cx > 0 && sz.cy > 0) {
                float k = std::min(MAX_TW / sz.cx, MAX_TH / sz.cy);
                tw = std::max(MIN_TW, sz.cx * k); th = sz.cy * k;
                if (sz.cx * k < MIN_TW) th = std::min(MAX_TH, sz.cy * (MIN_TW / sz.cx));
            }
        }
        if (x + tw + PAD > monW && !g_cards.empty()) { if (c.th) DwmUnregisterThumbnail(c.th); break; }
        c.box = { x - 4, PAD - 2, x + tw + 4, 0 };
        c.thumb = { x, PAD + TITLE_H, x + tw, PAD + TITLE_H + th };
        c.close = { x - 2, PAD + 3, x + 16, PAD + 21 };
        maxH = std::max(maxH, th);
        x += tw + GAP;
        g_cards.push_back(c);
    }
    if (g_cards.empty()) return false;
    Log(L"previa: %ls, %zu janela(s)", a->name.c_str(), g_cards.size());
    float W = x - GAP + PAD, H = PAD + TITLE_H + maxH + PAD;
    for (auto& c : g_cards) {  // same height row: thumbnails centred vertically
        float th = c.thumb.bottom - c.thumb.top, off = (maxH - th) / 2;
        c.thumb.top += off; c.thumb.bottom += off;
        c.box.bottom = H - PAD + 4;
    }
    int pw = (int)std::lround(W * g_scale), ph = (int)std::lround(H * g_scale), gap = (int)std::lround(8 * g_scale);
    int cx = (g_icon.left + g_icon.right) / 2, cy = (g_icon.top + g_icon.bottom) / 2, px, py;
    if (g_side == 0) { px = cx - pw / 2; py = g_icon.top - gap - ph; }
    else if (g_side == 1) { px = g_icon.right + gap; py = cy - ph / 2; }
    else { px = g_icon.left - gap - pw; py = cy - ph / 2; }
    px = std::clamp(px, (int)mi.rcMonitor.left + 8, (int)mi.rcMonitor.right - pw - 8);
    py = std::clamp(py, (int)mi.rcMonitor.top + 8, (int)mi.rcMonitor.bottom - ph - 8);
    ui::Acrylic(g_wnd, ui::Dark() ? 0xE6262424u : 0xE6F4F4F4u);
    SetWindowPos(g_wnd, HWND_TOPMOST, px, py, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_hot = g_hotClose = -1;
    Paint();
    dock::Update();  // the name label above the icon goes away (the panel has the titles)
    SetTimer(g_wnd, T_POLL, 80, nullptr);
    g_outSince = 0;
    return true;
}

int CardAt(POINT client, bool* onClose) {
    float x = client.x / g_scale, y = client.y / g_scale;
    for (int i = 0; i < (int)g_cards.size(); i++) {
        auto& c = g_cards[i];
        if (x >= c.box.left && x < c.box.right && y >= c.box.top && y < c.box.bottom) {
            if (onClose) *onClose = x >= c.close.left && x < c.close.right && y >= c.close.top && y < c.close.bottom;
            return i;
        }
    }
    return -1;
}

void Bring(HWND h) {
    App* a = apps::FindById(g_appId);
    HideNow();
    if (!IsWindow(h)) return;
    if (apps::IsHidden(h) && a) apps::Activate(*a);
    if (IsIconic(h)) shell::Restaurar(h);
    keybd_event(VK_MENU, 0, 0, 0); SetForegroundWindow(h); keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); Paint(); return 0; }
    case WM_MOUSEMOVE: {
        bool oc = false;
        int i = CardAt({ GET_X_LPARAM(l), GET_Y_LPARAM(l) }, &oc);
        int hc = oc ? i : -1;
        if (i != g_hot || hc != g_hotClose) { g_hot = i; g_hotClose = hc; Paint(); }
        return 0;
    }
    case WM_LBUTTONUP: {
        bool oc = false;
        int i = CardAt({ GET_X_LPARAM(l), GET_Y_LPARAM(l) }, &oc);
        if (i < 0) return 0;
        HWND win = g_cards[i].win;
        if (oc) { PostMessageW(win, WM_CLOSE, 0, 0); HideNow(); return 0; }
        Bring(win);
        return 0;
    }
    case WM_MBUTTONUP: {
        int i = CardAt({ GET_X_LPARAM(l), GET_Y_LPARAM(l) }, nullptr);
        if (i >= 0) { PostMessageW(g_cards[i].win, WM_CLOSE, 0, 0); HideNow(); }
        return 0;
    }
    case WM_TIMER:
        if (w == T_SHOW) { KillTimer(h, T_SHOW); if (!g_wantId.empty() && !Build()) HideNow(); return 0; }
        if (w == T_POLL) {
            if (CursorInside()) { g_outSince = 0; return 0; }
            if (!g_outSince) g_outSince = GetTickCount();
            else if (GetTickCount() - g_outSince > 220) HideNow();
            return 0;
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

bool Ensure() {
    if (g_wnd) return true;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = CLS; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLS, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hi, nullptr);
    if (!g_wnd) return false;
    DWORD round = 2 /*DWMWCP_ROUND*/; DwmSetWindowAttribute(g_wnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &round, sizeof(round));
    BOOL darkOn = TRUE; DwmSetWindowAttribute(g_wnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkOn, sizeof(darkOn));
    apps::ours.push_back(g_wnd);
    return true;
}
}  // namespace

void previa::Hover(const std::wstring& appId, RECT iconScreen, int side) {
    if (!Enabled() || !Ensure()) return;
    if (appId.empty()) return;  // saiu do ícone: o T_POLL decide (o ponteiro pode estar indo para o painel)
    App* a = apps::FindById(appId);
    if (!a || a->wins.empty()) { if (IsWindowVisible(g_wnd) && appId != g_appId) HideNow(); return; }
    if (appId == g_appId && IsWindowVisible(g_wnd)) { g_icon = iconScreen; return; }
    if (appId == g_wantId && !IsWindowVisible(g_wnd)) { g_wantIcon = iconScreen; return; }  // already waiting: the timer keeps running
    g_wantId = appId; g_wantIcon = iconScreen; g_wantSide = side;
    // painel já aberto (de outro app): troca na hora, como a barra do Windows; senão espera o ponteiro parar
    if (IsWindowVisible(g_wnd)) { KillTimer(g_wnd, T_SHOW); if (!Build()) HideNow(); return; }
    int ms = std::clamp(_wtoi(shell::Setting(L"dock_previa_ms", L"450").c_str()), 0, 3000);
    SetTimer(g_wnd, T_SHOW, std::max(ms, 1), nullptr);
}

void previa::Leave() {
    if (!g_wnd) return;
    if (!IsWindowVisible(g_wnd)) { KillTimer(g_wnd, T_SHOW); g_wantId.clear(); }
}

void previa::Hide() { if (g_wnd) HideNow(); }

bool previa::Showing(const std::wstring& appId) { return g_wnd && IsWindowVisible(g_wnd) && appId == g_appId; }
