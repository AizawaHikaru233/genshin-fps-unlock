#include <Windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "Utils.h"
#include "FpsClamp.h"
#include "FpsClampResolve.h"

// 2026-11-12: the client keeps its configured frame rate in a single int32 and reads it
// back through a 7-byte leaf getter
//
//     8B 05 <disp32>   mov eax, dword ptr [rip+disp]
//     C3               ret
//
// reached from il2cpp code through E9 jump thunks. Reporting the unlocked value to the
// client is what makes it treat the session as misconfigured, so the getter is rewritten
// to
//
//     E9 <rel32> 90    jmp stub / nop
//
// and the stub loads the same variable, quantizes it to the tiers the game itself offers
// (>=60 -> 60, >=45 -> 45, >=30 -> 30) and returns. No trampoline is needed: the replaced
// instruction is a complete leaf body, so the stub returns by itself and the engine keeps
// running with the unlocked value.
//
// Measured on YuanShen.exe 450,927,000 B (2026-11-04): .text 0x16b9220 reads RVA
// 0x55b0764, which is exactly the variable the setter (0x16b9230) writes and SetupData()
// resolves. That equality is asserted before anything is patched, but the getter itself is
// no longer located by one fixed signature: FpsClampResolve combines the call sites with a
// scan for leaf getters reading that variable, so a game update that shifts the signature
// still resolves.
namespace FpsClamp
{
	std::wstring SettingsPath();
	bool IsEnabled();
}

namespace
{
	uint8_t* g_getter = nullptr;
	uint8_t* g_stub = nullptr;
	uint8_t g_originalBytes[6]{};

	void LogToFile(const wchar_t* message);

	void AppendBytes(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes)
	{
		code.insert(code.end(), bytes.begin(), bytes.end());
	}

	void AppendInt32(std::vector<uint8_t>& code, int32_t value)
	{
		const auto raw = reinterpret_cast<const uint8_t*>(&value);
		code.insert(code.end(), raw, raw + sizeof(value));
	}

	// The client calls the getter from its own threads, and a patch written in two stores can
	// be executed half applied. When the target sits inside an aligned 8-byte block the bytes
	// are exchanged with a single store, keeping the neighbours untouched.
	void WriteCode(uint8_t* target, const uint8_t* bytes, size_t count)
	{
		if (count <= sizeof(uint64_t) && (reinterpret_cast<uintptr_t>(target) & 7) == 0)
		{
			uint64_t block = 0;
			memcpy(&block, target, sizeof(block));
			memcpy(&block, bytes, count);
			InterlockedExchange64(reinterpret_cast<volatile LONG64*>(target), static_cast<LONG64>(block));
		}
		else
		{
			memcpy(target, bytes, count);
		}
	}

	bool Fail(std::wstring& reason, std::wstring message)
	{
		reason = std::move(message);
		LogToFile(reason.c_str());
		return false;
	}

	// Every executable section of the game module. The getter used to be looked up in the
	// il2cpp section only, but both it and its thunks can live in .text, and a rebuilt
	// client may rename sections, so the whole executable surface is scanned.
	std::vector<FpsClampResolve::Region> CodeRegions()
	{
		std::vector<FpsClampResolve::Region> regions{};

		const auto imageBase = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
		const auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(imageBase);
		const auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(imageBase + dosHeader->e_lfanew);

		std::span sections{ IMAGE_FIRST_SECTION(ntHeaders), ntHeaders->FileHeader.NumberOfSections };
		for (const auto& section : sections)
		{
			if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
				continue;

			if (section.Misc.VirtualSize < 32)
				continue;

			regions.push_back({ imageBase + section.VirtualAddress, section.Misc.VirtualSize });
		}

		return regions;
	}

	// No debug output viewer is installed on the test machine and the stub runs inside the
	// game, so the install result is also appended to %TEMP%\fpsclamp.log. Written once at
	// install time only; the frame loop never touches this.
	void LogToFile(const wchar_t* message)
	{
		if (!FpsClamp::IsLoggingEnabled())
			return;

		wchar_t directory[MAX_PATH]{};
		if (!GetTempPathW(MAX_PATH, directory))
			return;

		std::wstring path = directory;
		path += L"fpsclamp.log";

		const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return;

		SYSTEMTIME now{};
		GetLocalTime(&now);

		wchar_t line[1024]{};
		const int characters = swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u pid %lu | %s\r\n",
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId(), message);

		if (characters > 0)
		{
			// UTF-8 so the file can be read without guessing an encoding from a log viewer.
			char utf8[3072]{};
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, characters, utf8, sizeof(utf8), nullptr, nullptr);
			if (bytes > 0)
			{
				DWORD written = 0;
				WriteFile(file, utf8, static_cast<DWORD>(bytes), &written, nullptr);
			}
		}

		CloseHandle(file);
	}

	void Trace(const wchar_t* format, ...)
	{
		wchar_t message[1024]{};
		va_list args;
		va_start(args, format);
		vswprintf_s(message, format, args);
		va_end(args);

		OutputDebugStringW(message);
		LogToFile(message);
	}
}

std::wstring FpsClamp::SettingsPath()
{
	static const std::wstring path = []
	{
		wchar_t module[MAX_PATH]{};
		HMODULE self = nullptr;

		// Any address inside this DLL maps back to its own module, which is where the
		// launcher extracts the stub and where the package GUI writes the settings file.
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&FpsClamp::IsEnabled), &self))
			return std::wstring(L"FpsClamp.ini");

		const auto length = GetModuleFileNameW(self, module, MAX_PATH);
		if (length == 0 || length == MAX_PATH)
			return std::wstring(L"FpsClamp.ini");

		std::wstring directory(module, length);
		const auto slash = directory.find_last_of(L'\\');
		if (slash != std::wstring::npos)
			directory.resize(slash + 1);

		return directory + L"FpsClamp.ini";
	}();

	return path;
}

