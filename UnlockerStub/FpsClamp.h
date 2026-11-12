#pragma once
#include <cstdint>
#include <string>

// Keeps the client from noticing that its frame rate was unlocked, by making the
// frame rate getter report a stock tier (60/45/30) while the engine keeps the value
// the unlocker writes. FpsClamp.cpp documents the patched instructions.
namespace FpsClamp
{
	enum class Status
	{
		Installed,
		// Another copy of the stub in this process patched the getter first, which is not
		// an error: the launcher's own copy and a DllList-injected copy can both run here.
		AlreadyInstalled,
		Failed,
	};

	// expectedVariable must be the int32 the client itself writes when the frame rate
	// changes (SetupData() already resolved it). Installation aborts when none of the
	// resolved getters reads exactly that variable, so a drifted signature can never end
	// up clamping an unrelated value.
	Status Install(const int32_t* expectedVariable, std::wstring& reason);
	void Uninstall();

	// The switch is the unlocker's own fps_config.json, next to this stub, so the option
	// lives with the rest of the launcher settings instead of in a file of its own:
	// "FpsClamp": false. A missing file or key counts as enabled, so a package that ships
	// an older config file still gets the clamp.
	std::wstring SettingsPath();
	bool IsEnabled();

	// Diagnostic trace: OutputDebugStringW plus one line in %TEMP%\fpsclamp.log, so an
	// install that never happened can still be told apart from a stub that never loaded.
	void Log(const wchar_t* format, ...);
}
