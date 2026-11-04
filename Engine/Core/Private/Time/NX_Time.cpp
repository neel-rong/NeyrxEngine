#include "Neyrx/Time/NX_Time.h"
#include <cassert>




using namespace std::chrono;

// ==========================================================================================================
// NX_Time
// ==========================================================================================================

namespace Neyrx::Time
{
	static NX_TimePoint m_startTime{};
	static bool bTimeInitailize = false;

	void Initialize()
	{
		m_startTime = NX_Clock::now();
		bTimeInitailize = true;
	}




	double GetElapsedSeconds()
	{
		assert(bTimeInitailize == true && "NX_Time not initialized");

		auto elapsed = NX_Clock::now() - m_startTime;

		return duration<double>(elapsed).count();
	}




	double GetElapsedMilliseconds()
	{
		assert(bTimeInitailize == true && "NX_Time not initialized");

		auto elapsed = NX_Clock::now() - m_startTime;

		return duration<double, std::milli>(elapsed).count();
	}




	double GetDurationSeconds(NX_TimePoint TimePoint)
	{
		assert(bTimeInitailize == true && "NX_Time not initialized");

		auto elapsed = TimePoint - m_startTime;

		return duration<double>(elapsed).count();
	}




	double GetDurationMilliseconds(NX_TimePoint TimePoint)
	{
		assert(bTimeInitailize == true && "NX_Time not initialized");

		auto elapsed = TimePoint - m_startTime;

		return duration<double, std::milli>(elapsed).count();
	}
}