// dllmain.cpp : Defines the entry point for the DLL application.
#include <stdio.h>
#include <Windows.h>
#include <stdint.h>
#include <string_view>
//#include <stacktrace>
#include <iostream>
#include <MinHook.h>
#include "types.h"
#include "hookutil.h"

using namespace std::literals::string_view_literals;

struct {
	// Timekeeping::InitializeTimekeeping
	std::string_view* InitializeTimekeeping = new std::string_view[2]{
		"\x40\x53\x48\x83\xEC\x20\xBB\x08\x00\x00\x00"sv,
		"\xFF\xF3\x48\x83\xEC\x20\xBB\x08\x00\x00\x00"sv // Hearts
	};
	// CardAnimationManager::Update
	std::string_view CAMUpdate = "\x48\x8B\xC4\x48\x89\x58\x20\xF3\x0F\x11\x48\x10"sv;
	// Timekeeping::Check30FPS
	std::string_view Check30FPS = "\x8B\x15\x2A\x2A\x06\x00\x48\x8B\x0D\x2A\x2A\x06\x00\x44\x8B\x0D\x2A\x2A\x06\x00\x85\xD2"sv;
	// FreeCellGame::Update
	std::string_view FreeCellGameUpdate = "\x48\x8B\xC4\x41\x54\x48\x83\xEC\x50\x48\xC7\x44\x24\x20\xFE\xFF\xFF\xFF"sv;
	// SolitaireGame::OnRunState
	std::string_view SolitaireGameORS = "\x48\x8B\xC4\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x83\xEC\x40\x48\xC7\x44\x24\x20\xFE\xFF\xFF\xFF\x48\x89\x58\x08\x48\x89\x68\x10\x48\x89\x70\x18\x8B\xF2"sv;
	// SpiderGame::OnRunState
	std::string_view SpiderGameORS = "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x57"sv;
} sigs;

typedef bool(*InitializeTimekeeping)(void* _this);
typedef void(*OnClockTick)(void* _this, float a2);
typedef bool(*FreeCellGameUpdate)(void* _this, float a2);
typedef int(*OnRunState)(void* _this, int a2, int a3);

u64 baseAddr = 0;
u64 initTimekeepingAddr = 0;

// corresponds to mov cs:g_ThirtyFPSTicks, rdx in Timekeeping::InitializeTimekeeping
u64 potentialFirstOffsets[] = { 0x9F, 0x84 };
// corresponds to mov r11, qword ptr cs:g_PerfFreq in Timekeeping::InitializeTimekeeping
u64 potentialSecondOffsets[] = { 0x73, 0x58 };
u64 firstOffset = potentialFirstOffsets[0];
u64 secondOffset = potentialSecondOffsets[0];

LARGE_INTEGER* g_NewTime = nullptr;
LARGE_INTEGER* g_LastTime = nullptr;

// used for keeping state updates at 30 FPS
LARGE_INTEGER g_MyPerfFreq;
s64 g_MyThirtyFPSTicks;
LARGE_INTEGER g_MyNewTime;
LARGE_INTEGER g_MyLastTime;
int g_LastORSRet = 0;

InitializeTimekeeping orig_InitializeTimekeeping = nullptr;
OnClockTick orig_CAMUpdate = nullptr;
FreeCellGameUpdate orig_FreeCellGameUpdate = nullptr;
OnRunState orig_OnRunState = nullptr;

int verifyInstructions(u8* instructions, u8* expected, int expectedSize) {
	for (int i = 0; i < expectedSize; i++) {
		if (instructions[i] != expected[i]) {
			return i;
		}
	}

	return 0;
}

