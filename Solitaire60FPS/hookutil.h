#pragma once

u64 FindSig(u64 start, u64 end, u8 sig[], u64 siglen);
u64 FindSig(const wchar_t* dll, std::string_view sig);
void MakeHook(void* addr, void* func);
void MakeHook(void* addr, void* func, void** origFunc);
void MakeHook(const wchar_t* dll, std::string_view sig, void* func);
void MakeHook(const wchar_t* dll, std::string_view sig, void* func, void** origFunc);
void MakeHook(void* vtable, u64 index, void* func);
void MakePatch(void* addr, u8 patch[], u64 patchlen);
void MakePatch(void* addr, std::string_view patch);
void MakePatch(const wchar_t* dll, std::string_view sig, std::string_view patch);
u64 RelativeToAbsolute(u64 relAddr, u64 nextInstructionAddr);
u64 AbsoluteToRelative(u64 absAddr, u64 nextInstructionAddr);