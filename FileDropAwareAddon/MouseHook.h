#pragma once
#include <windows.h>
#include <string>
#include <set>
#include <node.h>

#define WM_PERFORM_DRAG_CHECK	(WM_USER + 100)
#define WM_PERFORM_DRAG_RELEASE (WM_USER + 101)

class MouseHook
{
public:
	static v8::Local<v8::Function> m_fileDropCallback;
	static v8::Isolate* m_Isolate;
public:
	static void InitMouseHook(std::set<std::wstring> supportedExtensions);
	static void UninitMouseHook();
	static void SetFileDropCallback(v8::Local<v8::Function> callback);
	static void SetIsolate(v8::Isolate* isolate);
	static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
protected:
private:
};
