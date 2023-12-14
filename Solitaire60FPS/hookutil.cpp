#include <string_view>
#include <cassert>
#include "types.h"
#include "hookutil.h"

#define WIN32_LEAN_AND_MEAN
#include <MinHook.h>

u64 FindSig(u64 start, u64 end, u8 sig[], u64 siglen) {
	for (u64 cur = start; cur < end; cur++) {
		for (u64 i = 0; i < siglen; i++) {
			u8* b = (u8*)(cur + i);
			if (*b != sig[i] && sig[i] != '*') break;
			if (i == siglen - 1) return cur;
		}
	}

	return 0;
}

u64 FindSig(const wchar_t* dll, std::string_view sig) {
	u64 base = (u64)GetModuleHandle(dll);

	auto dos = (IMAGE_DOS_HEADER*)base;
	auto nt = (IMAGE_NT_HEADERS*)((u8*)dos + dos->e_lfanew);

	return FindSig(base, base + nt->OptionalHeader.SizeOfImage, (u8*)sig.data(), sig.length());
}

void MakeHook(void* addr, void* func) {
	MH_CreateHook(addr, func, nullptr);
	MH_EnableHook(addr);
}

void MakeHook(void* addr, void* func, void** origFunc) {
	MH_CreateHook(addr, func, origFunc);
	MH_EnableHook(addr);
}

void MakeHook(const wchar_t* dll, std::string_view sig, void* func) {
	u64 addr = FindSig(dll, sig);
	
	assert(addr);
	
	if (addr != 0) MakeHook((void*)addr, func);
}

void MakeHook(const wchar_t* dll, std::string_view sig, void* func, void** origFunc) {
	u64 addr = FindSig(dll, sig);
	
	assert(addr);

	if (addr != 0) MakeHook((void*)addr, func, origFunc);
}

void MakeHook(void* vtable, u64 index, void* func) {
	DWORD oldProtect;
	void** vt = *(void***)vtable;
	VirtualProtect(&vt[index], sizeof(u64), PAGE_EXECUTE_READWRITE, &oldProtect);
	vt[index] = func;
	VirtualProtect(&vt[index], sizeof(u64), oldProtect, &oldProtect);
}

void MakePatch(void* addr, u8 patch[], u64 patchlen) {
	DWORD oldProtect;
	VirtualProtect(addr, patchlen, PAGE_EXECUTE_READWRITE, &oldProtect);

	for (u64 i = 0; i < patchlen; i++) {
		u8* byte = (u8*)((u64)addr + i);
		*byte = patch[i];
	}

	VirtualProtect(addr, patchlen, oldProtect, &oldProtect);
}

void MakePatch(void* addr, std::string_view patch) {
	MakePatch(addr, (u8*)patch.data(), patch.length());
}

void MakePatch(const wchar_t* dll, std::string_view sig, std::string_view patch) {
	u64 addr = FindSig(dll, sig);

	if (addr != 0) MakePatch((void*)addr, (u8*)patch.data(), patch.length());
}

u64 RelativeToAbsolute(u64 relAddr, u64 nextInstructionAddr) {
	return relAddr + nextInstructionAddr;
}

u64 AbsoluteToRelative(u64 absAddr, u64 nextInstructionAddr) {
	return absAddr - nextInstructionAddr;
}