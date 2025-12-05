#pragma once
#include <string>
#include <set>

//#include <shldisp.h>
//#include <exdisp.h>
//#include <shlwapi.h>
#include <atlbase.h> // 使用 CComPtr 简化 COM 内存管理

class FileDetector
{
private:
    static std::set<std::wstring> m_Extensions;
public:
    FileDetector();
    ~FileDetector();
public:
    static bool ComInitialize();
    static void ComUninitialize();
    static void SetExtensions(const std::set<std::wstring>& extensions);
    static bool IsDraggingSupportedFile();
private:
    static bool IsTargetExtension(const std::wstring& path);
    static bool HasValidSelection(IDispatch* pDispWindow);
    static bool IsContentArea(HWND hWnd, const POINT& mousePos, bool isDesktop);
    static HWND FindShellParent(HWND hWnd, bool& isDesktop);
};