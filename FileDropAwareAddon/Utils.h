#pragma once
#include <string>
#include <windows.h>

std::string WcharToUtf8(const wchar_t* wstr);

std::wstring Utf8ToWstring(const std::string& utf8Str);

std::wstring HResultToHexString(HRESULT hr);

BOOL GetModuleDirectory(std::wstring& path);
