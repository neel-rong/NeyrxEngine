#pragma once
#include <string_view>
#include <iostream>
#include <stdio.h>
#include <chrono>
#include <array>
#include <thread>
#include <cassert>
#include "Container/Queue.h"
#include <semaphore>

static constexpr uint64_t InvalidSequenceNumber = std::numeric_limits<uint64_t>::max();
static constexpr uint64_t Invalidindex = std::numeric_limits<uint64_t>::max();

static constexpr std::size_t m_numberOfThreads = 4;

using namespace std::chrono_literals;

void TRACE(uint64_t sequence, std::string event)
{
	//std::cout << "\mTRACE: \nSequence: " << sequence << " | Event: " << event << " | ThreadID: " << std::this_thread::get_id << " | CLOCK: " << std::chrono::system_clock::now() << "\n\n";
}

namespace Neyrx
{
	// ========================================
	// Logger
	// ========================================

	namespace Logger
	{

		// =============================
		// Log Levels
		// =============================
#define NEYRX_LOG_LEVELS(X) \
	X(Info)					\
	X(Warning)				\
	X(Error)				\
	X(Fatal)

		enum class LogLevel
		{
#define X(Name) Name,
			NEYRX_LOG_LEVELS(X)
#undef X
		};

		static constexpr std::string_view ToString(LogLevel InLevel)
		{
			switch (InLevel)
			{
#define X(Name) case LogLevel::Name : return #Name;
				NEYRX_LOG_LEVELS(X);
#undef X
			}

			return {};
		}

		// =============================
		// Log Category
		// =============================

#define NEYRX_LOG_CATEGORY(X)	\
	X(Temp)						\


		enum class LogCategory
		{
#define X(Name) Name,
			NEYRX_LOG_CATEGORY(X)
#undef X
		};

		static constexpr std::string_view ToString(LogCategory InCategory)
		{
			switch (InCategory)
			{
#define X(Name) case LogCategory::Name : return #Name;
				NEYRX_LOG_CATEGORY(X);
#undef X
			}

			return {};
		}


		// =============================
		// Log Data
		// =============================

		struct LogData
		{
			LogData() {};

			LogData(const LogData&) = default;
			LogData(LogData&&) noexcept = default;

			LogData& operator=(const LogData&) = default;
			LogData& operator=(LogData&&) noexcept = default;

			LogData(LogLevel InLevel, LogCategory InCategory, std::string InMsg, uint64_t InSequenceNumber) :
				m_level(InLevel),
				m_category(InCategory),
				m_msg(std::move(InMsg)),
				m_sequence(InSequenceNumber),
				m_time(std::chrono::system_clock::now())
			{
			}

			void PrintData() const
			{
				std::cout << "[" << m_time << "]" << "	" << ToString(m_level) << "	" << ToString(m_category) << "	" << m_msg << " | Sequence: " << m_sequence << "\n";
			}


			LogLevel m_level = LogLevel::Info;
			LogCategory m_category = LogCategory::Temp;
			std::string m_msg = "";

			std::chrono::system_clock::time_point m_time = std::chrono::system_clock::now();
			uint64_t m_sequence = InvalidSequenceNumber;
		};


		// ====================================
		// Logger Sink
		// ====================================

		class ILoggerSink
		{
		public:
			virtual ~ILoggerSink() = default;

			virtual void Consume(const LogData& InLogData) = 0;
		};


		class IConsoleSink : public ILoggerSink
		{
		public:
			virtual ~IConsoleSink() = default;
			inline virtual void Consume(const LogData& InLogData) override
			{
				InLogData.PrintData();
			}
		};


		static class LoggerClass
		{
		public:

			LoggerClass() : m_loggerState(), m_semaphoreNextSequence(0) {}

			// Member Functions
			inline void QueueMessage(LogCategory InCategory, LogLevel InLevel, std::string InMsg)
			{
				uint64_t sequenceNumber = m_globalSequenceNumber.fetch_add(1, std::memory_order_relaxed);

				if (!g_threadContext->m_logBuffer.IsFull())
				{
					g_threadContext->m_logBuffer.Enqueue(LogData(InLevel, InCategory, InMsg, sequenceNumber));

					if (sequenceNumber == m_lastConsumedSequenceNumber.load(std::memory_order_relaxed) + 1)
					{
						m_semaphoreNextSequence.release();
					}
				}
				else
				{
					std::cout << "\nQueue is Full. Dropping Log!!!!!!!!" << std::endl;
					m_skippedLogs.fetch_add(1, std::memory_order_release);
				}
			}

			inline bool StageThreadContexts()
			{
				bool staged = false;

				for (std::size_t i = 0; i < m_loggerState.m_threadContexts.size(); ++i)
				{
					assert(m_stagedQueue.IsValidQueueIndex(i));
					assert(m_loggerState.IsValidThreadIndex(i));

					if (!m_stagedQueue.IsInvalidSequence(i)) continue;

					ThreadContext& context = m_loggerState.m_threadContexts[i];

					LogData* data = m_stagedQueue.GetData(i);

					if (context.m_logBuffer.Dequeue(*data))
					{
						staged = true;
					}
				}

				return staged;
			}

