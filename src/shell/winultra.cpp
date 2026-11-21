// WinUltra: a ilha de vidro (Dynamic Island) de github.com/Gxmi2006/WinUltra (MIT, Gamindu Sasmitha), embutida no
// WacOS. O código fica em src\winultra (C# WPF, compilado pelo build.cmd em bin\WinUltra.exe); aqui o Shell só a abre
// com --wacos (sem a janela de primeira execução; anima no ritmo da tela) e a fecha ao sair.
// Ajuste winultra=1 (padrão; Ajustes do Sistema > Ilha Dinâmica). Uma instância só: o próprio WinUltra tem um mutex
// (Local\WinUltraFriends) e fecha pelo evento Local\WinUltraFriendsQuit.
#include "shell.h"

namespace {
bool g_ours;  // fomos nós que abrimos (ao sair, só fecha o que abriu)

bool Running() {
    if (HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\WinUltraFriends")) { CloseHandle(m); return true; }
    return false;
}

bool Wanted() { return shell::Setting(L"winultra", L"1") == L"1"; }
}

void winultra::Start() {
    if (!Wanted() || Running()) return;
    std::wstring exe = shell::dir + L"\\WinUltra.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) { Log(L"winultra: %ls não existe", exe.c_str()); return; }
    std::wstring cmd = L"\"" + exe + L"\" --wacos";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, shell::dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        g_ours = true;
        Log(L"winultra: aberto (pid %lu)", pi.dwProcessId);
    } else Log(L"winultra: não abriu (erro %lu)", GetLastError());
}

void winultra::Stop() {
    if (!g_ours) return;
    g_ours = false;
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\WinUltraFriendsQuit")) {
        SetEvent(ev); CloseHandle(ev);
        Log(L"winultra: fechado");
    }
}

void winultra::Reload() {
    if (Wanted()) Start();
    else if (Running()) { g_ours = true; Stop(); }
}
