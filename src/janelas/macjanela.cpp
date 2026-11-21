// MacJanela.dll: barra de título do macOS nas janelas comuns do Windows (as que usam a barra padrão).
// Carregada só nos programas escolhidos pelo Shell (gancho WH_GETMESSAGE por thread, nunca global).
// Em cada janela: esconde título, ícone e botões do Windows (SetWindowThemeAttribute + estilo sem
// os botões), guarda a barra de menus para a barra de menus do Shell e põe por cima da barra de título
// uma janela transparente nossa com os três botões do Mac e o título centralizado.
// Tudo é desfeito com a mensagem "MacJanelaDesfazer" (o Shell manda ao sair).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <map>
#include "../comum/sombra.h"
#include "../comum/semaforo.h"
#include "../comum/gancho_mapa.h"
#include "../comum/leopard.h"  // tema Mac OS X Leopard: barra de título cinza metálico e botões de gel
#include "../comum/jogos.h"    // jogos e anti-cheat (a DLL nunca fica neles) e apps Chromium/Electron

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:GetMsgProc=_GetMsgProc@12")
#pragma comment(linker, "/EXPORT:WacOSHostW=_WacOSHostW@16")
#endif

void StopShowHook();
namespace controles {  // controles.cpp: o miolo dos diálogos no visual do Mac
void Style(HWND top);
void Child(HWND h);
void Unstyle(HWND top);
bool IsDialog(HWND h);
bool IsAlert(HWND h);  // caixa de mensagem virou alerta do Mac: sem barra de título
}

namespace {

HMODULE g_mod;
const wchar_t* PROP = L"MacJanela";          // janela já tratada
const wchar_t* PROP_MENU = L"MacJanelaMenu";  // a barra de menus guardada (a barra do Shell lê daqui)
const UINT_PTR SUBCLASS_ID = 0x4D41434A;
UINT g_msgTile;  // "MacJanelaEncaixar": reaplica o encaixe logo depois do fim do arrasto
// carregada pelo próprio app do WacOS (WacOSPropria, barra_mac.h), não pelo gancho do Shell: a barra é do app e
// o "desfazer" do Shell (ao sair) não vale aqui
bool g_proprio = false;
UINT g_msgUndo, g_msgZoom;  // Zoom: "MacJanelaAmpliar" (o Shell manda no Win+↑/↓; a DLL usa para converter o maximizar)
ID2D1Factory* g_d2d;
IDWriteFactory* g_dw;
IDWriteTextFormat* g_tf;
IDWriteTextFormat* g_tfLeo;  // Leopard: o título em Lucida (Grande no Mac; Sans Unicode no Windows) 13 normal
IDWriteTextFormat* g_tfTool;  // título das janelas de plug-in (menor, normal)

struct Info {
    HWND owner = nullptr, overlay = nullptr;
    LONG style = 0;          // estilo original
    HMENU menu = nullptr;    // barra de menus original
    bool active = true, hoverLights = false, dark = false;
    int press = -1;
    COLORREF cap = CLR_INVALID;  // cor da barra lida do próprio app (o topo da área dele)
    COLORREF lastRead = CLR_INVALID;  // última leitura (só guarda a cor lida duas vezes igual)
    DWORD fastUntil = 0;         // até quando reler a cada quadro (logo depois de aparecer ou ativar)
    bool needColor = true;       // ainda não leu a cor de verdade desta janela
    bool zoomed = false;         // ampliada pelo botão verde (ocupa a área de trabalho, mas não está "maximizada")
    RECT before{};               // tamanho de antes de ampliar
    bool tiled = false;          // encaixada numa metade/quarto da tela (arrastada até a borda)
    bool dropShadow = false;     // a classe pedia a sombra antiga do Windows (CS_DROPSHADOW): desligada enquanto tratamos
    DWORD attachedAt = GetTickCount();  // quando a DLL pegou a janela (FixOversize)
    bool moving = false;         // arrastando/redimensionando agora
    RECT moveStart{};            // retângulo no começo do arrasto
    DWORD lastStartAt = 0;
    RECT lastStart{};            // retângulo no começo do último arrasto (o "antes" quando o Shell encaixa)
    sombra::Sombra shadow;       // sombra grande do Mac, logo abaixo da janela
    // modo "barra própria" (Chrome, Discord, VS Code...): o app desenha a barra e os botões dele;
    // por cima vão os botões do Mac à esquerda e uma faixa da cor da barra sobre os botões do app
    bool custom = false;
    // Barra própria: os botões do Mac à esquerda, na própria linha do app (como o Chrome do Mac, sem barra a mais),
    // e por cima dos botões do app uma faixa da largura medida deles (MedirBarra)
    float patchR = 0;            // largura dos botões do app (pt); 0 = ainda não medida (PATCH_R)
    bool capas = false;          // capas dos cantos à mostra (CapasCantos): os cantos do Finder por cima dos 8 px do DWM
    RECT capasVis{};             // retângulo da última foto do fundo dos cantos
    DWORD capasAt = 0;           // quando foi (refaz a cada 10 s ou quando a janela muda de lugar)
    DWORD layoutAt = 0;          // última medição (0 = medir na próxima volta do timer)
    // janela de ferramenta com barra (plug-ins VST do Ableton e afins): barra escura compacta, como as janelas de
    // plug-in no Mac (botões menores, título cinza no meio), sem pegar a cor do app
    bool tool = false;
    bool transit = false;        // animação do "ampliar" em curso: a janela está escondida (cloak) mudando de tamanho
    DWORD colorFreezeUntil = 0;  // sem ler a cor da barra até aqui (logo depois do ampliar o topo do app ainda está preto)
    int barH = 0;                // altura da barra do app (px), medida na tela
    COLORREF leftColor = RGB(32, 32, 32), rightColor = RGB(32, 32, 32);
    SIZE rgn{};                  // tamanho com que o recorte arredondado foi feito (0 = sem recorte)
    int il = -1, it = 0, ir = 0, ib = 0;  // bordas invisíveis medidas antes do recorte (depois o DWM não informa mais)
    HWND preview = nullptr;      // prévia do encaixe enquanto arrasta (atrás da janela)
    RECT previewFor{};
    HWND corner[1]{};            // moldura: cantos suaves e contorno do Mac (o recorte sozinho fica serrilhado)
    COLORREF bottom[2]{ CLR_INVALID, CLR_INVALID };  // cor do app junto aos cantos de baixo (esq., dir.)
    COLORREF topc[2]{ CLR_INVALID, CLR_INVALID };    // e junto aos de cima, quando o próprio app desenha ali (barra unificada)
    // o que a moldura desenhou da última vez (W, H, raio, cor de cima, de baixo esq./dir., escuro): igual, só anda
    UINT32 cornerKey[10]{};
};

bool Dark() {
    DWORD v = 1, cb = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &cb);
    return v == 0;
}

float Scale(HWND h) { return GetDpiForWindow(h) / 96.f; }

// Faixa da barra de título na tela: do topo visível da janela até o começo da área do programa
RECT CaptionRect(HWND h) {
    RECT wr; GetWindowRect(h, &wr);
    POINT c{ 0, 0 }; ClientToScreen(h, &c);
    RECT cr; GetClientRect(h, &cr);
    int side = c.x - wr.left;  // borda (com a parte invisível de redimensionar)
    int top = IsZoomed(h) ? wr.top + side : wr.top + 1;
    return { c.x, top, c.x + cr.right, c.y };
}

// Barra unificada (macOS 27, apps do WacOS feitos com o app2d.h): o app tira a barra do Windows e desenha a
// barra de ferramentas dele nos primeiros N pt da área do app (prop "WacOSBarraUnificada" = N). A DLL continua
// com os cantos, a sombra, o ampliar e os três botões, desenhados por cima no meio dessa faixa, a 26 pt da borda.
int UnifiedBar(HWND h) { return (int)(INT_PTR)GetPropW(h, L"WacOSBarraUnificada"); }

bool BarraPropria();       // mais abaixo: barra_mac_propria=1 no ajustes.txt do Shell (desligado por padrão: em testes)
bool ProcessoChromium();   // mais abaixo
bool Fullscreen(HWND h);   // mais abaixo
// 0: não mexe; 1: barra padrão do Windows; 2: barra desenhada pelo próprio app (Chromium/Electron e afins)
int Mode(HWND h, bool showing = false) {  // showing: chamada no WM_SHOWWINDOW, antes de ficar visível
    if (GetAncestor(h, GA_ROOT) != h || (!showing && !IsWindowVisible(h)) || GetPropW(h, PROP)) return 0;
    LONG st = GetWindowLongW(h, GWL_STYLE), ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (st & WS_CHILD) return 0;
    // janela de ferramenta: só a que tem barra de título e dono (janela de plug-in do Ableton: Vst3PlugWindow) ou,
    // sem dono, com o botão de fechar (janelas de plug-in do Cubase: "06 - Kontakt 8", ferramenta sem dono).
    // Paletas soltas sem fechar continuam de fora.
    if ((ex & WS_EX_TOOLWINDOW) && ((st & WS_CAPTION) != WS_CAPTION || (!GetWindow(h, GW_OWNER) && !(st & WS_SYSMENU)))) return 0;
    RECT wr; GetWindowRect(h, &wr);
    POINT c{ 0, 0 }; ClientToScreen(h, &c);
    RECT cr; GetClientRect(h, &cr);
    float sc = Scale(h);
    // maximizada: o topo do retângulo fica acima da tela (borda invisível); a barra começa depois dela
    int top = IsZoomed(h) ? wr.top + (c.x - wr.left) : wr.top;
    int cap = c.y - top;
    if ((st & WS_CAPTION) == WS_CAPTION && cap >= 20 * sc && cap <= 90 * sc) return 1;
    if ((st & WS_CAPTION) == WS_CAPTION && UnifiedBar(h) > 0) return 1;  // barra unificada: a faixa é do app
    // sem barra do Windows: janela principal redimensionável, com título e de bom tamanho
    // Apps com barra própria (Chrome, Discord, Electron): desligado. Converter o maximizar quebrava o F11
    // (tela cheia passa pelo maximizado), o recorte errava as bordas deles e o ganho era pequeno
    // (já têm cantos do Windows 11 e a sombra vem do Shell). Fica o modo 2 no código para outra tentativa.
    // Volta (21/11/2026, "a barra do Mac em todos os apps"; Reddit: botões do Mac não apareciam no Edge e no Figma):
    // os três botões do Mac à esquerda e a faixa da cor da barra por cima dos botões do Windows, SEM converter o
    // maximizar (o app maximiza e entra em tela cheia sozinho: o F11 continua o dele) e sem recorte. O modo leve
    // (cantos do DWM, contorno e a animação de ampliar) continua junto. Desliga com barra_mac_propria=0.
    (void)cr;
    if (cap < 20 * sc && BarraPropria() &&  /* o Chromium mantém WS_CAPTION; a barra do Windows é que não existe */ (st & WS_THICKFRAME) && (st & WS_MAXIMIZEBOX) &&
        !(ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) && !Fullscreen(h)) {
        wchar_t cls[32] = {}; GetClassNameW(h, cls, 32);
        if (!wcscmp(cls, L"Chrome_WidgetWin_1") && ProcessoChromium() && wr.right - wr.left >= 300 && wr.bottom - wr.top >= 200) return 2;
    }
    return 0;
}

// modo barra própria: a faixa cobre o topo visível da janela (sem a borda invisível)
// O Chromium/Electron deixa a área do app do tamanho da parte visível (a borda invisível de 11 px fica de fora dos
// lados e embaixo). O DWM nem sempre sabe disso: com o quadro dele desligado, o "retângulo visível" vinha igual ao da
// janela inteira e a faixa saía 11 px para fora (o vermelho cortado no Discord, 21/11/2026). Vale a interseção dos dois.
RECT FrameRect(HWND h) {
    POINT c{ 0, 0 }; ClientToScreen(h, &c);
    RECT cr; GetClientRect(h, &cr);
    RECT cli{ c.x, c.y, c.x + cr.right, c.y + cr.bottom }, r;
    bool aware = GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(h)) != DPI_AWARENESS_UNAWARE;
    if (!aware || FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) return cli;
    RECT x{ std::max(r.left, cli.left), std::min(r.top, cli.top), std::min(r.right, cli.right), std::min(r.bottom, cli.bottom) };
    return x.right > x.left && x.bottom > x.top ? x : cli;
}
bool TemaTahoe(bool force = false);  // mais abaixo: tema Tahoe ligado no ajustes.txt do Shell
bool TemaGolden();                   // mais abaixo: macOS 27 Golden Gate (liquid_glass=1 + golden_gate=1)
int CornerRadius(HWND h);  // mais abaixo: o canto da janela (px)
COLORREF CapColor(Info* in);  // mais abaixo: a cor da barra

// Recorte arredondado [x0, x1) × [y0, y1) com raio r (px), simétrico e sempre por dentro do arco que a moldura
// suavizada (UpdateCorners) desenha: um pixel só entra se o centro dele fica a 0,5 px ou mais para dentro do arco.
// O CreateRoundRectRgn do Windows não é simétrico (sai ~1 px a mais à direita e embaixo): os degraus dele
// passavam do arco suavizado e sobravam "cantinhos" claros nas janelas no tema claro (20/11/2026).
HRGN RoundRgn(int x0, int y0, int x1, int y1, int r) {
    int w = x1 - x0, h = y1 - y0;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    std::vector<RECT> rows;
    auto inset = [&](int y) {  // quantos pixels ficam de fora no começo da linha y (0 = topo)
        int yy = y < r ? y : h - 1 - y;
        if (yy >= r) return 0;
        float dy = r - (yy + 0.5f), lim = r - 0.5f;
        float in2 = lim * lim - dy * dy;
        if (in2 < 0) return -1;  // linha inteira fora do arco
        return (int)std::ceil(r - std::sqrt(in2) - 0.5f);
    };
    for (int y = 0; y < h; y++) {
        int k = inset(y);
        if (k < 0 || 2 * k >= w) continue;
        if (y >= r && y < h - r) { rows.push_back({ x0, y0 + y, x1, y0 + h - r }); y = h - r - 1; continue; }  // o meio de uma vez
        rows.push_back({ x0 + k, y0 + y, x1 - k, y0 + y + 1 });
    }
    std::vector<char> buf(sizeof(RGNDATAHEADER) + rows.size() * sizeof(RECT));
    auto* rd = (RGNDATA*)buf.data();
    rd->rdh = { sizeof(RGNDATAHEADER), RDH_RECTANGLES, (DWORD)rows.size(), (DWORD)(rows.size() * sizeof(RECT)), { x0, y0, x1, y1 } };
    memcpy(rd->Buffer, rows.data(), rows.size() * sizeof(RECT));
    return ExtCreateRegion(nullptr, (DWORD)buf.size(), rd);
}
bool OwnApp();     // mais abaixo: um dos apps do WacOS
const float PATCH_L = 80, PATCH_R = 3 * 46 + 6;  // pt: botões do Mac / os três botões do app (46 pt cada no Windows)
float PatchR(Info* in) { return in->patchR > 0 ? in->patchR : PATCH_R; }  // a medida do app, quando já se sabe
RECT BarRect(Info* in);

// Apps do Windows (barra de título padrão, mais alta que a do Mac): os botões 4 pt mais para dentro; na medida do
// Mac eles pareciam colados na borda (Reddit, 20/11/2026). Os apps do WacOS, barras próprias e o Leopard ficam como estão.
float WinAppInset(Info* in) {
    return in->custom || in->tool || OwnApp() || UnifiedBar(in->owner) || leopard::On() ? 0.f : 4.f;
}

// posição dos três botões (pt): centro do 1º, passo entre centros e raio; o desenho e o clique usam esta
void LightsLayout(Info* in, float& lx0, float& lpitch, float& lr) {
    lx0 = in->tool ? 14.f : 19.f; lpitch = in->tool ? 20.f : 23.f; lr = in->tool ? 6.f : 7.f;
    // Tahoe (guia macOS26-Theme-Reference, seção 9.1, medido): só título = 14×14 com o 1º em x 9 (centro 16),
    // 23 entre eles; painel utilitário = 10×10 em x 8 (centro 13), 17 entre eles
    if (TemaTahoe()) { lx0 = in->tool ? 13.f : 16.f; lpitch = in->tool ? 17.f : 23.f; lr = in->tool ? 5.f : 7.f; }
    if (int u = UnifiedBar(in->owner)) { lx0 = u > 40 ? 26.f : 16.f; lpitch = 23; lr = 7; }  // macOS 27: com barra de ferramentas 26, 49, 72; só título 16, 39, 62
    if (leopard::On() && !in->tool && !UnifiedBar(in->owner)) { lx0 = 14.5f; lpitch = 21; lr = 7; }  // Leopard (medido: 14 px, 21 entre centros)
    // Barra própria (Chrome e afins): como o Chrome do Mac, medido no print do usuário com o Finder do mesmo print como
    // régua (23 pt entre os botões = 27 px): 1º centro a ~18,7 pt da borda, 23 pt entre eles, 14 pt (21/11/2026)
    if (in->custom && !in->tool && !leopard::On()) { lx0 = 19; lpitch = 23; lr = 7; }
    lx0 += WinAppInset(in);
}
// Buttons on the right (semaforo::Direita, botoes_direita=1): Windows order, close at the edge. Not in the WacOS apps
// that use this DLL for their bar (their toolbars are laid out after left buttons)
bool Right(Info* in) { return semaforo::Direita() && !OwnApp() && !UnifiedBar(in->owner); }
// centre x (pt) of button k (0 close, 1 minimise, 2 zoom) in a bar W pt wide
float LightX(Info* in, int k, float W) {
    float lx0, lp, lr; LightsLayout(in, lx0, lp, lr);
    if (!Right(in)) return lx0 + k * lp;
    int pos = k == 0 ? 0 : k == 2 ? 1 : 2;
    return W - lx0 - pos * lp;
}
// largura (pt) da faixa da esquerda na barra própria: só até 6 pt depois do verde (cobre o mínimo do app)
float PatchL(Info* in) { float lx0, lp, lr; LightsLayout(in, lx0, lp, lr); return lx0 + 2 * lp + lr + 6; }
// onde a linha das abas começa (gancho do Chrome): no Chrome do Mac a 1ª aba (com o "pé" curvo) começa ~16 pt depois
// do verde, a 88 pt da borda; o Chrome ainda põe uns 6 pt dele depois da área reservada (medido no 157 com o ˅ da busca
// de abas desafixado: com 72 + 10 a aba começava em 94)
float LeadW(Info* in) { float lx0, lp, lr; LightsLayout(in, lx0, lp, lr); return lx0 + 2 * lp + lr + 4; }
// altura (pt) do centro dos botões: no meio da barra do app (a linha das abas no Chrome, como no Mac)
float LightsY(Info* in, float H) {
    if (in->custom && in->barH) return in->barH / Scale(in->owner) / 2;
    return H / 2;
}

void Insets(Info* in, int& l, int& t, int& rr, int& b);
RECT BarRect(Info* in) {
    if (int u = UnifiedBar(in->owner)) {  // a faixa de cima da área do app
        POINT c{ 0, 0 }; ClientToScreen(in->owner, &c);
        RECT cr; GetClientRect(in->owner, &cr);
        return { c.x, c.y, c.x + cr.right, c.y + (int)std::lround(u * Scale(in->owner)) };
    }
    if (!in->custom) {
        RECT r = CaptionRect(in->owner);
        // tamanho fixo (diálogos): o recorte pega a janela inteira, com a borda do Windows dos lados da barra; a barra
        // vai até o recorte, senão sobrava uma faixa lisa ao lado dela (bem visível no metal do Leopard, 20/11/2026)
        if (in->rgn.cx && !IsZoomed(in->owner)) {
            int l, t, rr, b; Insets(in, l, t, rr, b);
            RECT wr; GetWindowRect(in->owner, &wr);
            r.left = std::min(r.left, wr.left + l); r.right = std::max(r.right, wr.right - rr);
            r.top = std::min(r.top, wr.top + t);  // e de cima, rente ao recorte (a linha de 1 px do topo também)
        }
        return r;
    }
    RECT f = FrameRect(in->owner);
    float sc = Scale(in->owner);
    int h = in->barH ? in->barH : (int)(32 * sc);
    if (in->capas) { f.left = in->capasVis.left; f.right = in->capasVis.right; }  // rente às capas (com o fio da borda do DWM)
    return { f.left, f.top, f.right, f.top + h };
}

// ---- desenho da faixa (janela em camadas, alfa por pixel)
void Line(ID2D1RenderTarget* rt, ID2D1Brush* b, float x1, float y1, float x2, float y2, float w) { rt->DrawLine({ x1, y1 }, { x2, y2 }, b, w); }

