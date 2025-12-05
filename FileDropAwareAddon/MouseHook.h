#pragma once
#include <windows.h>
#include <string>
#include <set>

#define WM_PERFORM_DRAG_CHECK	(WM_USER + 100)
#define WM_PERFORM_DRAG_RELEASE (WM_USER + 101)

class MouseHook
{
public:
public:
	static void InitMouseHook(std::set<std::wstring> supportedExtensions);
	static void UninitMouseHook();
	static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
protected:
private:
};
