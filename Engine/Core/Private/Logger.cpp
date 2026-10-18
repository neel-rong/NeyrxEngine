#include "Neyrx/Core/Logger/Logger.h"



using namespace Neyrx::Logger;

// ==========================================================================================================
// Logger Back End
// ==========================================================================================================

void LoggerBackEnd::Initialize()
{
	m_orderedLogWindow.Initialize(&m_loggerThreadRegistry);
}




void LoggerBackEnd::AddSink(ILoggerSink* InSink)
{
	m_sinks.push_back(InSink);
}




void LoggerBackEnd::RegisterLogQueue()
{
	size_t index = m_QueueIndex.fetch_add(1);
	if (index >= m_MaxQueueCount)
	{
		std::cerr << "Maximum number of log queues reached. Cannot register more queues." << std::endl;
		return;
	}
	g_threadContext = &m_loggerThreadRegistry.m_threadContexts[index];
}




bool LoggerBackEnd::IsIdle() const
{
	for (const auto& context : m_loggerThreadRegistry.m_threadContexts)
	{
		if (!context.m_logBuffer.IsEmpty())
		{
			return false;
		}
	}
	return true;
}




uint64_t LoggerBackEnd::GetSkippedCount()
{
	return m_skippedLogs.load(std::memory_order_relaxed);
}




LogThreadContext* LoggerBackEnd::GetLogThreadContext() const;
{
	return g_threadContext;
}




void LoggerBackEnd::QueueMessage(LogCategory InCategory, LogLevel InLevel, std::string InMsg)
{
	auto startTime = std::chrono::steady_clock::now();
	uint64_t sequenceNumber = m_globalSequenceNumber.fetch_add(1, std::memory_order_relaxed);

	if (!g_threadContext->m_logBuffer.Enqueue(LogData(sequenceNumber, InCategory, InLevel, InMsg)))
	{
		std::cout << "\nEnqueue Failed\n" << std::endl;
		m_skippedLogs.fetch_add(1, std::memory_order_release);
		return;
	}
}




void LoggerBackEnd::TryAndPublishLogData()
{
	// Call reserve Slot at the start of every functionCall
	m_orderedLogWindow.ReserveLogSlotsForTracking();

	auto& trackedData = m_orderedLogWindow.GetTrackedLogData();

	PublishCursor_Start = trackedData.Sequence_Begin;

	if (trackedData.GetTrackerState() == LogTrackerState::UNTRACKED)
	{
		PublishCursor_End = PublishCursor_Start;
	}

	const LogTrackerState& trackerState = m_orderedLogWindow.TrackLogData();

	m_orderedLogWindow.PublishStaleData([this](LogData& Data)
		{
			for (auto& sink : m_sinks)
			{
				sink->Publish(Data);
			}
		});

	switch (trackerState)
	{
	case LogTrackerState::TRACKED_ALL :
	{
		PublishCursor_End = trackedData.Sequence_End;

		m_orderedLogWindow.PublishLogData(PublishCursor_End, [this](LogData& Data)
			{
				for (auto& sink : m_sinks)
				{
					sink->Publish(Data);
				}
			});

		break;
	}

	case LogTrackerState::TRACKED_WITH_MISSING :
	{
		uint64_t missing = m_orderedLogWindow.GetFirstMissingSequenceAfter(PublishCursor_End);

		PublishCursor_End = missing - 1;

		m_orderedLogWindow.PublishLogData(PublishCursor_End, [this](LogData& Data)
			{
				for (auto& sink : m_sinks)
				{
					sink->Publish(Data);
				}
			});

		uint64_t validSeqAfterMissing = m_orderedLogWindow.GetFirstValidSequenceAfter(missing);

		const OrderedLogSlot* validSlotAfterMissing = m_orderedLogWindow.GetTrackedSlot(validSeqAfterMissing);

		auto timeStamp = validSlotAfterMissing->Data->Time;

		// Sleep for the Grace Period
		std::this_thread::sleep_until(timeStamp + 1ms);

		PublishCursor_End = trackedData.Sequence_End;

		break;
	}

	case LogTrackerState::FAILURE_EMPTY :
	{
		std::this_thread::sleep_for(1ms);
	}

	default:
	{
		std::cout << "FAILED TO PUBLISH" << std::endl;
		break;
	}

	}
}