void Paint(Info* in) {
    if (!in->overlay || !g_d2d) return;
    RECT r = BarRect(in);
    int w = std::max<int>(r.right - r.left, 1), h = std::max<int>(r.bottom - r.top, 1);
    float sc = Scale(in->owner);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    ID2D1DCRenderTarget* rt = nullptr;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * sc, 96 * sc);
    if (SUCCEEDED(g_d2d->CreateDCRenderTarget(&props, &rt))) {
        RECT br{ 0, 0, w, h };
        rt->BindDC(mem, &br);
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &b);
        float H = h / sc, W = w / sc;
        if (in->custom) {
            // fundo da cor da barra do app atrás dos botões do Mac, e por cima dos botões do app (cantos da janela)
            auto fill = [&](COLORREF c, D2D1_RECT_F rr, bool leftCorner) {
                b->SetColor(D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1));
                ID2D1PathGeometry* g = nullptr;
                g_d2d->CreatePathGeometry(&g);
                ID2D1GeometrySink* sk = nullptr;
                g->Open(&sk);
                // maximizada: canto reto (o arco deixava ver um pedaço do app); com as capas, o raio do Finder
                float R = IsZoomed(in->owner) ? 0.f : in->capas ? CornerRadius(in->owner) / sc : 8.f;
                if (leftCorner) {
                    sk->BeginFigure({ rr.left, rr.bottom }, D2D1_FIGURE_BEGIN_FILLED);
                    sk->AddLine({ rr.left, rr.top + R });
                    sk->AddArc(D2D1::ArcSegment({ rr.left + R, rr.top }, { R, R }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    sk->AddLine({ rr.right, rr.top }); sk->AddLine({ rr.right, rr.bottom });
                } else {
                    sk->BeginFigure({ rr.left, rr.bottom }, D2D1_FIGURE_BEGIN_FILLED);
                    sk->AddLine({ rr.left, rr.top }); sk->AddLine({ rr.right - R, rr.top });
                    sk->AddArc(D2D1::ArcSegment({ rr.right, rr.top + R }, { R, R }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    sk->AddLine({ rr.right, rr.bottom });
                }
                sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                sk->Close(); sk->Release();
                rt->FillGeometry(g, b);
                g->Release();
            };
            if (!Right(in)) fill(in->leftColor, D2D1::RectF(0, 0, PatchL(in), H), true);
            fill(in->rightColor, D2D1::RectF(W - PatchR(in), 0, W, H), false);
            if (in->capas && !IsZoomed(in->owner)) {  // o fio da borda do DWM, que as faixas cobrem nos lados (continua o das capas)
                int fundo = (int)(INT_PTR)GetPropW(in->owner, L"MacJanelaFundo");
                bool dk = fundo ? fundo == 2 : Dark();
                b->SetColor(dk ? D2D1::ColorF(0x4A / 255.f, 0x4A / 255.f, 0x4D / 255.f) : D2D1::ColorF(0xC2 / 255.f, 0xC2 / 255.f, 0xC5 / 255.f));
                float Rr = CornerRadius(in->owner) / sc, o = 0.5f / sc, lw = 1 / sc;
                for (int side = Right(in) ? 1 : 0; side < 2; side++) {
                    ID2D1PathGeometry* g = nullptr;
                    if (FAILED(g_d2d->CreatePathGeometry(&g))) continue;
                    ID2D1GeometrySink* sk = nullptr;
                    if (SUCCEEDED(g->Open(&sk))) {
                        if (!side) {
                            sk->BeginFigure({ o, H }, D2D1_FIGURE_BEGIN_HOLLOW);
                            sk->AddLine({ o, Rr });
                            sk->AddArc(D2D1::ArcSegment({ Rr, o }, { Rr - o, Rr - o }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                            sk->AddLine({ PatchL(in), o });
                        } else {
                            sk->BeginFigure({ W - PatchR(in), o }, D2D1_FIGURE_BEGIN_HOLLOW);
                            sk->AddLine({ W - Rr, o });
                            sk->AddArc(D2D1::ArcSegment({ W - o, Rr }, { Rr - o, Rr - o }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                            sk->AddLine({ W - o, H });
                        }
                        sk->EndFigure(D2D1_FIGURE_END_OPEN);
                        sk->Close(); sk->Release();
                        rt->DrawGeometry(g, b, lw);
                    }
                    g->Release();
                }
            }
        }
        // Leopard: a barra de título padrão vira o metal cinza do 10.5 de ponta a ponta (com os cantos de cima da
        // janela); a barra própria dos apps (Chromium e afins) e as janelas de plug-in ficam como estão
        const bool leo = leopard::On() && !in->custom && !in->tool && !UnifiedBar(in->owner);
        if (leo) leopard::Metal(rt, D2D1::RectF(0, 0, W, H), in->active, in->dark, true, IsZoomed(in->owner) ? 0.f : CornerRadius(in->owner) / sc);
        // Janela que desenha tudo pela placa de vídeo (WS_EX_NOREDIRECTIONBITMAP: Windows Terminal com a barra do
        // Windows, apps GTK/WinUI): a barra que o PaintNC pinta com GDI não aparece nelas, e com o recorte o DWM não
        // pinta nada: a barra ficava transparente, com o papel de parede atrás (issue #20, 21/11/2026). A cor da barra
        // vai aqui, na faixa, com os cantos de cima da janela.
        else if (!in->custom && !in->tool && !UnifiedBar(in->owner) && (GetWindowLongW(in->owner, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP)) {
            COLORREF cc = CapColor(in);
            b->SetColor(D2D1::ColorF(GetRValue(cc) / 255.f, GetGValue(cc) / 255.f, GetBValue(cc) / 255.f, 1));
            float R = IsZoomed(in->owner) ? 0.f : CornerRadius(in->owner) / sc;
            rt->PushAxisAlignedClip(D2D1::RectF(0, 0, W, H), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, W, H + R + 1), R, R), b);
            rt->PopAxisAlignedClip();
        }
        // os três botões no tamanho padrão do projeto (o do Monitor de Atividade): 14 pt, 23 pt entre centros
        static const D2D1_COLOR_F on_c[3] = { D2D1::ColorF(1.0f, 0.373f, 0.341f), D2D1::ColorF(0.996f, 0.737f, 0.18f), D2D1::ColorF(0.157f, 0.784f, 0.251f) }; const D2D1_COLOR_F* on = semaforo::Pick(on_c, semaforo::FILL);
        LONG st = in->style;
        bool can[3] = { true, (st & WS_MINIMIZEBOX) != 0, (st & WS_MAXIMIZEBOX) != 0 };
        float lx0, lpitch, lr;
        LightsLayout(in, lx0, lpitch, lr);
        for (int k = 0; k < 3; k++) {
            D2D1_POINT_2F c{ LightX(in, k, W), LightsY(in, H) };
            bool lit = (in->active || in->hoverLights) && can[k];
            b->SetColor(lit ? on[k] : (in->dark ? D2D1::ColorF(1, 1, 1, 0.22f) : D2D1::ColorF(0, 0, 0, 0.18f)));
            rt->FillEllipse(D2D1::Ellipse(c, lr, lr), b);
            semaforo::Gloss(rt, c, lr, b);
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.12f));
            rt->DrawEllipse(D2D1::Ellipse(c, lr - 0.25f, lr - 0.25f), b, 0.5f);
            if (!in->hoverLights || !can[k]) continue;
            b->SetColor(D2D1::ColorF(0, 0, 0, 0.55f));
            float a = 2.6f;
            if (k == 0) { Line(rt, b, c.x - a, c.y - a, c.x + a, c.y + a, 1.2f); Line(rt, b, c.x - a, c.y + a, c.x + a, c.y - a, 1.2f); }
            else if (k == 1) Line(rt, b, c.x - 3.2f, c.y, c.x + 3.2f, c.y, 1.3f);
            else if (TemaTahoe() && !in->tool) {  // Tahoe: setas diagonais da tela cheia (o "+" é a variante do ⌥)
                ID2D1PathGeometry* g = nullptr;
                if (SUCCEEDED(g_d2d->CreatePathGeometry(&g))) {
                    ID2D1GeometrySink* sk = nullptr;
                    if (SUCCEEDED(g->Open(&sk))) {
                        float s = 3.6f, o = 0.6f;
                        sk->BeginFigure({ c.x - s, c.y - s }, D2D1_FIGURE_BEGIN_FILLED);
                        sk->AddLine({ c.x + s - o * 2, c.y - s }); sk->AddLine({ c.x - s, c.y + s - o * 2 });
                        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sk->BeginFigure({ c.x + s, c.y + s }, D2D1_FIGURE_BEGIN_FILLED);
                        sk->AddLine({ c.x - s + o * 2, c.y + s }); sk->AddLine({ c.x + s, c.y - s + o * 2 });
                        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sk->Close(); sk->Release();
                        rt->FillGeometry(g, b);
                    }
                    g->Release();
                }
            }
            else { Line(rt, b, c.x - 3.2f, c.y, c.x + 3.2f, c.y, 1.3f); Line(rt, b, c.x, c.y - 3.2f, c.x, c.y + 3.2f, 1.3f); }
        }
        // título centralizado
        wchar_t t[256] = {}; GetWindowTextW(in->owner, t, 256);
        if (in->tool && g_tfTool && t[0]) {  // plug-in: título cinza, menor, no meio
            b->SetColor(D2D1::ColorF(1, 1, 1, in->active ? 0.62f : 0.38f));
            rt->DrawTextW(t, (UINT32)wcslen(t), g_tfTool, D2D1::RectF(70, 0, W - 70, H), b);
        } else if (g_tf && t[0] && !in->custom && !(semaforo::Aqua() && OwnApp())) {  // macOS 27: os apps do sistema não mostram o título
            if (leo && !in->dark) {  // Leopard: o título em relevo (sombra branca 1 pt abaixo)
                b->SetColor(D2D1::ColorF(1, 1, 1, in->active ? 0.55f : 0.35f));
                rt->DrawTextW(t, (UINT32)wcslen(t), g_tfLeo ? g_tfLeo : g_tf, D2D1::RectF(80, 1, W - 80, H + 1), b);
            }
            b->SetColor(in->dark ? D2D1::ColorF(1, 1, 1, in->active ? 0.86f : 0.42f) : D2D1::ColorF(0, 0, 0, in->active ? 0.85f : 0.40f));
            rt->DrawTextW(t, (UINT32)wcslen(t), leo && g_tfLeo ? g_tfLeo : g_tf, D2D1::RectF(80, 0, W - 80, H), b);
        }
        b->Release();
        rt->EndDraw();
        rt->Release();
    }
    POINT dst{ r.left, r.top }, src{ 0, 0 };
    SIZE sz{ w, h };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(in->overlay, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

int HitLight(Info* in, POINT clientPt) {
    float sc = Scale(in->owner);
    RECT r = BarRect(in);
    float H = (r.bottom - r.top) / sc;
    float x = clientPt.x / sc, y = clientPt.y / sc, W = (r.right - r.left) / sc;
    for (int k = 0; k < 3; k++) if (std::fabs(x - LightX(in, k, W)) <= 9 && std::fabs(y - LightsY(in, H)) <= 8) return k;  // mesmas posições do desenho
    return -1;
}

// "Ampliar" do Mac: a janela cresce até ocupar a área de trabalho (entre a barra de menus e o Dock),
// sem entrar no estado maximizado do Windows, que tira os cantos arredondados e a sombra.
// Bordas invisíveis de redimensionar (fora da parte visível da janela), em px da janela
void FrameInsets(HWND h, int& l, int& t, int& rr, int& b) {
    RECT wr, fb;
    GetWindowRect(h, &wr);
    bool aware = GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(h)) != DPI_AWARENESS_UNAWARE;
    if (aware && SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &fb, sizeof(fb)))) {
        l = fb.left - wr.left; t = fb.top - wr.top; rr = wr.right - fb.right; b = wr.bottom - fb.bottom;
    } else {
        UINT dpi = GetDpiForWindow(h);
        l = rr = b = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        t = 0;
    }
}

// Cantos mais arredondados que os 8 px do Windows: a janela é recortada num retângulo arredondado do
// tamanho da parte visível (o recorte não tem suavização e tira a borda fina do Windows; a sombra é a nossa).
void Insets(Info* in, int& l, int& t, int& rr, int& b) {
    // só vale medir com a janela no estado normal: maximizada, o retângulo dela passa da tela e as
    // "bordas" medidas seriam falsas (o Discord ficava cortado 10 px de cada lado)
    {
        // rente à área do app nos lados e embaixo (sem sobrar nada da borda clássica); em cima, a barra
        // inteira (barra padrão) ou nada (barra própria: a área do app é a janela visível inteira)
        RECT wr; GetWindowRect(in->owner, &wr);
        if (in->custom) { RECT f = FrameRect(in->owner); l = f.left - wr.left; t = f.top - wr.top; rr = wr.right - f.right; b = wr.bottom - f.bottom; return; }
        POINT c{ 0, 0 }; ClientToScreen(in->owner, &c);
        RECT cr; GetClientRect(in->owner, &cr);
        l = c.x - wr.left; rr = wr.right - (c.x + cr.right); b = wr.bottom - (c.y + cr.bottom);
        t = in->custom || controles::IsAlert(in->owner) ? c.y - wr.top : (IsZoomed(in->owner) ? l : 0);
        // tamanho fixo (diálogos): não há borda de redimensionar para esconder; o retângulo inteiro vira a
        // janela (as bordas ganham a cor da barra). Rente à área do app, sobrava em volta um retângulo
        // escuro do tamanho da janela (o diálogo de salvar do Ableton)
        // Janela de plug-in (ferramenta): rente ao conteúdo mesmo sem redimensionar, como no Mac (a moldura de
        // 11 px do Windows ficava grossa dos lados e embaixo, na cor da barra)
        if (!in->custom && !in->tool && !(GetWindowLongW(in->owner, GWL_STYLE) & WS_THICKFRAME) && !IsZoomed(in->owner)) l = rr = b = 0;
        return;
    }
    if (in->il < 0 || in->rgn.cx == 0) {
        if (IsZoomed(in->owner)) { FrameInsets(in->owner, l, t, rr, b); return; }
        FrameInsets(in->owner, in->il, in->it, in->ir, in->ib);
    }
    l = in->il; t = in->it; rr = in->ir; b = in->ib;
}

// Cor da barra lembrada por programa (HKCU\Software\ShellProprio\MacJanela\Cores): a janela já abre
// na cor certa, sem passar pela cor padrão até a primeira leitura.
const wchar_t* COLORS_KEY = L"Software\\ShellProprio\\MacJanela\\Cores";
std::wstring ExeName() {
    wchar_t p[MAX_PATH] = {}; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p; s = s.substr(s.find_last_of(L'\\') + 1);
    for (auto& c : s) c = towlower(c);
    return s;
}
// por janela: o programa e, nas janelas secundárias (com dono, ex.: "Export Audio/Video" do Ableton), o
// título; antes era uma cor só por programa e o diálogo nascia na cor da janela principal
std::wstring ColorKey(HWND h) {
    std::wstring k = ExeName();
    if (h && GetWindow(h, GW_OWNER)) { wchar_t t[80] = {}; GetWindowTextW(h, t, 80); if (t[0]) k += std::wstring(L"|") + t; }
    return k;
}
COLORREF SavedColor(HWND h = nullptr) {
    DWORD v = 0, cb = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, COLORS_KEY, ColorKey(h).c_str(), RRF_RT_REG_DWORD, nullptr, &v, &cb) == ERROR_SUCCESS) return v;
    if (h && GetWindow(h, GW_OWNER)) return SavedColor(nullptr);  // diálogo novo: a do programa por enquanto
    return CLR_INVALID;
}
void SaveColor(HWND h, COLORREF c) { DWORD v = c; RegSetKeyValueW(HKEY_CURRENT_USER, COLORS_KEY, ColorKey(h).c_str(), REG_DWORD, &v, sizeof(v)); }

COLORREF CapColor(Info* in) {
    if (in->cap != CLR_INVALID) return in->cap;
    return Dark() ? RGB(0x2D, 0x2D, 0x2F) : RGB(0xE9, 0xE9, 0xEA);
}

// Com o recorte o Windows desenha a barra no estilo clássico (degradê, título e borda grossa):
// pintamos nós a área fora do programa, lisa, na cor da barra (os botões e o título vêm da faixa por cima).
void ApplyColors(Info* in);

void PaintNC(Info* in) {
    if (in->custom || !in->rgn.cx) return;
    HWND h = in->owner;
    HDC dc = GetWindowDC(h);
    if (!dc) return;
    RECT wr; GetWindowRect(h, &wr);
    POINT c{ 0, 0 }; ClientToScreen(h, &c);
    RECT cr; GetClientRect(h, &cr);
    ExcludeClipRect(dc, c.x - wr.left, c.y - wr.top, c.x - wr.left + cr.right, c.y - wr.top + cr.bottom);
    HBRUSH b = CreateSolidBrush(CapColor(in));
    RECT all{ 0, 0, wr.right - wr.left, wr.bottom - wr.top };
    FillRect(dc, &all, b);
    DeleteObject(b);
    ReleaseDC(h, dc);
}

// tela cheia do próprio app (Ableton Ctrl+Shift+F, vídeos...): a área do app cobre o monitor inteiro.
// Aí nada de cantos, contorno, faixa ou sombra, como no Mac.
bool Fullscreen(HWND h) {
    if (IsZoomed(h) || IsIconic(h)) return false;
    if ((GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) return false;  // com barra de título não é tela cheia (só grande demais)
    RECT cr; GetClientRect(h, &cr);
    POINT a{ 0, 0 }; ClientToScreen(h, &a);
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return a.x <= mi.rcMonitor.left && a.y <= mi.rcMonitor.top && a.x + cr.right >= mi.rcMonitor.right && a.y + cr.bottom >= mi.rcMonitor.bottom;
}

// Tema macOS Tahoe (liquid_glass=1 no ajustes.txt do Shell): as janelas são mais redondas (16 pt; Sequoia: 12 pt).
// Lido do arquivo no máximo a cada 3 s (a DLL roda dentro dos outros programas, sem acesso aos ajustes do Shell).
// um dos apps do próprio WacOS (WacOS*.exe): no macOS 27 a barra deles não tem título (Lembretes, Calendário…)
bool OwnApp() {
    static int own = -1;
    if (own < 0) { wchar_t p[MAX_PATH] = {}; GetModuleFileNameW(nullptr, p, MAX_PATH); const wchar_t* n = wcsrchr(p, L'\\'); own = n && !_wcsnicmp(n + 1, L"WacOS", 5) ? 1 : 0; }
    return own == 1;
}
static bool g_golden;  // lido junto com o Tahoe (TemaTahoe)
// barra_mac_propria (TemaTahoe). Desligado por padrão desde a build-25: os botões do Mac nos apps de barra própria
// ainda estão em testes (sessão "macOS Title bar for all apps"); barra_mac_propria=1 liga
static bool g_propria = false;
bool BarraPropria() { TemaTahoe(); return g_propria; }
bool TemaGolden() { return TemaTahoe() && g_golden; }
bool TemaTahoe(bool force) {
    static DWORD at; static bool on;
    DWORD now = GetTickCount();
    if (at && now - at < 3000 && !force) return on;
    at = now ? now : 1;
    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH)) return on;
    wcscat_s(path, L"\\Shell\\ajustes.txt");
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return on;
    char buf[8192]; DWORD n = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &n, nullptr);
    CloseHandle(f);
    buf[n] = 0;
    on = strstr(buf, "liquid_glass=1") != nullptr;
    g_golden = strstr(buf, "golden_gate=1") != nullptr;
    g_propria = strstr(buf, "barra_mac_propria=1") != nullptr;
    return on;
}
int CornerRadius(HWND h) { return (int)std::lround((TemaTahoe() ? 16 : 12)  /* Golden Gate: também 16 pt (medido) */ * Scale(h)); }

