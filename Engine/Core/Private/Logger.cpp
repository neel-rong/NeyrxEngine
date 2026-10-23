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




void Logger::QueueMessage(LogCategory InCategory, LogLevel InLevel, std::string InMsg)
{
	/*LogThreadContext* context = GetLogThreadContext();

	while (context->ThreadQueue.IsFull())
	{
		std::this_thread::yield();
	}*/

	uint64_t sequenceNumber = m_globalSequenceNumber.fetch_add(1, std::memory_order_relaxed);

	if (!g_threadContext->ThreadQueue.Enqueue(LogData(std::this_thread::get_id(), sequenceNumber, InCategory, InLevel, InMsg)))
	{
		std::cout << "Queue Failed for Seq: " << sequenceNumber << "\n";
	}
}




void Logger::TryAndPublishLogData()
{
	// Call reserve Slot at the start of every functionCall
	m_orderedLogWindow.ReserveLogSlotsForTracking();

	// Get a reference to the TrackedLogData
	auto& trackedData = m_orderedLogWindow.GetTrackedLogData();

	// Always set PublishCursor_Start to tracked begin. PublishCursor_End is resolved based on missing index.
	PublishCursor_Start = trackedData.Sequence_Begin;

	if (trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED)
	{
		PublishCursor_Start = 0;
		PublishCursor_End = 0;
	}
	else if (trackedData.GetTrackerState() == LogTrackerState::UNTRACKED)
	{
		PublishCursor_Start = trackedData.Sequence_MaxTracked + 1;
		PublishCursor_End = PublishCursor_Start;
	}

	const LogTrackerState& trackerState = m_orderedLogWindow.TrackLogData();


	switch (trackerState)
	{
	case LogTrackerState::TRACKED_ALL :
	{
		PublishCursor_End = trackedData.Sequence_End;

		m_orderedLogWindow.PublishLogData(PublishCursor_End, [this](const LogData& Data)
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

		if (missing != InvalidSequenceNumber)
		{
			if (missing == trackedData.Sequence_Begin)
			{
				break;
			}
			PublishCursor_End = missing - 1;
		}

		m_orderedLogWindow.PublishLogData(PublishCursor_End, [this](LogData& Data)
			{
				for (auto& sink : m_sinks)
				{
					sink->Publish(Data);
				}
			});

		if (!trackedData.Sequence_Missing.empty())
		{
			missing = m_orderedLogWindow.GetFirstMissingSequenceAfter(PublishCursor_End);

			const OrderedLogSlot* validSlotAfterMissing = m_orderedLogWindow.GetFirstValidLogSlotAfter(missing);

			if (validSlotAfterMissing == nullptr)
			{
				__debugbreak();
				assert(false);
				return;
			}

			auto timeStamp = validSlotAfterMissing->Data->Time;

			// Sleep for the Grace Period
			//std::cout << "Sleeping............" << std::endl;
			std::this_thread::sleep_until(timeStamp + std::chrono::milliseconds(1));
		}

		PublishCursor_End = trackedData.Sequence_End;

		break;
	}

	case LogTrackerState::FAILURE_EMPTY :
	{
		//std::cout << "Sleeping............" << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		break;
	}

	default:
	{
		//std::cout << "FAILED TO PUBLISH" << std::endl;
		break;
	}

	}

	// After the TrackedLogData Publish loop, check for any stale data and publish and Dequeue
	m_orderedLogWindow.PublishLogData_Stale([this](const LogData& Data)
		{
			for (const auto& sink : m_sinks)
			{
				sink->Publish(Data);
			}
		});
}