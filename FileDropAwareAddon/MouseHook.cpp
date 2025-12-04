#include "MouseHook.h"
#include "FileDetector.h"
#include <iostream>

// 全局主窗口句柄（用于发送消息）
static DWORD	g_mainThreadId		= 0;
static bool		g_isLButtonDown		= false;
static bool		g_isDragging		= false;
static bool		g_detectionCalled	= false;
static bool		g_supportedFile		= false;
static POINT	g_dragStartPos		= { 0, 0 };
// 系统拖拽阈值
static int		g_minDragX			= 0;
static int		g_minDragY			= 0;
// 钩子句柄
static HHOOK	g_mouseHook			= NULL;

v8::Local<v8::Function> MouseHook::m_fileDropCallback = v8::Local<v8::Function>();
v8::Isolate* MouseHook::m_Isolate = NULL;

extern void LogInfo(const std::wstring& info);
extern void LogError(const std::wstring& error);

void MouseHook::InitMouseHook(std::set<std::wstring> supportedExtensions) {
	LogInfo(L"Init mouse hook, monitoring mouse... Drag a file (e.g., .txt) to see detection.");
	if (!FileDetector::ComInitialize())
	{
		LogError(L"Failed to initialize COM! Error: " + std::to_wstring(GetLastError()));
		return;
	}
	FileDetector::SetExtensions(supportedExtensions);

	g_mainThreadId = GetCurrentThreadId();
	
	g_minDragX = GetSystemMetrics(SM_CXDRAG);
	g_minDragY = GetSystemMetrics(SM_CYDRAG);
	
	LogInfo(L"Mouse drag threshold: " + std::to_wstring(g_minDragX) + L"px");

	g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
	if (g_mouseHook == NULL)
	{
		LogError(L"Failed to install hook! Error: " + std::to_wstring(GetLastError()));
		FileDetector::ComUninitialize();
		return;
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		// 检查是否是我们自定义的消息
		if (msg.message == WM_PERFORM_DRAG_CHECK)
		{
			// 确保 COM 操作在主线程（STA线程）中执行
			g_supportedFile = FileDetector::IsDraggingSupportedFile();
			if (g_supportedFile)
			{
				LogInfo(L"[Detected] Dragging supported file detected!");
				v8::Local<v8::Value> argv[1] = {
					v8::String::NewFromUtf8(m_Isolate, std::to_string(WM_PERFORM_DRAG_CHECK).c_str()).ToLocalChecked(),
				};

				// 调用回调函数
				m_fileDropCallback->Call(m_Isolate->GetCurrentContext(),
					Null(m_Isolate),
					1, argv).ToLocalChecked();
			}
		}
		else if (msg.message == WM_PERFORM_DRAG_RELEASE)
		{
			if (g_supportedFile)
			{
				g_supportedFile = false;
				LogInfo(L"[Detected] Dragging released.");
				v8::Local<v8::Value> argv[1] = {
						v8::String::NewFromUtf8(m_Isolate, std::to_string(WM_PERFORM_DRAG_RELEASE).c_str()).ToLocalChecked(),
				};

				// 调用回调函数
				m_fileDropCallback->Call(m_Isolate->GetCurrentContext(),
					Null(m_Isolate),
					1, argv).ToLocalChecked();
			}
		}
		else
		{
			// 处理其他标准系统消息
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	UninitMouseHook();
}

void MouseHook::UninitMouseHook() {
	UnhookWindowsHookEx(g_mouseHook);
	FileDetector::ComUninitialize();
}

void MouseHook::SetFileDropCallback(v8::Local<v8::Function> callback) {
	m_fileDropCallback = callback;
}

void MouseHook::SetIsolate(v8::Isolate* isolate) {
    m_Isolate = isolate;
}

LRESULT CALLBACK MouseHook::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
	// 确保处理是有效的 (nCode >= 0)
	if (nCode >= 0)
	{
		MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
		POINT currentPos = pMouseStruct->pt;

		switch (wParam)
		{
		case WM_LBUTTONDOWN:
		{
			// 鼠标左键按下
			g_isLButtonDown = true;
			g_isDragging = false;
			g_detectionCalled = false;
			g_dragStartPos = currentPos;
			//std::cout << "\n[EVENT] LButton Down.\n";
			break;
		}

		case WM_MOUSEMOVE:
		{
			if (g_isLButtonDown)
			{
				if (!g_isDragging)
				{
					// 正在按下，检查是否超过拖拽阈值
					long dx = std::abs(currentPos.x - g_dragStartPos.x);
					long dy = std::abs(currentPos.y - g_dragStartPos.y);

					if (dx >= g_minDragX || dy >= g_minDragY)
					{
						// 达到拖拽阈值
						g_isDragging = true;
						//std::cout << "[EVENT] Dragging Started.\n";
					}
				}

				if (g_isDragging && !g_detectionCalled)
				{
					// 正在拖拽，执行文件检测
					PostThreadMessage(g_mainThreadId, WM_PERFORM_DRAG_CHECK, 0, 0);
					g_detectionCalled = true;
				}
			}
			break;
		}

		case WM_LBUTTONUP:
		{
			// 鼠标左键释放
			if (g_isDragging && g_detectionCalled)
			{
				//std::cout << "[EVENT] Dragging Released.\n";
				PostThreadMessage(g_mainThreadId, WM_PERFORM_DRAG_RELEASE, 0, 0);
			}
			// 重置状态
			g_isLButtonDown = false;
			g_isDragging = false;
			g_detectionCalled = false;
			break;
		}
		}
	}

	// 务必调用下一个钩子，将事件传递下去
	return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}