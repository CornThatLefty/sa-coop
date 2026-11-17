// coop-server.exe — SA:COOP relay server.
//
//   coop-server.exe [--port N] [--name "..."] [--password X] [--world <save file> | --new-game] [--host <nick>]
//                   [--friendly-fire 0|1] [--partner-teleport 0|1] [--shared-life 0|1] [--partner-cheats 0|1]
//                   [--config server.ini] [--bind <address>] [--dedicated]
//                   [--data <dir>]
//
// Linux (the dedicated server, docs/FORK.md "Linux server"): the same options as ./coop-server; SIGINT and SIGTERM stop
// it as the console's quit does.
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <csignal>
#endif

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "app/server/Server.h"
#include "common/config/version/Version.h"
#include "common/lib/ini/Ini.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "features/balance/BalanceConfig.h"

using namespace coop;

static server::Server g_server;

#ifdef _WIN32
static const wchar_t kSlash = L'\\';
static BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT) {
        g_server.Stop();
        Sleep(300);
        return TRUE;
    }
    return FALSE;
}
#else
static const wchar_t kSlash = L'/';
static void OnSignal(int) { g_server.Stop(); }  // sets a flag, no more: the network thread ends its loop and cleans up
#endif

static void WriteDefaultConfig(const std::wstring& path) {
    const char* text =
        "; Настройки сервера SA:COOP\r\n"
        "[server]\r\n"
        "name = SA:COOP server\r\n"
        "port = 7788\r\n"
        "; Оставьте пустым, если пароль не нужен.\r\n"
        "password =\r\n"
        "; Сколько игроков в игре: от 2 до 4.\r\n"
        "max_players = 2\r\n"
        "; Ник игрока, который всегда будет хостом. Если пусто, хостом становится тот, кто зашёл первым.\r\n"
        "host =\r\n"
        "motd = Добро пожаловать! Чтобы увидеть список команд, напишите /help.\r\n"
        "; Поставьте 1, чтобы игроки могли ранить и убивать друг друга.\r\n"
        "friendly_fire = 0\r\n"
        "; Поставьте 0, чтобы напарники не могли перемещаться командами /tp и /tphere.\r\n"
        "partner_teleport = 1\r\n"
        "; Поставьте 1, чтобы гибель напарника проваливала сюжетную миссию хоста.\r\n"
        "shared_life = 0\r\n"
        "; Поставьте 0, чтобы у напарников не работали чит-коды.\r\n"
        "partner_cheats = 1\r\n"
        "; Игра через интернет (UPnP, код приглашения, пробивка NAT). Если поставить 0, останется только локальная сеть.\r\n"
        "internet = 1\r\n"
        "; Ретранслятор на случай, если друг не может подключиться по коду.\r\n"
        "; С бесплатным аккаунтом metered.ca:   turn = metered:<имя приложения>:<API-ключ>\r\n"
        "; С любым TURN-сервером:               turn = turn:<адрес>:<порт>:<логин>:<пароль>\r\n"
        "turn =\r\n";
    WriteFileBytes(path, text, strlen(text));
}

#ifndef _WIN32
static bool Writable(const std::wstring& dir) {
    EnsureDirectory(dir);
    std::wstring probe = dir + L"write-test";  // WriteFileBytes makes write-test.tmp and renames it
    if (!WriteFileBytes(probe, "", 0, false)) return false;
    RemoveFile(probe);
    return true;
}
#else
static bool Writable(const std::wstring& dir) {
    EnsureDirectory(dir);
    std::wstring probe = dir + L"write-test.tmp";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// The server runs as its invoker with no file virtualization (its manifest, since 0.3.0): in a game under Program Files
// it wrote nothing, and the partners lost their progress every session. Then its data goes to the user's own folder,
// and what 0.2.x wrote to the VirtualStore (UAC's redirection of those writes) comes along once.
static std::wstring UserDataDir(const std::wstring& wanted) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return L"";
    std::wstring dir = std::wstring(buf) + L"\\SA-COOP\\server-data\\";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) && wanted.size() > 3 && wanted[1] == L':') {
        std::wstring virtualStore = std::wstring(buf) + L"\\VirtualStore\\" + wanted.substr(3);
        if (std::filesystem::exists(virtualStore, ec)) {
            std::filesystem::create_directories(dir, ec);
            std::filesystem::copy(virtualStore, dir, std::filesystem::copy_options::recursive, ec);
        }
    }
    return Writable(dir) ? dir : L"";
}