			inline void ConsumeMessage()
			{
				StageThreadContexts();

				if (m_stagedQueue.IsStageEmpty()) return;

				std::size_t index = m_stagedQueue.GetNextIndexFromSequenceNumber();

				if (index == Invalidindex) return; // TODO - all queues are empty, need to sleep until atleast one queue is full

				LogData* data = m_stagedQueue.GetData(index);

				uint64_t expected;

				uint64_t lastConsumed = m_lastConsumedSequenceNumber.load(std::memory_order_relaxed);

				if (m_lastConsumedSequenceNumber == InvalidSequenceNumber) expected = 0;
				else expected = m_lastConsumedSequenceNumber + 1;

				if (data->m_sequence != expected)
				{
					m_semaphoreNextSequence.try_acquire_for(1ms);

					StageThreadContexts();

					index = m_stagedQueue.GetNextIndexFromSequenceNumber();

					if (index == Invalidindex) return; // TODO - all queues are empty, need to sleep until atleast one queue is full

					data = m_stagedQueue.GetData(index);

					// If the next Sequence has failed to arrive within the grace period
					// the consumer abondons strict ordering for this sequence
					// rather than stalling the consumer indefintely
				}

				for (auto sink : m_sinks)
				{
					sink->Consume(*data);
				}

				m_lastConsumedSequenceNumber.store(data->m_sequence, std::memory_order_release);
				m_stagedQueue.MarkInvalid(index);
			}

			inline void RegisterLogQueue()
			{
				size_t index = m_QueueIndex.fetch_add(1);
				if (index >= m_MaxQueueCount)
				{
					std::cerr << "Maximum number of log queues reached. Cannot register more queues." << std::endl;
					return;
				}
				g_threadContext = &m_loggerState.m_threadContexts[index];
			}

			void AddSink(ILoggerSink* InSink)
			{
				m_sinks.push_back(InSink);
			}

			bool IsIdle() const
			{
				for (const auto& context : m_loggerState.m_threadContexts)
				{
					if (!context.m_logBuffer.IsEmpty())
					{
						return false;
					}
				}
				return true;
			}

			int GetSkippedCount()
			{
				return m_skippedLogs.load(std::memory_order_relaxed);
			}

		private:
			struct ThreadContext
			{
				ThreadContext() : m_logBuffer() {}

				SPSCRingBuffer<LogData, 128> m_logBuffer;
			};

			// Staged Queue Fronts corresponding to each thread context in the order of their respective index,
			// This is used to determine the next queue to consume from
			struct StagedQueue
			{
			public:
				// Constructor
				StagedQueue() : m_stagedData() {}

				// Member Functions

				void StageData(LogData InData, size_t InIndex)
				{
					if (InIndex >= m_stagedData.size())
					{
						std::cerr << "Invalid index for staging data." << std::endl;
						return;
					}

					m_stagedData[InIndex] = std::move(InData);
				}

				const std::chrono::system_clock::time_point GetOldestStagedTime()
				{
					auto time = std::chrono::system_clock::now();

					for (std::size_t i = 0; i < m_stagedData.size(); ++i)
					{
						if (m_stagedData[i].m_time < time)
						{
							time = m_stagedData[i].m_time;
						}
					}

					return time;
				}

				const std::size_t GetNextIndexFromSequenceNumber()
				{
					uint64_t minSequenceNumber = InvalidSequenceNumber;
					std::size_t nextIndex = Invalidindex;

					for (std::size_t i = 0; i < m_stagedData.size(); ++i)
					{
						if (m_stagedData[i].m_sequence == InvalidSequenceNumber) continue;

						if (m_stagedData[i].m_sequence < minSequenceNumber)
						{
							minSequenceNumber = m_stagedData[i].m_sequence;
							nextIndex = i;
						}
					}

					return nextIndex;
				}

				bool IsValidQueueIndex(std::size_t Index)
				{
					return (Index >= 0 && Index < m_numberOfThreads);
				}

				bool IsInvalidSequence(uint64_t Index) const
				{
					return m_stagedData[Index].m_sequence == InvalidSequenceNumber;
				}

				void MarkInvalid(uint64_t Index)
				{
					assert(Index < m_stagedData.size() && "Index out of bounds");
					m_stagedData[Index].m_sequence = InvalidSequenceNumber;
				}

				LogData* GetData(std::size_t Index)
				{
					return &m_stagedData[Index];
				}

				bool IsStageEmpty()
				{
					bool result = true;

					for (auto data : m_stagedData)
					{
						if (data.m_sequence != InvalidSequenceNumber)
						{
							result = false;
						}
					}

					return result;
				}

				std::array<LogData, m_numberOfThreads> m_stagedData;
			};

			struct LoggerState
			{
				LoggerState() {}

				std::array<ThreadContext, m_numberOfThreads> m_threadContexts;

				bool IsValidThreadIndex(std::size_t Index)
				{
					return Index >= 0 && Index < m_numberOfThreads;
				}
			};

			LoggerState m_loggerState;
			inline static thread_local ThreadContext* g_threadContext;
			std::atomic<size_t> m_QueueIndex{ 0 };
			const size_t m_MaxQueueCount = 4;

			std::atomic<uint64_t> m_globalSequenceNumber{ 0 };
			std::atomic<uint64_t> m_lastConsumedSequenceNumber = InvalidSequenceNumber;
			std::counting_semaphore<1> m_semaphoreNextSequence;
			StagedQueue m_stagedQueue;

			std::atomic<int> m_skippedLogs{ 0 };

			std::vector<ILoggerSink*> m_sinks;

		} NX_Logger;
	}
};




#define NX_LOG_INFO Neyrx::Logger::LogLevel::Info
#define NX_LOG_WARNING Neyrx::Logger::LogLevel::Warning
#define NX_LOG_ERROR Neyrx::Logger::LogLevel::Error
#define NX_LOG_FATAL Neyrx::Logger::LogLevel::Fatal

#define NX_LOG_TEMP Neyrx::Logger::LogCategory::Temp

#define NX_LOG(CATEGORY, LEVEL, LOGMSG)  Neyrx::Logger::NX_Logger.QueueMessage(CATEGORY, LEVEL, LOGMSG);