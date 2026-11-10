// ============================================================================
// file_dialog.cpp
// ============================================================================
#include "file_dialog.hpp"

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#else
#include <sys/wait.h>
#endif

namespace ch::import {

#ifdef __EMSCRIPTEN__
// The page's buttons do this on the web (web/shell.html).
void openFileDialog() {}
bool fileDialogOpen() { return false; }
std::vector<std::string> takeChosenFiles(std::string* error) {
    if (error) error->clear();
    return {};
}
#else
namespace {
std::mutex& lock() { static std::mutex m; return m; }
std::vector<std::string>& chosen() { static std::vector<std::string> v; return v; }
std::string& failure() { static std::string s; return s; }
std::atomic<bool>& busy() { static std::atomic<bool> b{false}; return b; }

#ifdef _WIN32
std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::vector<std::string> run(std::string& error) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::wstring buf(64 * 1024, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"All files\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = DWORD(buf.size());
    ofn.lpstrTitle = L"Add game files";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    std::vector<std::string> out;
    if (GetOpenFileNameW(&ofn)) {
        // One file: its full path. Several: the folder, then each name, each ending in a NUL.
        const std::wstring first(buf.c_str());
        const wchar_t* p = buf.c_str() + first.size() + 1;
        if (!*p) out.push_back(utf8(first));
        for (; *p; p += std::wcslen(p) + 1) out.push_back(utf8(first + L"\\" + p));
    } else if (const DWORD e = CommDlgExtendedError(); e != 0) {
        error = "the file dialog failed (" + std::to_string(e) + ")";
    }
    CoUninitialize();
    return out;
}
#else
// A command's output, one path per line; false when it could not be run.
bool lines(const char* cmd, std::vector<std::string>& out) {
    FILE* f = popen(cmd, "r");
    if (!f) return false;
    std::string line;
    for (int c; (c = std::fgetc(f)) != EOF;) {
        if (c != '\n') { line.push_back(char(c)); continue; }
        if (!line.empty()) out.push_back(line);
        line.clear();
    }
    if (!line.empty()) out.push_back(line);
    const int status = pclose(f);
    return status != -1 && !(WIFEXITED(status) && WEXITSTATUS(status) == 127);   // 127: no such command
}

std::vector<std::string> run(std::string& error) {
    std::vector<std::string> out;
#ifdef __APPLE__
    if (lines("osascript -e 'set fs to choose file with prompt \"Add game files\" with multiple selections allowed' "
              "-e 'set out to \"\"' -e 'repeat with f in fs' -e 'set out to out & POSIX path of f & linefeed' "
              "-e 'end repeat' -e 'out' 2>/dev/null", out))
        return out;
#else
    if (lines("zenity --file-selection --multiple --separator='\n' --title='Add game files' 2>/dev/null", out)) return out;
    out.clear();
    if (lines("kdialog --getopenfilename --multiple --separate-output . 2>/dev/null", out)) return out;
#endif
    error = "no file dialog found (install zenity or kdialog), drop the files onto the window instead";
    return {};
}
#endif
} // namespace

void openFileDialog() {
    if (busy().exchange(true)) return;
    std::thread([] {
        std::string error;
        auto files = run(error);
        {
            std::lock_guard g(lock());
            for (auto& f : files) chosen().push_back(std::move(f));
            failure() = std::move(error);
        }
        busy() = false;
    }).detach();
}

bool fileDialogOpen() { return busy(); }

std::vector<std::string> takeChosenFiles(std::string* error) {
    std::lock_guard g(lock());
    if (error) *error = std::exchange(failure(), {});
    return std::exchange(chosen(), {});
}
#endif

} // namespace ch::import