bool hooked_InitializeTimekeeping(void* _this) {
	bool ret = orig_InitializeTimekeeping(_this);

	// need g_ThirtyFPSTicks
	u8* instructions = (u8*)(initTimekeepingAddr + firstOffset);
	u8 expectedInstructions[] = { 0x48, 0x89, 0x15 };
	int mismatch = verifyInstructions(instructions, expectedInstructions, sizeof(expectedInstructions));

	if (mismatch) {
		wchar_t msg[256];
		wsprintf(msg, L"Unexpected instructions at Timekeeping::InitializeTimekeeping+%llX (expected 0x%X, actual was 0x%X)\n\nUnable to apply FPS patch!", firstOffset + mismatch, expectedInstructions[mismatch], instructions[mismatch]);
		MessageBox(nullptr, msg, L"Win7CardGames60FPS", MB_ICONERROR | MB_OK);
		return ret;
	}

	u64 relAddr = *(u32*)(initTimekeepingAddr + firstOffset + 3);
	u64* g_ThirtyFPSTicks = (u64*)(relAddr + initTimekeepingAddr + firstOffset + 3 + 4);

	// great, now we need g_PerfFreq
	instructions = (u8*)(initTimekeepingAddr + secondOffset);
	expectedInstructions[0] = 0x4C;
	expectedInstructions[1] = 0x8B;
	expectedInstructions[2] = 0x1D;
	mismatch = verifyInstructions(instructions, expectedInstructions, sizeof(expectedInstructions));

	if (mismatch) {
		wchar_t msg[256];
		wsprintf(msg, L"Unexpected instructions at Timekeeping::InitializeTimekeeping+%llX (expected 0x%X, actual was 0x%X)\n\nUnable to apply FPS patch!", secondOffset + mismatch, expectedInstructions[mismatch], instructions[mismatch]);
		MessageBox(nullptr, msg, L"Win7CardGames60FPS", MB_ICONERROR | MB_OK);
		return ret;
	}

	relAddr = *(u32*)(initTimekeepingAddr + secondOffset + 3);
	LARGE_INTEGER* g_PerfFreq = (LARGE_INTEGER*)(relAddr + initTimekeepingAddr + secondOffset + 3 + 4);
	int targetFramerate = GetPrivateProfileInt(L"Config", L"FPS", 60, L".\\config.ini");
	*g_ThirtyFPSTicks = g_PerfFreq->QuadPart / targetFramerate;

	return ret;
}

void hooked_CAMUpdate(void* _this, float a2) {
	int targetUpdateRate = GetPrivateProfileInt(L"Config", L"UPS", 60, L".\\config.ini");
	orig_CAMUpdate(_this, 1.0f / targetUpdateRate);
}

bool hooked_Check30FPS(void* _this) {
	// TODO: this assumes that the current timekeeping method uses getCurTime
	if (g_NewTime->LowPart - g_LastTime->LowPart > 1000 / GetPrivateProfileInt(L"Config", L"FPS", 60, L".\\config.ini")) {
		if (g_NewTime->LowPart - g_LastTime->LowPart <= 2000) {
			g_LastTime->LowPart += 1000 / GetPrivateProfileInt(L"Config", L"FPS", 60, L".\\config.ini");
		} else {
			g_LastTime->LowPart = g_NewTime->LowPart;
		}
		return true;
	} else {
		return false;
	}
}

bool hooked_FreeCellGameUpdate(void* _this, float a2) {
	int targetUpdateRate = GetPrivateProfileInt(L"Config", L"UPS", 60, L".\\config.ini");
	return orig_FreeCellGameUpdate(_this, 1.0f / targetUpdateRate);
}

void initStateTimekeeping() {
	QueryPerformanceFrequency(&g_MyPerfFreq);
	QueryPerformanceCounter(&g_MyLastTime);
	g_MyThirtyFPSTicks = g_MyPerfFreq.QuadPart / 30;
}

void updateStateTimekeeping() {
	QueryPerformanceCounter(&g_MyNewTime);
	if (g_MyLastTime.QuadPart > g_MyNewTime.QuadPart)
	{
		g_MyNewTime = g_MyLastTime;
	}
}

bool check30FPSStateTimekeeping() {
	if (g_MyNewTime.QuadPart - g_MyLastTime.QuadPart > g_MyThirtyFPSTicks)
	{
		if (g_MyNewTime.QuadPart - g_MyLastTime.QuadPart <= 2 * g_MyPerfFreq.QuadPart)
			g_MyLastTime.QuadPart += g_MyThirtyFPSTicks;
		else
			g_MyLastTime = g_MyNewTime;
		return true;
	}

	return false;
}

int hooked_OnRunState(void* _this, int a2, int a3) {
	updateStateTimekeeping();

	if (check30FPSStateTimekeeping()) {
		g_LastORSRet = orig_OnRunState(_this, a2, a3);
		return g_LastORSRet;
	}

	// just return that last value that we got from OnRunState while we wait
	return g_LastORSRet;
}