// Moldura e prévia: transparentes para cliques. A moldura confere sozinha (timer) se a janela dela
// ainda existe, está visível e ainda a reconhece como moldura: senão some (sobrava um contorno
// arredondado na tela depois que um diálogo fechava)
LRESULT CALLBACK CornerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_TIMER) {
        HWND target = (HWND)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (!target) return 0;
        // sem a marca "MacJanela" a janela já voltou ao Windows (desfeita por outra cópia da DLL, ou o desfazer não
        // achou esta moldura): sobravam 4 arcos brancos por fora dos cantos do Deluge depois de reiniciar o Shell
        if (!IsWindow(target) || GetPropW(target, L"MacJanelaMoldura") != (HANDLE)h || !GetPropW(target, PROP)) { DestroyWindow(h); return 0; }
        if (!IsWindowVisible(target) || IsIconic(target)) ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// O contorno da janela, de cada tema (em px da tela; larguras fracionárias viram meio-tom na borda):
//  - Sequoia e Tahoe: 1 px escuro por fora (55% no escuro, 22% no claro) e, no escuro, o brilho de 1 px por dentro (16%);
//  - macOS 27 Golden Gate (capturas 2x do 512 Pixels: Contatos, Calculadora, Relógio): por fora meio ponto quase preto
//    no escuro (95%) e fraco no claro (20%); por dentro, só no escuro, 1 pt claro (10%);
//  - Leopard (10.5): só o fio escuro de 1 px, sem brilho por dentro (o metal já tem o próprio realce).
struct Borda { float outW, outA, inW, inA; int id; };
Borda BordaDoTema(bool dark, float sc) {
    if (leopard::On()) return { 1.f, dark ? 0.60f : 0.38f, 0.f, 0.f, 3 };
    if (TemaGolden()) return { std::clamp(0.5f * sc, 0.75f, 1.f), dark ? 0.95f : 0.20f, dark ? 1.f * sc : 0.f, dark ? 0.10f : 0.f, 2 };
    return { 1.f, dark ? 0.55f : 0.22f, 1.f, dark ? 0.16f : 0.f, TemaTahoe() ? 1 : 0 };
}

void HideCorners(Info* in) { for (HWND c : in->corner) if (c) ShowWindow(c, SW_HIDE); if (in->capas) { in->capas = false; in->capasAt = 0; } }
void KeepAbove(Info* in);  // mais abaixo

// Moldura por cima da janela (uma janela em camadas, transparente para cliques, do tamanho da parte
// visível + 1 px): o SetWindowRgn só corta pixel inteiro, então aqui vão
//  - o arco dos cantos suavizado, na cor da própria janela naquele ponto (a barra em cima, a cor lida da tela embaixo);
//  - o contorno do Mac: 1 px escuro por fora e, no tema escuro, o brilho claro de 1 px por dentro.
// Só as faixas das bordas são calculadas (distância até o retângulo arredondado, por pixel).
void UpdateCorners(Info* in, RECT vis) {
    HWND h = in->owner;
    if (!in->rgn.cx || in->moving || !IsWindowVisible(h) || IsIconic(h) || IsZoomed(h) || Fullscreen(h)) { HideCorners(in); return; }
    int r = CornerRadius(h), S = r + 2;
    // cor embaixo: só lê da tela quando o ponto é mesmo desta janela (não coberto por outra)
    HDC screen = GetDC(nullptr);
    for (int i = 0; i < 2; i++) {
        POINT p{ i ? vis.right - 4 : vis.left + 3, vis.bottom - S - 2 };
        HWND at = WindowFromPoint(p);
        if (at && GetAncestor(at, GA_ROOT) == h) {
            COLORREF c = GetPixel(screen, p.x, p.y);
            if (c != CLR_INVALID) in->bottom[i] = c;
        }
    }
    // Em cima, cada canto com a cor dele quando é o app que desenha ali (barra unificada dos apps do WacOS). Com uma
    // cor só (a mais comum na faixa de cima), o Tempo com a lateral clara e o céu escuro ganhava um arco escuro
    // grosso no canto de cima à esquerda (issue #8, 21/11/2026). A barra do Mac por cima (outra janela) não conta.
    for (int i = 0; i < 2; i++) {
        in->topc[i] = CLR_INVALID;
        if (!UnifiedBar(h)) continue;
        POINT p{ i ? vis.right - 4 : vis.left + 3, vis.top + S + 2 };
        HWND at = WindowFromPoint(p);
        if (at && GetAncestor(at, GA_ROOT) == h) in->topc[i] = GetPixel(screen, p.x, p.y);
    }
    ReleaseDC(nullptr, screen);
    HWND& fw = in->corner[0];
    if (fw && !IsWindow(fw)) { fw = nullptr; memset(in->cornerKey, 0, sizeof(in->cornerKey)); }  // a moldura se fechou sozinha (CornerProc)
    if (!fw) {
        WNDCLASSW wc{};
        if (!GetClassInfoW(g_mod, L"MacJanelaCanto", &wc)) {
            wc.lpfnWndProc = CornerProc; wc.hInstance = g_mod; wc.lpszClassName = L"MacJanelaCanto";
            RegisterClassW(&wc);
        }
        fw = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MacJanelaCanto", L"",
                             WS_POPUP, 0, 0, 1, 1, h, nullptr, g_mod, nullptr);
        if (!fw) return;
        SetWindowLongPtrW(fw, GWLP_USERDATA, (LONG_PTR)h);
        SetPropW(h, L"MacJanelaMoldura", fw);
        SetTimer(fw, 1, 500, nullptr);
    }
    const int M = 1;  // margem para o contorno de fora
    int W = vis.right - vis.left + 2 * M, H = vis.bottom - vis.top + 2 * M;
    if (W <= 2 * S || H <= 2 * S || W > 16384 || H > 16384) { HideCorners(in); return; }
    // Nada mudou além do lugar: só move. Antes refazia a cada 1,5 s (o timer da faixa) um bitmap do tamanho da
    // janela inteira (3440×1400 = 19 MB, alocado, zerado e enviado ao DWM) dentro do programa dos outros.
    POINT pos{ vis.left - M, vis.top - M };
    COLORREF top = CapColor(in);
    Borda bd = BordaDoTema(in->dark, Scale(h));
    UINT32 key[10]{ (UINT32)W, (UINT32)H, (UINT32)r, (UINT32)top, (UINT32)in->bottom[0], (UINT32)in->bottom[1], (UINT32)in->dark, (UINT32)in->topc[0], (UINT32)in->topc[1], (UINT32)bd.id };
    if (!memcmp(key, in->cornerKey, sizeof(key)) && IsWindowVisible(fw)) {
        UpdateLayeredWindow(fw, nullptr, &pos, nullptr, nullptr, nullptr, 0, nullptr, 0);
        return;
    }
    memset(in->cornerKey, 0, sizeof(in->cornerKey));  // só vale depois de desenhar de verdade
    HDC mem = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);  // começa zerado (transparente)
    if (!bmp) { DeleteDC(mem); return; }
    auto* px = (UINT32*)bits;
    bool dark = in->dark;
    (void)dark;
    float outerA = bd.outA, innerA = bd.inA, outW = bd.outW, inW = bd.inW;  // contorno escuro / brilho, do tema (BordaDoTema)
    float hw = (vis.right - vis.left) / 2.f, hh = (vis.bottom - vis.top) / 2.f, cxm = M + hw, cym = M + hh;
    auto cov = [](float d) { return std::clamp(0.5f - d, 0.f, 1.f); };  // fração do pixel com distância <= 0
    auto pixel = [&](int x, int y) {
        float qx = std::fabs(x + 0.5f - cxm) - (hw - r), qy = std::fabs(y + 0.5f - cym) - (hh - r);
        float d = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;  // < 0 dentro
        float R = 0, G = 0, B = 0, A = 0;  // pré-multiplicado
        auto over = [&](float cr, float cg, float cb, float a) { R = cr * a + R * (1 - a); G = cg * a + G * (1 - a); B = cb * a + B * (1 - a); A = a + A * (1 - a); };
        // arco suavizado: só nos cantos, na faixa junto ao arco (o recorte corta um pouco por dentro)
        if (qx > 0 && qy > 0 && d > -2.5f) {
            COLORREF c = y < H / 2 ? (in->topc[x >= W / 2] != CLR_INVALID ? in->topc[x >= W / 2] : top) : in->bottom[x >= W / 2];
            if (c == CLR_INVALID) c = top;
            over(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, cov(d));
        }
        if (innerA > 0) over(1, 1, 1, innerA * (cov(d) - cov(d + inW)));   // brilho: por dentro da borda
        over(0, 0, 0, outerA * (cov(d - outW) - cov(d)));                     // contorno: por fora
        if (A <= 0) return;
        px[y * W + x] = (UINT32)std::lround(A * 255) << 24 | (UINT32)std::lround(R * 255) << 16 | (UINT32)std::lround(G * 255) << 8 | (UINT32)std::lround(B * 255);
    };
    int band = S + M + 1;
    for (int y = 0; y < H; y++) {
        bool edgeRow = y < band || y >= H - band;
        if (edgeRow) { for (int x = 0; x < W; x++) pixel(x, y); }
        else {
            for (int x = 0; x < band; x++) pixel(x, y);
            for (int x = W - band; x < W; x++) pixel(x, y);
        }
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    POINT zero{ 0, 0 };
    SIZE size{ W, H };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (UpdateLayeredWindow(fw, nullptr, &pos, &size, mem, &zero, 0, &bf, ULW_ALPHA)) memcpy(in->cornerKey, key, sizeof(key));
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    if (!IsWindowVisible(fw)) { ShowWindow(fw, SW_SHOWNOACTIVATE); KeepAbove(in); }  // nasce no topo da tela: desce para junto da janela
}

// sombra na parte visível (com o recorte, sem a borda invisível); os cantos suaves vão junto
// janela de abrir/salvar que o Shell está trocando pelo Finder (marcada por ele, ver dialogs.cpp): nada
// nosso aparece em volta dela (título, botões, sombra e cantos piscavam no canto da tela)
bool TakenByShell(HWND h) { return GetPropW(h, L"ShellDialogoPorFora") != nullptr; }

// ---- Cantos do Finder nos apps de barra própria (Chrome, Discord, Electron)
// O DWM arredonda essas janelas em 8 px e não deixa mais; o recorte não corta o que o Chromium desenha pela GPU. Então,
// em vez de cortar, a DLL COBRE a sobra entre o arco de 8 px e o do Finder com o que está atrás da janela naquele
// ponto: uma foto da janela de trás (PrintWindow, com o conteúdo da GPU; a área de trabalho do Shell ou outro app)
// escurecida pela nossa sombra, e por cima o contorno do Mac no arco grande. A foto é refeita quando a janela muda
// de lugar/tamanho e a cada 10 s; arrastando, as capas somem (voltam os 8 px) e reaparecem ao soltar.
bool JanelaNossa(HWND w) {
    wchar_t c[40] = {};
    GetClassNameW(w, c, 40);
    return !wcsncmp(c, L"MacJanela", 9);
}
// a primeira janela abaixo de h (ordem Z) que cobre o ponto p, fora as camadas transparentes (sombras, molduras)
HWND JanelaAtras(HWND h, POINT p) {
    for (HWND w = GetWindow(h, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w) || IsIconic(w) || JanelaNossa(w)) continue;
        LONG ex = GetWindowLongW(w, GWL_EXSTYLE);
        if ((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT)) continue;
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT r;
        if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(w, &r);
        if (PtInRect(&r, p)) return w;
    }
    return nullptr;
}
// foto de um retângulo da tela tirada só da janela w (PrintWindow): BGRA, sem alfa útil
bool FotoDe(HWND w, RECT box, std::vector<UINT32>& out) {
    RECT wr; GetWindowRect(w, &wr);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    if (W <= 0 || H <= 0 || W > 16384 || H > 16384) return false;
    HDC mdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(mdc); return false; }
    HGDIOBJ old = SelectObject(mdc, bmp);
    bool ok = PrintWindow(w, mdc, 2 /*PW_RENDERFULLCONTENT*/) != FALSE;
    int bw = box.right - box.left, bh = box.bottom - box.top;
    out.assign((size_t)bw * bh, 0);
    if (ok) {
        auto* px = (UINT32*)bits;
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                int sx = box.left + x - wr.left, sy = box.top + y - wr.top;
                if (sx >= 0 && sy >= 0 && sx < W && sy < H) out[(size_t)y * bw + x] = px[(size_t)sy * W + sx] & 0xFFFFFF;
            }
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc);
    return ok;
}

void CapasCantos(Info* in, RECT vis) {
    HWND h = in->owner;
    static const bool prova = wcsstr(GetCommandLineW(), L"--wacos-teste-abas") != nullptr;  // PROVA: só no Chrome de teste
    if (!prova) { HideCorners(in); return; }
    // A forma é a parte visível que o DWM desenha: o fio da borda dele fica 1 px por fora da área do app (dos lados e
    // embaixo). Rente à área do app, o arco de 8 px do DWM sobrava 1 px por fora da capa (o risquinho no cantinho) e o
    // fio do arco grande ficava 1 px para dentro do fio reto. Vale o retângulo do DWM quando ele difere da área do
    // app só nesse fio (com o quadro desligado ele vinha com a borda invisível de 11 px: aí fica a área do app).
    {
        RECT e;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &e, sizeof(e))) && std::abs(e.left - vis.left) <= 2 &&
            std::abs(e.top - vis.top) <= 2 && std::abs(e.right - vis.right) <= 2 && std::abs(e.bottom - vis.bottom) <= 2)
            vis = e;
    }
    float sc = Scale(h);
    int r = CornerRadius(h), r0 = (int)std::lround((leopard::On() ? 4 : 8) * sc);  // o nosso raio / o do DWM
    if (in->moving || in->transit || !IsWindowVisible(h) || IsIconic(h) || IsZoomed(h) || Fullscreen(h) || r <= r0 + 1 || TakenByShell(h)) {
        HideCorners(in);
        return;
    }
    // nada mudou de lugar e a foto é recente: as capas ficam como estão
    if (in->capas && EqualRect(&vis, &in->capasVis) && GetTickCount() - in->capasAt < 10000) return;
    int S = r + 2;
    const int M = 1;  // margem para o contorno de fora
    int W = vis.right - vis.left + 2 * M, H = vis.bottom - vis.top + 2 * M;
    if (W <= 2 * S || H <= 2 * S || W > 16384 || H > 16384) { HideCorners(in); return; }
    HWND& fw = in->corner[0];
    if (fw && !IsWindow(fw)) fw = nullptr;
    if (!fw) {
        WNDCLASSW wc{};
        if (!GetClassInfoW(g_mod, L"MacJanelaCanto", &wc)) {
            wc.lpfnWndProc = CornerProc; wc.hInstance = g_mod; wc.lpszClassName = L"MacJanelaCanto";
            RegisterClassW(&wc);
        }
        fw = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MacJanelaCanto", L"",
                             WS_POPUP, 0, 0, 1, 1, h, nullptr, g_mod, nullptr);
        if (!fw) return;
        SetWindowLongPtrW(fw, GWLP_USERDATA, (LONG_PTR)h);
        SetPropW(h, L"MacJanelaMoldura", fw);
        SetTimer(fw, 1, 500, nullptr);
    }
    // Fundo de cada canto: a foto da janela de trás, com 1 px a mais em volta (fora da janela). Em volta da janela a
    // tela tem as sombras (a nossa e a do próprio DWM, que o Chromium tem): a foto de trás não, e o cantinho coberto
    // saía mais claro que a tela ao lado. Então a capa compara, nesse 1 px de fora (a linha de cima/baixo e a coluna do
    // lado), a foto com a tela de verdade e escurece cada ponto na mesma proporção (média da linha e da coluna).
    std::vector<UINT32> bg[4], tela[4];
    RECT box[4];
    bool have[4]{};
    std::map<HWND, int> fotos;  // uma foto por janela de trás (a mesma janela costuma estar atrás de vários cantos)
    HDC sdc = GetDC(nullptr);
    for (int i = 0; i < 4; i++) {
        bool right = i & 1, bottom = (i & 2) != 0;
        int x0 = right ? vis.right - S : vis.left, y0 = bottom ? vis.bottom - S : vis.top;
        box[i] = { x0 - 1, y0 - 1, x0 + S + 1, y0 + S + 1 };
        POINT p{ right ? vis.right - 2 : vis.left + 1, bottom ? vis.bottom - 2 : vis.top + 1 };  // o pixel do canto
        HWND w = JanelaAtras(h, p);
        if (!w || (fotos.count(w) && fotos[w] > 3)) continue;
        fotos[w]++;
        have[i] = FotoDe(w, box[i], bg[i]);
        if (!have[i]) continue;
        int bw = box[i].right - box[i].left, bh = box[i].bottom - box[i].top;
        tela[i].assign((size_t)bw * bh, 0);
        HDC m2 = CreateCompatibleDC(sdc);
        BITMAPINFO b2{}; b2.bmiHeader = { sizeof(BITMAPINFOHEADER), bw, -bh, 1, 32, BI_RGB };
        void* tb = nullptr;
        HBITMAP tbm = CreateDIBSection(m2, &b2, DIB_RGB_COLORS, &tb, nullptr, 0);
        if (tbm) {
            HGDIOBJ o2 = SelectObject(m2, tbm);
            if (BitBlt(m2, 0, 0, bw, bh, sdc, box[i].left, box[i].top, SRCCOPY)) memcpy(tela[i].data(), tb, tela[i].size() * 4);
            SelectObject(m2, o2); DeleteObject(tbm);
        }
        DeleteDC(m2);
    }
    ReleaseDC(nullptr, sdc);
    if (!have[0] && !have[1] && !have[2] && !have[3]) { HideCorners(in); return; }
    HDC mem = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);  // zerado (transparente)
    if (!bmp) { DeleteDC(mem); return; }
    auto* px = (UINT32*)bits;
    // o fio da borda: o mesmo que o DWM desenha nos lados (CantoLeve), 1 px por dentro; o arco continua a borda reta
    int fundo = (int)(INT_PTR)GetPropW(h, L"MacJanelaFundo");
    bool dk = fundo ? fundo == 2 : Dark();
    float bR = (dk ? 0x4A : 0xC2) / 255.f, bG = (dk ? 0x4A : 0xC2) / 255.f, bB = (dk ? 0x4D : 0xC5) / 255.f;
    float hw = (vis.right - vis.left) / 2.f, hh = (vis.bottom - vis.top) / 2.f, cxm = M + hw, cym = M + hh;
    auto cov = [](float d) { return std::clamp(0.5f - d, 0.f, 1.f); };
    auto sdf = [&](float x, float y, float rad) {
        float qx = std::fabs(x - cxm) - (hw - rad), qy = std::fabs(y - cym) - (hh - rad);
        return std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - rad;
    };
    for (int i = 0; i < 4; i++) {
        if (!have[i]) continue;
        bool right = i & 1, bottom = (i & 2) != 0;
        int bw = box[i].right - box[i].left, bh = box[i].bottom - box[i].top;
        // quanto a tela de verdade é mais escura que a foto, por canal: na linha de fora (cima/baixo) para cada x e na
        // coluna de fora (esquerda/direita) para cada y
        auto ratio = [&](int x, int y, int c) {
            UINT32 f = bg[i][(size_t)y * bw + x], t = tela[i][(size_t)y * bw + x] & 0xFFFFFF;
            float a = (float)((f >> (c * 8)) & 255), b = (float)((t >> (c * 8)) & 255);
            return a < 8 ? 1.f : std::clamp(b / a, 0.f, 1.2f);
        };
        int ry = bottom ? bh - 1 : 0, rx = right ? bw - 1 : 0;
        std::vector<float> kr((size_t)bw * 3), kc((size_t)bh * 3);
        for (int x = 0; x < bw; x++) for (int c = 0; c < 3; c++) kr[(size_t)x * 3 + c] = ratio(x, ry, c);
        for (int y = 0; y < bh; y++) for (int c = 0; c < 3; c++) kc[(size_t)y * 3 + c] = ratio(rx, y, c);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                int sx = box[i].left + x, sy = box[i].top + y;          // tela
                int lx = sx - vis.left + M, ly = sy - vis.top + M;       // na capa
                float fx = lx + 0.5f, fy = ly + 0.5f;
                float d = sdf(fx, fy, (float)r), d8 = sdf(fx, fy, (float)r0);
                float A = std::clamp(cov(d8) - cov(d), 0.f, 1.f);       // o pedaço do app que o canto do Mac esconde
                float R = 0, G = 0, B = 0, Aa = 0;  // pré-multiplicado
                auto over = [&](float cr, float cg, float cb, float a) { R = cr * a + R * (1 - a); G = cg * a + G * (1 - a); B = cb * a + B * (1 - a); Aa = a + Aa * (1 - a); };
                if (A > 0) {
                    UINT32 v = bg[i][(size_t)y * bw + x];
                    float kb = (kr[(size_t)x * 3 + 0] + kc[(size_t)y * 3 + 0]) / 2, kg = (kr[(size_t)x * 3 + 1] + kc[(size_t)y * 3 + 1]) / 2,
                          kR = (kr[(size_t)x * 3 + 2] + kc[(size_t)y * 3 + 2]) / 2;
                    over(std::min(1.f, ((v >> 16) & 255) / 255.f * kR), std::min(1.f, ((v >> 8) & 255) / 255.f * kg), std::min(1.f, (v & 255) / 255.f * kb), A);
                }
                over(bR, bG, bB, std::clamp(cov(d) - cov(d + 1), 0.f, 1.f));  // o fio da borda no arco grande
                if (Aa <= 0) continue;
                if (lx < 0 || ly < 0 || lx >= W || ly >= H) continue;
                px[(size_t)ly * W + lx] = (UINT32)std::lround(Aa * 255) << 24 | (UINT32)std::lround(R * 255) << 16 | (UINT32)std::lround(G * 255) << 8 | (UINT32)std::lround(B * 255);
            }
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    POINT pos{ vis.left - M, vis.top - M }, zero{ 0, 0 };
    SIZE size{ W, H };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(fw, nullptr, &pos, &size, mem, &zero, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    memset(in->cornerKey, 0, sizeof(in->cornerKey));  // a moldura das janelas recortadas refaz do zero se voltar a ser usada
    bool was = in->capas;
    in->capas = true; in->capasVis = vis; in->capasAt = GetTickCount() | 1;
    if (!IsWindowVisible(fw)) { ShowWindow(fw, SW_SHOWNOACTIVATE); KeepAbove(in); }
    if (!was) Paint(in);  // a faixa dos botões passa a ter o arco do Finder
}

void UpdateShadow(Info* in) {
    if (in->transit) return;  // ampliando: nada em volta da janela escondida
    if (GetPropW(in->owner, L"MacJanelaPintando")) return;  // aparecendo: a sombra espera a janela pintar
    if (TakenByShell(in->owner)) { if (in->shadow.wnd) ShowWindow(in->shadow.wnd, SW_HIDE); HideCorners(in); return; }
    in->shadow.tahoe = TemaTahoe(); in->shadow.dark = Dark();  // Tahoe: sombra medida no macOS 26
    if (in->custom) { RECT f = FrameRect(in->owner); in->shadow.Update(in->active, &f); CapasCantos(in, f); return; }  // rente à parte visível
    if (!in->rgn.cx) { in->shadow.Update(in->active); HideCorners(in); return; }
    int l, t, rr, b;
    Insets(in, l, t, rr, b);
    RECT wr; GetWindowRect(in->owner, &wr);
    RECT vis{ wr.left + l, wr.top + t, wr.right - rr, wr.bottom - b };
    // sombra e cantos seguem o recorte exato (as bordas medidas podem não bater em diálogos)
    RECT box;
    if (GetWindowRgnBox(in->owner, &box) != ERROR) vis = { wr.left + box.left, wr.top + box.top, wr.left + box.right, wr.top + box.bottom };
    in->shadow.Update(in->active, &vis);
    UpdateCorners(in, vis);
}

