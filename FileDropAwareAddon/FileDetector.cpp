#include "FileDetector.h"
#include <iostream>
#include <algorithm>
#include <shlobj.h>

#include "Utils.h"

extern void LogInfo(const std::wstring& info);
extern void LogError(const std::wstring& error);

std::set<std::wstring> FileDetector::m_Extensions = {};

FileDetector::FileDetector() {
}

FileDetector::~FileDetector() {}

bool FileDetector::IsTargetExtension(const std::wstring& path) {
	LPWSTR ext = PathFindExtensionW(path.c_str());
	if (ext == NULL) { 
		LogError(L"there is no extension in the path");
		return false; 
	}

	std::wstring extStr(ext);
	// 转换为小写进行比较
	std::transform(extStr.begin(), extStr.end(), extStr.begin(), ::towlower);

	return m_Extensions.count(extStr) > 0;
}

bool FileDetector::HasValidSelection(IDispatch* pDispWindow) {
	if (!pDispWindow) {
		LogError(L"pDispWindow is null");
		return false;
	}

	CComPtr<IWebBrowser2> pBrowser;
	HRESULT hr = pDispWindow->QueryInterface(IID_IWebBrowser2, (void**)&pBrowser);
	if (FAILED(hr)) {
		LogError(L"Get IWebBrowser2 failed");
		return false;
	}

	// 获取 Document
	CComPtr<IDispatch> pDispDoc;
	hr = pBrowser->get_Document(&pDispDoc);
	if (FAILED(hr) || !pDispDoc) {
		LogError(L"Get Document failed");
		return false;
	}

	// 获取 Folder View
	CComPtr<IShellFolderViewDual> pFolderView;
	hr = pDispDoc->QueryInterface(IID_IShellFolderViewDual, (void**)&pFolderView);
	if (FAILED(hr)) {
        LogError(L"Get Folder View failed");
		return false;
	}

	// 获取 SelectedItems
	CComPtr<FolderItems> pSelectedItems;
	hr = pFolderView->SelectedItems(&pSelectedItems);
	if (FAILED(hr) || !pSelectedItems) {
		LogError(L"Get Selected Items failed");
		return false;
	}

	long count = 0;
	pSelectedItems->get_Count(&count);
	if (count == 0) {
		LogError(L"No selected items");
		return false;
	}

	// 遍历选中项
	for (long i = 0; i < count; i++)
	{
		CComVariant varIndex(i);
		CComPtr<FolderItem> pItem;
		hr = pSelectedItems->Item(varIndex, &pItem);

		if (SUCCEEDED(hr) && pItem)
		{
			VARIANT_BOOL isFolder = VARIANT_FALSE;
			pItem->get_IsFolder(&isFolder);

			// 如果是文件夹，跳过
			if (isFolder == VARIANT_TRUE) continue;

			BSTR bstrPath = NULL;
			pItem->get_Path(&bstrPath);
			if (bstrPath)
			{
				std::wstring path(bstrPath, SysStringLen(bstrPath));
				SysFreeString(bstrPath);

				if (IsTargetExtension(path))
				{
					return true;
				}
			}
		}
	}
	return false;
}

HWND FileDetector::FindShellParent(HWND hWnd, bool& isDesktop) {
	isDesktop = false;
	HWND current = hWnd;
	// SHELLDLL_DefView: 桌面图标的实际窗口的父容器, 或者文件资源管理器窗口（CabinetWClass 或 ExplorerWClass）内部，
	// 显示文件和文件夹列表的区域也是一个 SHELLDLL_DefView 类的窗口实例
	// CabinetWClass: 是由 Windows Shell 进程（explorer.exe）创建的标准文件或文件夹窗口的标识符。
	wchar_t firstClassName[256];
	GetClassNameW(current, firstClassName, 256);
	if (wcscmp(firstClassName, L"CabinetWClass") == 0)
	{
		return NULL;
	}

	while (current != NULL)
	{
		wchar_t className[256];
		GetClassNameW(current, className, 256);
		LogInfo(L"Drag window class name: " + std::wstring(className));
		// 检查是否是普通资源管理器
		if (wcscmp(className, L"CabinetWClass") == 0)
		{
			return current;
		}

		// 检查是否是桌面
		if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0)
		{
			isDesktop = true;
			return current;
		}

		current = GetParent(current);
	}

	// 兜底检查：如果上面的循环没找到，但 WorkerW 可能覆盖在 Progman 上
	HWND progman = FindWindowW(L"Progman", NULL);
	if (progman != NULL)
	{
		// 这里简化处理，如果找不到父级但也没报错，返回 NULL
		LogInfo(L"Failed to find shell parent, but found Progman");
	}

	return NULL;
}

