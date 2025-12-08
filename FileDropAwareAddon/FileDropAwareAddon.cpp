// FileDropAwareAddon.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include <node.h>
#include <uv.h>
#include <string>
#include <cstring>
#include <iostream>
#include <queue>
#include <mutex>
#include "MouseHook.h"
#include "Utils.h"

v8::Isolate* isolate = NULL;

static v8::Persistent<v8::Function> logCallback;

static uv_async_t async_log_handle;
// 存储日志信息 (需要线程安全)
struct LogMessage {
	std::wstring message;
	// 可能还需要一个类型指示器 (Info/Error)
};

std::mutex log_mutex;
std::queue<LogMessage> log_queue;

static void LogBase(const std::wstring& info) {
	if (isolate == NULL)
	{
		std::wcerr << L"isolate is null" << std::endl;
		return;
	}
	// 声明 HandleScope，确保局部 V8 对象的安全创建
	v8::HandleScope handle_scope(isolate);

	std::string logStr = WcharToUtf8(info.c_str());
	// 从 Persistent 句柄获取 Local 句柄用于本次调用
	if (logCallback.IsEmpty())
	{
		std::wcerr << L"logCallback is null" << std::endl;
		return;
	}
	v8::Local<v8::Function> localCallback = v8::Local<v8::Function>::New(isolate, logCallback);
	v8::Local<v8::Value> argv[1] = {
		v8::String::NewFromUtf8(isolate, logStr.c_str()).ToLocalChecked()
	};
	if (localCallback->IsNull() || localCallback->IsUndefined()) {
		std::wcerr << L"localCallback is null" << std::endl;
		return;
	}
	localCallback->Call(isolate->GetCurrentContext(),
		Null(isolate),
		1, argv).ToLocalChecked();
}

// 异步回调：在 Node.js 主线程上执行
static void AsyncLogCallback(uv_async_t* handle) {
	// 必须在这里创建 HandleScope 
	v8::HandleScope handle_scope(isolate);

	// 从队列中取出所有等待的日志消息
	std::lock_guard<std::mutex> lock(log_mutex);

	while (!log_queue.empty()) {
		LogMessage msg = log_queue.front();
		log_queue.pop();

		// 调用原始的 LogFunc (现在它在主线程上是安全的)
		// 注意：LogFunc 内部需要被修改，不再需要锁和 HandleScope，因为它现在是被主线程调用的。
		// 为了避免修改 LogFunc，我们在这里直接调用其逻辑：
		//std::string logStr = WcharToUtf8(msg.message.c_str());

		// ... (V8 调用逻辑，使用 logCallback 和 isolate) ...
		LogBase(msg.message);
	}
}

static void LogFunc(const std::wstring& info) {
	std::wcout << info << std::endl;
	// 1. 将日志信息放入线程安全队列
	{
		std::lock_guard<std::mutex> lock(log_mutex);
		log_queue.push({ info });
	}

	// 2. 触发 Libuv 事件，通知主线程执行 AsyncLogCallback
	// 这是安全的，因为它只发送一个信号，不涉及 V8 对象。
	uv_async_send(&async_log_handle);
}

void LogError(const std::wstring& error) {
	std::wstring logInfo = L"[drop file error] " + error;
	LogFunc(logInfo);
}

void LogInfo(const std::wstring& info) {
	std::wstring logInfo = L"[drop file info] " + info;
	LogFunc(logInfo);
}

static void OnExit(void* arg) {
	LogInfo(L"Monitoring stopped by process exit");
}

static void HandleExist(uv_signal_s* handle, int signal) {
	LogInfo(L"Monitoring stopped by user");
}

static void AwareInitialize(const v8::FunctionCallbackInfo<v8::Value>& args) {
	isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	// 检查参数是否有效
	if (args.Length() < 3) {
		isolate->ThrowException(v8::Exception::TypeError(
			v8::String::NewFromUtf8(isolate, "必须传入两个回调函数和一个字符串数组").ToLocalChecked()));
		return;
	}
	if (!args[0]->IsArray()) {
		isolate->ThrowException(v8::Exception::TypeError(
			v8::String::NewFromUtf8(isolate, "第一个参数必须是字符串数组").ToLocalChecked()));
		return;
	}
	if (!args[1]->IsFunction()) {
		isolate->ThrowException(v8::Exception::TypeError(
			v8::String::NewFromUtf8(isolate, "第二个参数必须是回调函数").ToLocalChecked()));
		return;
	}
	if (!args[2]->IsFunction()) {
		isolate->ThrowException(v8::Exception::TypeError(
			v8::String::NewFromUtf8(isolate, "第三个参数必须是回调函数").ToLocalChecked()));
		return;
	}

	uv_async_init(uv_default_loop(), &async_log_handle, AsyncLogCallback);

	node::Environment* env = node::GetCurrentEnvironment(isolate->GetCurrentContext());
	if (env)
	{
		node::AtExit(env, OnExit, nullptr);
	}
	else {
		std::wcerr << L"env is null" << std::endl;
		// LogError(L"env is null");
	}

	MouseHook::SetIsolate(isolate);
	v8::Local<v8::Function> callBack = v8::Local<v8::Function>::Cast(args[1]);
	MouseHook::SetFileDropCallback(callBack);
	
	// ❗ 关键修正：存储 Persistent 句柄
	v8::Local<v8::Function> logFunc = v8::Local<v8::Function>::Cast(args[2]);
	if (!logCallback.IsEmpty()) {
		logCallback.Reset(); // 释放旧的引用
	}
	logCallback.Reset(isolate, logFunc); // 存储新的 Persistent 引用

	v8::Local<v8::Array> jsArray = v8::Local<v8::Array>::Cast(args[0]);
	uint32_t arrayLength = jsArray->Length();
	std::set<std::wstring> targetExtensions;

	for (uint32_t i = 0; i < arrayLength; i++) {
		v8::Local<v8::Value> element;
		if (jsArray->Get(context, i).ToLocal(&element)) {
			if (element->IsString()) {
				// 将JavaScript字符串转换为std::string
				v8::String::Utf8Value utf8Str(isolate, element);
				if (*utf8Str) {
					// 转换为std::wstring并添加到集合
					std::wstring wstr = Utf8ToWstring(*utf8Str);
					targetExtensions.insert(wstr);
				}
			}
		}
	}

	std::wstring setContents;
	for (std::wstring wstr : targetExtensions) {
		setContents += wstr + L" ";
	}
	std::wcout << L"Target extensions: " << setContents << std::endl;
	MouseHook::InitMouseHook(targetExtensions);
}

void Initialize(v8::Local<v8::Object> exports) {
	NODE_SET_METHOD(exports, "AwareInitialize", AwareInitialize);

	uv_signal_t* signalHandler = new uv_signal_t;
	uv_signal_init(uv_default_loop(), signalHandler);
	uv_signal_start(signalHandler, HandleExist, SIGTERM);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Initialize)

//int main()
//{
//	const std::set<std::wstring> targetExtensions = {
//				L".txt", L".csv", L".log", L".xml", L".json", L".cs", L".xlsx",
//				L".png", L".doc", L".docx", L".pdf", L".jpg", L".jpeg", L".bmp"
//	};
//    MouseHook::InitMouseHook(targetExtensions);
//    std::cout << "Hello World!\n";
//}
