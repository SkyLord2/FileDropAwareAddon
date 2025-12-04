// FileDropAwareAddon.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include <node.h>
#include <uv.h>
#include <string>
#include <cstring>
#include <iostream>
#include "MouseHook.h"
#include "Utils.h"

v8::Isolate* isolate = NULL;

v8::Local<v8::Function> logCallback;

static void LogFunc(const std::wstring& info) {
	std::wcout << info << std::endl;
	std::string logStr = WcharToUtf8(info.c_str());
	v8::Local<v8::Value> argv[1] = {
		v8::String::NewFromUtf8(isolate, logStr.c_str()).ToLocalChecked()
	};
	if (logCallback->IsNull() || logCallback->IsUndefined())
	{
		std::wcerr << L"logCallback is null" << std::endl;
		return;
	}
	if (isolate == NULL)
	{
		std::wcerr << L"isolate is null" << std::endl;
		return;
	}
	logCallback->Call(isolate->GetCurrentContext(),
		Null(isolate),
		1, argv).ToLocalChecked();
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

	node::Environment* env = node::GetCurrentEnvironment(isolate->GetCurrentContext());
	if (env)
	{
		node::AtExit(env, OnExit, nullptr);
	}
	else {
		std::wcerr << L"env is null" << std::endl;
		LogError(L"env is null");
	}
	
	v8::Local<v8::Function> callBack = v8::Local<v8::Function>::Cast(args[1]);
	MouseHook::SetFileDropCallback(callBack);
	MouseHook::SetIsolate(isolate);

    logCallback = v8::Local<v8::Function>::Cast(args[2]);

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