bool FileDetector::ComInitialize() {	
	HRESULT hr = CoInitialize(NULL);
	if (hr != S_OK && hr != S_FALSE)
	{
		LogError(L"COM Initialization failed (HRESULT: " + HResultToHexString(hr) + L"). Shell operations require STA.");
		return false;
	}
	return true;
}

void FileDetector::ComUninitialize() {
	CoUninitialize();
}

void FileDetector::SetExtensions(const std::set<std::wstring>& extensions) {
    m_Extensions = extensions;
}

bool FileDetector::IsDraggingSupportedFile() {
	// COM 初始化 (实际使用中建议在线程入口处初始化一次，不要在函数内频繁调用)
	//CoInitialize(NULL);
	bool result = false;

	try
	{
		// 1. 获取鼠标位置
		POINT mousePos;
		GetCursorPos(&mousePos);

		// 2. 获取鼠标下的窗口句柄
		HWND targetHwnd = WindowFromPoint(mousePos);
		if (targetHwnd == NULL) throw 0;

		// 3. 向上回溯
		bool isDesktop = false;
		HWND shellHwnd = FindShellParent(targetHwnd, isDesktop);
		if (shellHwnd == NULL) throw 0;

		// 4. 初始化 ShellWindows
		CComPtr<IShellWindows> pShellWindows;
		HRESULT hr = pShellWindows.CoCreateInstance(CLSID_ShellWindows);
		if (FAILED(hr))
		{
			LogError(L"Failed to create IShellWindows instance: " + HResultToHexString(hr));
			throw 0;
		}

		// 5. 根据类型查找
		if (isDesktop)
		{
			// 对应 C#: shellWindows.FindWindowSW(..., SWC_DESKTOP, ..., SWFO_NEEDDISPATCH)
			// SWC_DESKTOP = 8, SWFO_NEEDDISPATCH = 1
			CComVariant vMissing;
			vMissing.vt = VT_ERROR;
			vMissing.scode = DISP_E_PARAMNOTFOUND;
			int hwndVal = 0;
			CComPtr<IDispatch> pDispDesktop;

			hr = pShellWindows->FindWindowSW(
				&vMissing, &vMissing,
				SWC_DESKTOP,
				(long*)&hwndVal,
				SWFO_NEEDDISPATCH,
				&pDispDesktop
			);

			if (SUCCEEDED(hr) && pDispDesktop)
			{
				result = HasValidSelection(pDispDesktop);
			}
		}
		else
		{
			// 遍历所有打开的 Explorer 窗口
			long winCount = 0;
			pShellWindows->get_Count(&winCount);

			for (long i = 0; i < winCount; i++)
			{
				CComVariant index(i);
				CComPtr<IDispatch> pDisp;
				hr = pShellWindows->Item(index, &pDisp);

				if (SUCCEEDED(hr) && pDisp)
				{
					CComPtr<IWebBrowser2> pBrowser;
					hr = pDisp->QueryInterface(IID_IWebBrowser2, (void**)&pBrowser);
					if (SUCCEEDED(hr))
					{
						SHANDLE_PTR hWindow = 0;
						pBrowser->get_HWND(&hWindow);

						if ((HWND)hWindow == shellHwnd)
						{
							result = HasValidSelection(pDisp);
							if (result) break;
						}
					}
				}
			}
		}
	}
	catch (...)
	{
		result = false;
	}

	//CoUninitialize();
	return result;
}