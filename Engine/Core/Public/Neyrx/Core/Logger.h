#pragma once
#include <string_view>
#include "Container/Queue.h"
#include <iostream>
#include <stdio.h>
#include <chrono>
#include <array>
#include <thread>

static constexpr uint64_t InvalidSequenceNumber = std::numeric_limits<uint64_t>::max();
static constexpr uint64_t Invalidindex = std::numeric_limits<uint64_t>::max();

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

			LogData(LogLevel InLevel, LogCategory InCategory,std::string InMsg, uint64_t InSequenceNumber) :
				m_level(InLevel),
				m_category(InCategory),
				m_msg(std::move(InMsg)),
				m_sequence(InSequenceNumber),
				m_time(std::chrono::system_clock::now())
			{ }

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


		static class LoggerClass
		{
		public:

			LoggerClass() : m_loggerState() {}

			// Member Functions
			inline void QueueMessage(LogCategory InCategory, LogLevel InLevel, std::string InMsg)
			{
				uint64_t sequenceNumber = m_globalSequenceNumber.fetch_add(1, std::memory_order_relaxed);
				g_threadContext->InflightSequenceNumber.store(sequenceNumber, std::memory_order_release);

				std::this_thread::sleep_for(std::chrono::microseconds(3));

				g_threadContext->m_logBuffer.Enqueue(LogData(InLevel, InCategory, InMsg, sequenceNumber));

				g_threadContext->InflightSequenceNumber.store(InvalidSequenceNumber, std::memory_order_release);

			}

			inline bool StageThreadContexts()
			{
				bool staged = false;

				for (size_t i = 0; i < m_loggerState.m_threadContexts.size(); ++i)
				{
					if (!m_stagedQueue.IsInvalid(i)) continue;

					if (m_loggerState.m_threadContexts[i].m_logBuffer.IsEmpty())
					{
						if (m_loggerState.m_threadContexts[i].InflightSequenceNumber.load(std::memory_order_relaxed) != InvalidSequenceNumber)
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(100));

							if (m_loggerState.m_threadContexts[i].m_logBuffer.IsEmpty()) continue;
						}
						else
						{
							continue;
						}
					}

					LogData logData;

					if (m_loggerState.m_threadContexts[i].m_logBuffer.Dequeue(logData))
					{
						m_stagedQueue.StageData(std::move(logData), i);
						staged = true;
					}
				}

				return staged;
			}

			inline void ConsumeMessage()
			{
				StageThreadContexts();

				const std::size_t index = m_stagedQueue.GetNextQueueIndexFromSequence();

				if(index == Invalidindex)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					return;
				}

				if (m_stagedQueue.m_stagedData[index].m_sequence != InvalidSequenceNumber)
				{
					m_stagedQueue.m_stagedData[index].PrintData();
					m_stagedQueue.MarkInvalid(index);
				}
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

		private:
			struct ThreadContext
			{
				ThreadContext() : m_logBuffer() {}

				SPSCRingBuffer<LogData, 10> m_logBuffer;
				std::atomic<uint64_t> InflightSequenceNumber = InvalidSequenceNumber;
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

					const std::size_t GetNextQueueIndexFromSequence() const
					{
						uint64_t minSequence = InvalidSequenceNumber;
						size_t minIndex = Invalidindex;

						for (size_t i = 0; i < m_stagedData.size(); ++i)
						{
							TRACE(m_stagedData[i].m_sequence, "SEQUENCE For Thread OUT LOOP");
							if (m_stagedData[i].m_sequence < minSequence)
							{
								TRACE(m_stagedData[i].m_sequence, "SEQUENCE For Thread IN LOOP");
								minSequence = m_stagedData[i].m_sequence;
								minIndex = i;
							}
						}

						return minIndex;
					}

					bool IsInvalid(uint64_t Index) const
					{
						return m_stagedData[Index].m_sequence == InvalidSequenceNumber;
					}

					void MarkInvalid(uint64_t Index)
					{
						assert(Index < m_stagedData.size() && "Index out of bounds");
						m_stagedData[Index].m_sequence = InvalidSequenceNumber;
					}

				std::array<LogData, 4> m_stagedData;
			};

			struct LoggerState
			{
				LoggerState() {}

				std::array<ThreadContext, 4> m_threadContexts;
			};

			LoggerState m_loggerState;
			inline static thread_local ThreadContext* g_threadContext;
			std::atomic<size_t> m_QueueIndex {0};
			const size_t m_MaxQueueCount = 4;

			std::atomic<uint64_t> m_globalSequenceNumber{ 0 };
			std::size_t m_lastConsumedSequenceNumber = 0;
			StagedQueue m_stagedQueue;

		} NX_Logger;
	}
};

#define NX_LOG_INFO Neyrx::Logger::LogLevel::Info
#define NX_LOG_WARNING Neyrx::Logger::LogLevel::Warning
#define NX_LOG_ERROR Neyrx::Logger::LogLevel::Error
#define NX_LOG_FATAL Neyrx::Logger::LogLevel::Fatal

#define NX_LOG_TEMP Neyrx::Logger::LogCategory::Temp

#define NX_LOG(CATEGORY, LEVEL, LOGMSG)  Neyrx::Logger::NX_Logger.QueueMessage(CATEGORY, LEVEL, LOGMSG);