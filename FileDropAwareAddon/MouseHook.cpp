#include "MouseHook.h"
#include "FileDetector.h"
#include <iostream>
#include <thread>

// 全局主窗口句柄（用于发送消息）
DWORD			g_mainThreadId		= 0;
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

// 新增全局变量控制并发
std::atomic<bool> g_isChecking(false);

v8::Persistent<v8::Function> MouseHook::m_fileDropCallback;
v8::Isolate* MouseHook::m_Isolate = NULL;

extern void LogInfo(const std::wstring& info);
extern void LogError(const std::wstring& error);

void MouseHook::InitMouseHook(std::set<std::wstring> supportedExtensions) {
	LogInfo(L"Init mouse hook, monitoring mouse... Drag a file (e.g., .txt) to see detection.");
	/*if (!FileDetector::ComInitialize())
	{
		LogError(L"Failed to initialize COM! Error: " + std::to_wstring(GetLastError()));
		return;
	}*/
	FileDetector::SetExtensions(supportedExtensions);

	g_mainThreadId = GetCurrentThreadId();
	
	g_minDragX = GetSystemMetrics(SM_CXDRAG);
	g_minDragY = GetSystemMetrics(SM_CYDRAG);
	
	LogInfo(L"Mouse drag threshold: " + std::to_wstring(g_minDragX) + L"px");

	g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
	if (g_mouseHook == NULL)
	{
		LogError(L"Failed to install hook! Error: " + std::to_wstring(GetLastError()));
		//FileDetector::ComUninitialize();
		return;
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		// 检查是否是我们自定义的消息
		if (msg.message == WM_PERFORM_DRAG_CHECK)
		{
			// 如果正在检查中，丢弃本次请求，防止堆积
			if (g_isChecking) continue;
			g_isChecking = true;

			std::thread([]() {
				// 必须在线程内部初始化 COM，因为 COM 是线程相关的 (STA)
				if (!FileDetector::ComInitialize())
				{
					LogError(L"Failed to initialize COM! Error: " + std::to_wstring(GetLastError()));
					return;
				}

				bool result = FileDetector::IsDraggingSupportedFile();

				if (result) {
					LogInfo(L"[Detected] Dragging supported file detected0000!");
					// 检测成功，通知主线程/UI线程 (这里需要一种机制回调主线程，或者直接在这里处理回调)
					// 由于 V8/Node.js 回调通常需要在主线程执行，我们发送一个成功消息回主队列
					// 注意：需要拿到主线程ID，这里假设 g_mainThreadId 可访问
					PostThreadMessage(g_mainThreadId, WM_DRAG_CHECK_SUCCESS, 0, 0);
				}

				FileDetector::ComUninitialize();
				g_isChecking = false;
			}).detach();

			// 确保 COM 操作在主线程（STA线程）中执行
			// g_supportedFile = FileDetector::IsDraggingSupportedFile();
			// if (g_supportedFile)
			// {
			// 	LogInfo(L"[Detected] Dragging supported file detected!");
			// 	v8::Local<v8::Value> argv[1] = {
			// 		v8::String::NewFromUtf8(m_Isolate, std::to_string(WM_PERFORM_DRAG_CHECK).c_str()).ToLocalChecked(),
			// 	};

			// 	// 调用回调函数
			// 	m_fileDropCallback->Call(m_Isolate->GetCurrentContext(),
			// 		Null(m_Isolate),
			// 		1, argv).ToLocalChecked();
			// }
		}
		else if (msg.message == WM_DRAG_CHECK_SUCCESS)
		{
            g_supportedFile = true;
			LogInfo(L"[Detected] Dragging supported file detected!");
			// 确保有Isolate
			if (m_Isolate == NULL) {
				LogError(L"Isolate is null when trying to call callback.");
				break;
			}
			// 进入Isolate的作用域
    		v8::Isolate::Scope isolate_scope(m_Isolate);
			// 必须声明 HandleScope，否则无法创建 v8::String
			v8::HandleScope handle_scope(m_Isolate);

			// 检查回调是否为空
			if (!m_fileDropCallback.IsEmpty()) {
				// 获取当前上下文
				v8::Local<v8::Context> context = m_Isolate->GetCurrentContext();

				// 从 Persistent 获取 Local 句柄
				v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(m_Isolate, m_fileDropCallback);

				v8::Local<v8::Value> argv[1] = {
					v8::String::NewFromUtf8(m_Isolate, std::to_string(WM_PERFORM_DRAG_CHECK).c_str()).ToLocalChecked(),
				};

				// 执行回调
				callback->Call(context, v8::Null(m_Isolate), 1, argv).ToLocalChecked();
			}
			else
			{
				LogError(L"m_fileDropCallback is empty.");
			}
		}
		else if (msg.message == WM_PERFORM_DRAG_RELEASE)
		{
			if (g_supportedFile)
			{
				g_supportedFile = false;
				LogInfo(L"[Detected] Dragging released.");
				// 确保有Isolate
				if (m_Isolate == NULL) {
					LogError(L"Isolate is null when trying to call callback.");
					break;
				}
				// 进入Isolate的作用域
    			v8::Isolate::Scope isolate_scope(m_Isolate);
				v8::HandleScope handle_scope(m_Isolate); // 必须添加 Scope

				if (!m_fileDropCallback.IsEmpty()) {
					v8::Local<v8::Context> context = m_Isolate->GetCurrentContext();
					v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(m_Isolate, m_fileDropCallback);

					v8::Local<v8::Value> argv[1] = {
						v8::String::NewFromUtf8(m_Isolate, std::to_string(WM_PERFORM_DRAG_RELEASE).c_str()).ToLocalChecked(),
					};

					callback->Call(context, v8::Null(m_Isolate), 1, argv).ToLocalChecked();
				}
				else
				{
					LogError(L"m_fileDropCallback is empty.");
				}
				//v8::Local<v8::Value> argv[1] = {
				//		v8::String::NewFromUtf8(m_Isolate, std::to_string(WM_PERFORM_DRAG_RELEASE).c_str()).ToLocalChecked(),
				//};

				//// 调用回调函数
				//m_fileDropCallback->Call(m_Isolate->GetCurrentContext(),
				//	Null(m_Isolate),
				//	1, argv).ToLocalChecked();
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
	/*FileDetector::ComUninitialize();*/
}

void MouseHook::SetFileDropCallback(v8::Local<v8::Function> callback) {
	// ? 关键修正：确保 Isolate 和 Context 都已设置
    if (m_Isolate == NULL) {
		LogError(L"m_Isolate is null");
        // LogError 或抛出异常
        return;
    }
    
    // 1. 获取当前的 Context
    v8::Local<v8::Context> context = m_Isolate->GetCurrentContext();

    // 2. ? 致命修正：创建 Context 作用域，确保 V8 操作在有效的环境中进行
	// 进入Isolate的作用域
    // v8::Isolate::Scope isolate_scope(m_Isolate);
    v8::Context::Scope context_scope(context);

	// 如果之前设置过，先重置（释放旧的引用）
	if (!m_fileDropCallback.IsEmpty()) {
		m_fileDropCallback.Reset();
	}
	// 这样即使 SetFileDropCallback 函数执行完毕，回调函数也不会被垃圾回收
	m_fileDropCallback.Reset(m_Isolate, callback);
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