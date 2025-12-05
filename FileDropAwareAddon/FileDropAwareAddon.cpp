// FileDropAwareAddon.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include <string>
#include <cstring>
#include <iostream>
#include "MouseHook.h"
#include "Utils.h"


static void LogFunc(const std::wstring& info) {
	std::wcout << info << std::endl;
}

void LogError(const std::wstring& error) {
	std::wstring logInfo = L"[drop file error] " + error;
	LogFunc(logInfo);
}

void LogInfo(const std::wstring& info) {
	std::wstring logInfo = L"[drop file info] " + info;
	LogFunc(logInfo);
}

int main()
{
	SetConsoleOutputCP(CP_UTF8);
	const std::set<std::wstring> targetExtensions = {
				L".txt", L".csv", L".log", L".xml", L".json", L".cs", L".xlsx",
				L".png", L".doc", L".docx", L".pdf", L".jpg", L".jpeg", L".bmp"
	};
    MouseHook::InitMouseHook(targetExtensions);
    std::cout << "Hello World!\n";
}
