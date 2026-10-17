#pragma once
#include <string_view>
#include <chrono>
#include <array>
#include <thread>
#include <cassert>
#include <semaphore>
#include "Neyrx/Core/Container/Queue.h"
#include "Neyrx/Core/Logger/LogDataStructures.h"
#include "Neyrx/Core/Logger/Sink.h"

#include <iostream>// temp
#include <stdio.h> // temp

static constexpr std::size_t NumberOfThreads = 4;
static constexpr std::size_t ThreadQueueSize = 256;

using namespace std::chrono_literals;

void TRACE(uint64_t sequence, std::string event)
{
	//std::cout << "\mTRACE: \nSequence: " << sequence << " | Event: " << event << " | ThreadID: " << std::this_thread::get_id << " | CLOCK: " << std::chrono::system_clock::now() << "\n\n";
}

namespace Neyrx::Logger
{
	// ==========================================================================================================
	// Logger Back End
	// ==========================================================================================================

	class LoggerBackEnd
	{
	public:

		LoggerBackEnd() : m_loggerState() {}

		void Initialize();

		// Member Functions
		void QueueMessage(LogCategory InCategory, LogLevel InLevel, std::string InMsg);



		void TryAndPublishLogData();

		inline void RegisterLogQueue();

		void AddSink(ILoggerSink* InSink)
		{
			m_sinks.push_back(InSink);
		}

		bool IsIdle() const;

		uint64_t GetSkippedCount();

		static ThreadContext* GetThreadContext()
		{
			return g_threadContext;
		}

	private:

		// Member Variables

		LoggerState m_loggerState;
		inline static thread_local ThreadContext* g_threadContext;
		std::atomic<size_t> m_QueueIndex{ 0 };
		const size_t m_MaxQueueCount = 4;

		OrderedLogWindow m_orderedLogWindow;

		std::atomic<uint64_t> m_globalSequenceNumber{ 0 };
		std::atomic<uint64_t> m_lastConsumedSequenceNumber = InvalidSequenceNumber;

		std::atomic<uint64_t> m_skippedLogs{ 0 };

		std::vector<ILoggerSink*> m_sinks;

		uint64_t PublishCursor_Start = InvalidSequenceNumber;
		uint64_t PublishCursor_End = InvalidSequenceNumber;
	};
};

static Neyrx::Logger::LoggerBackEnd NX_Logger;




#define NX_LOG_INFO Neyrx::Logger::LogLevel::Info
#define NX_LOG_WARNING Neyrx::Logger::LogLevel::Warning
#define NX_LOG_ERROR Neyrx::Logger::LogLevel::Error
#define NX_LOG_FATAL Neyrx::Logger::LogLevel::Fatal

#define NX_LOG_TEMP Neyrx::Logger::LogCategory::Temp

#define NX_LOG(CATEGORY, LEVEL, LOGMSG)  NX_Logger.QueueMessage(CATEGORY, LEVEL, LOGMSG);