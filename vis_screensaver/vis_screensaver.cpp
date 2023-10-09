#include <Windows.h>
#include <windowsx.h>
#include "shlobj.h"
#include <Shlwapi.h>

#include <string>
#include <vector>
#include <map>

#include "Winamp/wa_ipc.h"
#include "vis.h"

#pragma comment(lib, "Shlwapi.lib")

//////////////////////////
// FORWARD DECLARATIONS //
//////////////////////////

void showConfigDialog(struct winampVisModule* this_mod);
int init(struct winampVisModule* this_mod);
int render(struct winampVisModule* this_mod);
void quit(struct winampVisModule* this_mod);

///////////////
// VARIABLES //
///////////////

HINSTANCE myself = NULL;
wchar_t configFileName[MAX_PATH];
wchar_t savestateFileName[MAX_PATH];

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	myself = (HINSTANCE)hModule;
	return TRUE;
}

////////////////////////
// PLUGIN DESCRIPTORS //
////////////////////////

std::vector<winampVisModule> visDescriptors;

winampVisModule* getModule(int which)
{
	if (which < visDescriptors.size())
		return &visDescriptors[which];
	else
		return NULL;
}

winampVisHeader visHeader = {
	VIS_HDRVER,
	const_cast<char*>("SCREENSAVER VIS"),	// description
	getModule
};

extern "C" __declspec(dllexport) winampVisHeader * winampVisGetHeader()
{
	wchar_t pluginDir[MAX_PATH];
	GetModuleFileName(myself, pluginDir, MAX_PATH);
	*wcsrchr(pluginDir, '\\') = '\0';
	wsprintf(configFileName, L"%s\\vis_screensaver.ini", pluginDir);

	wchar_t tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	wsprintf(savestateFileName, L"%s\\vis_screensaver_state.ini", tempPath);

	wchar_t scrPath[MAX_PATH];
	GetPrivateProfileString(L"config", L"scrdir", L"c:\\Windows\\System32\\", scrPath, MAX_PATH, configFileName);

	if (visDescriptors.size() == 0)
	{
		wchar_t searchCriteria[1024];
		WIN32_FIND_DATA FindFileData;
		HANDLE searchHandle = INVALID_HANDLE_VALUE;

		wsprintf(searchCriteria, L"%s*.scr", scrPath);
		searchHandle = FindFirstFile(searchCriteria, &FindFileData);
		if (searchHandle != INVALID_HANDLE_VALUE)
		{
			do
			{
				size_t retVal;
				char desc[MAX_PATH];
				wcstombs_s(&retVal, desc, FindFileData.cFileName, MAX_PATH);
				char* descDup = _strdup(desc);

				wchar_t fileName[MAX_PATH];
				wsprintf(fileName, L"%s%s", scrPath, FindFileData.cFileName);
				wchar_t* fileNameDup = _wcsdup(fileName);

				winampVisModule visDescriptor = {
					descDup,// description: description of module
					NULL,	// hwndParent: parent window (filled in by calling app)
					NULL,	// hDllInstance: instance handle to this DLL (filled in by calling app)
					0,		// sRate: sample rate (filled in by calling app)
					0,		// nCh: number of channels (filled in...)
					10,		// latencyMS: latency from call of RenderFrame to actual drawing (calling app looks at this value when getting data)
					15,		// delayMS: delay between calls in ms
					0,		// spectrumNch
					2,		// waveformNch
					{ 0 },	// spectrumData[2][576]
					{ 0 },	// waveformData[2][576]
					showConfigDialog,	// configuration dialog
					init,				// 0 on success, creates window, etc
					render,				// returns 0 if successful, 1 if vis should end
					quit,				// call when done
					fileNameDup			// userData: user data, optional (in our case, the full file name)
				};
				visDescriptors.push_back(visDescriptor);
			} while (FindNextFile(searchHandle, &FindFileData));
			FindClose(searchHandle);
		}
	}

	return &visHeader;
}

///////////////////////////
// PLUGIN IMPLEMENTATION //
///////////////////////////

// this is used to identify the skinned frame to allow for embedding/control by modern skins if needed
// note: this is taken from vis_milk2 so that by having a GUID on the window, the embedding of ths vis
//		 window will work which if we don't set a guid is fine but that prevents other aspects of the
//		 skin being able to work out / interact with the window correctly (gen_ff is horrible at times)
// {0000000A-000C-0010-FF7B-01014263450C}
static const GUID embed_guid =
{ 10, 12, 16, { 255, 123, 1, 1, 66, 99, 69, 12 } };

embedWindowState myWindowState = { 0 };
HWND displayWnd = NULL;
DWORD currentPid = -1;

void runProcessInBackground(wchar_t* cmdLine)
{
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	if (CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
		currentPid = pi.dwProcessId;
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
}

void stopProcess()
{
	if (currentPid != -1)
	{
		HANDLE procHandle = OpenProcess(PROCESS_TERMINATE, false, currentPid);
		TerminateProcess(procHandle, 0);
		CloseHandle(procHandle);

		currentPid = -1;
	}
}

LRESULT CALLBACK visWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_WINDOWPOSCHANGED:
	{
		stopProcess();
		render(NULL);
	}
	return 0;
	}
	return DefWindowProc(hwnd, message, wParam, lParam);
}

