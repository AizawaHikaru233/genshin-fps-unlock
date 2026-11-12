#include <Windows.h>
#include <algorithm>
#include <cstdarg>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <span>
#include "Utils.h"
#include "FpsClampResolve.h"

namespace
{
	// GetFrameCount call site: call, test eax,eax, jle, call, xorps xmm0, cvtsi2ss, jmp.
	constexpr const char* kGetFrameCountSignature =
		"E8 ? ? ? ? 85 C0 7E 0E E8 ? ? ? ? 0F 57 C0 F3 0F 2A C0 EB 08";

	// SetFrameCount call site. Only used when the getter cannot be confirmed otherwise.
	constexpr const char* kSetFrameCountSignature =
		"E8 ? ? ? ? E8 ? ? ? ? 83 F8 1F 0F 9C 05 ? ? ? ? 48 8B 05";

	using Regions = std::vector<FpsClampResolve::Region>;

	bool InCode(const Regions& code, const void* address, size_t length)
	{
		const auto p = static_cast<const uint8_t*>(address);
		for (const auto& region : code)
		{
			if (p >= region.base && p + length <= region.base + region.size)
				return true;
		}

		return false;
	}

	// Call sites reach the body through E9 thunks, and a thunk may point at another thunk.
	// EB (short jump) shows up in hand-written wrappers, so both forms are followed.
	uint8_t* FollowJumps(const Regions& code, uint8_t* address)
	{
		for (int hop = 0; hop < 4; ++hop)
		{
			if (!InCode(code, address, 6))
				return nullptr;

			if (address[0] == 0xE9)
				address += *reinterpret_cast<int32_t*>(address + 1) + 5;
			else if (address[0] == 0xEB)
				address += *reinterpret_cast<int8_t*>(address + 1) + 2;
			else
				return address;
		}

		return address;
	}

	// mov eax,[rip+disp32]; ret -- the getter whose value the client reports.
	bool IsGetterBody(const Regions& code, uint8_t* body)
	{
		return InCode(code, body, 7) && body[0] == 0x8B && body[1] == 0x05 && body[6] == 0xC3;
	}

	// mov [rip+disp32],ecx; ret -- the setter, used only to learn the variable.
	bool IsSetterBody(const Regions& code, uint8_t* body)
	{
		return InCode(code, body, 7) && body[0] == 0x89 && body[1] == 0x0D && body[6] == 0xC3;
	}

	const int32_t* RipTarget(uint8_t* body)
	{
		return reinterpret_cast<const int32_t*>(body + 6 + *reinterpret_cast<int32_t*>(body + 2));
	}

	std::vector<uint8_t*> ScanAll(const Regions& code, const char* pattern)
	{
		std::vector<uint8_t*> hits;
		for (const auto& region : code)
		{
			if (region.size < 32)
				continue;

			const auto found = Utils::PatternScanAll(std::span<uint8_t>(region.base, region.size), pattern);
			hits.insert(hits.end(), found.begin(), found.end());
		}

		return hits;
	}

	// Every leaf function of the form mov eax,[rip+disp32]; ret that reads the given
	// variable. This is the observation that survives a code move or a shifted signature,
	// because it only depends on the variable the unlocker itself writes.
	std::vector<uint8_t*> ScanLeafGetters(const Regions& code, const int32_t* variable)
	{
		std::vector<uint8_t*> leaves;
		const auto wanted = reinterpret_cast<const uint8_t*>(variable);

		for (const auto& region : code)
		{
			if (region.size < 7)
				continue;

			const auto* cursor = region.base;
			const auto* last = region.base + region.size - 7;

			while (cursor <= last)
			{
				const auto span = static_cast<size_t>(last - cursor) + 1;
				const auto* found = static_cast<const uint8_t*>(std::memchr(cursor, 0x8B, span));
				if (!found)
					break;

				if (found[1] == 0x05 && found[6] == 0xC3)
				{
					const auto* target = found + 6 + *reinterpret_cast<const int32_t*>(found + 2);
					if (target == wanted)
						leaves.push_back(const_cast<uint8_t*>(found));
				}

				cursor = found + 1;
			}
		}

		return leaves;
	}

	struct Candidate
	{
		uint8_t* getter;
		const wchar_t* source;
		// True when a call site observation points at this exact getter, which is what
		// lets an otherwise ambiguous set of leaves still be resolved safely.
		bool confirmed;
	};

	void AppendDetail(std::wstring& detail, const wchar_t* format, ...)
	{
		wchar_t buffer[256]{};
		va_list args;
		va_start(args, format);
		vswprintf_s(buffer, format, args);
		va_end(args);

		if (!detail.empty())
			detail += L"; ";

		detail += buffer;
	}
}