bool FpsClamp::IsEnabled()
{
	return GetPrivateProfileIntW(L"FpsClamp", L"Enabled", 1, SettingsPath().c_str()) != 0;
}

bool FpsClamp::IsLoggingEnabled()
{
	return GetPrivateProfileIntW(L"FpsClamp", L"Log", 1, SettingsPath().c_str()) != 0;
}

bool FpsClamp::Install(const int32_t* expectedVariable, std::wstring& reason)
{
	if (g_getter)
		return true;

	const auto moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	Trace(L"install requested, game module base %p, variable %p, settings %s", reinterpret_cast<void*>(moduleBase),
		expectedVariable, SettingsPath().c_str());

	const auto regions = CodeRegions();
	if (regions.empty())
		return Fail(reason, L"no executable section found in the game module");

	FpsClampResolve::Result resolution{};
	if (!FpsClampResolve::Resolve(regions, expectedVariable, resolution, reason))
		return Fail(reason, reason);

	const auto getter = resolution.getter;
	const auto getterVariable = resolution.variable;
	const auto stub = Utils::AllocatePage(getter, 0x1000, PAGE_EXECUTE_READWRITE);
	if (!stub)
		return Fail(reason, L"failed to allocate a page next to the getter");

	// Utils::AllocatePage() walks outwards from the getter and, when the neighbourhood is
	// occupied, can hand back a page far away. Both the jump from the getter and the load
	// inside the stub use 32-bit relative displacements, so a distant page would make the
	// client jump into nowhere. Measured: running the same stub from a distant page crashed
	// with 0xC0000005, so the distance is asserted before anything is patched.
	constexpr auto kMaxRel32 = static_cast<intptr_t>(INT32_MAX);
	constexpr auto kMinRel32 = -kMaxRel32 - 1;
	const auto jumpDistance = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(getter + 5);
	const auto loadDistance = reinterpret_cast<intptr_t>(getterVariable) - reinterpret_cast<intptr_t>(stub + 6);
	if (jumpDistance < kMinRel32 || jumpDistance > kMaxRel32 || loadDistance < kMinRel32 || loadDistance > kMaxRel32)
	{
		VirtualFree(stub, 0, MEM_RELEASE);

		wchar_t message[192]{};
		swprintf_s(message, L"allocated stub at %p is out of reach from %p, refusing to patch", stub, getter);
		return Fail(reason, message);
	}

	// mov eax,[rip+disp] / cmp eax,60 / jl next / mov eax,60 / ret / (45) / (30) / ret
	std::vector<uint8_t> code;
	code.reserve(40);
	AppendBytes(code, { 0x8B, 0x05 });
	AppendInt32(code, static_cast<int32_t>(reinterpret_cast<intptr_t>(getterVariable) - reinterpret_cast<intptr_t>(stub + 6)));

	for (const int32_t tier : { 60, 45, 30 })
	{
		AppendBytes(code, { 0x83, 0xF8, static_cast<uint8_t>(tier) });
		AppendBytes(code, { 0x7C, 0x06 });
		AppendBytes(code, { 0xB8 });
		AppendInt32(code, tier);
		AppendBytes(code, { 0xC3 });
	}

	AppendBytes(code, { 0xC3 });

	memcpy(stub, code.data(), code.size());
	FlushInstructionCache(GetCurrentProcess(), stub, code.size());

	memcpy(g_originalBytes, getter, sizeof(g_originalBytes));

	uint8_t jump[sizeof(g_originalBytes)];
	memcpy(jump, g_originalBytes, sizeof(jump));
	jump[0] = 0xE9;
	*reinterpret_cast<int32_t*>(&jump[1]) = static_cast<int32_t>(jumpDistance);
	jump[5] = 0x90;

	DWORD oldProtect = 0;
	if (!VirtualProtect(getter, sizeof(g_originalBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		VirtualFree(stub, 0, MEM_RELEASE);
		return Fail(reason, L"failed to make the getter writable");
	}

	WriteCode(getter, jump, sizeof(jump));
	VirtualProtect(getter, sizeof(g_originalBytes), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), getter, sizeof(g_originalBytes));

	g_getter = getter;
	g_stub = stub;

	Trace(L"[FPS Unlocker] frame rate clamp installed: getter %p -> stub %p, variable %p (reads %d), via %s [%s]",
		getter, stub, getterVariable, *getterVariable, resolution.via.c_str(), resolution.detail.c_str());
	return true;
}

void FpsClamp::Uninstall()
{
	if (!g_getter)
		return;

	DWORD oldProtect = 0;
	VirtualProtect(g_getter, sizeof(g_originalBytes), PAGE_EXECUTE_READWRITE, &oldProtect);
	WriteCode(g_getter, g_originalBytes, sizeof(g_originalBytes));
	VirtualProtect(g_getter, sizeof(g_originalBytes), oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), g_getter, sizeof(g_originalBytes));

	VirtualFree(g_stub, 0, MEM_RELEASE);
	g_getter = nullptr;
	g_stub = nullptr;
}

namespace FpsClamp
{
	void Log(const wchar_t* format, ...)
	{
		wchar_t message[1024]{};
		va_list args;
		va_start(args, format);
		vswprintf_s(message, format, args);
		va_end(args);

		OutputDebugStringW(message);
		LogToFile(message);
	}
}