void ApplyRegion(Info* in) {
    HWND h = in->owner;
    // Barra própria (Chromium/Electron): nunca recorta. A região não corta o conteúdo da GPU e desligava o quadro do
    // DWM: sobrava a borda invisível escura em volta do Discord, o canto ficava quadrado e o DWM passava a informar
    // a janela inteira como visível (a faixa dos botões ia junto para fora). Os cantos são os do DWM (modo leve).
    if (in->custom) {
        if (in->rgn.cx) {
            in->rgn = {};
            SetWindowRgn(h, nullptr, TRUE);
            DWMNCRENDERINGPOLICY on = DWMNCRP_USEWINDOWSTYLE;
            DwmSetWindowAttribute(h, DWMWA_NCRENDERING_POLICY, &on, sizeof(on));
        }
        return;
    }
    if (IsZoomed(h) || IsIconic(h) || Fullscreen(h)) {  // maximizada de verdade ou tela cheia: sem cantos
        if (in->rgn.cx) {
            in->rgn = {};
            SetWindowRgn(h, nullptr, TRUE);
            DWMNCRENDERINGPOLICY on = DWMNCRP_USEWINDOWSTYLE;
            DwmSetWindowAttribute(h, DWMWA_NCRENDERING_POLICY, &on, sizeof(on));
            DWORD round = 2; DwmSetWindowAttribute(h, 33, &round, sizeof(round));
            ApplyColors(in);  // borda fina de volta
        }
        return;
    }
    RECT wr; GetWindowRect(h, &wr);
    SIZE sz{ wr.right - wr.left, wr.bottom - wr.top };
    if (sz.cx == in->rgn.cx && sz.cy == in->rgn.cy) return;
    int l, t, rr, b;
    Insets(in, l, t, rr, b);
    // por dentro do arco: a borda suavizada (UpdateCorners) cobre o que sobra
    int R = CornerRadius(h);
    in->rgn = sz;
    // a borda fina e o arredondamento do Windows seriam desenhados no tamanho antigo, em volta do recorte
    COLORREF none = 0xFFFFFFFE;  // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &none, sizeof(none));
    DWORD square = 1;  // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &square, sizeof(square));
    // sem o quadro do DWM: senão ele ainda desenhava a sombra/fundo do retângulo inteiro da janela (com as
    // bordas invisíveis), um retângulo escuro de bordas retas em volta de diálogos
    DWMNCRENDERINGPOLICY off = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(h, DWMWA_NCRENDERING_POLICY, &off, sizeof(off));
    SetWindowRgn(h, RoundRgn(l, t, sz.cx - rr, sz.cy - b, R), TRUE);
    PaintNC(in);
}

void Insets(Info* in, int& l, int& t, int& rr, int& b);

RECT ZoomRect(Info* in) {
    HWND h = in->owner;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    // bordas invisíveis de redimensionar: ficam para fora da área, como numa janela maximizada
    int l, t, rr, b;
    Insets(in, l, t, rr, b);  // as mesmas bordas do recorte (com recorte o DWM informa 0)
    RECT wa = mi.rcWork;
    return { wa.left - l, wa.top - t, wa.right + rr, wa.bottom + b };
}

// Encaixe (sem o botão de maximizar o Windows não encaixa sozinho): com o mouse na borda esquerda/direita
// = metade da tela; num canto = um quarto; no topo = ampliar. 0: nada, 1: encaixar em t, 2: ampliar
int SnapTarget(Info* in, RECT* t) {
    HWND h = in->owner;
    if (!(in->style & WS_THICKFRAME)) return 0;
    POINT c; GetCursorPos(&c);
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi)) return 0;
    RECT m = mi.rcMonitor, wa = mi.rcWork;
    int e = (int)(4 * Scale(h)), q = (int)(60 * Scale(h));
    // topo: a janela para na barra de menus, então o gesto vale já com o mouse dentro da barra (antes só na
    // beirada de cima da tela)
    bool L = c.x <= m.left + e, R = c.x >= m.right - 1 - e, T = c.y <= std::max(m.top + e, wa.top);
    int mx = (wa.left + wa.right) / 2, my = (wa.top + wa.bottom) / 2;
    if (L || R) {
        *t = L ? RECT{ wa.left, wa.top, mx, wa.bottom } : RECT{ mx, wa.top, wa.right, wa.bottom };
        if (c.y <= wa.top + q) t->bottom = my; else if (c.y >= wa.bottom - q) t->top = my;
        return 1;
    }
    if (T && (in->style & WS_MAXIMIZEBOX)) { *t = wa; return 2; }
    return 0;
}

void HidePreview(Info* in) {
    if (in->preview) ShowWindow(in->preview, SW_HIDE);
    in->previewFor = {};
}

// Prévia do Mac: retângulo arredondado translúcido, com borda clara, atrás da janela arrastada
void ShowPreview(Info* in, RECT t) {
    float sc = Scale(in->owner);
    int g = (int)(6 * sc);
    InflateRect(&t, -g, -g);
    if (EqualRect(&t, &in->previewFor) && in->preview && IsWindowVisible(in->preview)) return;
    in->previewFor = t;
    if (!in->preview) {
        WNDCLASSW wc{};
        if (!GetClassInfoW(g_mod, L"MacJanelaCanto", &wc)) {
            wc.lpfnWndProc = CornerProc; wc.hInstance = g_mod; wc.lpszClassName = L"MacJanelaCanto";
            RegisterClassW(&wc);
        }
        in->preview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MacJanelaCanto", L"",
                                      WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_mod, nullptr);
        if (!in->preview) return;
    }
    int W = t.right - t.left, H = t.bottom - t.top;
    if (W <= 0 || H <= 0) return;
    HDC mem = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(mem); return; }
    auto* px = (UINT32*)bits;
    float r = 12 * sc, hw = W / 2.f, hh = H / 2.f;
    auto cov = [](float d) { return std::clamp(0.5f - d, 0.f, 1.f); };
    const float fillA = 0.22f, lineA = 0.55f;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float qx = std::fabs(x + 0.5f - hw) - (hw - r), qy = std::fabs(y + 0.5f - hh) - (hh - r);
            float d = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
            float inside = cov(d), line = inside - cov(d + 1);
            float a = (inside - line) * fillA + line * lineA;  // branco pré-multiplicado
            BYTE v = (BYTE)std::lround(a * 255);
            px[y * W + x] = (UINT32)v << 24 | (UINT32)v << 16 | (UINT32)v << 8 | v;
        }
    HGDIOBJ old = SelectObject(mem, bmp);
    POINT pos{ t.left, t.top }, zero{ 0, 0 };
    SIZE size{ W, H };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(in->preview, nullptr, &pos, &size, mem, &zero, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    SetWindowPos(in->preview, in->owner, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);  // logo atrás da janela
}

// Janela cobrindo o monitor inteiro com barra de título (o Ableton guarda esse tamanho como "normal",
// de quando maximizava por cima da barra de menus e do Dock, e volta a ele ao abrir e ao sair do
// ampliado): vai para o tamanho normal, 3/4 da tela no meio. Ampliar fica com o botão verde.
void FixOversize(Info* in) {
    HWND h = in->owner;
    if (in->custom || in->zoomed || in->moving || IsZoomed(h) || IsIconic(h)) return;
    if (!(in->style & WS_THICKFRAME) || !(in->style & WS_MAXIMIZEBOX)) return;
    RECT wr; GetWindowRect(h, &wr);
    MONITORINFO mi{ sizeof(mi) };
    int l, t, r, b;
    Insets(in, l, t, r, b);
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return;
    // Quase toda a área de trabalho (95%) e, além disso, maior que ela ou recém-aberta (10 s). O Ableton
    // abre em tamanhos como 12 px menor dos lados mas passando do fim da tela: "cobrir exatamente" o
    // monitor ou a área de trabalho deixava esses casos passarem. Quem só arrasta a borda até quase
    // tudo, depois de aberta, não é encolhido.
    RECT wa = mi.rcWork;
    int vw = (wr.right - r) - (wr.left + l), vh = (wr.bottom - b) - (wr.top + t);
    int ww = wa.right - wa.left, wh = wa.bottom - wa.top;
    bool nearly = vw >= ww * 95 / 100 && vh >= wh * 95 / 100;
    bool bigger = vw > ww || vh > wh;
    if (!nearly || !(bigger || GetTickCount() - in->attachedAt < 10000)) return;
    int w = (wa.right - wa.left) * 3 / 4, hh = (wa.bottom - wa.top) * 3 / 4;
    int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - hh) / 2;
    in->before = { x - l, y - t, x + w + r, y + hh + b };
    SetWindowPos(h, nullptr, in->before.left, in->before.top, in->before.right - in->before.left, in->before.bottom - in->before.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// "Ampliar" animado (como o zoom do Mac), com foto: o Shell mostra a foto de agora parada por cima; a janela
// de verdade muda de tamanho escondida (cloak, só vale para janelas do próprio processo, por isso aqui na DLL)
// e se redesenha; ~100 ms depois o Shell tira a foto já no layout novo e anima só ela, do tamanho antigo ao
// novo; no fim ele avisa ("MacJanelaMostrar") e a janela reaparece idêntica à foto. Tentativas anteriores:
// pular de tamanho na hora (o app redesenhava com faixas pretas), redimensionar ao vivo quadro a quadro
// (o Ableton não acompanha: engasgava) e animar a foto antiga (o layout do app muda: "pulo" ou "fantasma").
LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref);
void PlaceOverlay(Info* in);
const UINT_PTR ZOOM_TIMER = 0x4D4A;

void ApplyColors(Info* in);
void ZoomShow(HWND h) {  // a janela aparece de novo (fim da animação, ou segurança)
    KillTimer(h, ZOOM_TIMER);
    DWORD_PTR ref = 0;
    Info* in = GetWindowSubclass(h, OwnerProc, SUBCLASS_ID, &ref) ? (Info*)ref : nullptr;
    // a barra (pintada por nós) pode não ter sido pintada enquanto a janela estava escondida: ela voltava
    // preta. Pinta antes de mostrar e de novo logo depois.
    if (in) { ApplyColors(in); PaintNC(in); }
    RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    BOOL off = FALSE; DwmSetWindowAttribute(h, 13 /*DWMWA_CLOAK*/, &off, sizeof(off));
    if (in) PaintNC(in);
    RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    if (in) in->colorFreezeUntil = GetTickCount() + 600;
    {   // avisa o Shell: a janela já está na tela, a foto pode sumir
        static UINT pronto = RegisterWindowMessageW(L"ShellAmpliarPronto");
        if (HWND s = FindWindowW(L"ShellMensagens", nullptr)) PostMessageW(s, pronto, (WPARAM)h, 0);
    }
    if (in && in->transit) {
        in->transit = false;
        PlaceOverlay(in);
        UpdateShadow(in);
    }
}
void CALLBACK ZoomSafety(HWND h, UINT, UINT_PTR, DWORD) { ZoomShow(h); }  // o Shell não avisou: não fica escondida
void CALLBACK ZoomPhoto(HWND w, UINT, UINT_PTR id, DWORD) {
    KillTimer(w, id);
    static UINT msg = RegisterWindowMessageW(L"ShellAmpliarFoto");
    if (HWND s = FindWindowW(L"ShellMensagens", nullptr)) PostMessageW(s, msg, (WPARAM)w, 0);
}

// Janela do Qt (OBS Studio, DaVinci, apps Qt5/Qt6: classe "Qt<versão>QWindow..."). A prévia do OBS e outras áreas
// desenhadas pela placa de vídeo não se redesenham escondidas: a foto da janela escondida no tamanho novo saía com o
// conteúdo velho esticado e, ao reaparecer, a janela "pulava" (vídeo do Reddit, 20/11/2026). Para elas o Shell usa o
// caminho dos apps que não desenham escondidos (flags 4): anima só a foto de antes, mostra a janela de verdade no
// fim e espera ela estar desenhada para tirar a foto (ampliar.cpp CheckReady).
bool QtWindow(HWND h) {
    wchar_t c[64] = {};
    return GetClassNameW(h, c, 64) && !wcsncmp(c, L"Qt", 2) && wcsstr(c, L"QWindow");
}

void MoveAnimated(Info* in, RECT z) {
    HWND h = in->owner;
    int l, t, rr, b;
    Insets(in, l, t, rr, b);
    RECT wr; GetWindowRect(h, &wr);
    RECT from{ wr.left + l, wr.top + t, wr.right - rr, wr.bottom - b };
    RECT box;
    if (GetWindowRgnBox(h, &box) != ERROR && box.right > box.left) from = { wr.left + box.left, wr.top + box.top, wr.left + box.right, wr.top + box.bottom };
    RECT to{ z.left + (from.left - wr.left), z.top + (from.top - wr.top), z.right - (wr.right - from.right), z.bottom - (wr.bottom - from.bottom) };
    // a cor da barra vai junto: a foto às vezes sai com a barra sem pintar (preta)
    struct Msg { unsigned long long hwnd; RECT from, to; int flags; COLORREF cap; } msg{ (unsigned long long)(UINT_PTR)h, from, to,
        (in->custom ? 0 : in->tool ? 3 : 1) | (QtWindow(h) ? 4 : 0), in->cap == CLR_INVALID ? (in->dark ? RGB(0x2D, 0x2D, 0x2F) : RGB(0xE9, 0xE9, 0xEA)) : in->cap };
    HWND sh = FindWindowW(L"ShellMensagens", nullptr);
    DWORD_PTR ok = 0;
    COPYDATASTRUCT cd{ 0x414D504C /*'AMPL'*/, sizeof(msg), &msg };
    bool anim = sh && IsWindowVisible(h) && !IsIconic(h) && !in->transit &&
                SendMessageTimeoutW(sh, WM_COPYDATA, (WPARAM)h, (LPARAM)&cd, SMTO_ABORTIFHUNG, 400, &ok) && ok;
    if (anim) {  // a foto já está na tela, por cima: esconde a janela e o que vai em volta dela
        in->transit = true;
        BOOL on = TRUE; DwmSetWindowAttribute(h, 13 /*DWMWA_CLOAK*/, &on, sizeof(on));
        if (in->overlay) ShowWindow(in->overlay, SW_HIDE);
        if (in->shadow.wnd) ShowWindow(in->shadow.wnd, SW_HIDE);
        HideCorners(in);
        SetTimer(h, ZOOM_TIMER, 1500, ZoomSafety);
    }
    SetWindowPos(h, nullptr, z.left, z.top, z.right - z.left, z.bottom - z.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (anim) {  // o app redesenha no tamanho novo (escondido): o Shell tira a foto já com o layout novo
        UpdateWindow(h);
        SetTimer(h, ZOOM_TIMER + 1, 50, ZoomPhoto);  // já redesenhou (UpdateWindow): só dá um respiro ao app
    }
}

void ToggleZoom(Info* in) {
    HWND h = in->owner;
    if (in->zoomed) {
        in->zoomed = false;
        RemovePropW(h, L"MacJanelaAmpliada");
        MoveAnimated(in, in->before);
        return;
    }
    if (IsZoomed(h)) {  // maximizada pelo Windows/pelo app: vira "ampliada" (o tamanho normal guardado é o de antes)
        WINDOWPLACEMENT wp{ sizeof(wp) };
        GetWindowPlacement(h, &wp);
        in->before = wp.rcNormalPosition;
        MONITORINFO mi{ sizeof(mi) };
        if (!(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) && GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi))
            OffsetRect(&in->before, mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top);
        ShowWindow(h, SW_RESTORE);
    } else GetWindowRect(h, &in->before);
    RECT z = ZoomRect(in);
    in->zoomed = true;
    SetPropW(h, L"MacJanelaAmpliada", (HANDLE)1);  // o Shell sabe que Win+↓ deve desfazer
    MoveAnimated(in, z);
}

void SampleCaption(Info* in);
void SampleBar(Info* in);
void MedirBarra(Info* in);

LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Info* in = (Info*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCHITTEST: {
        if (!in) break;
        POINT p{ (short)LOWORD(l), (short)HIWORD(l) };
        RECT r = BarRect(in);
        p.x -= r.left; p.y -= r.top;
        float sc = Scale(in->owner);
        if (in->custom) {  // só as duas faixas pintadas; o resto deixa o clique passar para a barra do app (abas, arrastar)
            if (!Right(in) && p.x / sc < PatchL(in)) return HTCLIENT;
            if (p.x / sc > (r.right - r.left) / sc - PatchR(in)) return HTCLIENT;
            return HTTRANSPARENT;
        }
        float zone = (UnifiedBar(in->owner) > 40 ? 84 : 72) + WinAppInset(in);
        if (Right(in) ? p.x / sc > (r.right - r.left) / sc - zone : p.x / sc < zone) return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_TIMER:
        if (w == 7) {  // ponteiro parado no botão verde: o menu de encaixe do Shell (como no macOS Sequoia)
            KillTimer(h, 7);
            POINT c; GetCursorPos(&c);
            POINT p = c; ScreenToClient(h, &p);
            if (in && HitLight(in, p) == 2 && (in->style & WS_MAXIMIZEBOX) && GetAsyncKeyState(VK_LBUTTON) >= 0) {
                static UINT msg = RegisterWindowMessageW(L"ShellMenuVerde");
                if (HWND sh = FindWindowW(L"ShellMensagens", nullptr)) PostMessageW(sh, msg, (WPARAM)in->owner, MAKELPARAM(c.x, c.y + 14));
            }
            return 0;
        }
        if (in) {
            if (in->custom) {
                SampleBar(in);
                // onde ficam os botões: logo ao pegar a janela, depois de mudar de tamanho/ativar, e a cada 8 s ativa
                // (um grupo de abas novo no Chrome). A foto da faixa de cima custa alguns ms; não mais que isso.
                if (!in->layoutAt || (in->active && GetTickCount() - in->layoutAt > 8000)) MedirBarra(in);
            } else SampleCaption(in);
            UpdateShadow(in);
            // logo depois de aparecer ou ativar: relê a cada quadro até a cor firmar; depois, devagar
            bool fast = !in->custom && in->needColor && GetTickCount() < in->fastUntil;
            SetTimer(h, 1, fast ? 16 : in->active ? 1500 : 4000, nullptr);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (in && !in->hoverLights) {
            in->hoverLights = true;
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            Paint(in);
        }
        if (in) {  // em cima do verde: conta o tempo parado; saiu dele: cancela
            POINT p{ (short)LOWORD(l), (short)HIWORD(l) };
            if (HitLight(in, p) == 2) { if (!GetPropW(h, L"MacVerdeEspera")) { SetPropW(h, L"MacVerdeEspera", (HANDLE)1); SetTimer(h, 7, 650, nullptr); } }
            else if (RemovePropW(h, L"MacVerdeEspera")) KillTimer(h, 7);
        }
        return 0;
    case WM_MOUSELEAVE: if (in) { in->hoverLights = false; in->press = -1; Paint(in); } RemovePropW(h, L"MacVerdeEspera"); KillTimer(h, 7); return 0;
    case WM_LBUTTONDOWN: RemovePropW(h, L"MacVerdeEspera"); KillTimer(h, 7); if (in) { POINT p{ (short)LOWORD(l), (short)HIWORD(l) }; in->press = HitLight(in, p); SetCapture(h); } return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (!in) return 0;
        POINT p{ (short)LOWORD(l), (short)HIWORD(l) };
        int k = HitLight(in, p);
        if (k >= 0 && k == in->press) {
            if (k == 0) PostMessageW(in->owner, WM_SYSCOMMAND, SC_CLOSE, 0);
            else if (k == 1 && (in->style & WS_MINIMIZEBOX)) ShowWindow(in->owner, SW_MINIMIZE);
            else if (k == 2 && (in->style & WS_MAXIMIZEBOX) && in->custom) ShowWindow(in->owner, IsZoomed(in->owner) ? SW_RESTORE : SW_MAXIMIZE);  // (o Chromium ignora o SC_MAXIMIZE postado)
            else if (k == 2 && (in->style & WS_MAXIMIZEBOX)) ToggleZoom(in);
        }
        in->press = -1;
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// A faixa e os cantos ficam logo acima da janela. O Cubase traz as janelas de plug-in para a frente com
// SWP_NOOWNERZORDER e elas passavam por cima das próprias janelas de dono: a faixa com os botões sumia atrás
// do Kontakt (ao clicar e, principalmente, ao reaparecer depois de o Cubase escondê-las)
void KeepAbove(Info* in) {
    HWND h = in->owner;
    if (!IsWindowVisible(h)) return;
    for (HWND o : { in->overlay, in->corner[0] }) {
        if (!o || !IsWindowVisible(o)) continue;
        // logo acima da janela: só as camadas dela (faixa, cantos, prévia) podem estar no meio. Antes bastava estar
        // "em algum lugar acima"; ao reiniciar o Shell a DLL recria as camadas no topo da tela e as de janelas que
        // estavam atrás de outras (o Relógio atrás do Chrome) ficavam por cima de tudo: o contorno e os três botões
        // "fantasma" por cima do Chrome (19/11/2026)
        bool above = false;
        for (HWND x = GetWindow(h, GW_HWNDPREV); x; x = GetWindow(x, GW_HWNDPREV)) {
            if (x == o) { above = true; break; }
            if (GetWindow(x, GW_OWNER) != h && IsWindowVisible(x)) break;  // outra janela no meio: não está colada
        }
        if (above) continue;
        HWND prev = GetWindow(h, GW_HWNDPREV);
        SetWindowPos(o, prev ? prev : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void PlaceOverlay(Info* in) {
    if (!in->overlay || in->transit) return;
    if (GetPropW(in->owner, L"MacJanelaPintando")) return;  // aparecendo: a barra espera a janela pintar
    bool show = IsWindowVisible(in->owner) && !IsIconic(in->owner) && !Fullscreen(in->owner) && !controles::IsAlert(in->owner) && !TakenByShell(in->owner);
    if (!show) { ShowWindow(in->overlay, SW_HIDE); return; }
    Paint(in);  // UpdateLayeredWindow já posiciona e redimensiona
    if (!IsWindowVisible(in->overlay)) { ShowWindow(in->overlay, SW_SHOWNOACTIVATE); KeepAbove(in); }
}

// Barra com a cor do próprio app (como no Mac, onde a barra de título faz parte da janela do app);
// antes da primeira leitura, a cor da barra do Mac no tema do Windows.
void ApplyColors(Info* in) {
    if (in->custom) return;  // barra própria: a cor, a borda e o escuro são do app (o contorno é do modo leve, CantoLeve)
    COLORREF cap = in->cap;
    if (cap == CLR_INVALID) { in->dark = Dark(); cap = in->dark ? RGB(0x2D, 0x2D, 0x2F) : RGB(0xE9, 0xE9, 0xEA); }
    else in->dark = (GetRValue(cap) * 299 + GetGValue(cap) * 587 + GetBValue(cap) * 114) / 1000 < 128;
    DwmSetWindowAttribute(in->owner, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    COLORREF border = in->rgn.cx ? 0xFFFFFFFE : RGB(GetRValue(cap) * 8 / 10, GetGValue(cap) * 8 / 10, GetBValue(cap) * 8 / 10);
    DwmSetWindowAttribute(in->owner, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
    PaintNC(in);
    BOOL dk = in->dark;
    DwmSetWindowAttribute(in->owner, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dk, sizeof(dk));
}

// Lê a cor do topo da área do app, logo abaixo da barra (uma faixa fina, várias amostras; vale a mais
// comum, para não pegar texto ou ícones). Só com a janela na frente: senão a faixa pode estar coberta.
// a cor mais comum numa faixa de 4 px (src: a tela ou o DC da própria janela); CLR_INVALID se não deu
COLORREF StripColor(HDC src, int x, int y, int w) {
    const int h = 4;
    HDC mem = CreateCompatibleDC(src);
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(mem); return CLR_INVALID; }
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = BitBlt(mem, 0, 0, w, h, src, x, y, SRCCOPY);
    auto* px = (UINT32*)bits;
    std::vector<std::pair<UINT32, int>> count;
    for (int y = 0; y < h; y++)
        for (int x = w / 10; x < w - w / 10; x += std::max(1, w / 40)) {
            UINT32 v = px[y * w + x] & 0xFFFFFF;
            auto it = std::find_if(count.begin(), count.end(), [&](auto& p) { return p.first == v; });
            if (it == count.end()) count.push_back({ v, 1 }); else it->second++;
        }
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    if (!ok || count.empty()) return CLR_INVALID;
    auto best = std::max_element(count.begin(), count.end(), [](auto& a, auto& b) { return a.second < b.second; });
    UINT32 v = best->first;  // BGR do DIB
    COLORREF col = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    return col == 0 ? CLR_INVALID : col;  // preto puro: o app ainda não desenhou
}

// usa a cor lida: aplica na hora; guarda só quando duas leituras seguidas concordam (a primeira, com o
// app ainda desenhando, às vezes vem errada e a janela abria nela da próxima vez)
void UseColor(Info* in, COLORREF col) {
    if (col == CLR_INVALID || in->tool) return;  // plug-in: barra escura fixa
    // ampliando, ou logo depois: o app ainda está redesenhando no tamanho novo e o topo dele vem preto; a barra
    // ficava escura por um instante e depois voltava ao cinza
    if (in->transit || (int)(GetTickCount() - in->colorFreezeUntil) < 0) return;
    bool stable = col == in->lastRead;
    in->lastRead = col;
    if (stable) { in->needColor = false; if (SavedColor(in->owner) != col) SaveColor(in->owner, col); }
    if (col == in->cap) return;
    in->cap = col;
    ApplyColors(in);
    Paint(in);
}

// Direto da imagem da própria janela (a barra vive dentro do app): logo depois de o app desenhar, sem
// esperar a tela compor. Vale para quem desenha com GDI; quem desenha pela placa de vídeo devolve preto e
// fica com a leitura da tela.
bool SampleOwnDC(Info* in) {
    if (IsIconic(in->owner)) return false;
    RECT cr; GetClientRect(in->owner, &cr);
    if (cr.right < 60 || cr.bottom < 30) return false;
    HDC dc = GetDC(in->owner);
    COLORREF col = StripColor(dc, 0, 1, cr.right);
    ReleaseDC(in->owner, dc);
    if (col == CLR_INVALID) return false;
    UseColor(in, col);
    return true;
}

void SampleCaption(Info* in) {
    if (GetForegroundWindow() != in->owner || IsIconic(in->owner)) return;
    POINT c{ 0, 0 }; ClientToScreen(in->owner, &c);
    RECT cr; GetClientRect(in->owner, &cr);
    if (cr.right < 60 || cr.bottom < 30) return;
    HDC screen = GetDC(nullptr);
    COLORREF col = StripColor(screen, c.x, c.y + 1, cr.right);
    ReleaseDC(nullptr, screen);
    UseColor(in, col);
}

// Modo barra própria: lê a cor da barra do app ao lado dos botões dele e na esquerda, e a altura da
// barra (até onde a cor ao lado dos botões continua igual). Só com a janela na frente.
void SampleBar(Info* in) {
    if (IsIconic(in->owner) || !IsWindowVisible(in->owner) || in->transit) return;
    RECT f = FrameRect(in->owner);
    float sc = Scale(in->owner);
    // Inativa também: o Chrome clareia a barra ao perder o foco e a faixa ficava na cor de ativa. Só lê se os pontos
    // lidos estão à mostra (nenhuma outra janela por cima deles).
    if (GetForegroundWindow() != in->owner) {
        POINT pts[2] = { { f.right - (int)((PatchR(in) + 12) * sc), f.top + (int)(5 * sc) }, { f.left + (int)((PATCH_L + 12) * sc), f.top + (int)(5 * sc) } };
        for (POINT p : pts) {
            HWND at = WindowFromPoint(p), root = at ? GetAncestor(at, GA_ROOT) : nullptr;
            if (root != in->owner && root != in->overlay) return;
        }
    }
    int w = f.right - f.left, h = std::min<int>((int)(64 * sc), f.bottom - f.top);
    if (w < 300 || h < 20) return;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, f.left, f.top, SRCCOPY);
    auto* px = (UINT32*)bits;
    auto at = [&](int x, int y) { UINT32 v = px[std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)] & 0xFFFFFF; return v; };
    auto col = [](UINT32 v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); };
    auto near_ = [](UINT32 a, UINT32 b) {
        int d = std::abs((int)(a & 255) - (int)(b & 255)) + std::abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)) + std::abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255));
        return d <= 12;
    };
    int xr = w - (int)((PatchR(in) + 4) * sc), y0 = (int)(5 * sc);
    // a cor mais comum num trecho de 30 pt logo antes dos botões (um ponto só às vezes caía num ícone ou numa aba)
    UINT32 right = at(xr, y0);
    {
        std::vector<std::pair<UINT32, int>> cnt;
        for (int x = std::max(0, xr - (int)(30 * sc)); x <= xr; x++) {
            UINT32 v = at(x, y0);
            auto it = std::find_if(cnt.begin(), cnt.end(), [&](auto& p) { return p.first == v; });
            if (it == cnt.end()) cnt.push_back({ v, 1 }); else it->second++;
        }
        if (!cnt.empty()) right = std::max_element(cnt.begin(), cnt.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
        for (int x = xr; x >= std::max(0, xr - (int)(30 * sc)); x--) if (at(x, y0) == right) { xr = x; break; }  // a altura é medida numa coluna dessa cor
    }
    // altura da barra: desce ao lado dos botões enquanto a cor é a mesma
    int barH = h;
    for (int y = y0; y < h; y++) if (!near_(at(xr, y), right) && !near_(at(xr, y + 1), right)) { barH = y; break; }
    // esquerda: a cor mais comum numa linha do topo, logo depois da faixa dos botões do Mac
    std::vector<std::pair<UINT32, int>> count;
    for (int x = (int)(PATCH_L * sc) + 4; x < std::min(w, (int)(260 * sc)); x += 2) {
        UINT32 v = at(x, y0);
        auto it = std::find_if(count.begin(), count.end(), [&](auto& p) { return p.first == v; });
        if (it == count.end()) count.push_back({ v, 1 }); else it->second++;
    }
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr, screen);
    UINT32 left = count.empty() ? right : std::max_element(count.begin(), count.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
    barH = std::clamp(barH, (int)(30 * sc), (int)(48 * sc));  // mínimo 30 pt: a barra preta do Discord media 20 e a faixa não cobria o pé dos botões dele
    COLORREF L = col(left), R = col(right);
    if (L == in->leftColor && R == in->rightColor && barH == in->barH) return;
    if (barH != in->barH) in->layoutAt = 0;  // outra altura de barra: mede de novo onde ficam os botões
    in->leftColor = L; in->rightColor = R; in->barH = barH;
    in->dark = (GetRValue(L) * 299 + GetGValue(L) * 587 + GetBValue(L) * 114) / 1000 < 128;  // o fundo dos botões do Mac
    PlaceOverlay(in);
}

// Foto da faixa de cima da janela (PrintWindow com o conteúdo da GPU; a tela teria a nossa faixa por cima) para
// decidir: (1) o canto esquerdo, onde iriam os botões do Mac, está vazio (só a cor da barra)? (2) qual a largura dos
// três botões do app à direita (Chrome/Edge/Claude 46 pt cada, Discord ~36 pt)? A faixa da direita cobria 144 pt fixos
// e escondia o "?" do Discord e o "Compartilhar" do Claude; a da esquerda escondia os grupos de abas do Chrome.
static float g_ultimoPatch = 0;  // a última medida neste processo (a próxima janela do Chrome já nasce com ela)
void MedirBarra(Info* in) {
    HWND h = in->owner;
    if (!IsWindowVisible(h) || IsIconic(h) || Fullscreen(h) || in->moving || in->transit) return;
    RECT wr; GetWindowRect(h, &wr);
    RECT f = FrameRect(h);
    float sc = Scale(h);
    int bh = in->barH ? in->barH : (int)(32 * sc);
    int W = wr.right - wr.left, H = (f.top - wr.top) + bh;
    if (W < 200 || W > 16384 || H < 16 || H > 1024) return;
    HDC mdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(mdc); return; }
    HGDIOBJ old = SelectObject(mdc, bmp);
    BOOL ok = PrintWindow(h, mdc, 2 /*PW_RENDERFULLCONTENT*/);
    auto* px = (UINT32*)bits;
    auto at = [&](int x, int y) { return px[y * W + x] & 0xFFFFFF; };
    auto dist = [](UINT32 a, UINT32 b) {
        return std::abs((int)(a & 255) - (int)(b & 255)) + std::abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)) + std::abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255));
    };
    int x0 = std::max(0, (int)(f.left - wr.left)), x1 = std::min(W, (int)(f.right - wr.left)), y0 = f.top - wr.top;
    int ya = y0 + (int)(3 * sc), yb = std::min(H, y0 + bh - (int)(3 * sc));
    bool done = false;
    if (ok && yb > ya && x1 - x0 > 200) {
        // fundo da barra: a cor mais comum
        std::vector<std::pair<UINT32, int>> cnt;
        for (int y = ya; y < yb; y += 3)
            for (int x = x0; x < x1; x += 4) {
                UINT32 v = at(x, y);
                auto it = std::find_if(cnt.begin(), cnt.end(), [&](auto& p) { return p.first == v; });
                if (it == cnt.end()) { if (cnt.size() < 256) cnt.push_back({ v, 1 }); } else it->second++;
            }
        UINT32 bg = cnt.empty() ? 0 : std::max_element(cnt.begin(), cnt.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
        // tudo preto: o app ainda não desenhou (tenta de novo na próxima volta). Só o fundo preto não basta: o Discord
        // (tema escuro) tem a barra preto puro e nunca era medido
        bool drawn = bg != 0;
        for (int y = ya; y < yb && !drawn; y += 2) for (int x = x0; x < x1 && !drawn; x += 3) drawn = at(x, y) != 0;
        if (drawn) {
            done = true;
            // os três botões do app: os três grupos de colunas com desenho mais à direita (×, □ e —), com o mesmo
            // passo entre eles; a faixa vai até meio passo antes do centro do "—"
            std::vector<std::pair<int, int>> gr;  // [começo, fim] de cada grupo, da direita para a esquerda
            bool ink = false; int end = 0;
            int lim = std::max(x0, x1 - (int)(240 * sc));
            for (int x = x1 - 2; x >= lim; x--) {
                bool any = false;
                for (int y = ya; y < yb && !any; y++) any = dist(at(x, y), bg) > 40;
                if (any && !ink) { ink = true; end = x; }
                else if (!any && ink) { ink = false; gr.push_back({ x + 1, end }); if (gr.size() == 3) break; }
            }
            float pr = 0;
            if (gr.size() == 3) {
                float c0 = (gr[0].first + gr[0].second) / 2.f, c1 = (gr[1].first + gr[1].second) / 2.f, c2 = (gr[2].first + gr[2].second) / 2.f;
                float p1 = c0 - c1, p2 = c1 - c2;
                if (std::fabs(p1 - p2) <= 4 * sc && p1 >= 24 * sc && p1 <= 64 * sc) pr = ((x1 - c2) + p1 / 2) / sc + 2;
            }
            if (pr > 0 && std::fabs(pr - in->patchR) >= 1) { in->patchR = pr; g_ultimoPatch = pr; PlaceOverlay(in); }
        }
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc);
    if (done) in->layoutAt = GetTickCount() | 1;
}

// ---- Abas horizontais com espaço para os botões do Mac (Chrome), como o Chrome do Mac
// O Chromium monta a barra pelo BrowserFrameViewWin::GetBrowserLayoutParams: além da área do app, ele devolve uma
// "área reservada" no começo da linha (leading_exclusion, usada no Mac para os três botões) e outra no fim (os botões
// do Windows). No Windows a do começo vem zerada e as abas encostam na borda: os botões do Mac cobriam o grupo de
// abas. O gancho troca o ponteiro dessa função na tabela virtual da classe (sem reescrever instruções do app), chama
// a original e põe na área do começo a largura dos botões do Mac: o próprio Chrome empurra as abas para a direita.
// Só para as versões medidas (tabela abaixo, achada com os símbolos públicos do Google): o ponteiro na tabela tem de
// apontar exatamente para a função e os primeiros 16 bytes dela têm de bater. Qualquer diferença: não mexe.
struct GanchoChromium { const wchar_t* dll; DWORD vt; int slot; DWORD fn; BYTE sig[16]; };
static const GanchoChromium g_ganchos[] = {
    // Chrome 157.0.8103.79 (chrome.dll.pdb 57DD8BAFFD89996F4C4C44205044422E1): vftable 0x100845a0, slot 123
    { L"chrome.dll", 0x100845a0, 123, 0x371cb40, { 0x48, 0x89, 0xd0, 0x0f, 0x57, 0xc0, 0x0f, 0x11, 0x42, 0x20, 0x0f, 0x11, 0x42, 0x10, 0x0f, 0x10 } },
};
using LayoutParamsFn = void* (__fastcall*)(void* self, void* out);
static LayoutParamsFn g_lpOrig;
static void** g_lpSlot;
static volatile float g_leadW;  // largura (pt = DIP) dos botões do Mac no começo da linha
// BrowserLayoutParams: gfx::Rect visual_client_area (0x00), leading_exclusion { SizeF content; float hpad, vpad } (0x10),
// trailing_exclusion (0x20). Em floats: [4] largura e [5] altura do começo, [9] altura do fim.
static void* __fastcall LayoutParamsHook(void* self, void* out) {
    void* r = g_lpOrig(self, out);
    float need = g_leadW;
    if (out && need > 0) {
        float* p = (float*)out;
        if (p[4] < need) {
            p[4] = need;
            if (p[5] < p[9]) p[5] = p[9];
            if (p[5] <= 0) p[5] = 30;
        }
    }
    return r;
}
// liga (true) ou desfaz (false). true: o gancho está posto (agora ou antes)
bool GanchoAbas(bool on) {
    if (!on) {
        if (g_lpSlot && *g_lpSlot == (void*)&LayoutParamsHook) {
            DWORD old;
            if (VirtualProtect(g_lpSlot, sizeof(void*), PAGE_READWRITE, &old)) {
                InterlockedExchangePointer(g_lpSlot, (void*)g_lpOrig);
                VirtualProtect(g_lpSlot, sizeof(void*), old, &old);
            }
        }
        g_lpSlot = nullptr;
        return false;
    }
    if (g_lpSlot) return *g_lpSlot == (void*)&LayoutParamsHook;
    // PROVA (21/11/2026): só no Chrome de teste aberto com --wacos-teste-abas; o Chrome do usuário fica como está
    if (!wcsstr(GetCommandLineW(), L"--wacos-teste-abas")) return false;
    for (const auto& e : g_ganchos) {
        auto* base = (BYTE*)GetModuleHandleW(e.dll);
        if (!base) continue;
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        DWORD size = nt->OptionalHeader.SizeOfImage;
        if (e.vt + (e.slot + 1) * sizeof(void*) > size || e.fn + sizeof(e.sig) > size) continue;
        void** slot = (void**)(base + e.vt) + e.slot;
        BYTE* fn = base + e.fn;
        if (*slot != fn || memcmp(fn, e.sig, sizeof(e.sig))) continue;  // outra versão: não mexe
        DWORD old;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
        g_lpOrig = (LayoutParamsFn)fn;
        InterlockedExchangePointer(slot, (void*)&LayoutParamsHook);
        VirtualProtect(slot, sizeof(void*), old, &old);
        g_lpSlot = slot;
        return true;
    }
    return false;
}
// o Chrome refaz a barra com a área nova (o mesmo caminho de quando a moldura muda)
// (o SWP_FRAMECHANGED e um WM_SIZE de mentira não bastavam: só uma mudança de tamanho de verdade refaz a linha das
// abas; 1 px a mais e de volta, só ao ligar/desligar o gancho)
void RefazBarra(HWND h) {
    if (IsIconic(h) || !IsWindowVisible(h)) return;
    RECT wr; GetWindowRect(h, &wr);
    const UINT f = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    SetWindowPos(h, nullptr, 0, 0, wr.right - wr.left + 1, wr.bottom - wr.top, f);
    SetWindowPos(h, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, f);
}

void Detach(HWND h, Info* in);

void CALLBACK UncloakShown(HWND h, UINT, UINT_PTR id, DWORD);

// Arrastando: a janela não sobe por cima da barra de menus (como no Mac; antes o título dela ficava por baixo
// das letras da barra). O Dock pode ser cruzado no arraste; ao soltar, o Shell traz de volta (limites.cpp).
bool ClampTopDrag(HWND h, RECT* r) {
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    if (mi.rcWork.top <= mi.rcMonitor.top) return false;  // nada reservado em cima (barra escondida)
    RECT wr, vis;
    GetWindowRect(h, &wr);
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof(vis)))) vis = wr;
    int top = r->top + (vis.top - wr.top);  // topo visível (sem a borda invisível de redimensionar)
    if (top >= mi.rcWork.top) return false;
    int dy = mi.rcWork.top - top;
    r->top += dy; r->bottom += dy;
    return true;
}

LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    Info* in = (Info*)ref;
    if (m == WM_PAINT && GetPropW(h, L"MacJanelaPintando") == (HANDLE)1) {  // pintou: aparece em ~2 quadros (só a 1ª pintura conta)
        SetPropW(h, L"MacJanelaPintando", (HANDLE)2);
        SetTimer(h, 0x4D50, 34, UncloakShown);
    }
    if (m == g_msgUndo && g_proprio) return 1;  // app do WacOS: a barra fica (o Shell saindo não a tira)
    if (m == g_msgUndo) { StopShowHook(); Detach(h, in); return 1; }
    static UINT msgSettings = RegisterWindowMessageW(L"ShellAjustesMudaram");  // tema trocado (Leopard, Tahoe…): redesenha já
    if (m == msgSettings && msgSettings) {
        semaforo::Ler(true); TemaTahoe(true); ApplyColors(in);
        if (!in->custom) { DWORD c2 = leopard::On() ? 3 : 2; DwmSetWindowAttribute(h, 33, &c2, sizeof(c2)); }
        // o tema pode ter mudado a altura da barra (Leopard: 22): refaz a moldura e reposiciona a faixa
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Paint(in);
    }
    // Leopard: a barra de título do 10.5 tem 22 pt (a do Windows, 31): a área do programa sobe para começar 22 pt
    // abaixo do topo visível; a faixa da DLL (CaptionRect) acompanha. Só janelas com a barra padrão e no estado normal.
    if (m == WM_NCCALCSIZE && w && leopard::On() && !in->custom && !in->tool && !IsZoomed(h) && !controles::IsAlert(h) &&
        (GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
        auto* p = (NCCALCSIZE_PARAMS*)l;
        LONG wtop = p->rgrc[0].top;  // o topo da janela proposto
        LRESULT r = DefSubclassProc(h, m, w, l);
        LONG want = wtop + (LONG)std::lround(22 * GetDpiForWindow(h) / 96.f);
        if (p->rgrc[0].top > want) p->rgrc[0].top = want;
        return r;
    }
    static UINT msgShow = RegisterWindowMessageW(L"MacJanelaMostrar");  // o Shell terminou a animação do ampliar
    if (m == msgShow && msgShow) { ZoomShow(h); return 1; }
    static UINT msgMove = RegisterWindowMessageW(L"MacJanelaMover");  // o Shell: volta para a área (limites.cpp)
    if (m == msgMove && msgMove) {  // wParam: quanto desce (px, com sinal); lParam: altura nova da janela inteira
        RECT wr; GetWindowRect(h, &wr);
        int dy = (int)(INT_PTR)w, nh = (int)(INT_PTR)l;
        if (nh <= 0) nh = wr.bottom - wr.top;
        MoveAnimated(in, RECT{ wr.left, wr.top + dy, wr.right, wr.top + dy + nh });  // a foto desliza no compositor
        return 1;
    }
    if (m == g_msgTile && g_msgTile) {
        // o Shell encaixou a janela arrastada até a borda (encaixe.cpp). w = l = 0: preencher (o "ampliar");
        // senão o retângulo visível pedido: l = esquerda/topo, w = direita/baixo (16 bits cada)
        RECT s = in->lastStart;  // do arrasto que acabou agora (Win+←/→ não arrasta: o retângulo de agora)
        if (s.right <= s.left || GetTickCount() - in->lastStartAt > 3000) GetWindowRect(h, &s);
        if (!w && !l) {
            if (in->custom) { if (!IsZoomed(h)) ShowWindow(h, SW_MAXIMIZE); in->tiled = false; return 1; }
            if (!in->zoomed) { in->before = s; ToggleZoom(in); in->before = s; }
            in->tiled = false;
            return 1;
        }
        RECT t{ (short)LOWORD(l), (short)HIWORD(l), (short)LOWORD(w), (short)HIWORD(w) };
        if (!in->tiled && !in->zoomed) in->before = s;
        if (in->zoomed) { in->zoomed = false; RemovePropW(h, L"MacJanelaAmpliada"); }
        if (IsZoomed(h)) ShowWindow(h, SW_RESTORE);
        int il, it, ir, ib;
        Insets(in, il, it, ir, ib);  // bordas invisíveis ficam para fora, como no ampliar
        SetWindowPos(h, nullptr, t.left - il, t.top - it, (t.right - t.left) + il + ir, (t.bottom - t.top) + it + ib, SWP_NOZORDER | SWP_NOACTIVATE);
        in->tiled = true;
        UpdateShadow(in);
        return 1;
    }
    if (m == g_msgZoom && g_msgZoom && w == 2) { FixOversize(in); return 1; }  // grande demais: ver FixOversize
    if (m == WM_PAINT && !in->custom && in->needColor) {  // o app acabou de desenhar: a cor já está na imagem dele
        LRESULT r = DefSubclassProc(h, m, w, l);
        SampleOwnDC(in);
        return r;
    }
    // Pedido de maximizar pelo menu do sistema (Alt+Espaço, Win+↑ do Windows, atalho do app): vira o "ampliar" ANTES
    // de o Windows maximizar. Pelo caminho de baixo (WM_SIZE maximizado) a janela aparecia um quadro maximizada,
    // voltava ao tamanho de antes e só então animava: o "salto" do OBS no vídeo do Reddit (20/11/2026).
    if (m == WM_SYSCOMMAND && (w & 0xFFF0) == SC_MAXIMIZE && !in->custom && !IsZoomed(h) && !IsIconic(h) && (in->style & WS_MAXIMIZEBOX) && !in->zoomed && g_msgZoom) {
        ToggleZoom(in);
        return 0;
    }
    if (m == g_msgZoom && g_msgZoom && in->custom) {  // barra própria: Win+↑/↓ do Shell maximiza de verdade (w = 0)
        if (w == 0) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);  // o Edge ignorava o WM_SYSCOMMAND postado (testado 21/11/2026)
        return 1;
    }
    if (m == g_msgZoom && g_msgZoom) {
        // w = 1: o app (ou o Windows) maximizou; vira o "ampliar" com cantos. w = 0: alternar (Win+↑/↓ do Shell)
        if (w == 1 && IsZoomed(h)) {
            if (in->zoomed) {  // já estava ampliada e o app pediu maximizar de novo (duplo clique): volta ao tamanho de antes
                ShowWindow(h, SW_RESTORE);
                in->zoomed = false;
                RemovePropW(h, L"MacJanelaAmpliada");
                SetWindowPos(h, nullptr, in->before.left, in->before.top, in->before.right - in->before.left, in->before.bottom - in->before.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            } else ToggleZoom(in);
        }
        else if ((in->style & WS_MAXIMIZEBOX) || in->zoomed) ToggleZoom(in);
        return 1;
    }
    switch (m) {
    case WM_WINDOWPOSCHANGED: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        // depois desta mensagem: redimensionar aqui dentro deixava o app desenhando no tamanho antigo
        // (o Ableton ficava cortado embaixo)
        if (!(((WINDOWPOS*)l)->flags & SWP_NOSIZE) && g_msgZoom) PostMessageW(h, g_msgZoom, 2, 0);
        ApplyRegion(in); PlaceOverlay(in); UpdateShadow(in);
        if (!(((WINDOWPOS*)l)->flags & SWP_NOZORDER)) KeepAbove(in);
        return r;
    }
    case WM_NCHITTEST: {
        // o recorte corta a borda invisível de redimensionar: os 6 px de dentro da janela fazem esse papel
        LRESULT r = DefSubclassProc(h, m, w, l);
        if (!in->rgn.cx || !(GetWindowLongW(h, GWL_STYLE) & WS_THICKFRAME)) return r;
        if (r != HTCLIENT && r != HTCAPTION && r != HTNOWHERE && r != HTSYSMENU && r != HTMENU) return r;
        POINT p{ (short)LOWORD(l), (short)HIWORD(l) };
        RECT wr; GetWindowRect(h, &wr);
        int il, it, ir, ib;
        Insets(in, il, it, ir, ib);
        RECT f{ wr.left + il, wr.top + it, wr.right - ir, wr.bottom - ib };
        float sc = Scale(h);
        int e = (int)(6 * sc), c = (int)(14 * sc);
        bool L = p.x < f.left + e, R = p.x >= f.right - e, T = p.y < f.top + (int)(4 * sc), B = p.y >= f.bottom - e;
        bool Lc = p.x < f.left + c, Rc = p.x >= f.right - c, Tc = p.y < f.top + c, Bc = p.y >= f.bottom - c;
        if ((T && Lc) || (L && Tc)) return HTTOPLEFT;
        if ((T && Rc) || (R && Tc)) return HTTOPRIGHT;
        if ((B && Lc) || (L && Bc)) return HTBOTTOMLEFT;
        if ((B && Rc) || (R && Bc)) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (B) return HTBOTTOM;
        if (T) return HTTOP;
        return r;
    }
    case WM_SIZE: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        if (w == SIZE_MAXIMIZED && g_msgZoom) PostMessageW(h, g_msgZoom, 1, 0);  // depois desta mensagem: vira "ampliar"
        if (in->custom) in->layoutAt = 0;  // a barra do app se rearruma no tamanho novo: mede de novo
        ApplyRegion(in);
        PlaceOverlay(in);
        UpdateShadow(in);
        return r;
    }
    case WM_ENTERSIZEMOVE:
        in->moving = true;
        GetWindowRect(h, &in->moveStart);
        HideCorners(in);  // a moldura atrasaria um quadro atrás da janela (a "linha" que a seguia); volta no fim
        break;
    case WM_MOVING: {
        // o encaixe nas bordas (prévia e soltar) é do Shell (src\shell\encaixe.cpp), igual para todas as janelas
        bool clamped = l && ClampTopDrag(h, (RECT*)l);
        // arrastou uma janela encaixada ou ampliada: volta ao tamanho de antes, presa no mesmo ponto do mouse
        if (!(in->tiled || in->zoomed) || !(in->style & WS_THICKFRAME)) { if (clamped) return TRUE; break; }
        auto* r = (RECT*)l;
        RECT s = in->moveStart;
        int bw = in->before.right - in->before.left, bh = in->before.bottom - in->before.top;
        if (bw <= 0 || bh <= 0) { if (clamped) return TRUE; break; }
        POINT c; GetCursorPos(&c);
        float fx = (c.x - s.left) / (float)std::max<LONG>(s.right - s.left, 1);
        int left = c.x - (int)(fx * bw), top = r->top;
        *r = { left, top, left + bw, top + bh };
        ClampTopDrag(h, r);
        in->moveStart = *r;
        in->tiled = false;
        if (in->zoomed) { in->zoomed = false; RemovePropW(h, L"MacJanelaAmpliada"); }
        return TRUE;
    }
    case WM_EXITSIZEMOVE: {  // o usuário arrastou ou redimensionou
        in->moving = false;
        RECT wr; GetWindowRect(h, &wr);
        bool moved = wr.right - wr.left == in->moveStart.right - in->moveStart.left && wr.bottom - wr.top == in->moveStart.bottom - in->moveStart.top;
        if (in->zoomed) {
            RECT z = ZoomRect(in);
            if (!EqualRect(&z, &wr)) { in->zoomed = false; RemovePropW(h, L"MacJanelaAmpliada"); }
        }
        if (!moved) in->tiled = false;  // redimensionou: deixa de estar encaixada
        if (moved) { in->lastStart = in->moveStart; in->lastStartAt = GetTickCount(); }  // o Shell pode encaixar logo depois (MacJanelaEncaixar)
        UpdateShadow(in);
        break;
    }
    case WM_SHOWWINDOW: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        if (!w) { ShowWindow(in->overlay, SW_HIDE); ShowWindow(in->shadow.wnd, SW_HIDE); HideCorners(in); } else { PlaceOverlay(in); UpdateShadow(in); }
        return r;
    }
    case WM_NCPAINT:
        if (!in->custom && in->rgn.cx) { PaintNC(in); return 0; }
        break;
    case WM_NCACTIVATE:
        if (!in->custom && in->rgn.cx) {  // sem o desenho clássico da barra (lParam -1); pintamos nós
            in->active = w != FALSE;
            DefSubclassProc(h, m, w, -1);
            PaintNC(in);
            Paint(in);
            UpdateShadow(in);
            if (in->active && in->overlay) { in->needColor = true; in->fastUntil = GetTickCount() + 700; SetTimer(in->overlay, 1, 16, nullptr); }
            return TRUE;
        }
        in->active = w != FALSE;
        Paint(in);
        UpdateShadow(in);
        if (in->active && in->overlay) { in->needColor = true; in->fastUntil = GetTickCount() + 700; SetTimer(in->overlay, 1, 16, nullptr); }  // relê a cor do app logo depois de ativar
        else if (in->custom && in->overlay) SetTimer(in->overlay, 1, 200, nullptr);  // barra própria: a cor de inativa, depois de o app repintar
        break;
    case WM_SETTEXT: { LRESULT r = DefSubclassProc(h, m, w, l); PaintNC(in); Paint(in); return r; }
    case WM_DPICHANGED: { LRESULT r = DefSubclassProc(h, m, w, l); PlaceOverlay(in); return r; }
    case WM_SETTINGCHANGE:
        if (l && !lstrcmpiW((LPCWSTR)l, L"ImmersiveColorSet")) { ApplyColors(in); Paint(in); }
        break;
    case WM_GETMINMAXINFO: {
        // Sem WS_MAXIMIZEBOX/WS_SYSMENU o Windows maximiza sobre o monitor inteiro (por baixo da barra de
        // menus e do Dock). Maximizado = área de trabalho, com a borda de redimensionar para fora, como sempre.
        LRESULT r = DefSubclassProc(h, m, w, l);
        if (in->custom) return r;
        auto* mm = (MINMAXINFO*)l;
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) {
            UINT dpi = GetDpiForWindow(h);
            int bx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int by = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            RECT wa = mi.rcWork, mon = mi.rcMonitor;
            mm->ptMaxPosition = { wa.left - mon.left - bx, wa.top - mon.top - by };
            mm->ptMaxSize = { (wa.right - wa.left) + 2 * bx, (wa.bottom - wa.top) + 2 * by };
        }
        return r;
    }
    case WM_NCLBUTTONDBLCLK:  // sem o botão de maximizar do Windows: o duplo clique na barra continua maximizando
        if (w == HTCAPTION && (in->style & WS_MAXIMIZEBOX) && !in->custom) { ToggleZoom(in); return 0; }  // (barra própria: o app maximiza; vira ampliar)
        break;
    case WM_NCRBUTTONUP:  // o menu do sistema (sem WS_SYSMENU) não abre mais no clique direito da barra
        break;
    case WM_NCDESTROY: {
        in->shadow.Destroy();
        for (HWND& c : in->corner) if (c) { DestroyWindow(c); c = nullptr; }
        RemovePropW(h, L"MacJanelaMoldura");
        if (in->preview) { DestroyWindow(in->preview); in->preview = nullptr; }
        LRESULT r = DefSubclassProc(h, m, w, l);
        RemoveWindowSubclass(h, OwnerProc, SUBCLASS_ID);
        RemovePropW(h, PROP); RemovePropW(h, PROP_MENU); RemovePropW(h, L"MacJanelaAmpliada");
        delete in;
        return r;
    }
    }
    return DefSubclassProc(h, m, w, l);
}

void Attach(HWND h, int mode) {
    static bool cls = false;
    if (!cls) {
        WNDCLASSW wc{}; wc.lpfnWndProc = OverlayProc; wc.hInstance = g_mod; wc.lpszClassName = L"MacJanelaBarra";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        cls = true;
    }
    if (!g_d2d) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
        if (g_dw) {
            IDWriteFontCollection* fc = nullptr; g_dw->GetSystemFontCollection(&fc);
            const wchar_t* fam = L"Segoe UI";
            for (auto n : { L"SF Pro Text", L"Inter", L"Segoe UI Variable Text" }) {
                UINT32 i; BOOL ok = FALSE;
                if (fc) fc->FindFamilyName(n, &i, &ok);
                if (ok) { fam = n; break; }
            }
            if (fc) fc->Release();
            g_dw->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13, L"pt-br", &g_tf);
            if (g_dw->CreateTextFormat(L"Lucida Sans Unicode", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13, L"pt-br", &g_tfLeo) == S_OK && g_tfLeo) {
                g_tfLeo->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_tfLeo->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g_tfLeo->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
            if (g_tf) {
                g_tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g_tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                IDWriteInlineObject* ell = nullptr;
                g_dw->CreateEllipsisTrimmingSign(g_tf, &ell);
                g_tf->SetTrimming(&tr, ell);
                if (ell) ell->Release();
            }
            g_dw->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12, L"pt-br", &g_tfTool);
            if (g_tfTool) {
                g_tfTool->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_tfTool->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g_tfTool->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }
    }
    auto* in = new Info;
    in->owner = h;
    in->style = GetWindowLongW(h, GWL_STYLE);
    // antes do subclass: o recorte pode ser calculado já na primeira mensagem, e sem saber que é plug-in
    // ele ficava com a moldura grossa (e não era refeito, o tamanho não mudava)
    in->tool = (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    in->active = GetForegroundWindow() == h;
    SetPropW(h, PROP, (HANDLE)1);
    if (!SetWindowSubclass(h, OwnerProc, SUBCLASS_ID, (DWORD_PTR)in)) { RemovePropW(h, PROP); delete in; return; }
    in->custom = mode == 2;
    if (!in->custom) {
        // sem título, ícone e botões do Windows (a barra fica só com a cor)
        WTA_OPTIONS o{ WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU, WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU };
        SetWindowThemeAttribute(h, WTA_NONCLIENT, &o, sizeof(o));
        SetWindowLongW(h, GWL_STYLE, in->style & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        // a barra de menus vai para a barra de menus do Shell (lá em cima), como no Mac
        in->menu = GetMenu(h);
        if (in->menu) { SetPropW(h, PROP_MENU, (HANDLE)in->menu); SetMenu(h, nullptr); }
        in->cap = in->tool ? RGB(0x2C, 0x2C, 0x2E) : SavedColor(h);  // plug-in: a barra escura das janelas de plug-in do Mac
        if (in->tool) in->needColor = false;
        if (in->cap == CLR_INVALID) in->cap = GetSysColor(COLOR_3DFACE);  // fundo padrão das janelas (a maioria desses apps)
        ApplyColors(in);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        in->dark = Dark();  // barra própria: os botões do Mac e a faixa por cima dos botões do app (BarRect/Paint)
        SetPropW(h, L"MacJanelaPropria", (HANDLE)1);
        in->patchR = g_ultimoPatch;
        g_leadW = LeadW(in);
        if (!Right(in) && GanchoAbas(true)) RefazBarra(h);  // Chrome medido: as abas começam depois dos botões do Mac
    }
    in->overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MacJanelaBarra", L"", WS_POPUP, 0, 0, 1, 1, h, nullptr, g_mod, nullptr);
    if (in->overlay) { SetWindowLongPtrW(in->overlay, GWLP_USERDATA, (LONG_PTR)in); in->fastUntil = GetTickCount() + 900; SetTimer(in->overlay, 1, 16, nullptr); }
    DWORD corner = leopard::On() ? 3 : 2;  // DWMWCP_ROUND (Leopard: ROUNDSMALL, o canto pequeno do 10.5); mesmo em apps que pedem cantos retos
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    // A sombra antiga do Windows (CS_DROPSHADOW, comum em diálogos) é do retângulo inteiro da janela:
    // aparecia como um retângulo escuro de bordas retas em volta do recorte. Fica só a nossa.
    ULONG_PTR cs = GetClassLongPtrW(h, GCL_STYLE);
    if (cs & CS_DROPSHADOW) { in->dropShadow = true; SetClassLongPtrW(h, GCL_STYLE, cs & ~CS_DROPSHADOW); }
    in->shadow.Create(h, g_mod, L"MacJanelaSombra");
    // diálogos: o miolo também no visual do Mac (desliga com HKCU\Software\ShellProprio\MacJanela\Controles = 0)
    DWORD ctl = 1, cb = sizeof(ctl);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\ShellProprio\\MacJanela", L"Controles", RRF_RT_REG_DWORD, nullptr, &ctl, &cb);
    // REAPER: os diálogos (janela de FX, ações...) têm o tema escuro dele desenhado pelo próprio app; os nossos
    // controles por cima deixavam campos brancos, botões cortados ("Remov") e a caixinha de ligar como um
    // círculo e um quadrado. Lá fica só a barra do Mac.
    static const bool ownTheme = ExeName() == L"reaper.exe";
    if (!in->custom && ctl && !ownTheme && controles::IsDialog(h)) controles::Style(h);
    in->rgn = {};  // refaz o recorte com tudo já decidido
    ApplyRegion(in);
    PlaceOverlay(in);
    UpdateShadow(in);
    if (IsZoomed(h) && (in->style & WS_MAXIMIZEBOX) && g_msgZoom) PostMessageW(h, g_msgZoom, 1, 0);
    else if (g_msgZoom) PostMessageW(h, g_msgZoom, 2, 0);
}

void Detach(HWND h, Info* in) {
    controles::Unstyle(h);
    if (in->overlay) { DestroyWindow(in->overlay); in->overlay = nullptr; }
    in->shadow.Destroy();
    for (HWND& c : in->corner) if (c) { DestroyWindow(c); c = nullptr; }
    RemovePropW(h, L"MacJanelaMoldura");
    if (in->preview) { DestroyWindow(in->preview); in->preview = nullptr; }
    // peças órfãs desta janela na thread (moldura ou faixa de uma volta anterior, de outra cópia da DLL): somem junto
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM owner) -> BOOL {
        wchar_t c[32] = {};
        if (GetWindow(w, GW_OWNER) == (HWND)owner && GetClassNameW(w, c, 32) && (!wcscmp(c, L"MacJanelaCanto") || !wcscmp(c, L"MacJanelaBarra")))
            DestroyWindow(w);
        return TRUE;
    }, (LPARAM)h);
    if (in->dropShadow) SetClassLongPtrW(h, GCL_STYLE, GetClassLongPtrW(h, GCL_STYLE) | CS_DROPSHADOW);
    DWORD cornerDef = 0;  // DWMWCP_DEFAULT
    DwmSetWindowAttribute(h, 33, &cornerDef, sizeof(cornerDef));
    if (in->rgn.cx) {
        SetWindowRgn(h, nullptr, TRUE);
        DWMNCRENDERINGPOLICY on = DWMNCRP_USEWINDOWSTYLE;
        DwmSetWindowAttribute(h, DWMWA_NCRENDERING_POLICY, &on, sizeof(on));
    }
    if (in->custom) {
        RemovePropW(h, L"MacJanelaAmpliada"); RemovePropW(h, PROP); RemovePropW(h, L"MacJanelaPropria");
        RemoveWindowSubclass(h, OwnerProc, SUBCLASS_ID);
        delete in;
        if (g_lpSlot) GanchoAbas(false);
        if (g_lpOrig) RefazBarra(h);  // as abas voltam para a borda (a versão nova põe de novo)
        return;
    }
    COLORREF defc = 0xFFFFFFFF;
    DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &defc, sizeof(defc));
    SetWindowLongW(h, GWL_STYLE, (GetWindowLongW(h, GWL_STYLE) & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)) | (in->style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)));
    WTA_OPTIONS o{ 0, WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU };
    SetWindowThemeAttribute(h, WTA_NONCLIENT, &o, sizeof(o));
    COLORREF def = 0xFFFFFFFF;  // DWMWA_COLOR_DEFAULT
    DwmSetWindowAttribute(h, 35, &def, sizeof(def));
    if (in->menu && !GetMenu(h)) SetMenu(h, in->menu);
    RemovePropW(h, PROP_MENU);
    RemovePropW(h, L"MacJanelaAmpliada");
    RemovePropW(h, PROP);
    RemoveWindowSubclass(h, OwnerProc, SUBCLASS_ID);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    delete in;
}