bool FpsClampResolve::Resolve(const std::vector<Region>& code, const int32_t* expectedVariable, Result& result, std::wstring& reason)
{
	if (code.empty())
	{
		reason = L"no executable region to scan";
		return false;
	}

	std::wstring detail;

	// --- observation 1: the GetFrameCount call site
	uint8_t* signatureGetter = nullptr;
	const int32_t* signatureVariable = nullptr;
	{
		const auto hits = ScanAll(code, kGetFrameCountSignature);
		if (hits.size() == 1)
		{
			auto* callSite = hits.front();
			auto* body = FollowJumps(code, callSite + 5 + *reinterpret_cast<int32_t*>(callSite + 1));
			if (body && IsGetterBody(code, body))
			{
				signatureGetter = body;
				signatureVariable = RipTarget(body);
			}

			if (!signatureGetter)
				AppendDetail(detail, L"GetFrameCount signature hit at %p does not lead to a getter body", callSite);
		}
		else
		{
			AppendDetail(detail, L"GetFrameCount signature matched %zu times (needs exactly 1)", hits.size());
		}
	}

	// A signature that leads somewhere other than the variable the client actually keeps
	// its frame rate in is not evidence: drop it instead of patching the wrong function.
	if (signatureVariable && expectedVariable && signatureVariable != expectedVariable)
	{
		AppendDetail(detail, L"GetFrameCount getter reads %p, not the frame rate variable %p", signatureVariable, expectedVariable);
		signatureGetter = nullptr;
		signatureVariable = nullptr;
	}

	// The fast path: the call site is where it always was and it reads the known variable.
	if (signatureGetter && (!expectedVariable || signatureVariable == expectedVariable))
	{
		result.getter = signatureGetter;
		result.variable = signatureVariable;
		result.via = L"GetFrameCount call site";
		AppendDetail(detail, L"resolved through the GetFrameCount call site, variable %p", signatureVariable);
		result.detail = detail;
		return true;
	}

	// --- observation 2: the SetFrameCount call site reveals the same variable
	const int32_t* setterVariable = nullptr;
	{
		const auto hits = ScanAll(code, kSetFrameCountSignature);
		if (hits.size() == 1)
		{
			auto* callSite = hits.front();
			auto* body = FollowJumps(code, callSite + 5 + *reinterpret_cast<int32_t*>(callSite + 1));
			if (body && IsSetterBody(code, body))
				setterVariable = RipTarget(body);

			if (!setterVariable)
				AppendDetail(detail, L"SetFrameCount signature hit at %p does not lead to a setter body", callSite);
		}
		else
		{
			AppendDetail(detail, L"SetFrameCount signature matched %zu times (needs exactly 1)", hits.size());
		}
	}

	if (setterVariable && expectedVariable && setterVariable != expectedVariable)
	{
		AppendDetail(detail, L"SetFrameCount setter writes %p, not the frame rate variable %p", setterVariable, expectedVariable);
		setterVariable = nullptr;
	}

	const int32_t* variable = expectedVariable ? expectedVariable : (signatureVariable ? signatureVariable : setterVariable);
	if (!variable)
	{
		result.detail = detail;
		reason = L"none of the strategies found the frame rate variable";
		if (!detail.empty())
			reason += L" (" + detail + L")";

		return false;
	}

	// --- observation 3: the leaf getter itself
	const auto leaves = ScanLeafGetters(code, variable);
	AppendDetail(detail, L"leaf getters reading %p: %zu", variable, leaves.size());

	std::vector<Candidate> candidates;
	if (signatureGetter && signatureVariable == variable)
		candidates.push_back({ signatureGetter, L"GetFrameCount call site", true });

	for (auto* leaf : leaves)
	{
		const bool known = std::ranges::any_of(candidates, [leaf](const Candidate& candidate) { return candidate.getter == leaf; });
		if (!known)
			candidates.push_back({ leaf, L"leaf getter reading the frame rate variable", false });
	}

	if (candidates.empty())
	{
		result.detail = detail;
		reason = L"no getter returns the frame rate variable";
		if (!detail.empty())
			reason += L" (" + detail + L")";

		return false;
	}

	// More than one leaf would mean patching an arbitrary one of them, and a wrong choice
	// is invisible at run time (the client simply keeps reading the unlocked value), so
	// only a candidate confirmed by the call site, or a single unambiguous leaf, is taken.
	const Candidate* chosen = &candidates.front();
	if (candidates.size() > 1)
	{
		const auto confirmed = std::ranges::find_if(candidates, [](const Candidate& candidate) { return candidate.confirmed; });
		if (confirmed == candidates.end())
		{
			result.detail = detail;
			wchar_t message[160]{};
			swprintf_s(message, L"%zu candidate getters and none of them confirmed by a call site", candidates.size());
			reason = message;
			return false;
		}

		chosen = &*confirmed;
	}

	result.getter = chosen->getter;
	result.variable = variable;
	result.via = chosen->source;
	AppendDetail(detail, L"chose %p through the %s", chosen->getter, chosen->source);
	result.detail = detail;
	return true;
}
