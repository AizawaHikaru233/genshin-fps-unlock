#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Resolves the frame rate getter without trusting a single byte sequence. Three
// independent observations are available on every build of the client:
//
//   1. the GetFrameCount call site (an explicit call, then test/jle/cvtsi2ss),
//   2. the SetFrameCount call site, whose setter reveals the same variable,
//   3. the 7-byte leaf getter itself: mov eax,[rip+disp]; ret reading that variable.
//
// (3) is the version-proof one: it depends on the shape of a one-instruction function
// and on the variable the unlocker already writes, not on any surrounding code. (1) and
// (2) are used to confirm it, and to keep working when the leaf shape drifts instead.
namespace FpsClampResolve
{
	struct Region
	{
		uint8_t* base;
		size_t size;
	};

	struct Result
	{
		uint8_t* getter = nullptr;
		const int32_t* variable = nullptr;
		// Which observation produced the getter, for the install log.
		std::wstring via;
		// Every observation and what it produced, so a future failure can be diagnosed
		// from the log alone instead of from a debugger session.
		std::wstring detail;
	};

	// expectedVariable is the int32 the unlocker writes (SetupData() resolved it); it may
	// be null when the caller has no opinion. Returns false with a reason when no getter
	// can be proven, or when the observations contradict each other.
	bool Resolve(const std::vector<Region>& code, const int32_t* expectedVariable, Result& result, std::wstring& reason);
}