// Apps com barra própria (Chrome, Discord, Electron: Claude, Streamlabs...): o visual fica o deles, mas o
// maximizar/restaurar ganha a animação do Shell. Pega a troca de estado em si (WINDOWPOS com SWP_STATECHANGED),
// venha do botão do app, do duplo clique na barra ou do Win+seta: o Shell tira a foto ainda no tamanho antigo,
// a janela troca de tamanho escondida e o Shell anima a foto nova (como no ampliar dos outros).
const wchar_t* PROP_LEVE = L"MacJanelaLeve";
const UINT_PTR LEVE_ID = 0x4C455645;
// Cantos e borda nas janelas de barra própria (Chromium/Electron). O recorte (SetWindowRgn) NÃO serve nelas:
// testado de novo em 20/11/2026 no Edge 157 com a DLL de teste dentro do processo: o Chromium desenha por
// DirectComposition numa janela filha do processo da GPU ("Intermediate D3D Window"); a região não corta esse
// conteúdo (nem recortando também a janela filha, o que dá certo pelo processo do navegador) e ainda tira o
// arredondamento do DWM: o canto ficava QUADRADO e a borda invisível aparecia. O único corte que vale para o
// conteúdo da GPU é o do próprio DWM. Então: pede o canto arredondado ao DWM (DWMWCP_ROUND; no Leopard o pequeno,
// como as janelas do 10.5) — as janelas sem a moldura do Windows (Electron com frame:false) não eram arredondadas —
// e troca a borda de 1 px do Windows (cinza ou a cor de destaque) pelo contorno do Mac. O raio é o do Windows
// (8 px a 100%); maior que isso o DWM não deixa. A sombra do Mac é do Shell (sombras.cpp).
// Maximizada, minimizada ou em tela cheia: o DWM já não arredonda; nada a fazer.
const wchar_t* PROP_CANTO = L"MacJanelaCanto";
// O fundo do PRÓPRIO app é escuro? O contorno seguia o tema do sistema: Discord escuro com o Windows claro ganhava um
// contorno cinza-claro que parecia uma borda branca em volta (20/11/2026). O Discord nem avisa o DWM que é escuro
// (DWMWA_USE_IMMERSIVE_DARK_MODE = 0), então olha o conteúdo: uma foto da janela (PrintWindow com o conteúdo da GPU) e
// a média de uns pontos perto das bordas. 1 claro, 2 escuro (guardado em MacJanelaFundo); 0 = ainda não sabe.
const wchar_t* PROP_FUNDO = L"MacJanelaFundo";
int AmostraFundo(HWND h) {
    BOOL imm = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &imm, sizeof(imm))) && imm) return 2;
    RECT wr, fb;
    if (!GetWindowRect(h, &wr)) return 0;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &fb, sizeof(fb)))) fb = wr;
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    if (W < 80 || H < 80 || W > 16384 || H > 16384) return 0;
    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, W, H);
    HGDIOBJ old = SelectObject(mdc, bm);
    int r = 0;
    if (PrintWindow(h, mdc, 2 /*PW_RENDERFULLCONTENT*/)) {
        int ox = fb.left - wr.left, oy = fb.top - wr.top, fw = fb.right - fb.left, fh = fb.bottom - fb.top;
        POINT pts[5] = { { ox + 12, oy + fh - 12 }, { ox + fw - 12, oy + fh - 12 }, { ox + fw / 2, oy + fh - 6 },
                         { ox + 6, oy + fh / 2 }, { ox + fw - 6, oy + fh / 2 } };
        double L = 0; int n = 0;
        for (auto& pt : pts) {
            COLORREF c = GetPixel(mdc, pt.x, pt.y);
            if (c == CLR_INVALID) continue;
            L += 0.299 * GetRValue(c) + 0.587 * GetGValue(c) + 0.114 * GetBValue(c); n++;
        }
        if (n) r = L / n < 110 ? 2 : 1;
    }
    SelectObject(mdc, old); DeleteObject(bm); DeleteDC(mdc); ReleaseDC(nullptr, sdc);
    return r;
}

void CantoLeve(HWND h) {
    if (IsIconic(h)) return;
    DWORD pref = leopard::On() ? 3 /*DWMWCP_ROUNDSMALL*/ : 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref));
    // contorno do Mac: escuro numa janela clara, um cinza um pouco mais claro que a janela numa escura. Pela cor do
    // app (AmostraFundo); enquanto não sabe, pelo tema do sistema
    int fundo = (int)(INT_PTR)GetPropW(h, PROP_FUNDO);
    bool dark = fundo ? fundo == 2 : Dark();
    COLORREF border = dark ? RGB(0x4A, 0x4A, 0x4D) : RGB(0xC2, 0xC2, 0xC5);
    if (GetPropW(h, PROP_CANTO) != (HANDLE)(INT_PTR)(dark ? 2 : 1)) {
        DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
        SetPropW(h, PROP_CANTO, (HANDLE)(INT_PTR)(dark ? 2 : 1));
    }
}
void CantoLeveDesfazer(HWND h) {
    DWORD def = 0;  // DWMWCP_DEFAULT
    DwmSetWindowAttribute(h, 33, &def, sizeof(def));
    COLORREF c = 0xFFFFFFFF;  // DWMWA_COLOR_DEFAULT
    DwmSetWindowAttribute(h, 34, &c, sizeof(c));
    RemovePropW(h, PROP_CANTO);
    RemovePropW(h, PROP_FUNDO);
    RemovePropW(h, L"MacJanelaFundoN");
}

// Este processo é Chromium/Electron? (só eles recebem o modo barra própria). Pelo módulo do navegador carregado ou
// pelos pacotes do Chromium ao lado do .exe (jogos.h). Calculado uma vez.
bool ProcessoChromium() {
    static int r = -1;
    if (r < 0) {
        bool ok = GetModuleHandleW(L"chrome.dll") || GetModuleHandleW(L"msedge.dll") || GetModuleHandleW(L"libcef.dll") ||
                  GetModuleHandleW(L"opera_browser.dll") || GetModuleHandleW(L"vivaldi.dll");
        if (!ok) { wchar_t p[MAX_PATH] = {}; GetModuleFileNameW(nullptr, p, MAX_PATH); ok = jogos::ProgramaChromium(p); }
        r = ok ? 1 : 0;
    }
    return r == 1;
}

LRESULT CALLBACK LeveProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    static UINT msgShow = RegisterWindowMessageW(L"MacJanelaMostrar");
    if (m == g_msgUndo && g_msgUndo && g_proprio) return DefSubclassProc(h, m, w, l);
    if (m == g_msgUndo && g_msgUndo) CantoLeveDesfazer(h);
    if (m == WM_NCDESTROY) RemovePropW(h, PROP_CANTO);
    if (m == WM_TIMER && w == 0x4D43) {  // tema claro/escuro mudou: a cor do contorno
        // a cor do app: só nas duas primeiras voltas do timer com a janela à mostra (2 e 4 s, depois de ele pintar a tela
        // de verdade); a foto custa alguns ms e não vale ficar repetindo
        int vezes = (int)(INT_PTR)GetPropW(h, L"MacJanelaFundoN");
        if (vezes < 2 && IsWindowVisible(h) && !IsIconic(h)) {
            SetPropW(h, L"MacJanelaFundoN", (HANDLE)(INT_PTR)(vezes + 1));
            if (int f = AmostraFundo(h)) SetPropW(h, PROP_FUNDO, (HANDLE)(INT_PTR)f);
        }
        CantoLeve(h);
        return 0;
    }  // tema claro/escuro mudou: a cor do contorno
    // Desfazer: solta e PASSA ADIANTE. Na barra própria o modo leve fica por cima do OwnerProc (instalado depois); com
    // "return 1" o pedido parava aqui, a barra (OwnerProc) nunca se soltava e a DLL nova não conseguia assumir a janela:
    // o Discord ficava com a faixa da versão antiga e a nova reenviava o pedido a cada 400 ms (21/11/2026)
    if (m == g_msgUndo && g_msgUndo) {
        BOOL on = FALSE; DwmSetWindowAttribute(h, 3, &on, sizeof(on)); ZoomShow(h); RemovePropW(h, PROP_LEVE); RemoveWindowSubclass(h, LeveProc, LEVE_ID);
        return GetPropW(h, PROP) ? DefSubclassProc(h, m, w, l) : 1;
    }
    if (m == msgShow && msgShow) { ZoomShow(h); return 1; }
    if (m == WM_NCDESTROY) { RemovePropW(h, PROP_LEVE); RemoveWindowSubclass(h, LeveProc, LEVE_ID); }
    // Arrastar pela barra um app de barra própria maximizado (Chrome, Discord, Electron): sai do maximizado e segue o
    // ponteiro, como no Windows e no Mac. Quem fazia isso era o Snap do Windows, que o Shell desliga para usar o encaixe
    // dele (SPI_SETWINARRANGING): com ele desligado o Windows nem começa a mover uma janela maximizada, e a janela não
    // saía do lugar (PR #14 apontou, 21/11/2026). Só quando o Snap do Windows está desligado; ligado, ele mesmo faz.
    if (m == WM_NCLBUTTONDOWN && w == HTCAPTION && IsZoomed(h)) {
        BOOL arranjo = TRUE;
        SystemParametersInfoW(SPI_GETWINARRANGING, 0, &arranjo, 0);
        POINT pt{ (short)LOWORD(l), (short)HIWORD(l) };
        if (!arranjo && DragDetect(h, pt)) {
            RECT fb; if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &fb, sizeof(fb)))) GetWindowRect(h, &fb);
            float fx = (pt.x - fb.left) / (float)std::max<LONG>(1, fb.right - fb.left);  // onde o ponteiro estava na largura
            int dy = pt.y - fb.top;                                                       // e abaixo do topo
            WINDOWPLACEMENT wp{ sizeof(wp) }; GetWindowPlacement(h, &wp);
            int nw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            SetPropW(h, PROP_LEVE, (HANDLE)1);  // sem a animação de ampliar no meio do arrasto
            ShowWindow(h, SW_RESTORE);
            POINT c; GetCursorPos(&c);
            RECT wr, vis; GetWindowRect(h, &wr);
            if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof(vis)))) vis = wr;
            if (nw <= 0) nw = vis.right - vis.left;
            int x = c.x - (int)std::lround(fx * (vis.right - vis.left)) - (vis.left - wr.left), y = c.y - dy - (vis.top - wr.top);
            SetWindowPos(h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(h, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, MAKELPARAM(c.x, c.y));  // o arrasto continua daqui
            return 0;
        }
    }
    if (m == WM_MOVING && l && ClampTopDrag(h, (RECT*)l)) return TRUE;  // não passa por cima da barra de menus
    // só quando o tamanho mudou: o Chromium/Electron troca a moldura (SWP_FRAMECHANGED, sem tamanho) já marcado
    // como maximizado e antes de crescer; guardar ali fazia o maximizar de verdade passar sem animação
    if (m == WM_WINDOWPOSCHANGED && l && !(((WINDOWPOS*)l)->flags & SWP_NOSIZE) && !IsIconic(h)) SetPropW(h, PROP_LEVE, (HANDLE)(INT_PTR)(IsZoomed(h) ? 2 : 1));
    if (m == WM_WINDOWPOSCHANGING && l) {
        auto* wp = (WINDOWPOS*)l;
        BOOL cloaked = FALSE;
        // O Windows já marca a janela como maximizada (ou não) antes deste aviso, e ela ainda está no lugar
        // antigo. Compara com o estado guardado no último WM_WINDOWPOSCHANGED (a marca SWP_STATECHANGED do
        // Chromium/Electron chegava só depois de já ter maximizado: a foto saía grande, um "snap").
        HANDLE zNow = (HANDLE)(INT_PTR)(IsZoomed(h) ? 2 : 1);
        if (GetPropW(h, PROP_LEVE) != zNow && !(wp->flags & (SWP_NOSIZE | SWP_HIDEWINDOW)) && wp->x > -30000 &&
            IsWindowVisible(h) && !IsIconic(h) && (!GetPropW(h, PROP) || GetPropW(h, L"MacJanelaPropria")) && (SetPropW(h, PROP_LEVE, zNow), true) &&
            !(SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)) {
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
            RECT mon = mi.rcMonitor;
            // tela cheia (F11, vídeo) não é maximizada; o maximizar do Electron pede o monitor inteiro primeiro
            // (ajusta à área de trabalho depois) e passava por tela cheia
            bool full = !IsZoomed(h) && wp->x <= mon.left && wp->y <= mon.top && wp->x + wp->cx >= mon.right && wp->y + wp->cy >= mon.bottom;
            RECT from;
            if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &from, sizeof(from)))) GetWindowRect(h, &from);
            bool wasFull = from.left <= mon.left && from.top <= mon.top && from.right >= mon.right && from.bottom >= mon.bottom;
            HWND sh = FindWindowW(L"ShellMensagens", nullptr);
            if (!full && !wasFull && sh) {  // tela cheia (F11, jogos, vídeo): sem animação
                struct Msg { unsigned long long hwnd; RECT from, to; int flags; COLORREF cap; } msg{ (unsigned long long)(UINT_PTR)h, from, from, 4 | 8 /* 8: barra própria (Chrome/Electron): o Shell espera ela pintar */, RGB(0, 0, 0) };
                COPYDATASTRUCT cd{ 0x414D504C /*'AMPL'*/, sizeof(msg), &msg };
                DWORD_PTR ok = 0;
                if (SendMessageTimeoutW(sh, WM_COPYDATA, (WPARAM)h, (LPARAM)&cd, SMTO_ABORTIFHUNG, 400, &ok) && ok) {
                    BOOL on = TRUE; DwmSetWindowAttribute(h, 13 /*DWMWA_CLOAK*/, &on, sizeof(on));
                    SetTimer(h, ZOOM_TIMER, 1500, ZoomSafety);
                    SetTimer(h, ZOOM_TIMER + 1, 40, ZoomPhoto);  // só precisa do retângulo novo (flags 4 não usa foto nova)
                }
            }
        }
    }
    return DefSubclassProc(h, m, w, l);
}

// janela principal com barra própria: redimensionável, com maximizar, sem a barra do Windows
void AttachLeve(HWND h) {
    if (GetAncestor(h, GA_ROOT) != h || !IsWindowVisible(h) || (GetPropW(h, PROP) && !GetPropW(h, L"MacJanelaPropria")) || GetPropW(h, PROP_LEVE)) return;
    LONG st = GetWindowLongW(h, GWL_STYLE), ex = GetWindowLongW(h, GWL_EXSTYLE);
    if ((st & WS_CHILD) || !(st & WS_THICKFRAME) || !(st & WS_MAXIMIZEBOX) || (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))) return;
    // só a janela principal do Chromium/Electron (regra de 20/11/2026): em qualquer outro programa sem a barra do
    // Windows (jogos em janela, apps de motor próprio) a DLL não mexe
    wchar_t cls[32] = {}; GetClassNameW(h, cls, 32);
    if (wcscmp(cls, L"Chrome_WidgetWin_1") || !ProcessoChromium()) return;
    RECT r; GetWindowRect(h, &r);
    if (r.right - r.left < 300 || r.bottom - r.top < 200) return;
    if (SetWindowSubclass(h, LeveProc, LEVE_ID, 0)) {
        SetPropW(h, PROP_LEVE, (HANDLE)(INT_PTR)(IsZoomed(h) ? 2 : 1));  // 1 normal, 2 maximizada
        // sem a animação de maximizar do próprio Windows: ela tocava junto com a nossa e piscava no fim
        BOOL off = TRUE; DwmSetWindowAttribute(h, 3 /*DWMWA_TRANSITIONS_FORCEDISABLED*/, &off, sizeof(off));
        CantoLeve(h);
        SetTimer(h, 0x4D43, 2000, nullptr);
    }
}

// Janela ainda presa a uma versão antiga desta DLL (o Shell reiniciou e o pedido de soltar expirou com o app
// ocupado): pede à antiga que solte, aqui mesmo na thread do app, e assume. Sem isso a nova via a marca
// "já tratada" e as correções novas nunca chegavam (Ableton com as duas versões carregadas).
void TakeOver(HWND h) {
    if (GetAncestor(h, GA_ROOT) != h) return;
    DWORD_PTR ref = 0;
    bool old = (GetPropW(h, PROP) && !GetWindowSubclass(h, OwnerProc, SUBCLASS_ID, &ref)) ||
               (GetPropW(h, PROP_LEVE) && !GetWindowSubclass(h, LeveProc, LEVE_ID, &ref));
    // algumas vezes: o modo leve das versões até 21/11/2026 engolia o pedido (cada vez solta uma camada, até a barra)
    for (int i = 0; old && g_msgUndo && i < 4; i++) {
        SendMessageW(h, g_msgUndo, 0, 0);
        old = (GetPropW(h, PROP) && !GetWindowSubclass(h, OwnerProc, SUBCLASS_ID, &ref)) || (GetPropW(h, PROP_LEVE) && !GetWindowSubclass(h, LeveProc, LEVE_ID, &ref));
    }
}

bool g_fora = false;  // apareceu janela de jogo neste processo: a DLL não faz mais nada aqui
BOOL CALLBACK EnumProc(HWND h, LPARAM) {
    wchar_t cls[64] = {};
    if (GetClassNameW(h, cls, 64) && jogos::ClasseDeJogo(cls)) { g_fora = true; return FALSE; }
    TakeOver(h);
    if (int mode = Mode(h)) Attach(h, mode);
    AttachLeve(h);  // barra própria: o modo leve também (cantos, contorno, animação); nas outras não faz nada
    return TRUE;
}

// a janela (e a sombra/barra/cantos, que esperaram junto) aparece: o app já pintou, ou passou o limite
// Abrir/fechar como no Mac (o Shell anima: abrir.cpp e ampliar::Fechar). Só janelas de app comuns: com
// título, de tamanho de janela, fora da tela cheia e sem a nossa barra (essas já esperam a pintura sozinhas);
// nada enquanto o botão do mouse está apertado (aba do Chrome sendo arrastada vira janela nova).
bool OpenCloseEligible(HWND h) {
    if (GetAncestor(h, GA_ROOT) != h || IsZoomed(h) || IsIconic(h)) return false;
    LONG st = GetWindowLongW(h, GWL_STYLE), ex = GetWindowLongW(h, GWL_EXSTYLE);
    if ((st & WS_CHILD) || (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED))) return false;
    if ((st & WS_CAPTION) != WS_CAPTION && !GetPropW(h, PROP_LEVE)) return false;
    if (GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0) return false;
    RECT r; GetWindowRect(h, &r);
    if (r.right - r.left < 200 || r.bottom - r.top < 120) return false;
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi) &&
        r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return false;
    wchar_t cls[32] = {}; GetClassNameW(h, cls, 32);
    if (!wcscmp(cls, L"#32768") || !wcscmp(cls, L"tooltips_class32")) return false;
    return FindWindowW(L"ShellMensagens", nullptr) != nullptr;
}
void OpenReveal(HWND h) {  // mostra a janela de verdade (fim da animação, ou o Shell não respondeu)
    KillTimer(h, 0x4D52); KillTimer(h, 0x4D53);
    if (!RemovePropW(h, L"MacJanelaAbrindo")) return;
    BOOL off = FALSE; DwmSetWindowAttribute(h, 13 /*DWMWA_CLOAK*/, &off, sizeof(off));
    DWORD_PTR ref = 0;  // com a nossa barra: ela (escondida junto) aparece, e a sombra e os cantos voltam
    if (GetWindowSubclass(h, OwnerProc, SUBCLASS_ID, &ref) && ref) {
        Info* in = (Info*)ref;
        if (in->overlay) DwmSetWindowAttribute(in->overlay, 13, &off, sizeof(off));
        PlaceOverlay(in); UpdateShadow(in);
    }
}
void CALLBACK OpenSafety(HWND h, UINT, UINT_PTR, DWORD) { OpenReveal(h); }
void CALLBACK OpenReady(HWND h, UINT, UINT_PTR id, DWORD) {  // o app já pintou: o Shell anima
    KillTimer(h, id);
    static UINT msg = RegisterWindowMessageW(L"ShellJanelaAbrir");
    HWND sh = FindWindowW(L"ShellMensagens", nullptr);
    if (!sh || !PostMessageW(sh, msg, (WPARAM)h, 0)) OpenReveal(h);
}