// The game's voice chat asks first whether Windows' audio drivers survive being loaded (issue #80): on some machines the
// first WinMM call crashes in them, and in the game it took the game down. Here the crash is only the answer.
static LONG WINAPI ProbeCrashed(EXCEPTION_POINTERS* e) {
    TerminateProcess(GetCurrentProcess(), e->ExceptionRecord->ExceptionCode);  // no dialog: the code is the answer
    return EXCEPTION_EXECUTE_HANDLER;
}
static int ProbeAudio() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(ProbeCrashed);
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = 16000;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = 32000;
    HWAVEIN in = nullptr;
    if (waveInOpen(&in, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) waveInClose(in);
    HWAVEOUT out = nullptr;
    if (waveOutOpen(&out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) waveOutClose(out);
    return 0;
}
#endif

static int ServerMain(int argc, wchar_t** argv) {
#ifdef _WIN32
    if (argc == 2 && !wcscmp(argv[1], L"--probe-audio")) return ProbeAudio();
#endif
    // --write-config <server.ini>: the file as a first start writes it, every key at its preset, and
    // balance_exclude.ini beside it, then exits. The package's build makes the server.ini it ships so (tools/build.py):
    // its settings can be edited before the server's first start. An existing file keeps its values.
    if (argc == 3 && !wcscmp(argv[1], L"--write-config")) {
        std::wstring path = argv[2];
        if (!FileExists(path)) WriteDefaultConfig(path);
        server::WriteBalanceDefaults(path);
        return FileExists(path) && FileExists(server::BalanceExcludePath(path)) ? 0 : 1;
    }
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"SA:COOP server");
#endif

    std::wstring dir = ModuleDir();
    std::wstring configPath = dir + L"server.ini";
    std::wstring dataDir = dir + L"server-data" + kSlash;
    for (int i = 1; i + 1 < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--config") configPath = argv[++i];
        if (a == L"--data") {
            dataDir = argv[++i];
            if (!dataDir.empty() && dataDir.back() != L'\\' && dataDir.back() != L'/') dataDir += kSlash;
        }
    }
#ifdef _WIN32
    std::wstring wantedDir = dataDir;
    if (!Writable(dataDir))
        if (std::wstring own = UserDataDir(dataDir); !own.empty()) dataDir = own;
    log::Open(dataDir + L"server.log", true);
    if (dataDir != wantedDir) LOGW("Нет записи в %s: данные сервера в %s", WideToUtf8(wantedDir).c_str(), WideToUtf8(dataDir).c_str());
#else
    if (!Writable(dataDir)) {
        fprintf(stderr, "coop-server: no write access to the data folder %s (--data <dir> names another)\n", NativePath(dataDir).c_str());
        return 1;
    }
    log::Open(dataDir + L"server.log", true);
#endif

    if (!FileExists(configPath)) WriteDefaultConfig(configPath);
    Ini ini;
    ini.Load(configPath);
    server::Config cfg;
    cfg.name = ini.Get("server", "name", cfg.name);
    cfg.port = (uint16_t)ini.GetInt("server", "port", kDefaultPort);
    cfg.bind = Trim(ini.Get("server", "bind", ""));
    cfg.password = ini.Get("server", "password", "");
    cfg.maxPlayers = ini.GetInt("server", "max_players", 2);
    cfg.hostName = ini.Get("server", "host", "");
    cfg.motd = ini.Get("server", "motd", cfg.motd);
    cfg.friendlyFire = ini.GetBool("server", "friendly_fire", false);
    cfg.partnerTeleport = ini.GetBool("server", "partner_teleport", true);
    cfg.sharedLife = ini.GetBool("server", "shared_life", false);
    cfg.partnerCheats = ini.GetBool("server", "partner_cheats", true);
    cfg.internet = ini.GetBool("server", "internet", true);
    cfg.turn = Trim(ini.Get("server", "turn", ""));
    cfg.dedicated = ini.GetBool("server", "dedicated", false);
    for (auto& nick : Split(ini.Get("server", "admins", ""), ','))
        if (std::string n = ToLower(Trim(nick)); !n.empty()) cfg.admins.push_back(n);
    cfg.adminPassword = ini.Get("server", "admin_password", "");
    cfg.snapshotMinutes = ini.GetFloat("server", "snapshot_minutes", 5.0f);
    cfg.dataDir = dataDir;
    cfg.configPath = configPath;

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? WideToUtf8(argv[++i]) : std::string(); };
        if (a == L"--port") cfg.port = (uint16_t)atoi(next().c_str());
        else if (a == L"--name") cfg.name = next();
        else if (a == L"--bind") cfg.bind = next();
        else if (a == L"--password") cfg.password = next();
        else if (a == L"--host") cfg.hostName = next();
        else if (a == L"--max") cfg.maxPlayers = atoi(next().c_str());
        else if (a == L"--world" && i + 1 < argc) cfg.worldFile = argv[++i];
        else if (a == L"--new-game") cfg.newGame = true;
        else if (a == L"--dedicated") cfg.dedicated = true;
        else if (a == L"--friendly-fire") cfg.friendlyFire = next() == "1";
        else if (a == L"--partner-teleport") cfg.partnerTeleport = next() == "1";
        else if (a == L"--shared-life") cfg.sharedLife = next() == "1";
        else if (a == L"--partner-cheats") cfg.partnerCheats = next() == "1";
        else if (a == L"--config" || a == L"--data") ++i;
    }

    LOGI("%s сервер %s (протокол %u)", COOP_NAME, COOP_VERSION_STR, kProtocolVersion);
