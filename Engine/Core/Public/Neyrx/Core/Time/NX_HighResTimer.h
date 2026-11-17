#pragma once
#include <Windows.h>
#include <cstdint>




namespace Neyrx::Time
{
	class HighResTimer
	{
	public:
		HighResTimer() :
			m_handle(CreateWaitableTimerExW(nullptr, nullptr,
				CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS))
		{
			// pre-1803 Windows: fall back to a plain timer (coarser)
			if (!m_handle)
			{
				m_handle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
			}
		}

		~HighResTimer()
		{
			if (m_handle) CloseHandle(m_handle);
		}

		HighResTimer(const HighResTimer&) = delete;
		HighResTimer& operator=(const HighResTimer) = delete;

		void SleepForMicroseconds(uint64_t Microseconds)
		{
			LARGE_INTEGER due;
			due.QuadPart = -(Microseconds * 10);
			if (m_handle && SetWaitableTimer(m_handle, &due, 0, nullptr, nullptr, FALSE))
			{
				WaitForSingleObject(m_handle, INFINITE);
			}
			else
			{
				Sleep(1);	// last-resort fallback
			}
		}

	private:

		HANDLE m_handle = nullptr;
	};
}