// configuration dialog
void showConfigDialog(struct winampVisModule* this_mod)
{
	// Show config window
	// NOTE: Nope.
}

// 0 on success, creates window, etc
int init(struct winampVisModule* this_mod)
{
	HWND(*embed)(embedWindowState * v);
	*(void**)&embed = (void*)SendMessage(this_mod->hwndParent, WM_WA_IPC, (LPARAM)0, IPC_GET_EMBEDIF);
	if (!embed)
	{
		MessageBox(this_mod->hwndParent, L"This plugin requires Winamp 5.0+", L"Uh Oh", MB_OK);
		return 1;
	}

	// Move to correct position
	RECT rWinamp = { 0 };
	GetWindowRect(this_mod->hwndParent, &rWinamp);
	myWindowState.r.left = GetPrivateProfileInt(L"window", L"WindowPosLeft", rWinamp.left, savestateFileName);
	myWindowState.r.top = GetPrivateProfileInt(L"window", L"WindowPosTop", rWinamp.bottom, savestateFileName);
	myWindowState.r.right = GetPrivateProfileInt(L"window", L"WindowPosRight", rWinamp.right, savestateFileName);
	myWindowState.r.bottom = GetPrivateProfileInt(L"window", L"WindowPosBottom", rWinamp.bottom + 100, savestateFileName);

	// this sets a GUID which can be used in a modern skin / other parts of Winamp to
	// indentify the embedded window frame such as allowing it to activated in a skin
	SET_EMBED_GUID((&myWindowState), embed_guid);
	
	HWND parent = embed(&myWindowState);
	SetWindowText(myWindowState.me, L"SCREENSAVER VIS"); // set window title

	// Register window class
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = visWndProc;
	wc.hInstance = this_mod->hDllInstance;
	wc.lpszClassName = L"visscrclass";

	if (!RegisterClass(&wc)) {
		MessageBox(this_mod->hwndParent, L"Error registering class, this is serious!", L"Uh Oh", MB_OK);
		return 1;
	}
	else {
		displayWnd = CreateWindow(L"visscrclass", L"SCREENSAVER VIS", WS_VISIBLE | WS_CHILDWINDOW, 1, 1, 100, 20, parent, NULL, this_mod->hDllInstance, 0);
		if (!displayWnd) {
			MessageBox(this_mod->hwndParent, L"Could not create window, sorry but this is serious!", L"Uh Oh", MB_OK);
			UnregisterClass(L"visscrclass", this_mod->hDllInstance);
			return 1;
		}
	}

	SetWindowLong(displayWnd, GWL_USERDATA, (LONG)this_mod);
	SendMessage(this_mod->hwndParent, WM_WA_IPC, (WPARAM)displayWnd, IPC_SETVISWND);

	ShowWindow(parent, SW_SHOWNORMAL);

	return 0;
}

// returns 0 if successful, 1 if vis should end
int render(struct winampVisModule* this_mod)
{
	wchar_t cmdLine[1024];
	struct winampVisModule* currentMod = (struct winampVisModule*)GetWindowLong(displayWnd, GWL_USERDATA);
	wsprintf(cmdLine, L"\"%s\" /p %d", (wchar_t *)currentMod->userData, displayWnd);

	// Run screensaver process
	HANDLE procHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, currentPid);
	if (procHandle == NULL)
	{
		runProcessInBackground(cmdLine);
	}
	else
	{
		DWORD exitCode;
		if (GetExitCodeProcess(procHandle, &exitCode))
		{
			if (exitCode != STILL_ACTIVE)
			{
				runProcessInBackground(cmdLine);
			}
		}
	}

	return 0;
}

// call when done
void quit(struct winampVisModule* this_mod)
{
	// Kill screensaver process
	stopProcess();

	// Save window position
	WritePrivateProfileString(L"window", L"WindowPosLeft", std::to_wstring(myWindowState.r.left).c_str(), savestateFileName);
	WritePrivateProfileString(L"window", L"WindowPosRight", std::to_wstring(myWindowState.r.right).c_str(), savestateFileName);
	WritePrivateProfileString(L"window", L"WindowPosTop", std::to_wstring(myWindowState.r.top).c_str(), savestateFileName);
	WritePrivateProfileString(L"window", L"WindowPosBottom", std::to_wstring(myWindowState.r.bottom).c_str(), savestateFileName);

	// remove vis window, NOTE: THIS IS BAD AND NOT NEEDED EVIDENTLY
	if (IsWindow(this_mod->hwndParent))
		PostMessage(this_mod->hwndParent, WM_WA_IPC, 0, IPC_SETVISWND);

	DestroyWindow(displayWnd);
	displayWnd = NULL;

	if (IsWindow(myWindowState.me))
	{
		DestroyWindow(myWindowState.me);
		myWindowState.me = NULL;
	}

	UnregisterClass(L"visscrclass", this_mod->hDllInstance);
}
