// See python_fetch_win.h. Replaces bootstrap-windows.ps1 (until 0.2.6 the setup started PowerShell with
// -ExecutionPolicy Bypass for this, which antivirus heuristics read as a dropper; issue #58).
#ifdef _WIN32

#include "python_fetch_win.h"

#include "unzip_min.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4  // Windows 8.1+
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace {

std::wstring wide(const std::string& u) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
    return w;
}

std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u.data(), n, nullptr, nullptr);
    return u;
}

std::string env(const wchar_t* name) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(name, buf, 32768);
    return n > 0 && n < 32768 ? utf8(std::wstring(buf, n)) : std::string();
}

bool exists(const std::string& p) { return GetFileAttributesW(wide(p).c_str()) != INVALID_FILE_ATTRIBUTES; }

// "the server name or address could not be resolved (WinHTTP error 12007)"
std::string error_text(DWORD code) {
    wchar_t* msg = nullptr;
    HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
    bool http = code >= 12000 && code < 13000 && winhttp;
    if (http) flags |= FORMAT_MESSAGE_FROM_HMODULE;
    DWORD n = FormatMessageW(flags, http ? winhttp : nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::string s = n && msg ? utf8(msg) : "";
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return (s.empty() ? std::string("error") : s) + " (" + (http ? "WinHTTP " : "") + "error " + std::to_string(code) + ")";
}

struct Inet {
    HINTERNET h = nullptr;
    ~Inet() {
        if (h) WinHttpCloseHandle(h);
    }
};

// The whole file into out (the embeddable Python is about 11 MB). Uses the system's proxy settings.
bool download(const std::string& url, std::string& out, std::string& err) {
    std::wstring wurl = wide(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
        return err = "the download address is not an https URL: " + url, false;
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    Inet session, connect, request;
    session.h = WinHttpOpen(L"Wind Waker HD setup", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h)  // before Windows 8.1
        session.h = WinHttpOpen(L"Wind Waker HD setup", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) return err = "WinHTTP could not start: " + error_text(GetLastError()), false;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // no TLS 1.3 in this Windows
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    WinHttpSetTimeouts(session.h, 0, 30000, 30000, 60000);
    connect.h = WinHttpConnect(session.h, host.c_str(), uc.nPort, 0);
    if (connect.h)
        request.h = WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request.h || !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr))
        return err = "the download from " + utf8(host) + " failed: " + error_text(GetLastError()), false;
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return err = "the server answered HTTP " + std::to_string(status) + " for " + url, false;
    out.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail))
            return err = "the download was interrupted: " + error_text(GetLastError()), false;
        if (avail == 0) break;
        if (out.size() + avail > (256u << 20)) return err = "the download is far larger than expected", false;
        size_t at = out.size();
        out.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, out.data() + at, avail, &got))
            return err = "the download was interrupted: " + error_text(GetLastError()), false;
        out.resize(at + got);
    }
    return true;
}

// lower-case hex SHA-256 (Windows CNG)
bool sha256_hex(const std::string& data, std::string& hex) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char sum[32];
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0;
    for (size_t at = 0; ok && at < data.size(); at += 1u << 24) {
        ULONG n = (ULONG)std::min<size_t>(data.size() - at, 1u << 24);
        ok = BCryptHashData(h, (PUCHAR)data.data() + at, n, 0) == 0;
    }
    ok = ok && BCryptFinishHash(h, sum, sizeof sum, 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    hex.clear();
    for (unsigned char c : sum) {
        char b[3];
        snprintf(b, sizeof b, "%02x", c);
        hex += b;
    }
    return ok;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// one argument for CreateProcess's command line (the rules CommandLineToArgvW and the C runtime parse)
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (size_t i = 0;; i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') i++, bs++;
        if (i == a.size()) {
            q.append(bs * 2, L'\\');
            break;
        }
        if (a[i] == L'"') q.append(bs * 2 + 1, L'\\');
        else q.append(bs, L'\\');
        q += a[i];
    }
    return q + L"\"";
}

}  // namespace

std::string python_data_dir(const std::string& pkg) {
    std::string d = env(L"WWHD_DATA_DIR");
    if (!d.empty()) return d;
    if (exists(pkg + "portable.txt")) return pkg + "data";  // portable release: everything in the release folder
    return env(L"LOCALAPPDATA") + "\\WWHD";
}