extern "C" __declspec(dllexport) HRESULT WINAPI SLGetWindowsInformationDWORD(_In_ PCWSTR pwszValueName, _Out_ DWORD* pdwValue) {
	printf("checking %ls\n", pwszValueName);
	pdwValue[0] = 1;
	baseAddr = (u64)GetModuleHandle(nullptr);
	printf("addr of game: 0x%llX\n", baseAddr);
	//printf("g_uiConsoleMask? 0x%llX\n", baseAddr + 0xBB334);
	//printf("Enabling debug logs...\n");
	//u32* g_uiConsoleMask = (u32*)(baseAddr + 0xBB334);
	//*g_uiConsoleMask = 0xFFFFFFFF;
	//*g_uiConsoleMask = 0x8;
	//bool* g_bDebugEnabled = (bool*)(baseAddr + 0xBB124);
	//*g_bDebugEnabled = true;
	printf("Hooking Timekeeping::InitializeTimekeeping...\n");
	MakeHook(nullptr, sigs.CAMUpdate, hooked_CAMUpdate, (void**)&orig_CAMUpdate);

	bool hookSucceeded = false;

	for (int i = 0; i < 2; i++) {
		printf("Trying %i\n", i);
		initTimekeepingAddr = FindSig(nullptr, sigs.InitializeTimekeeping[i]);
		if (!initTimekeepingAddr) continue;
		firstOffset = potentialFirstOffsets[i];
		secondOffset = potentialSecondOffsets[i];
		MakeHook(nullptr, sigs.InitializeTimekeeping[i], hooked_InitializeTimekeeping, (void**)&orig_InitializeTimekeeping);
		hookSucceeded = true;
		printf("Success!\n");
		break;
	}

	if (!hookSucceeded) {
		printf("InitializeTimekeeping hook failed, trying to hook Check30FPS instead\n");

		// if all of the hook attempts failed, we're probably on the Vista versions, which are slightly different wrt timekeeping
		u64 addr_Check30FPS = FindSig(nullptr, sigs.Check30FPS);
		if (addr_Check30FPS) {
			u8* instructions = (u8*)(addr_Check30FPS + 6);
			u8 expectedInstructions[] = { 0x48, 0x8B, 0x0D };
			int mismatch = verifyInstructions(instructions, expectedInstructions, sizeof(expectedInstructions));
			if (mismatch) {
				wchar_t msg[256];
				wsprintf(msg, L"Unexpected instructions at Timekeeping::Check30FPS+%llX (expected 0x%X, actual was 0x%X)\n\nUnable to apply FPS patch!", addr_Check30FPS + 6 + mismatch, expectedInstructions[mismatch], instructions[mismatch]);
				MessageBox(nullptr, msg, L"Win7CardGames60FPS", MB_ICONERROR | MB_OK);
				return S_OK;
			}
			u64 relAddr = *(u32*)(addr_Check30FPS + 6 + 3);
			g_NewTime = (LARGE_INTEGER*)(relAddr + addr_Check30FPS + 6 + 3 + 4);
			instructions = (u8*)(addr_Check30FPS + 0x0D);
			expectedInstructions[0] = 0x44;
			mismatch = verifyInstructions(instructions, expectedInstructions, sizeof(expectedInstructions));

			if (mismatch) {
				if (mismatch) {
					wchar_t msg[256];
					wsprintf(msg, L"Unexpected instructions at Timekeeping::Check30FPS+%llX (expected 0x%X, actual was 0x%X)\n\nUnable to apply FPS patch!", addr_Check30FPS + 0xD + mismatch, expectedInstructions[mismatch], instructions[mismatch]);
					MessageBox(nullptr, msg, L"Win7CardGames60FPS", MB_ICONERROR | MB_OK);
					return S_OK;
				}
			}

			relAddr = *(u32*)(addr_Check30FPS + 0x0D + 3);
			g_LastTime = (LARGE_INTEGER*)(relAddr + addr_Check30FPS + 0x0D + 3 + 4);

			MakeHook(nullptr, sigs.Check30FPS, hooked_Check30FPS);
			hookSucceeded = true;
			printf("Success!\n");
		}
	}

	if (!hookSucceeded) {
		MessageBox(nullptr, L"Failed to find signatures necessary for the FPS patch.", L"Win7CardGames60FPS", MB_ICONERROR | MB_OK);
	}

	// a side effect of the fps patch causes hints to show up much faster than normal
	// unfortunately, there is no easy way to fix this universally

	if (wcsstr(pwszValueName, L"-FreeCell-")) {
		MakeHook(nullptr, sigs.FreeCellGameUpdate, hooked_FreeCellGameUpdate, (void**)&orig_FreeCellGameUpdate);
	} else if (wcsstr(pwszValueName, L"-SpiderSolitaire-")) {
		initStateTimekeeping();
		g_LastORSRet = 9;
		MakeHook(nullptr, sigs.SpiderGameORS, hooked_OnRunState, (void**)&orig_OnRunState);
	} else if (wcsstr(pwszValueName, L"-Solitaire-")) {
		initStateTimekeeping();
		g_LastORSRet = 6;
		MakeHook(nullptr, sigs.SolitaireGameORS, hooked_OnRunState, (void**)&orig_OnRunState);
	}

	return S_OK;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		// init
#ifdef _DEBUG
		AllocConsole();
		freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
#endif
		MH_Initialize();
		break;
	case DLL_PROCESS_DETACH:
		MH_Uninitialize();
		// deinit
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		// ignore threads, we don't want to accidentally unhook or hook multiple times!
		break;
	}
	return TRUE;
}

