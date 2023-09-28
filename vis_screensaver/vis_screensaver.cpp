#include <Windows.h>
#include <windowsx.h>
#include "shlobj.h"
#include <Shlwapi.h>

#include <string>

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

////////////////////////
// PLUGIN DESCRIPTORS //
////////////////////////

winampVisModule visDescriptor = {
	const_cast<char*>("SCREENSAVER VIS"),	// description: description of module
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
	NULL	// userData: user data, optional
};

winampVisModule* getModule(int which)
{
	switch (which)
	{
	case 0:	return &visDescriptor;
	default: return NULL;
	}
}

winampVisHeader visHeader = {
	VIS_HDRVER,
	const_cast<char*>("SCREENSAVER VIS"),	// description
	getModule
};

extern "C" __declspec(dllexport) winampVisHeader * winampVisGetHeader()
{
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

wchar_t configFileName[MAX_PATH];
embedWindowState myWindowState = { 0 };
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

// configuration dialog
void showConfigDialog(struct winampVisModule* this_mod)
{
	// Show config window
	// NOTE: Nope.
}

// 0 on success, creates window, etc
int init(struct winampVisModule* this_mod)
{
	char* pluginDir = (char*)SendMessage(this_mod->hwndParent, WM_WA_IPC, 0, IPC_GETPLUGINDIRECTORY);
	wsprintf(configFileName, L"%S\\vis_screensaver.ini", pluginDir);

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
	myWindowState.r.left = GetPrivateProfileInt(L"window", L"WindowPosLeft", rWinamp.left, configFileName);
	myWindowState.r.top = GetPrivateProfileInt(L"window", L"WindowPosLeft", rWinamp.bottom, configFileName);
	myWindowState.r.right = GetPrivateProfileInt(L"window", L"WindowPosLeft", rWinamp.right, configFileName);
	myWindowState.r.bottom = GetPrivateProfileInt(L"window", L"WindowPosLeft", rWinamp.bottom + 100, configFileName);

	// this sets a GUID which can be used in a modern skin / other parts of Winamp to
	// indentify the embedded window frame such as allowing it to activated in a skin
	SET_EMBED_GUID((&myWindowState), embed_guid);
	
	HWND parent = embed(&myWindowState);
	SetWindowText(myWindowState.me, L"SCREENSAVER VIS"); // set window title

	ShowWindow(parent, SW_SHOWNORMAL);

	return 0;
}

// returns 0 if successful, 1 if vis should end
int render(struct winampVisModule* this_mod)
{
	wchar_t scrFile[MAX_PATH];
	wchar_t cmdLine[1024];

	GetPrivateProfileString(L"config", L"screensaver", L"", scrFile, MAX_PATH, configFileName);
	wsprintf(cmdLine, L"\"%s\" /p %d", scrFile, myWindowState.me);

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

	char* pluginDir = (char*)SendMessage(this_mod->hwndParent, WM_WA_IPC, 0, IPC_GETPLUGINDIRECTORY);
	wsprintf(configFileName, L"%S\\vis_screensaver.ini", pluginDir);

	// Save window position
	WritePrivateProfileString(L"window", L"WindowPosLeft", std::to_wstring(myWindowState.r.left).c_str(), configFileName);
	WritePrivateProfileString(L"window", L"WindowPosRight", std::to_wstring(myWindowState.r.right).c_str(), configFileName);
	WritePrivateProfileString(L"window", L"WindowPosTop", std::to_wstring(myWindowState.r.top).c_str(), configFileName);
	WritePrivateProfileString(L"window", L"WindowPosBottom", std::to_wstring(myWindowState.r.bottom).c_str(), configFileName);

	// remove vis window, NOTE: THIS IS BAD AND NOT NEEDED EVIDENTLY
	if (IsWindow(this_mod->hwndParent))
		PostMessage(this_mod->hwndParent, WM_WA_IPC, 0, IPC_SETVISWND);

	if (IsWindow(myWindowState.me))
	{
		DestroyWindow(myWindowState.me);
		myWindowState.me = NULL;
	}
}
