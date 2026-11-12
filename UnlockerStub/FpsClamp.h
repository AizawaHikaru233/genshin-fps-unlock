#pragma once
#include <cstdint>
#include <string>

// Keeps the client from noticing that its frame rate was unlocked, by making the
// frame rate getter report a stock tier (60/45/30) while the engine keeps the value
// the unlocker writes. FpsClamp.cpp documents the patched instructions.
namespace FpsClamp
{
	// expectedVariable must be the int32 the client itself writes when the frame rate
	// changes (SetupData() already resolved it). Installation aborts when none of the
	// resolved getters reads exactly that variable, so a drifted signature can never end
	// up clamping an unrelated value.
	bool Install(const int32_t* expectedVariable, std::wstring& reason);
	void Uninstall();

	// Settings live next to the stub so the package GUI can flip them without touching
	// the launcher: %STUBDIR%\FpsClamp.ini, [FpsClamp] Enabled=1 / Log=1. A missing file
	// or key counts as enabled, so a package that ships no file still gets the clamp.
	std::wstring SettingsPath();
	bool IsEnabled();
	// Log=1 in the same file; the GUI keeps it on so a failed install is diagnosable.
	bool IsLoggingEnabled();

	// Diagnostic trace: OutputDebugStringW plus one line in %TEMP%\fpsclamp.log, so an
	// install that never happened can still be told apart from a stub that never loaded.
	// Silenced (file only) when the settings file sets Log=0.
	void Log(const wchar_t* format, ...);
}
