#include "common/lib/util/Util.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#endif

#include <cstdarg>
#include <cstdio>

namespace coop {

// The first half is the system's: Windows for the game and the server players host from it, Linux for the dedicated
// server (docs/FORK.md, "Linux server"). The second half is the same code for both.
#ifdef _WIN32

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static double QpcPeriodMs() {
    static double period = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / (double)f.QuadPart;
    }();
    return period;
}

uint64_t NowMs() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * QpcPeriodMs());
}

double NowSeconds() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * QpcPeriodMs() / 1000.0;
}

std::wstring ModuleDir(void* module) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW((HMODULE)module, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 512ll * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
    CloseHandle(h);
    return ok;
}

bool WriteFileBytes(const std::wstring& path, const void* data, size_t size, bool durable) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = size == 0 || (WriteFile(h, data, (DWORD)size, &written, nullptr) && written == size);
    ok = ok && (!durable || FlushFileBuffers(h));  // a power cut left an empty profile
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void EnsureDirectory(const std::wstring& path) {
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t c = path[i];
        cur += c;
        if ((c == L'\\' || c == L'/') && cur.size() > 3) CreateDirectoryW(cur.c_str(), nullptr);
    }
    if (!cur.empty()) CreateDirectoryW(cur.c_str(), nullptr);
}

bool RemoveFile(const std::wstring& path) { return DeleteFileW(path.c_str()) != 0; }
bool RenameFile(const std::wstring& from, const std::wstring& to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
bool CopyFileOver(const std::wstring& from, const std::wstring& to) { return CopyFileW(from.c_str(), to.c_str(), FALSE) != 0; }
std::string NativePath(const std::wstring& path) { return WideToUtf8(path); }
void SleepMs(unsigned ms) { Sleep(ms); }

static void LowerWide(std::wstring& w) { CharLowerBuffW(w.data(), (DWORD)w.size()); }

#else  // ---------------------------------------------------------------------------------------------------- Linux

// wchar_t holds a whole code point here (UTF-32), two halves of one on Windows (UTF-16): the texts are the same up to
// U+FFFF, which is all a nickname may hold
std::wstring Utf8ToWide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp = 0xFFFD;  // what a broken sequence becomes, one byte at a time
        size_t n = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) n = 2, cp = c & 0x1F;
        else if ((c & 0xF0) == 0xE0) n = 3, cp = c & 0x0F;
        else if ((c & 0xF8) == 0xF0) n = 4, cp = c & 0x07;
        bool whole = i + n <= s.size();
        for (size_t k = 1; k < n && whole; ++k) {
            uint8_t t = (uint8_t)s[i + k];
            if ((t & 0xC0) != 0x80) whole = false;
            else cp = cp << 6 | (t & 0x3F);
        }
        if (!whole) cp = 0xFFFD, n = 1;
        out += (wchar_t)cp;
        i += n;
    }
    return out;
}

std::string WideToUtf8(std::wstring_view s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t w : s) {
        uint32_t cp = (uint32_t)w;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | cp >> 6);
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | cp >> 12);
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | cp >> 18);
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

static uint64_t MonotonicNs() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

uint64_t NowMs() { return MonotonicNs() / 1000000ull; }
double NowSeconds() { return (double)MonotonicNs() / 1e9; }

std::wstring ModuleDir(void*) {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    std::string p(buf, n > 0 ? (size_t)n : 0);
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? L"./" : Utf8ToWide(p.substr(0, slash + 1));
}

// The server's code joins its folders with backslashes ("world\\world.b" after the data folder): slashes here
std::string NativePath(const std::wstring& path) {
    std::string p = WideToUtf8(path);
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    int fd = open(NativePath(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 512ll * 1024 * 1024) {
        close(fd);
        return false;
    }
    out.resize((size_t)st.st_size);
    size_t got = 0;
    while (got < out.size()) {
        ssize_t n = read(fd, out.data() + got, out.size() - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    return got == out.size();
}

bool WriteFileBytes(const std::wstring& path, const void* data, size_t size, bool durable) {
    std::string to = NativePath(path), tmp = to + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t put = 0;
    while (put < size) {
        ssize_t n = write(fd, (const uint8_t*)data + put, size - put);
        if (n <= 0) break;
        put += (size_t)n;
    }
    bool ok = put == size && (!durable || fsync(fd) == 0);  // a power cut left an empty profile
    ok = close(fd) == 0 && ok;
    if (!ok) {
        unlink(tmp.c_str());
        return false;
    }
    return rename(tmp.c_str(), to.c_str()) == 0;
}

bool FileExists(const std::wstring& path) {
    struct stat st{};
    return stat(NativePath(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void EnsureDirectory(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::create_directories(NativePath(path), ec);
}

bool RemoveFile(const std::wstring& path) { return unlink(NativePath(path).c_str()) == 0; }
bool RenameFile(const std::wstring& from, const std::wstring& to) {
    return rename(NativePath(from).c_str(), NativePath(to).c_str()) == 0;
}
bool CopyFileOver(const std::wstring& from, const std::wstring& to) {
    std::vector<uint8_t> bytes;
    return ReadFileBytes(from, bytes) && WriteFileBytes(to, bytes.data(), bytes.size(), false);
}

void SleepMs(unsigned ms) {
    timespec t{(time_t)(ms / 1000), (long)(ms % 1000) * 1000000l};
    nanosleep(&t, nullptr);
}

// Latin and Cyrillic, the letters a nickname may hold (SanitizeNickname), and the accented Latin ones of Latin-1
static void LowerWide(std::wstring& w) {
    for (wchar_t& c : w) {
        if ((c >= L'A' && c <= L'Z') || (c >= 0x0410 && c <= 0x042F) || (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)) c += 0x20;
        else if (c >= 0x0400 && c <= 0x040F) c += 0x50;
    }
}

#endif  // ------------------------------------------------------------------------------------- the same for both

uint32_t Crc32(const void* data, size_t size, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::string Trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return std::string(s.substr(a, b - a));
}

std::string ToLower(std::string_view s) {
    std::wstring w = Utf8ToWide(s);
    LowerWide(w);
    return WideToUtf8(w);
}

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string Format(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    if ((size_t)n < sizeof(buf)) return std::string(buf, n);
    std::string out(n, '\0');
    va_start(ap, fmt);
    vsnprintf(out.data(), n + 1, fmt, ap);
    va_end(ap);
    return out;
}

bool ReservedFileName(std::string_view name) {
    std::string n = ToLower(std::string(name.substr(0, name.find('.'))));
    static const char* const kDevices[] = {"con", "prn", "aux", "nul"};
    for (const char* d : kDevices)
        if (n == d) return true;
    return n.size() == 4 && (n.compare(0, 3, "com") == 0 || n.compare(0, 3, "lpt") == 0) && n[3] >= '1' && n[3] <= '9';
}

std::string SanitizeNickname(std::string_view nick) {
    std::wstring w = Utf8ToWide(Trim(nick));
    std::wstring out;
    for (wchar_t c : w) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                  (c >= 0x0410 && c <= 0x044F) || c == 0x0401 || c == 0x0451 || c == L'_' || c == L'-' ||
                  c == L'.' || c == L'[' || c == L']';
        if (ok) out += c;
        if (out.size() >= 20) break;
    }
    return WideToUtf8(out);
}

}  // namespace coop
