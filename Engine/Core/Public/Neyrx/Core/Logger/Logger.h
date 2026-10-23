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

namespace Neyrx::Logger
{
	// ==========================================================================================================
	// Logger Back End
	// ==========================================================================================================

	class Logger
	{
	public:

		Logger() : m_loggerThreadRegistry() {}

		// =====================================
		// Member Functions
		// =====================================

		void Initialize();
		void AddSink(ILoggerSink* InSink);
		void RegisterLogQueue();

		LogThreadContext* GetLogThreadContext();
		uint64_t GetInvertedCount();
		uint64_t GetSkippedCount();
		bool IsIdle() const;

		void QueueMessage(LogCategory InCategory, LogLevel InLevel, std::string InMsg);
		void TryAndPublishLogData();

	private:

		// =====================================
		// Member Parameters
		// =====================================

		LoggerThreadRegistry m_loggerThreadRegistry;
		inline static thread_local LogThreadContext* g_threadContext = nullptr;
		std::atomic<size_t> m_QueueIndex{ 0 };
		const size_t m_MaxQueueCount = NumberOfThreads;

		OrderedLogWindow m_orderedLogWindow;

		std::atomic<uint64_t> m_globalSequenceNumber{ 0 };
		std::atomic<uint64_t> m_lastConsumedSequenceNumber = InvalidSequenceNumber;

		std::vector<ILoggerSink*> m_sinks;

		uint64_t PublishCursor_Start = InvalidSequenceNumber;
		uint64_t PublishCursor_End = InvalidSequenceNumber;
	};
};

extern Neyrx::Logger::Logger NX_Logger;




#define NX_LOG_INFO Neyrx::Logger::LogLevel::Info
#define NX_LOG_WARNING Neyrx::Logger::LogLevel::Warning
#define NX_LOG_ERROR Neyrx::Logger::LogLevel::Error
#define NX_LOG_FATAL Neyrx::Logger::LogLevel::Fatal

#define NX_LOG_TEMP Neyrx::Logger::LogCategory::Temp

#define NX_LOG(CATEGORY, LEVEL, LOGMSG)  NX_Logger.QueueMessage(CATEGORY, LEVEL, LOGMSG);