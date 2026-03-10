#ifndef FEMM_COMPAT_TYPES_H
#define FEMM_COMPAT_TYPES_H

// Portable type definitions for non-Windows platforms.
// On Windows, these come from <windows.h> via MFC headers.

#ifndef _WIN32

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <string>
#include <thread>
#include <chrono>

// MSVC min/max macros
#ifndef __min
#define __min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef __max
#define __max(a,b) ((a) > (b) ? (a) : (b))
#endif

// Windows type shims
#ifndef BOOL
#define BOOL int
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef PSTR
#define PSTR char*
#endif
#ifndef UINT
#define UINT unsigned int
#endif
#ifndef DWORD
#define DWORD unsigned long
#endif

// MFC constants not in resource.h
#ifndef IDCANCEL
#define IDCANCEL 2
#endif
#ifndef OFN_HIDEREADONLY
#define OFN_HIDEREADONLY 0
#endif
#ifndef OFN_OVERWRITEPROMPT
#define OFN_OVERWRITEPROMPT 0
#endif

// MSVC string function aliases
#include <strings.h>
#define _strnicmp strncasecmp
#define _stricmp strcasecmp

// Windows API replacements
inline void Sleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline int DeleteFile(const char* path) {
    return std::remove(path) == 0;
}

inline int IsWindow(void* h) {
    return h != nullptr ? 1 : 0;
}

// MsgBox - prints to stderr on non-Windows
inline int MsgBox(const char* s) {
    fprintf(stderr, "%s\n", s);
    return 0;
}

inline int MsgBox(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    return 0;
}

// Minimal CString shim for solver code compatibility
class CString {
public:
    CString() {}
    CString(const char* s) : m_str(s ? s : "") {}
    CString(const std::string& s) : m_str(s) {}

    operator const char*() const { return m_str.c_str(); }
    const char* c_str() const { return m_str.c_str(); }

    int GetLength() const { return (int)m_str.length(); }
    CString Left(int n) const { return CString(m_str.substr(0, n)); }
    CString Mid(int first) const { return CString(m_str.substr(first)); }
    CString Mid(int first, int count) const { return CString(m_str.substr(first, count)); }
    CString Right(int n) const { return CString(m_str.substr(m_str.length() - n)); }
    int Find(char c, int start = 0) const {
        auto pos = m_str.find(c, start);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    void MakeUpper() { for (auto& c : m_str) c = toupper(c); }
    void MakeLower() { for (auto& c : m_str) c = tolower(c); }
    void Empty() { m_str.clear(); }
    bool IsEmpty() const { return m_str.empty(); }

    CString& operator=(const char* s) { m_str = s ? s : ""; return *this; }
    CString& operator=(const std::string& s) { m_str = s; return *this; }

    bool operator==(const char* s) const { return m_str == (s ? s : ""); }
    bool operator!=(const char* s) const { return !(*this == s); }

    void Format(const char* fmt, ...) {
        char buf[4096];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        m_str = buf;
    }

    CString& operator+=(const char* s) { if(s) m_str += s; return *this; }
    CString& operator+=(const CString& s) { m_str += s.m_str; return *this; }

    friend CString operator+(const CString& a, const CString& b) {
        return CString(a.m_str + b.m_str);
    }
    friend CString operator+(const CString& a, const char* b) {
        return CString(a.m_str + (b ? b : ""));
    }
    friend CString operator+(const char* a, const CString& b) {
        return CString(std::string(a ? a : "") + b.m_str);
    }

private:
    std::string m_str;
};

// CFileDialog shim - not used in CLI mode, but needed for compilation
class CWnd {
public:
    void* m_hWnd = (void*)1;
};

class CFileDialog {
public:
    CFileDialog(int, const char*, void*, int, const char*, void*) {}
    int DoModal() { return IDCANCEL; }
    CString GetPathName() { return CString(""); }
};

// __argc/__argv shims (available on MSVC, need portability)
#ifndef __argc
extern int __argc_compat;
extern char** __argv_compat;
#define __argc __argc_compat
#define __argv __argv_compat
#endif

#endif // !_WIN32

#endif // FEMM_COMPAT_TYPES_H
