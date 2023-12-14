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
} sigs;

typedef bool(*InitializeTimekeeping)(void* _this);
typedef void(*OnClockTick)(void* _this, float a2);

u64 baseAddr = 0;
u64 initTimekeepingAddr = 0;

// corresponds to mov cs:g_ThirtyFPSTicks, rdx in Timekeeping::InitializeTimekeeping
u64 firstOffset = 0x9F;
// corresponds to mov r11, qword ptr cs:g_PerfFreq in Timekeeping::InitializeTimekeeping
u64 secondOffset = 0x73;
u64 potentialFirstOffsets[] = { 0x9F, 0x84 };
u64 potentialSecondOffsets[] = { 0x73, 0x58 };

InitializeTimekeeping orig_InitializeTimekeeping = nullptr;
OnClockTick orig_CAMUpdate = nullptr;

bool hooked_InitializeTimekeeping(void* _this) {
	bool ret = orig_InitializeTimekeeping(_this);

	// need g_ThirtyFPSTicks
	u8* instructions = (u8*)(initTimekeepingAddr + firstOffset);
	u8 expectedInstructions[] = { 0x48, 0x89, 0x15 };

	for (int i = 0; i < sizeof(expectedInstructions); i++) {
		if (instructions[i] != expectedInstructions[i]) {
			printf("[WARNING] Unexpected instructions at Timekeeping::InitializeTimekeeping+%llX (expected 0x%X, actual was 0x%X)\n", firstOffset + i, expectedInstructions[i], instructions[i]);
			printf("[WARNING] Unable to apply FPS patch!\n");
			return ret;
		}
	}

	u64 relAddr = *(u32*)(initTimekeepingAddr + firstOffset + 3);
	u64* g_ThirtyFPSTicks = (u64*)(relAddr + initTimekeepingAddr + firstOffset + 3 + 4);

	// great, now we need g_PerfFreq
	instructions = (u8*)(initTimekeepingAddr + secondOffset);
	expectedInstructions[0] = 0x4C;
	expectedInstructions[1] = 0x8B;
	expectedInstructions[2] = 0x1D;

	for (int i = 0; i < sizeof(expectedInstructions); i++) {
		if (instructions[i] != expectedInstructions[i]) {
			printf("[WARNING] Unexpected instructions at Timekeeping::InitializeTimekeeping+%llX (expected 0x%X, actual was 0x%X)\n", secondOffset + i, expectedInstructions[i], instructions[i]);
			printf("[WARNING] Unable to apply FPS patch!\n");
			return ret;
		}
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

extern "C" __declspec(dllexport) HRESULT WINAPI SLGetWindowsInformationDWORD(_In_ PCWSTR pwszValueName, _Out_ DWORD * pdwValue) {
	printf("checking %ls\n", pwszValueName);
	pdwValue[0] = 1;
	baseAddr = (u64)GetModuleHandle(nullptr);
	printf("addr of game: 0x%llX\n", baseAddr);
	// printf("g_uiConsoleMask? 0x%llX\n", baseAddr + 0xBB334);
	// printf("Enabling debug logs...\n");
	//u32* g_uiConsoleMask = (u32*)(baseAddr + 0xBB334);
	//*g_uiConsoleMask = 0xFFFFFFFF;
	printf("Hooking Timekeeping::InitializeTimekeeping...\n");
	MakeHook(nullptr, sigs.CAMUpdate, hooked_CAMUpdate, (void**)&orig_CAMUpdate);

	for (int i = 0; i < 2; i++) {
		printf("Trying %i\n", i);
		initTimekeepingAddr = FindSig(nullptr, sigs.InitializeTimekeeping[i]);
		if (!initTimekeepingAddr) continue;
		firstOffset = potentialFirstOffsets[i];
		secondOffset = potentialSecondOffsets[i];
		MakeHook(nullptr, sigs.InitializeTimekeeping[i], hooked_InitializeTimekeeping, (void**)&orig_InitializeTimekeeping);
		printf("Success!\n");
		break;
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