#ifdef _WIN32
    if (!g_server.Start(cfg)) {
        LOGE("Сервер не запущен. Нажмите Enter для выхода.");
        getchar();
        return 1;
    }
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
#else
    if (!g_server.Start(cfg)) {
        LOGE("The server did not start.");  // no wait for a key: a service manager starts it again or reports it
        return 1;
    }
    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    signal(SIGPIPE, SIG_IGN);  // a console that went away is no reason to die
#endif
    LOGI("Команды консоли: players, kick <ник или номер> [причина], say <текст>, reloadbalance, quit");

    std::thread input([] {
#ifdef _WIN32
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        wchar_t buf[512];
        DWORD read = 0;
        while (ReadConsoleW(in, buf, 511, &read, nullptr) && read > 0) {
            std::wstring line(buf, read);
            while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
            if (!line.empty()) g_server.QueueConsoleCommand(WideToUtf8(line));
        }
#else
        // a terminal's lines; as a service the input is closed and this thread ends at once
        std::string line;
        while (std::getline(std::cin, line)) {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (!line.empty()) g_server.QueueConsoleCommand(line);
        }
#endif
    });
    input.detach();

    g_server.Run();
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return ServerMain(argc, argv); }
#else
int main(int argc, char** argv) {
    std::vector<std::wstring> wide;
    for (int i = 0; i < argc; ++i) wide.push_back(Utf8ToWide(argv[i]));
    std::vector<wchar_t*> ptrs;
    for (auto& w : wide) ptrs.push_back(w.data());
    ptrs.push_back(nullptr);
    return ServerMain(argc, ptrs.data());
}
#endif
