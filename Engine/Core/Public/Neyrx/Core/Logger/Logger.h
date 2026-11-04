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

		void TryAndPublishLogData();




		// Queue Message ------------------------------------------------------------------------------------------------------------
		template<FixedString Format, typename... Args>
		void QueueMessage(LogCategory InCategory, LogLevel InLevel, Args&&... InArgs)
		{
			// TODO - uncomment after development
			/*LogThreadContext* context = GetLogThreadContext();

			while (context->ThreadQueue.IsFull())
			{
				std::this_thread::yield();
			}*/

			using Holder = LogFormatMetadataHolder<Format, Args...>;

			constexpr auto ArgumentCount = GetSpecifierCount<Format>();

			static_assert(sizeof...(Args) == ArgumentCount,
				"Format specifier count does not match argument count");

			static_assert(ArgumentCount <= 8,
				"Argument count exceeded limit of 8");


			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

			uint64_t sequenceNumber = m_globalSequenceNumber.fetch_add(1, std::memory_order_relaxed);

			if (!g_threadContext->ThreadQueue.Enqueue(LogData(sequenceNumber,
				InCategory,
				InLevel,
				Holder::FormatString.Value,
				Holder::Metadata.Operations.data(),
				Holder::Metadata.OperationCount,
				Holder::Metadata.ArgumentCount,
				std::forward<Args>(InArgs)...)
				))
			{
				std::cout << "Queue Failed for Seq: " << sequenceNumber << "\n";
			}

			const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

			const auto& duration = end - start;
			if (duration > std::chrono::milliseconds(1))
				std::cout << "Queue Duration: " << duration << "\n";
		}

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

#define NX_LOG(CATEGORY, LEVEL, FORMAT, ...)  NX_Logger.QueueMessage<FORMAT>(CATEGORY, LEVEL, __VA_ARGS__);