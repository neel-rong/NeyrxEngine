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

static constexpr std::size_t NumberOfThreads = 4;
static constexpr std::size_t ThreadQueueSize = 256;

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
				auto startTime = std::chrono::steady_clock::now();
				uint64_t sequenceNumber = m_globalSequenceNumber.fetch_add(1, std::memory_order_relaxed);

				if (!g_threadContext->m_logBuffer.Enqueue(LogData(InLevel, InCategory, InMsg, sequenceNumber)))
				{
					std::cout << "\nEnqueue Failed\n" << std::endl;
					m_skippedLogs.fetch_add(1, std::memory_order_release);
					return;
				}
			}

			inline void ConsumeMessage()
			{
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

				SPSCRingBuffer<LogData, ThreadQueueSize> m_logBuffer;
			};



			struct LoggerState
			{
				LoggerState() = default;

				std::array<ThreadContext, NumberOfThreads> m_threadContexts;

				bool IsValidThreadIndex(std::size_t Index)
				{
					return Index >= 0 && Index < NumberOfThreads;
				}

				const std::size_t GetThreadIndex() const
				{
					std::size_t index = Invalidindex;

					for (std::size_t i = 0; i < m_threadContexts.size(); ++i)
					{
						if (&m_threadContexts[i] == g_threadContext)
						{
							index = i;
							break;
						}
					}

					return index;
				}
			};


			struct LogSlot
			{
				LogSlot() = default;
				LogSlot(std::size_t InOffset, std::size_t InLength, uint64_t InSequenceNumber, LogData* InData) :
					Offset(InOffset), Length(InLength), SequenceNumber(InSequenceNumber), Data(InData) { }
				LogSlot(uint64_t InSequenceNumber) : Offset(0), Length(0), SequenceNumber(InSequenceNumber), Data(nullptr) { }

				std::size_t Offset = 0;
				std::size_t Length = 0;
				uint64_t SequenceNumber = InvalidSequenceNumber;
				LogData* Data = nullptr;
			};


			// ====================================
			// Log Buffer
			// ====================================

			template<std::size_t Capacity>
			requires(Capacity > 0)
			struct OrderedLogBuffer
			{
				struct BufferPosition
				{
					std::size_t ChunkIndex = 0;
					std::size_t Offset = 0;
				};


				// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
				bool ReserveSlots(const uint64_t InStartSequenceNumber)
				{
					if (!HasFreeChunk()) return false;										// Not enough capacity to reserve all sequences

					if (OldestReservedSequenceNumber == InvalidSequenceNumber)				// First ReserveSlots function call, no prior ReserveSlots function call was made
					{
						OldestReservedSequenceNumber = InStartSequenceNumber;
					}

					if (InStartSequenceNumber < OldestReservedSequenceNumber) return false; // Stale logs should bypass the Ordered Window

					// Do not reserve Sequences that are already reserved, or leaves a gap with last reserved sequence number
					if (InStartSequenceNumber != OldestReservedSequenceNumber + GetSize()) return false;

					// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
					// Reservations continue from BufferWriteCursor, the next physical write
					// position. A full chunk advances the cursor to the next chunk at offset 0.
					// The next chunk must be empty before it can be used.
					for (std::size_t i = 0; i < Capacity; ++i)						// No more than single chunks capacity should be reserved during a pass
					{
						if (BufferWriteCursor.Offset >= Capacity)					// If the present chunk is full, move to the next chunk
						{
							BufferWriteCursor.ChunkIndex = (BufferWriteCursor.ChunkIndex + 1) % OrderedLogBufferChunkCount;

							assert(BufferSize[BufferWriteCursor.ChunkIndex] == 0 && "Log Buffer Chunk is not Empty");		// If the next chunk is not empty, then old data was not flushed.

							BufferWriteCursor.Offset = 0;
						}

						uint64_t sequence = InStartSequenceNumber + static_cast<uint64_t>(i);
						OrderedLogBuffers[BufferWriteCursor.ChunkIndex][BufferWriteCursor.Offset].SequenceNumber = sequence;
						++BufferWriteCursor.Offset;
						++BufferSize[BufferWriteCursor.ChunkIndex];
					}

					return true;
				}

				LogSlot* GetReservedSlot(const uint64_t InSequenceNumber)
				{
					// Return null if the buffer is empty, or no reserve functional call has been made yet
					if (IsEmpty() || OldestReservedSequenceNumber == InvalidSequenceNumber) return nullptr;

					// Return null if InSequenceNumber is a stale sequence number
					if (InSequenceNumber < OldestReservedSequenceNumber) return nullptr;

					std::size_t totalBufferSize = GetSize();

					uint64_t sequenceOffset = InSequenceNumber - OldestReservedSequenceNumber;

					// Return null if InSequenceNumber is not within reserved range
					if (sequenceOffset >= totalBufferSize) return nullptr;

					const std::size_t offset = static_cast<std::size_t>(sequenceOffset);

					std::size_t bindex = BufferIndexStart.Offset + offset;
					std::size_t cindex = BufferIndexStart.ChunkIndex;

					if (bindex >= Capacity)
					{
						std::size_t linearOffset = BufferIndexStart.Offset + offset;
						std::size_t chunkAdvance = linearOffset / Capacity;
						bindex = linearOffset % Capacity;
						cindex = (BufferIndexStart.ChunkIndex + chunkAdvance) % OrderedLogBufferChunkCount;
					}

					LogSlot* slot = &OrderedLogBuffers[cindex][bindex];

					return slot->SequenceNumber == InSequenceNumber ? slot : nullptr;
				}

				// Returns ture if the present write chunk or the next chunk is empty
				bool HasFreeChunk() const
				{
					// The write buffer offset will reset to 0 on a flush if the Chunk is emtpty.
					// Else it will retain it's last position
					// So the result here should never return true when the write chunk is empty while a cursor is not at 0
					bool result = BufferSize[BufferWriteCursor.ChunkIndex] == 0;

					if (result)
					{
						assert(BufferWriteCursor.Offset == 0 && "Orderer Log Bufer Chunk is Empty, while the Write Cursor offset is not 0");
					}

					if (!result)
					{
						return BufferSize[(BufferWriteCursor.ChunkIndex + 1) % OrderedLogBufferChunkCount] == 0;
					}

					return result;
				}

				std::size_t GetSize() const
				{
					std::size_t result = 0;

					for (auto size : BufferSize)
					{
						result += size;
					}

					return result;
				}

				bool IsEmpty() const
				{
					return GetSize() == 0;
				}

				// Member Parameters
				static constexpr std::size_t OrderedLogBufferChunkCount = 3;

				std::array<std::array <LogSlot, Capacity>, OrderedLogBufferChunkCount> OrderedLogBuffers { };

				BufferPosition BufferIndexStart { };		// Physical position of the oldest live reserved slot.
				BufferPosition BufferWriteCursor { };			// Physical position of the next slot to reserve.
				std::array<std::size_t, OrderedLogBufferChunkCount> BufferSize { };	// The present size of each buffer

				// Sequence number corresponding to BufferIndexStart.
				// Advances when the oldest reserved slots are removed.
				// Retains its last value when the buffer becomes empty.
				uint64_t OldestReservedSequenceNumber = InvalidSequenceNumber;
			};


			// ===========================================================================================
			// Ordered Window
			template<std::size_t Capacity>
			struct OrderedWindow
			{
			public:

				OrderedWindow(LoggerState* InLoggerState, uint64_t* InLastConsumedSequence) :
					m_loggerStatePointer(InLoggerState),
					m_lastConsumedSequence(InLastConsumedSequence)
				{
					m_missingSequences.reserve(Capacity / 2);
				}

				bool ReserveOrderedLogSlots()
				{
					return m_orderedLogBuffer.ReserveSlots(m_lastReservedSequence + 1);
				}

				bool UpdateOrderedLog(bool bGracePeriodCheck)
				{
					if (!bGracePeriodCheck)
					{
						if (m_OldestReservedSequence == InvalidSequenceNumber)
						{
							// First Update; sequence zero is valid
							m_OldestReservedSequence = 0;
						}

						m_OldestReservedSequenceThisPass = m_lastReservedSequence == InvalidSequenceNumber
							? 0
							: m_lastReservedSequence + 1;
					}


					bool bUpdated = false;

					for (ThreadContext& contex : m_loggerStatePointer->m_threadContexts)
					{
						auto& buffer = contex.m_logBuffer;

						if (buffer.IsEmpty()) continue;

						for (auto it = buffer.begin(); it != buffer.end(); ++it)
						{
							LogData* data = buffer.Peek(it.GetIndex());
							uint64_t seq = data->m_sequence;

							if (seq < m_OldestReservedSequenceThisPass) continue; // Stale sequence use the bypass function

							// If the sequence is such that it exceeds the max Sequence Number that can be reserved this pass,
							// break and conitnue with the next thread context.
							// This thread connot contain a smaller sequence number
							const uint64_t offset = seq - m_OldestReservedSequenceThisPass;
							if (offset >= Capacity) break;

							uint64_t lastconsumed = *m_lastConsumedSequence;

							// If no log has been consumed then use lastconsumed as 0. This results in the index of the incoming 0 sequence being 0.
							// Otherwise, if lastconsumed > seq, then data is a stale log, and must use the bypass function
							if (lastconsumed == InvalidSequenceNumber)
							{
								lastconsumed = 0;
							}
							else if (m_lastReservedSequence != InvalidSequenceNumber &&	seq < m_lastReservedSequence)
							{
								auto it = std::find(m_missingSequences.begin(), m_missingSequences.end(), seq);

								if (it == m_missingSequences.end())
								{
									continue; // If seq is not a missing tracked sequence, the seq has already been reserved in the previous pass and is waiting flush
								}

								// If seq is a missing sequence, pop the seq from the m_missingSequences
								auto index = static_cast<std::size_t>(std::distance(m_missingSequences.begin(), it));
								std::swap(m_missingSequences[index], m_missingSequences.back());
								m_missingSequences.pop_back();
							}
							else if (m_lastReservedSequence == InvalidSequenceNumber || seq > m_lastReservedSequence) // Gaurd against sentinal overflow
							{
								uint64_t expected = m_lastReservedSequence == InvalidSequenceNumber
									? 0
									: m_lastReservedSequence + 1;

								// if seq > m_lastReservedSequence + 1 (the expected sequence), mark all in between sequences as missing
								// and push back into m_missingSequences
								if (seq > expected)
								{
									for (std::size_t missing = expected; missing < seq; ++missing)
									{
										m_missingSequences.push_back(missing);
									}
								}

								m_lastReservedSequence = seq;
							}

							LogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);

							assert(slot != nullptr && "Invalid Slot. The Sequence Number was possibly not reserved");


							// If the slot returned by the LogBuffer, already contains a pointer to valid data,
							// It is an old or stale data that was not flushed.
							// The eariler condition statement should have prohinited such conditions to exist
							// If the asset below is hit, check the validity of condition statements for rejecting sequences first
							assert(slot->Data == nullptr && "Encountered a reserved slot with valid a data pointer");

							*slot = LogSlot(0, 0, seq, data);

							if (m_lastReservedSequence < seq)
							{
								m_lastReservedSequence = seq;
							}

							bUpdated = true;
						}
					}

					return bUpdated;
				}

			private:

				LogBuffer<Capacity> m_orderedLogBuffer;

				std::vector<char> m_Arena;
				std::vector<uint64_t> m_missingSequences;
				std::size_t m_cursorStart = 0;
				std::size_t m_cursorEnd = 0;
				std::size_t m_lastReservedSequence = InvalidSequenceNumber;
				uint64_t m_OldestReservedSequenceThisPass = InvalidSequenceNumber;
				uint64_t m_OldestReservedSequence = InvalidSequenceNumber;

				LoggerState* m_loggerStatePointer = nullptr;
				uint64_t* m_lastConsumedSequence = nullptr;
			};


			// Member Variables

			LoggerState m_loggerState;
			inline static thread_local ThreadContext* g_threadContext;
			std::atomic<size_t> m_QueueIndex{ 0 };
			const size_t m_MaxQueueCount = 4;

			std::atomic<uint64_t> m_globalSequenceNumber{ 0 };
			std::atomic<uint64_t> m_lastConsumedSequenceNumber = InvalidSequenceNumber;
			std::counting_semaphore<1> m_semaphoreNextSequence;

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