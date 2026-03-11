#ifndef FEMM_COMPAT_MFC_H
#define FEMM_COMPAT_MFC_H

// Portable MFC dialog shims for non-Windows platforms.
// These provide the same API surface that solver code calls through TheView.

#ifndef _WIN32

#include "compat_types.h"
#include <cstdio>
#include <functional>

// Progress bar shim - prints progress to stderr, or calls callback if set
struct CProgressCtrl {
    std::function<void(int)> onProgress;
    void SetPos(int pos) {
        if (onProgress) { onProgress(pos); return; }
        // Fallback: print progress percentage to stderr
        if (pos > 0 && (pos % 10 == 0)) {
            fprintf(stderr, "  Progress: %d%%\n", pos);
        }
    }
};

// CDataExchange shim
struct CDataExchange {};

// Base dialog shim with common solver interface
class CSolverDlg {
public:
    CProgressCtrl m_prog1;
    CProgressCtrl m_prog2;
    void* m_hWnd = (void*)1;  // always "valid"

    std::function<void(int, const char*)> onStatusText;
    std::function<void(const char*)> onLogMessage;

    void SetDlgItemText(int id, const char* text) {
        if (onStatusText) { onStatusText(id, text); return; }
        if (text && text[0]) {
            fprintf(stderr, "%s\n", text);
        }
    }

    void LogMessage(const char* text) {
        if (onLogMessage) { onLogMessage(text); return; }
        if (text && text[0]) {
            fprintf(stderr, "%s\n", text);
        }
    }

    void SetWindowText(const char* text) {
        if (text && text[0]) {
            fprintf(stderr, "%s\n", text);
        }
    }

    void InvalidateRect(void*, int) {}
    void UpdateWindow() {}

protected:
    virtual void DoDataExchange(CDataExchange*) {}
};

// Solver-specific dialog shims - all inherit from CSolverDlg
class CFknDlg : public CSolverDlg {
public:
    CFknDlg(void* = nullptr) {}
    CString ComLine;
};

class CbelasolvDlg : public CSolverDlg {
public:
    CbelasolvDlg(void* = nullptr) {}
    CString ComLine;
};

class ChsolvDlg : public CSolverDlg {
public:
    ChsolvDlg(void* = nullptr) {}
    CString ComLine;
};

class CcsolvDlg : public CSolverDlg {
public:
    CcsolvDlg(void* = nullptr) {}
    CString ComLine;
};

// CArray shim used in cuthill.cpp
template<typename T, typename U>
class CArray {
public:
    CArray() : m_data(nullptr), m_size(0), m_capacity(0) {}
    ~CArray() { delete[] m_data; }

    int GetSize() const { return m_size; }

    void SetSize(int size) {
        if (size > m_capacity) {
            T* newdata = new T[size]();
            if (m_data) {
                for (int i = 0; i < m_size && i < size; i++)
                    newdata[i] = m_data[i];
                delete[] m_data;
            }
            m_data = newdata;
            m_capacity = size;
        }
        m_size = size;
    }

    void Add(const T& val) {
        if (m_size >= m_capacity) {
            int newcap = m_capacity > 0 ? m_capacity * 2 : 16;
            T* newdata = new T[newcap]();
            for (int i = 0; i < m_size; i++)
                newdata[i] = m_data[i];
            delete[] m_data;
            m_data = newdata;
            m_capacity = newcap;
        }
        m_data[m_size++] = val;
    }

    T& operator[](int i) { return m_data[i]; }
    const T& operator[](int i) const { return m_data[i]; }

    void RemoveAll() { m_size = 0; }

private:
    T* m_data;
    int m_size;
    int m_capacity;
};

#endif // !_WIN32

#endif // FEMM_COMPAT_MFC_H
