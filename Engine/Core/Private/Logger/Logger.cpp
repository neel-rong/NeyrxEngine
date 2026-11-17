#include <immintrin.h>
#include "Neyrx/Core/Logger/Logger.h"




using namespace Neyrx::Logger;

Logger NX_Logger;

// ==========================================================================================================
// Logger Back End
// ==========================================================================================================

void Logger::Initialize()
{
	m_orderedLogWindow.Initialize(&m_loggerThreadRegistry);
}




void Logger::AddSink(ILoggerSink* InSink)
{
	m_sinks.push_back(InSink);
}




void Logger::RegisterLogQueue()
{
	size_t index = m_QueueIndex.fetch_add(1);
	if (index >= m_MaxQueueCount)
	{
		std::cerr << "Maximum number of log queues reached. Cannot register more queues." << std::endl;
		return;
	}
	g_threadContext = &m_loggerThreadRegistry.m_threadContexts[index];
}




bool Logger::IsIdle() const
{
	for (const auto& context : m_loggerThreadRegistry.m_threadContexts)
	{
		if (!context.ThreadQueue.IsEmpty())
		{
			return false;
		}
	}
	return true;
}




uint64_t Logger::GetInvertedCount()
{
	return m_orderedLogWindow.GetInvertedCount();
}




uint64_t Logger::GetSkippedCount()
{
	return m_orderedLogWindow.GetSkippedCount();
}




LogThreadContext* Logger::GetLogThreadContext()
{
	return g_threadContext;
}




void Logger::TryAndPublishLogData()
{
	const OrderedLogWindow::ConsumePassResult result = m_orderedLogWindow.ConsumePass([&](LogArena& Arena)
		{
			LogPublishState state;
			for (auto sink : m_sinks)
			{
				state = sink->Publish(Arena);

				if (state != LogPublishState::SUCCESS) assert(false && "Logger Sink failed to publish");
			}

			return state;
		});

	switch (result.State)
	{
	case LogPublishState::SUCCESS_WITH_MISSING :
	{
		NX_TimePoint now = NX_Clock::now();
		auto timeout = m_orderedLogWindow.GetGapTimeout();
		auto gapSince = m_orderedLogWindow.GetGapSince();

		if (result.PublishedCount == 0)
		{
			m_highResTimer.SleepForMicroseconds(100); // Queues are empty
		}
		else
		{
			for (int i = 0; i < 16; ++i) _mm_pause();
		}
		break;
	}

	case LogPublishState::FAILURE_EMPTY :
	{
		m_highResTimer.SleepForMicroseconds(100);
		break;
	}

	default :
	{
		assert(false && "Logger: Invalid internal state");
		break;
	}
	}
}