void CALLBACK UncloakShown(HWND h, UINT, UINT_PTR id, DWORD) {  // função nomeada: lambda como TIMERPROC falha no x86
    KillTimer(h, 0x4D50); KillTimer(h, 0x4D51);
    if (!RemovePropW(h, L"MacJanelaPintando")) return;
    if (GetPropW(h, L"MacJanelaAbrindo")) {  // abrir animado: a barra (escondida também) já se posiciona e o Shell anima as duas
        DWORD_PTR r0 = 0;
        Info* in = GetWindowSubclass(h, OwnerProc, SUBCLASS_ID, &r0) && r0 ? (Info*)r0 : nullptr;
        HWND ov = in ? in->overlay : nullptr;
        BOOL on = TRUE;
        if (ov) { DwmSetWindowAttribute(ov, 13, &on, sizeof(on)); PlaceOverlay(in); }
        if (in) { if (in->shadow.wnd) ShowWindow(in->shadow.wnd, SW_HIDE); HideCorners(in); }
        static UINT msg = RegisterWindowMessageW(L"ShellJanelaAbrir");
        HWND sh = FindWindowW(L"ShellMensagens", nullptr);
        if (sh && PostMessageW(sh, msg, (WPARAM)h, (LPARAM)ov)) { SetTimer(h, 0x4D53, 900, OpenSafety); return; }
        OpenReveal(h);
        return;
    }
    BOOL off = FALSE; DwmSetWindowAttribute(h, 13 /*DWMWA_CLOAK*/, &off, sizeof(off));
    DWORD_PTR ref = 0;
    if (GetWindowSubclass(h, OwnerProc, SUBCLASS_ID, &ref) && ref) { Info* in = (Info*)ref; PlaceOverlay(in); UpdateShadow(in); KeepAbove(in); }
    (void)id;
}

}  // namespace

// Gancho por thread instalado pelo Shell: a cada mensagem (no máximo a cada 400 ms) procura janelas
// novas desta thread e as transforma.
// Última varredura de cada thread. NÃO usar thread_local: numa DLL carregada por gancho o TLS
// implícito não existe nas threads que o programa já tinha, e gravar nele derrubava o programa
// (Chrome, Discord, OBS travaram assim). O TLS pedido ao Windows (TlsAlloc) vale em qualquer thread.
DWORD g_tls = TLS_OUT_OF_INDEXES, g_tlsShow = TLS_OUT_OF_INDEXES, g_tlsPos = TLS_OUT_OF_INDEXES;

// Aviso do Windows dentro do programa (WinEvent "no contexto", síncrono): menu nativo (#32768) sendo mostrado
// com o Shell trocando os menus pelo do Mac sai da tela antes de ela ser desenhada. O lugar que o Windows
// escolheu fica guardado (propriedades ShellMenuX/Y; o Shell abre o nosso ali). Os menus do Windows são do
// sistema (a janela é tratada no kernel): mudar a posição pelo gancho de mensagens não pegava, e o aviso de
// fora (no Shell) chegava depois de a janela aparecer; o submenu grande do Serum piscava no canto (0,0).
// Com a propriedade ShellMenuLivre (o Shell desistiu de trocar aquele menu), não mexe.
void CALLBACK MenuShowEvent(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != CHILDID_SELF || !h) return;
    wchar_t c[8] = {};
    if (!GetClassNameW(h, c, 8) || wcscmp(c, L"#32768") || GetPropW(h, L"ShellMenuLivre")) return;
    if (!FindWindowExW(HWND_MESSAGE, nullptr, L"ShellMenusHost", nullptr)) return;
    RECT r; GetWindowRect(h, &r);
    if (r.left <= -30000) return;
    SetPropW(h, L"ShellMenuX", (HANDLE)(INT_PTR)(r.left + 40000));  // +40000: 0 não vale como propriedade
    SetPropW(h, L"ShellMenuY", (HANDLE)(INT_PTR)(r.top + 40000));
    SetWindowPos(h, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Segundo gancho, posto pela própria DLL só nesta thread (sem módulo: não ocupa lugar na lista de DLLs
// de gancho do Windows): pega a janela nova no WM_SHOWWINDOW, antes de
// ela aparecer, então já abre com a barra do Mac na cor certa (pela varredura acima ela aparecia primeiro
// com a barra do Windows). Sai quando o Shell manda desfazer.
LRESULT CALLBACK ShowProc(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        auto* m = (CWPRETSTRUCT*)l;
        // Menu nativo (#32768) nascendo com o Shell trocando os menus pelo do Mac: já nasce invisível (alfa 0),
        // aqui dentro do programa, antes de aparecer. De fora o aviso chega tarde e um submenu grande piscava
        // no canto da tela (Serum no Ableton). O Shell mostra de volta se não conseguir trocar.
        if (m->message == WM_CREATE) {
            wchar_t c[8] = {};
            if (GetClassNameW(m->hwnd, c, 8) && !wcscmp(c, L"#32768") && FindWindowExW(HWND_MESSAGE, nullptr, L"ShellMenusHost", nullptr)) {
                SetWindowLongPtrW(m->hwnd, GWL_EXSTYLE, GetWindowLongPtrW(m->hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
                SetLayeredWindowAttributes(m->hwnd, 0, 0, LWA_ALPHA);
            }
        }
        // vai sumir (fechar/esconder, não minimizar): o Shell tira a foto agora, com ela ainda na tela, e anima
        if (m->message == WM_WINDOWPOSCHANGING && m->lParam && (((WINDOWPOS*)m->lParam)->flags & SWP_HIDEWINDOW) &&
            IsWindowVisible(m->hwnd) && !GetPropW(m->hwnd, L"MacJanelaAbrindo") && OpenCloseEligible(m->hwnd)) {
            BOOL cloaked = FALSE;
            if (!(SUCCEEDED(DwmGetWindowAttribute(m->hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)) {
                static UINT msg = RegisterWindowMessageW(L"ShellJanelaFechar");
                DWORD_PTR ok = 0, ref = 0;
                LPARAM bar = 0;  // com a nossa barra: o Shell desenha a barra na foto (cor e tipo dela)
                if (GetWindowSubclass(m->hwnd, OwnerProc, SUBCLASS_ID, &ref) && ref) {
                    Info* in = (Info*)ref;
                    COLORREF cap = in->cap == CLR_INVALID ? (in->dark ? RGB(0x2D, 0x2D, 0x2F) : RGB(0xE9, 0xE9, 0xEA)) : in->cap;
                    int fl = in->custom ? 0 : in->tool ? 3 : 1;
                    bar = (LPARAM)(0x1000000 | ((LPARAM)fl << 25) | (cap & 0xFFFFFF));
                }
                if (HWND sh = FindWindowW(L"ShellMensagens", nullptr)) SendMessageTimeoutW(sh, msg, (WPARAM)m->hwnd, bar, SMTO_ABORTIFHUNG, 150, &ok);
            }
        }
        // aparecendo por SetWindowPos(SWP_SHOWWINDOW) (não manda WM_SHOWWINDOW): também abre animado. O
        // gancho vê a mensagem depois de tratada, com a janela ainda escondida
        if (m->message == WM_WINDOWPOSCHANGING && m->lParam && (((WINDOWPOS*)m->lParam)->flags & SWP_SHOWWINDOW) &&
            !IsWindowVisible(m->hwnd) && !GetPropW(m->hwnd, PROP) && !GetPropW(m->hwnd, L"MacJanelaAbrindo") && OpenCloseEligible(m->hwnd)) {
            BOOL on = TRUE;
            if (SUCCEEDED(DwmSetWindowAttribute(m->hwnd, 13 /*DWMWA_CLOAK*/, &on, sizeof(on)))) {
                SetPropW(m->hwnd, L"MacJanelaAbrindo", (HANDLE)1);
                SetTimer(m->hwnd, 0x4D52, 45, OpenReady);
                SetTimer(m->hwnd, 0x4D53, 900, OpenSafety);
            }
        }
        if (m->message == WM_SHOWWINDOW && m->wParam) {
            if (int mode = Mode(m->hwnd, true)) Attach(m->hwnd, mode);
            // janela comum aparecendo: escondida (cloak) até o app pintar; o Shell anima e manda mostrar
            if (!GetPropW(m->hwnd, PROP) && !GetPropW(m->hwnd, L"MacJanelaAbrindo") && OpenCloseEligible(m->hwnd)) {
                BOOL on = TRUE;
                if (SUCCEEDED(DwmSetWindowAttribute(m->hwnd, 13 /*DWMWA_CLOAK*/, &on, sizeof(on)))) {
                    SetPropW(m->hwnd, L"MacJanelaAbrindo", (HANDLE)1);
                    SetTimer(m->hwnd, 0x4D52, 45, OpenReady);   // ~3 quadros para o app pintar
                    SetTimer(m->hwnd, 0x4D53, 900, OpenSafety);  // o Shell não respondeu: aparece assim mesmo
                }
            }
            // Janela com a nossa barra aparecendo: fica escondida (cloak) uns quadros até o app pintar. A troca
            // da moldura muda a área de dentro e a janela aparecia em branco antes (Preferências do Ableton).
            if (GetPropW(m->hwnd, PROP) && !IsZoomed(m->hwnd)) {
                BOOL on = TRUE; DwmSetWindowAttribute(m->hwnd, 13 /*DWMWA_CLOAK*/, &on, sizeof(on));
                SetPropW(m->hwnd, L"MacJanelaPintando", (HANDLE)1);
                if (OpenCloseEligible(m->hwnd)) SetPropW(m->hwnd, L"MacJanelaAbrindo", (HANDLE)1);  // depois de pintar: abrir animado
                DWORD_PTR ref = 0;  // a sombra e a barra esperam junto (antes a sombra aparecia sozinha primeiro)
                if (GetWindowSubclass(m->hwnd, OwnerProc, SUBCLASS_ID, &ref) && ref) {
                    Info* in = (Info*)ref;
                    if (in->overlay) ShowWindow(in->overlay, SW_HIDE);
                    if (in->shadow.wnd) ShowWindow(in->shadow.wnd, SW_HIDE);
                    HideCorners(in);
                }
                SetTimer(m->hwnd, 0x4D51, 80, UncloakShown);  // limite; o normal é logo depois da primeira pintura
            }
        }
        // controle criado depois (página nova de uma folha de propriedades): entra no visual do Mac também
        if (m->message == WM_CREATE && (GetWindowLongW(m->hwnd, GWL_STYLE) & WS_CHILD)) {
            HWND root = GetAncestor(m->hwnd, GA_ROOT);
            if (root && GetPropW(root, L"MacCtl")) controles::Child(m->hwnd);  // diálogo já no visual do Mac
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

void StopShowHook() {
    if (g_tlsShow == TLS_OUT_OF_INDEXES) return;
    if (HHOOK hk = (HHOOK)TlsGetValue(g_tlsShow)) { UnhookWindowsHookEx(hk); TlsSetValue(g_tlsShow, nullptr); }  // volta com a próxima varredura (Shell aberto de novo)
    if (g_tlsPos != TLS_OUT_OF_INDEXES)
        if (HWINEVENTHOOK hk = (HWINEVENTHOOK)TlsGetValue(g_tlsPos)) { UnhookWinEvent(hk); TlsSetValue(g_tlsPos, nullptr); }
}

extern "C" __declspec(dllexport) LRESULT CALLBACK GetMsgProc(int code, WPARAM w, LPARAM l) {
    if (code >= 0 && w == PM_REMOVE && l) {  // o Shell terminou a animação de abrir: a janela de verdade aparece
        static UINT opened = RegisterWindowMessageW(L"MacJanelaAbriu");
        auto* msg = (MSG*)l;
        if (msg->message == opened && opened && msg->hwnd) OpenReveal(msg->hwnd);
    }
    if (code >= 0 && g_tls != TLS_OUT_OF_INDEXES && !g_fora) {
        DWORD now = GetTickCount() | 1;  // nunca 0 (0 = ainda não varreu)
        DWORD last = (DWORD)(UINT_PTR)TlsGetValue(g_tls);
        if (!last || now - last > 400) {
            TlsSetValue(g_tls, (LPVOID)(UINT_PTR)now);
            if (!g_msgUndo) g_msgUndo = RegisterWindowMessageW(L"MacJanelaDesfazer");
            if (!g_msgZoom) g_msgZoom = RegisterWindowMessageW(L"MacJanelaAmpliar");
            if (!g_msgTile) g_msgTile = RegisterWindowMessageW(L"MacJanelaEncaixar");
            EnumThreadWindows(GetCurrentThreadId(), EnumProc, 0);
            if (g_tlsShow != TLS_OUT_OF_INDEXES && !TlsGetValue(g_tlsShow))
                TlsSetValue(g_tlsShow, SetWindowsHookExW(WH_CALLWNDPROCRET, ShowProc, nullptr, GetCurrentThreadId()));
            if (g_tlsPos != TLS_OUT_OF_INDEXES && !TlsGetValue(g_tlsPos))
                TlsSetValue(g_tlsPos, SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, g_mod, MenuShowEvent,
                                                      GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_INCONTEXT));
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

// Os apps do WacOS que não desenham os próprios botões (app2d.h, Pré-Visualização) carregam esta DLL eles mesmos
// (src\comumarra_mac.h) e chamam esta entrada na thread da interface, antes de criar a janela: o mesmo gancho
// do Shell, mas posto pelo próprio app só na thread dele e sem módulo (não ocupa lugar na lista de DLLs de gancho
// do Windows). Assim a barra é igual com e sem o Shell aberto.
extern "C" __declspec(dllexport) BOOL WINAPI WacOSPropria() {
    if (g_proprio) return TRUE;
    g_proprio = true;
    if (!g_msgUndo) g_msgUndo = RegisterWindowMessageW(L"MacJanelaDesfazer");
    if (!g_msgZoom) g_msgZoom = RegisterWindowMessageW(L"MacJanelaAmpliar");
    if (!g_msgTile) g_msgTile = RegisterWindowMessageW(L"MacJanelaEncaixar");
    DWORD tid = GetCurrentThreadId();
    HHOOK hk = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, nullptr, tid);
    // o gancho do WM_SHOWWINDOW já agora: a janela principal nasce com a barra do Mac (sem esperar a 1ª mensagem)
    if (g_tlsShow != TLS_OUT_OF_INDEXES && !TlsGetValue(g_tlsShow))
        TlsSetValue(g_tlsShow, SetWindowsHookExW(WH_CALLWNDPROCRET, ShowProc, nullptr, tid));
    return hk != nullptr;
}

// Programas de 32 bits: o Shell é de 64 bits e um gancho só carrega DLL da mesma arquitetura. Então o Shell
// abre o rundll32.exe de 32 bits do próprio Windows (SysWOW64) com esta DLL, entrada WacOSHost: ela fica lá
// esperando os pedidos do Shell ("ponha a barra de título do Mac na thread X") e sai quando o Shell manda.
namespace {
struct HostHook { HHOOK hk; ULONGLONG born; };
// o gancho dos programas de 32 bits também vai pela carregadora fixa (WacOSGancho32.dll, caminho no mapa do Shell,
// gancho_mapa.h): trocar de versão não gasta lugar na tabela de ganchos. Sem ela, esta própria DLL (o jeito antigo).
HMODULE g_hostMod;
HOOKPROC g_hostProc;
void HostGancho() {
    if (g_hostProc) return;
    if (HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, GANCHO_MAPA)) {
        if (auto* m = (GanchoMapa*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(GanchoMapa))) {
            if (m->tamanho == sizeof(GanchoMapa) && m->gancho32[0])
                if (HMODULE d = LoadLibraryW(m->gancho32))
                    if (auto p = (HOOKPROC)GetProcAddress(d, "GetMsgProc")) { g_hostMod = d; g_hostProc = p; }
            UnmapViewOfFile(m);
        }
        CloseHandle(h);
    }
    if (!g_hostProc) { g_hostMod = g_mod; g_hostProc = GetMsgProc; }
}
std::map<DWORD, HostHook> g_hostHooks;
const UINT HOST_HOOK = WM_APP + 1, HOST_QUIT = WM_APP + 2;

// quando a thread nasceu (0 se já não existe): o Windows reaproveita os números de thread
ULONGLONG ThreadBorn(DWORD tid) {
    HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (!th) return 0;
    FILETIME c{}, e{}, k{}, u{};
    ULONGLONG r = GetThreadTimes(th, &c, &e, &k, &u) ? ((ULONGLONG)c.dwHighDateTime << 32 | c.dwLowDateTime) : 0;
    CloseHandle(th);
    return r;
}

LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == HOST_HOOK) {
        DWORD tid = (DWORD)w;
        ULONGLONG born = ThreadBorn(tid);
        auto it = g_hostHooks.find(tid);
        // número de uma thread que já morreu, agora de outra (outro programa): o gancho antigo sumiu com ela.
        // Antes o número ficava marcado como "já tem" e o programa novo nunca ganhava a barra
        if (it != g_hostHooks.end() && it->second.born != born) { UnhookWindowsHookEx(it->second.hk); g_hostHooks.erase(it); }
        if (!g_hostHooks.count(tid))
            if (HostGancho(), true)
                if (HHOOK hk = SetWindowsHookExW(WH_GETMESSAGE, g_hostProc, g_hostMod, tid)) g_hostHooks[tid] = { hk, born };
        if (l) PostMessageW((HWND)l, WM_NULL, 0, 0);  // a thread processa uma mensagem e a DLL entra
        return 0;
    }
    if (m == HOST_QUIT) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) {
        for (auto& [t, hh] : g_hostHooks) UnhookWindowsHookEx(hh.hk);
        g_hostHooks.clear();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

extern "C" __declspec(dllexport) void CALLBACK WacOSHostW(HWND, HINSTANCE, LPWSTR, int) {
    if (FindWindowExW(HWND_MESSAGE, nullptr, L"MacJanela32Host", nullptr)) return;  // já tem um
    WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = g_mod; wc.lpszClassName = L"MacJanela32Host";
    RegisterClassW(&wc);
    if (!CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_mod, nullptr)) return;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) DispatchMessageW(&m);
}

// Segunda trava (a primeira é o Shell, src\shell\janelas.cpp): carregada dentro de um jogo, launcher ou programa com
// anti-cheat, recusa carregar (DllMain devolve FALSE e o Windows a descarrega na hora). Pelo caminho do .exe, pelos
// arquivos ao lado dele e pelos módulos de motor/loja/anti-cheat já carregados (overlay da Steam, Unity, EOS, BattlEye...).
bool DentroDeJogo() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring c = jogos::Minusculo(p);
    // os apps do próprio WacOS (WacOSPropria) e o hospedeiro de 32 bits (rundll32) sempre podem
    if (c.find(L"wacos") != std::wstring::npos || c.find(L"\\rundll32.exe") != std::wstring::npos) return false;
    if (jogos::ProgramaDeJogo(p)) return true;
    // OBS Studio (jogos.h ProgramaNaoJogo): o overlay da Steam (OBS da Steam) e o XInput do navegador embutido não
    // fazem dele um jogo; só os módulos de anti-cheat contam
    if (jogos::ProgramaNaoJogo(p)) {
        for (auto m : { L"BEClient_x64.dll", L"BEClient.dll", L"EasyAntiCheat_EOS.dll", L"EasyAntiCheat_x64.dll", L"EasyAntiCheat.dll" })
            if (GetModuleHandleW(m)) return true;
        return false;
    }
    for (auto m : { L"GameOverlayRenderer64.dll", L"GameOverlayRenderer.dll", L"UnityPlayer.dll", L"GameAssembly.dll",
                    L"steam_api64.dll", L"steam_api.dll", L"EOSSDK-Win64-Shipping.dll", L"EOSSDK-Win32-Shipping.dll",
                    L"BEClient_x64.dll", L"BEClient.dll", L"EasyAntiCheat_EOS.dll", L"EasyAntiCheat_x64.dll", L"EasyAntiCheat.dll",
                    L"xinput1_3.dll", L"galaxy64.dll", L"discord_game_sdk.dll" })
        if (GetModuleHandleW(m)) return true;
    return false;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (DentroDeJogo()) return FALSE;
        g_mod = h;
        g_tls = TlsAlloc();
        g_tlsShow = TlsAlloc();
        g_tlsPos = TlsAlloc();
        DisableThreadLibraryCalls(h);
        // fica carregada até o programa fechar: se o Shell cair e o gancho sumir, as janelas que
        // usam o nosso código não podem perder a DLL (o programa travaria)
        HMODULE pin;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&DllMain, &pin);
    }
    return TRUE;
}