bool fetch_python(const std::string& pkg, const PythonPin& pin, std::string& python_exe, std::string& err,
                  const std::function<void(const std::string&)>& say) {
    std::string pydir = python_data_dir(pkg) + "\\python\\" + pin.dir;
    python_exe = pydir + "\\python.exe";
    if (exists(python_exe)) return true;
    say("Getting Python for the setup (about 11 MB, once)...");
    std::string zip, sum;
    if (!download(pin.url, zip, err)) return false;
    if (!sha256_hex(zip, sum)) return err = "Windows could not compute the SHA-256 of the download", false;
    if (sum != lower(pin.sha256))
        return err = "The Python download is corrupt or was changed (SHA-256 mismatch). Nothing was installed.", false;
    // python.exe is written last: when it is there, the rest is too
    if (!unzip_to_folder(zip, pydir, "python.exe", err)) return false;
    return true;
}

namespace {

HANDLE g_out = INVALID_HANDLE_VALUE;

void print(const std::string& s) {
    std::string l = s + "\r\n";
    DWORD n = 0, mode = 0;
    if (g_out == INVALID_HANDLE_VALUE) return;
    if (GetConsoleMode(g_out, &mode)) {
        std::wstring w = wide(l);
        WriteConsoleW(g_out, w.data(), (DWORD)w.size(), &n, nullptr);
    } else {
        WriteFile(g_out, l.data(), (DWORD)l.size(), &n, nullptr);
    }
}

// the console keeps Ctrl+C for setup.py (KeyboardInterrupt); this program waits for it to finish
BOOL WINAPI ignore_ctrl(DWORD) { return TRUE; }

// an inheritable copy of a standard handle given to this program, or INVALID_HANDLE_VALUE
HANDLE given(DWORD which) {
    HANDLE h = GetStdHandle(which), dup = INVALID_HANDLE_VALUE;
    if (!h || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN) return INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return INVALID_HANDLE_VALUE;
    return dup;
}

HANDLE open_console(const wchar_t* name) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0,
                       nullptr);
}

}  // namespace

int console_setup(const std::string& pkg, const PythonPin* pin, const std::string& pin_error,
                  const std::vector<std::string>& args) {
    // This program is a GUI program: it has no console of its own. Output redirected by the caller (a pipe or
    // a file) is used as it is; otherwise setup.py talks to the console window of the .bat that started it.
    HANDLE in = given(STD_INPUT_HANDLE), out = given(STD_OUTPUT_HANDLE), errh = given(STD_ERROR_HANDLE);
    bool console = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (!console && in == INVALID_HANDLE_VALUE && out == INVALID_HANDLE_VALUE) console = AllocConsole() != 0;
    if (console) {
        if (in == INVALID_HANDLE_VALUE) in = open_console(L"CONIN$");
        if (out == INVALID_HANDLE_VALUE) out = open_console(L"CONOUT$");
        if (errh == INVALID_HANDLE_VALUE) errh = open_console(L"CONOUT$");
    }
    g_out = out;

    std::string python, err;
    if (pkg.empty()) {
        err = "the release files were not found (keep this program in the unzipped release folder, next to tools\\)";
    } else if (!pin) {
        err = pin_error;
    } else {
        fetch_python(pkg, *pin, python, err, print);
    }
    if (!err.empty()) {
        print("");
        print("Setup could not get Python: " + err);
        print("Check your internet connection and run the setup again.");
        return 1;
    }

    SetEnvironmentVariableW(L"PYTHONDONTWRITEBYTECODE", L"1");
    std::wstring cmd = quote_arg(wide(python)) + L" " + quote_arg(wide(pkg + "tools\\installer\\setup.py"));
    for (auto& a : args) cmd += L" " + quote_arg(wide(a));
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = errh;
    PROCESS_INFORMATION pi = {};
    // no console of its own: the output goes where ours goes (a pipe), without a window
    DWORD flags = console ? 0 : CREATE_NO_WINDOW;
    if (!CreateProcessW(wide(python).c_str(), cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi)) {
        print("Setup could not start " + python + ": " + error_text(GetLastError()));
        return 1;
    }
    SetConsoleCtrlHandler(ignore_ctrl, TRUE);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

#endif
