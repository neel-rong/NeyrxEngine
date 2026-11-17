#pragma once
#include <cstdint>



namespace Neyrx::Container
{
	enum class SequenceStatus : uint8_t
	{
		Accepted,			// Valid and inserted
		Duplicate,			// Already marked and is inside the window
		Stale,				// Preceeds the window head
		BeyondCapacity		// Beyond head + window size
	};


	[[nodiscard]] constexpr bool IsAccepted(SequenceStatus Status) noexcept
	{
		return Status == SequenceStatus::Accepted;
	}
}