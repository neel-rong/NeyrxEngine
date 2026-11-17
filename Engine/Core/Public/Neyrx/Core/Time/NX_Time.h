#pragma once

#include <chrono>

namespace Neyrx::Time
{
	using NX_Clock = std::chrono::steady_clock;
	using NX_TimePoint = NX_Clock::time_point;

	void Initialize();

	double GetElapsedSeconds();
	double GetElapsedMilliseconds();
	double GetDurationSeconds(NX_TimePoint TimePoint);
	double GetDurationMilliseconds(NX_TimePoint TimePoint);
}