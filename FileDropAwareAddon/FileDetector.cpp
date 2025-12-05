#include "FileDetector.h"
#include <iostream>
#include <algorithm>
#include <shlobj.h>
#include <UIAutomation.h>

#include "Utils.h"

// UIA 自动化对象的全局实例
static CComPtr<IUIAutomation> g_pAutomation = NULL;

/**
 * @brief 获取给定 UIA 元素的父元素。
 * * @param pElement 当前 UIA 元素。
 * @param ppParentElement 用于接收父元素指针的输出参数。
 * @return HRESULT 成功返回 S_OK，否则返回错误码。
 */
HRESULT GetParentElement(IUIAutomationElement* pElement, IUIAutomationElement** ppParentElement) {
	if (pElement == NULL || ppParentElement == NULL || g_pAutomation == NULL) {
		return E_POINTER;
	}

	*ppParentElement = NULL;
	CComPtr<IUIAutomationTreeWalker> pWalker = NULL;
	HRESULT hr = S_OK;

	// 1. 获取 TreeWalker 实例 (这里使用 RawViewWalker，因为它包含所有元素，包括容器)
	// 您也可以考虑使用 ControlViewWalker (只包含控件) 或 ContentViewWalker (只包含内容)。
	hr = g_pAutomation->get_RawViewWalker(&pWalker);
	if (FAILED(hr)) {
		return hr;
	}

	// 2. 使用 GetParentElement 方法获取父元素
	// TreeWalker 接口提供了直接的方法来导航树。
	hr = pWalker->GetParentElement(pElement, ppParentElement);

	if (hr == S_OK) {
		// 成功获取父元素
		return S_OK;
	}
	else if (hr == UIA_E_ELEMENTNOTAVAILABLE) {
		// 元素不可用（例如，窗口已关闭）
		return hr;
	}
	else if (hr == S_FALSE) {
		// 已经到达树的根部 (通常是桌面)，没有父元素
		return S_FALSE;
	}
	else {
		// 其他错误
		return hr;
	}
}

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

bool FileDetector::IsMouseOverFileItemUIA(const POINT& mousePos) {
	// 1. 初始化 UIA 自动化对象 (只需初始化一次)
	if (g_pAutomation == NULL) {
		// CLSID_CUIAutomation 对应 IUIAutomation 接口的自动化对象
		HRESULT hr = CoCreateInstance(
			CLSID_CUIAutomation,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_IUIAutomation,
			(void**)&g_pAutomation
		);
		if (FAILED(hr)) {
			LogError(L"Failed to CoCreate IUIAutomation.");
			return false;
		}
	}

	CComPtr<IUIAutomationElement> pElement = NULL;

	// 2. 获取鼠标坐标下的 UIA 元素
	HRESULT hr = g_pAutomation->ElementFromPoint(mousePos, &pElement);
	if (FAILED(hr) || pElement == NULL) {
		// 如果失败，通常是鼠标在屏幕外或不可访问区域
		LogError(L"Failed to get element from point.");
		return false; // 视情况处理 S_FALSE
	}
	// 3. 获取元素的 ControlType 属性
	CONTROLTYPEID controlTypeId;
	hr = pElement->get_CurrentControlType(&controlTypeId);
	if (FAILED(hr)) {
		LogError(L"Failed to get ControlType.");
		return false;
	}
	LogInfo(L"ControlType ID: " + std::to_wstring(controlTypeId));

	CComPtr<IUIAutomationElement> pParentElement;
	// 尝试获取父元素
	hr = GetParentElement(pElement, &pParentElement);
	if (FAILED(hr) || pParentElement == NULL) {
		LogError(L"Failed to get parent element.");
		return false;
	}

	// 成功获取父元素，现在可以检查 pParentElement 的 ControlType 或 Name 属性
	CONTROLTYPEID parentControlType;
	hr = pParentElement->get_CurrentControlType(&parentControlType);
	if (FAILED(hr)) {
		LogError(L"Failed to get parent ControlType.");
		return false;
	}
	LogInfo(L"parent element type id: " + std::to_wstring(parentControlType));
	// 例如：检查父元素是否是列表项容器
	if (parentControlType == UIA_ListItemControlTypeId || controlTypeId == UIA_ListItemControlTypeId) {
		return true;
	}

	/*
	空白
	UIA_ListControlTypeId 50008
	UIA_PaneControlTypeId 50033

	文件
	UIA_EditControlTypeId 50004
	UIA_ListItemControlTypeId 50007

	时间标签
	UIA_CustomControlTypeId 50025
	UIA_GroupControlTypeId 50026

	表头
	UIA_SplitButtonControlTypeId 50031
	UIA_HeaderControlTypeId 50034
	*/

	// 4. 过滤和分类
	//if (controlTypeId == UIA_ListItemControlTypeId) {
	//	// 命中了文件或文件夹项
	//	*pResult = HitType_FileItem;
	//}
	//else if (controlTypeId == UIA_GroupControlTypeId || controlTypeId == UIA_HeaderControlTypeId) {
	//	// 命中了分组标题 (如 "今天")
	//	*pResult = HitType_GroupHeader;
	//}
	//else {
	//	// 命中了滚动条、容器或其他元素
	//	*pResult = HitType_Other;
	//}

	return false;
}

/**===============================================================================================================================================================
Windows 资源管理器（Explorer）的窗口层级结构通常如下：

|CabinetWClass(主窗口 / 标题栏)
|
|---WorkerW / ReBarWindow32(包含地址栏、工具栏)
|
|---ShellTabWindowClass
|	|
|	|---SHELLDLL_DefView < --我们的目标区域
|		|
|	    |---DirectUIHWND(实际显示文件的控件)
|	
|---SysTreeView32(左侧导航栏，通常不包含在 DefView 中)

	逻辑流程对比：
	1. 拖动文件时：鼠标点在 DirectUIHWND 上 -> 向上找父级 -> 遇到 SHELLDLL_DefView -> IsContentArea 返回 true -> 继续检测选中项 -> 通过。

	2. 拖动窗口时：鼠标点在 CabinetWClass 的标题栏区域 -> 向上找父级 -> 立即遇到 CabinetWClass (没遇到 SHELLDLL_DefView) -> IsContentArea 返回 false -> 拦截。

	3. 点击左侧导航栏时：鼠标点在 SysTreeView32 -> 向上找父级 -> 遇到 CabinetWClass -> IsContentArea 返回 false -> 拦截（这也是合理的，因为通常左侧树的选中状态和右侧视图的选中状态是分离的）。
======================================================================================================================================================================*/
bool FileDetector::IsContentArea(HWND hWnd, const POINT& mousePos, bool isDesktop) {
	HWND current = hWnd;
	RECT contentRect;
	std::wstring preClassName;

	while (current != NULL) {
		wchar_t className[256];
		GetClassNameW(current, className, 256);

		//==========================WIN10===============================
		// 1. 搜索框
		if (wcscmp(className, L"SearchEditBoxWrapperClass") == 0) {
			return false;
		}
		// 2. 地址栏
		if (wcscmp(className, L"Address Band Root") == 0) {
			return false;
		}
		// 3. TravelBand
		if (wcscmp(className, L"TravelBand") == 0) {
			return false;
		}

		// 1. 如果遇到了文件视图核心窗口类，说明是在文件区域
		// SHELLDLL_DefView: 包含 DirectUIHWND 或 SysListView32 的容器
		if (wcscmp(className, L"SHELLDLL_DefView") == 0) {
			LogInfo(L"class name: " + std::wstring(className) + L", previous class name: " + preClassName);
			if (!isDesktop)
			{
				// 获取 SHELLDLL_DefView 容器的屏幕坐标边界
				if (GetWindowRect(current, &contentRect)) {

					// 空间判断：检查鼠标 Y 坐标是否在顶部 HEADING_HEIGHT 范围内
					if (mousePos.y <= contentRect.top + 30) {
						// 鼠标在顶部表头区域，返回 false
						LogInfo(L"Mouse is in the top header area of SHELLDLL_DefView (Y-coordinate check).");
						return false;
					}

					// Y 坐标通过了表头检查，则认为是有效的文件列表区域
					return true;
				}
			}
			return true;
		}

		// 2. 如果还没遇到 DefView 就已经到了顶层资源管理器窗口，
		// 说明鼠标点在了标题栏、菜单栏、状态栏或左侧导航树上
		if (wcscmp(className, L"CabinetWClass") == 0) {
			return false;
		}

		// 3. 特殊处理桌面
		// 桌面本身就是一个大 DefView，但也可能由 Progman/WorkerW 承载
		if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0) {
			return true;
		}

		current = GetParent(current);

		preClassName = className;
	}
	return false;
}

HWND FileDetector::FindShellParent(HWND hWnd, bool& isDesktop) {
	isDesktop = false;
	HWND current = hWnd;
	// SHELLDLL_DefView: 桌面图标的实际窗口的父容器, 或者文件资源管理器窗口（CabinetWClass 或 ExplorerWClass）内部，
	// 显示文件和文件夹列表的区域也是一个 SHELLDLL_DefView 类的窗口实例
	// CabinetWClass: 是由 Windows Shell 进程（explorer.exe）创建的标准文件或文件夹窗口的标识符。
	/*wchar_t firstClassName[256];
	GetClassNameW(current, firstClassName, 256);
	LogInfo(L"first window class name: " + std::wstring(firstClassName));
	if (
		wcscmp(firstClassName, L"CabinetWClass") == 0 ||
		wcscmp(firstClassName, L"TITLE_BAR_SCAFFOLDING_WINDOW_CLASS") == 0 ||
        wcscmp(firstClassName, L"Microsoft.UI.Content.DesktopChildSiteBridge") == 0
	)
	{
		return NULL;
	}*/

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

		// ================== 新增 UIA 检查 ==================
		if (!isDesktop)
		{
			bool onFile = IsMouseOverFileItemUIA(mousePos);
			if (!onFile)
			{
				return false;
			}
		}
		// ================== 新增检查 ==================
		// 如果鼠标不在文件显示区域（例如在标题栏），直接返回 false
		if (!IsContentArea(targetHwnd, mousePos, isDesktop)) {
			return false;
		}
		// =============================================

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