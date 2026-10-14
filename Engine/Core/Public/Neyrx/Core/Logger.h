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

			virtual void Consume(const LogData InLogData) = 0;
		};


		class IConsoleSink : public ILoggerSink
		{
		public:
			virtual ~IConsoleSink() = default;
			inline virtual void Consume(const LogData InLogData) override
			{
				InLogData.PrintData();
			}
		};


		class LoggerClass
		{
		public:

			LoggerClass() : m_loggerState(), m_semaphoreNextSequence(0) {}

			void Initialize()
			{
				m_orderedWindow.Initialize(&m_loggerState);
			}

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
				m_orderedWindow.ReserveOrderedLogSlots();
				bool bThisPassFinished = false;

				auto& tracked = m_orderedWindow.GetTrackedSequenceNumbers();
				bool bMissing = false;

				// Assign ConsumeEndSequenceThisPass equal to ConsumeStartSequenceThisPass only when the values are uninitialized
				// else, the start value is assinged at start of function, while end is a persistent state managed at the end of function
				ConsumeStartSequenceThisPass = tracked.Begin;
				if (ConsumeEndSequenceThisPass == InvalidSequenceNumber)
				{
					ConsumeEndSequenceThisPass = ConsumeStartSequenceThisPass;
				}

				if (m_orderedWindow.UpdateOrderedLog(false))
				{
					bMissing = m_orderedWindow.IsSequencesMissing();
					uint64_t missingSeq = InvalidSequenceNumber;

					if (bMissing)
					{
						missingSeq = m_orderedWindow.GetOldestMissingSequenceAfter(ConsumeEndSequenceThisPass);
						if (missingSeq != InvalidSequenceNumber)
						{
							ConsumeEndSequenceThisPass = missingSeq - 1;
						}
					}
					else
					{
						ConsumeEndSequenceThisPass = m_orderedWindow.GetLastReservedSequence();
					}

					for (auto sink : m_sinks)
					{
						if (m_orderedWindow.Flush(ConsumeStartSequenceThisPass, ConsumeEndSequenceThisPass, *sink))
						{
							ConsumeStartSequenceThisPass = tracked.Begin;
						}
					}

					if (bMissing)
					{
						const LogSlot* oldestValidSlot = m_orderedWindow.GetOldestValidLogSlot();
						if (oldestValidSlot == nullptr)
						{
							std::cout << "Oldest Valid Log Slot returned is NULL" << std::endl;
							return;
						}
						const LogData* oldestValiddata = oldestValidSlot->Data;
						if (oldestValiddata == nullptr)
						{
							std::cout << "Oldest Valid Log Slot Data returned is NULL" << std::endl;
							return;
						}
						if (oldestValiddata->m_sequence < missingSeq)
						{
							assert(false && "oldestData is older than missing data after PreGrace flush");
							return;
						}

						std::chrono::system_clock::time_point graceStart = oldestValiddata->m_time;

						std::this_thread::sleep_until(graceStart + 1ms);
					}

					ConsumeEndSequenceThisPass = m_orderedWindow.GetLastReservedSequence();
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


			// ==================================================
			// Log Slot
			// ==================================================

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
					BufferPosition() = default;
					BufferPosition(std::size_t InChunkIndex, std::size_t InOffset) :
						ChunkIndex(InChunkIndex),
						Offset(InOffset) { }

					std::size_t ChunkIndex = Invalidindex;
					std::size_t Offset = Invalidindex;

					bool IsValid() const
					{
						return ChunkIndex <  OrderedLogBufferChunkCount &&
							Offset < Capacity;
					}
				};


				// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
				// TODO - Define Sentinal Number Policy
				bool ReserveSlots(const uint64_t InStartSequenceNumber)
				{
					if (!HasFreeChunk()) return false;										// Not enough capacity to reserve all sequences

					if (InStartSequenceNumber == InvalidSequenceNumber || InvalidSequenceNumber - InStartSequenceNumber < Capacity)
					{
						std::cout << "Sequence Number Range exceeds Sentinal Limit";
						return false;
					}

					if (OldestReservedSequenceNumber == InvalidSequenceNumber)				// First ReserveSlots function call, no prior ReserveSlots function call was made
					{
						OldestReservedSequenceNumber = InStartSequenceNumber;
					}
					// Do not reserve Sequences that are already reserved, or leaves a gap with last reserved sequence number
					else
					{
						if (InStartSequenceNumber != OldestReservedSequenceNumber + GetSize()) return false;
					}
					uint64_t sequence = InvalidSequenceNumber;
					// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
					// Reservations continue from BufferWriteCursor, the next physical write
					// position. A full chunk advances the cursor to the next chunk at offset 0.
					// The next chunk must be empty before it can be used.
					for (std::size_t i = 0; i < Capacity;++i)						// No more than single chunks capacity should be reserved during a pass
					{
						if (BufferWriteCursor.Offset >= Capacity)					// If the present chunk is full, move to the next chunk
						{
							BufferWriteCursor.ChunkIndex = (BufferWriteCursor.ChunkIndex + 1) % OrderedLogBufferChunkCount;

							assert(BufferSize[BufferWriteCursor.ChunkIndex] == 0 && "Log Buffer Chunk is not Empty");		// If the next chunk is not empty, then old data was not flushed.

							BufferWriteCursor.Offset = 0;
						}

						sequence = InStartSequenceNumber + static_cast<uint64_t>(i);
						OrderedLogBuffers[BufferWriteCursor.ChunkIndex][BufferWriteCursor.Offset].SequenceNumber = sequence;
						++BufferWriteCursor.Offset;
						++BufferSize[BufferWriteCursor.ChunkIndex];
					}

					if (sequence != InvalidSequenceNumber)
					{
						LastReservedSequenceNumber = sequence;
					}

					return true;
				}

				bool DereserveSequences(uint64_t StartSequence, uint64_t EndSequence)
				{
					if (EndSequence == InvalidSequenceNumber || StartSequence == InvalidSequenceNumber ||
						StartSequence > EndSequence || StartSequence != OldestReservedSequenceNumber)	// TODO - Error Handling
					{
						std::cout << "Invalid Start or End Sequence" << std::endl;
						return false;
					}

					std::size_t size = GetSize();		// If later Dereserve Is more linient in respect of StartSequence this will be useful
					std::size_t offset = StartSequence - OldestReservedSequenceNumber;
					std::size_t count = EndSequence - StartSequence + 1;

					if (offset >= size || offset + count > size)
					{
						std::cout << "Sequences Out of Range" << std::endl;
						return false;
					}

					BufferPosition bufPos = GetBufferPosition(StartSequence);

					if (!bufPos.IsValid()) return false;

					for (uint64_t seq = StartSequence;; ++seq)
					{
						LogSlot& slot = OrderedLogBuffers[bufPos.ChunkIndex][bufPos.Offset];

						assert(slot.SequenceNumber == seq && "Unexpected Sequence Number in Reserved Slot");

						const std::size_t slotChunkIndex = bufPos.ChunkIndex;

						slot.SequenceNumber = InvalidSequenceNumber;
						slot.Data = nullptr;

						assert(BufferSize[slotChunkIndex] > 0);
						--BufferSize[slotChunkIndex];

						IncrementBufferPosition(BufferIndexStart);
						IncrementBufferPosition(bufPos);

						if (seq == EndSequence) break;
					}

					if (IsEmpty())
					{
						OldestReservedSequenceNumber = InvalidSequenceNumber;
						BufferIndexStart = BufferPosition{ 0, 0 };
						BufferWriteCursor = BufferPosition{ 0, 0 };
					}
					else
					{
						OldestReservedSequenceNumber = EndSequence + 1;
					}

					return true;
				}

				LogSlot* GetReservedSlot(const uint64_t InSequenceNumber)
				{
					BufferPosition bpos = GetBufferPosition(InSequenceNumber);

					if (!bpos.IsValid()) return nullptr;

					LogSlot* slot = &OrderedLogBuffers[bpos.ChunkIndex][bpos.Offset];

					return slot;
				}

				BufferPosition GetBufferPosition(const uint64_t InSequenceNumber)
				{
					// Return null if the buffer is empty, or no reserve functional call has been made yet
					if (IsEmpty() || OldestReservedSequenceNumber == InvalidSequenceNumber) return BufferPosition();

					// Return null if InSequenceNumber is a stale sequence number
					if (InSequenceNumber < OldestReservedSequenceNumber) return BufferPosition();

					std::size_t totalBufferSize = GetSize();

					uint64_t sequenceOffset = InSequenceNumber - OldestReservedSequenceNumber;

					// Return null if InSequenceNumber is not within reserved range
					if (sequenceOffset >= totalBufferSize) return BufferPosition();

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

					return BufferPosition(cindex, bindex);
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

				void IncrementBufferPosition(BufferPosition& InBufferPosition)
				{
					++InBufferPosition.Offset;

					if (InBufferPosition.Offset >= Capacity)
					{
						InBufferPosition.ChunkIndex = (InBufferPosition.ChunkIndex + 1) % OrderedLogBufferChunkCount;
						InBufferPosition.Offset = 0;
					}
				}

				const uint64_t GetOldestReservedSequenceNumber() const
				{
					return OldestReservedSequenceNumber;
				}

				const uint64_t GetLastReservedSequenceNumber() const
				{
					return LastReservedSequenceNumber;
				}

				// Member Parameters
				static constexpr std::size_t OrderedLogBufferChunkCount = 3;

				std::array<std::array <LogSlot, Capacity>, OrderedLogBufferChunkCount> OrderedLogBuffers { };

				BufferPosition BufferIndexStart{0 ,0 };		// Physical position of the oldest live reserved slot.
				BufferPosition BufferWriteCursor{0, 0};			// Physical position of the next slot to reserve.
				std::array<std::size_t, OrderedLogBufferChunkCount> BufferSize { };	// The present size of each buffer

				// Sequence number corresponding to BufferIndexStart.
				// Advances when the oldest reserved slots are removed.
				// Retains its last value when the buffer becomes empty.
				uint64_t OldestReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the actual reserved oldest sequnce. Set to invalid Sequnce Number when nothing is reserved
				uint64_t LastReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the last reserved sequence number. Retains the value even if nothing is stored.
			};




			// =======================================================================================================================================
			// Ordered Window
			// =======================================================================================================================================
			template<std::size_t Capacity>
			struct OrderedWindow
			{
			public:

				OrderedWindow() {}

				void Initialize(LoggerState* InLoggerState)
				{
					m_loggerStatePointer = InLoggerState;
					m_missingSequences.reserve(Capacity / 2);
				}

				struct TrackedSequenceNumbers
				{
					uint64_t Begin = InvalidSequenceNumber;
					uint64_t End = InvalidSequenceNumber;
				};

				bool ReserveOrderedLogSlots()
				{
					uint64_t lastRes = m_orderedLogBuffer.GetLastReservedSequenceNumber();

					if (lastRes != InvalidSequenceNumber &&
						lastRes >= InvalidSequenceNumber - Capacity)		// Sentinal Gaurd
					{
						return false;
					}

					uint64_t startRes = lastRes == InvalidSequenceNumber			// If LastRes is 0, then this is the first function call, use StartRes as 0
						? 0
						: lastRes + 1;

					if (!m_orderedLogBuffer.ReserveSlots(startRes)) return false;

					uint64_t oldestRes = m_orderedLogBuffer.GetOldestReservedSequenceNumber();

					if (oldestRes == InvalidSequenceNumber)
					{
						m_trackedSequences.Begin = startRes;
					}
					else
					{
						m_trackedSequences.Begin = oldestRes;
					}

					m_trackedSequences.End = startRes + Capacity - 1;

					return true;
				}

				bool UpdateOrderedLog(bool bGracePeriodCheck)
				{
					uint64_t oldestSeq = m_orderedLogBuffer.GetOldestReservedSequenceNumber();

					if (!bGracePeriodCheck)
					{
						// First Update; sequence zero is valid
						if (oldestSeq == InvalidSequenceNumber)
						{
							return false;	// Nothing is reserved in the OrderedLogBuffer;
						}

						// First Update; sequence zero is valid
						m_OldestExpectedSequenceThisPass = m_highestReservedSequence == InvalidSequenceNumber
							? 0
							: m_highestReservedSequence + 1;
					}


					bool bUpdated = false;

					for (ThreadContext& contex : m_loggerStatePointer->m_threadContexts)
					{
						auto& buffer = contex.m_logBuffer;

						if (buffer.IsEmpty()) continue;

						for (auto it = buffer.begin(); it != buffer.end(); ++it)
						{
							const LogData* data = &buffer.Peek(it.GetIndex());

							if (data == nullptr)
							{
								std::cout << "Data is Null in SPSC Queue" << std::endl;
								continue;
							}

							uint64_t seq = data->m_sequence;

							if (seq == InvalidSequenceNumber)
							{
								std::cout << "INVALID Sequence Number FOUND in Context Queue" << std::endl;
								continue;
							}

							if (seq < oldestSeq) continue; // Stale sequence use the bypass function

							// If the sequence is such that it exceeds the max Sequence Number that can be reserved this pass,
							// break and conitnue with the next thread context.
							// This thread context connot contain a smaller sequence number
							const uint64_t rangeStart = m_OldestExpectedSequenceThisPass == InvalidSequenceNumber ? 0 : m_OldestExpectedSequenceThisPass;

							if (seq > rangeStart)			// seq if not greater than range start, is greater than oldestRserved, and is a missing data. Should bypass this check
							{
								const uint64_t offset = seq - rangeStart;
								if (offset >= Capacity) break;
							}

							// If no log has been consumed/reserved then use expected as 0. This results in the index of the incoming sequence passing the threashold.
							uint64_t expected = m_highestReservedSequence == InvalidSequenceNumber ? 0 : m_highestReservedSequence + 1;

							std::size_t bMissingIndex = Invalidindex;

							// If expected > seq, then check if the seq is found in m_missingSequences
							// if expected < seq, reserve all Sequence Numbers between expected and seq
							if (seq < expected)
							{
								auto it = std::find(m_missingSequences.begin(), m_missingSequences.end(), seq);

								if (it == m_missingSequences.end())
								{
									if (bGracePeriodCheck)
									{
										continue; // If seq is not a missing tracked sequence, the seq has already been reserved in the previous pass and is waiting flush
									}
									else
									{
										assert(false && "Reserve Slot and Updated Order Log Bug");
										continue;
									}
								}

								// If seq is a missing sequence, store the index, and pop only if successfully reserved
								bMissingIndex = static_cast<std::size_t>(std::distance(m_missingSequences.begin(), it));
							}
							else if (seq > expected) // Gaurd against sentinal overflow and Sequence Gap
							{
								// if seq > expected, mark all in between sequences as missing
								// and push back into m_missingSequences
								if (seq > expected)
								{
									for (uint64_t missing = expected; missing < seq; ++missing)
									{
										m_missingSequences.push_back(missing);
									}
								}
							}

							LogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);

							if (slot == nullptr)
							{
								std::cout << "Invalid Slot. The Sequence Number was possibly not reserved" << std::endl;
								assert(false);
								continue;
							}


							// If the slot returned by the LogBuffer, already contains a pointer to valid data,
							// It is an old or stale data that was not flushed.
							// The eariler condition statement should have prohinited such conditions to exist
							// If the asset below is hit, check the validity of condition statements for rejecting sequences first
							if (slot->Data != nullptr)
							{
								std::cout << "Encountered a reserved slot with valid a data pointer" << std::endl;
								assert(false);
								continue;
							}

							*slot = LogSlot(0, 0, seq, const_cast<LogData*>(data));

							if (m_highestReservedSequence == InvalidSequenceNumber || m_highestReservedSequence < seq)
							{
								m_highestReservedSequence = seq;
							}

							// If seq is a missing index, swap and pop from m_missingSequences
							if (bMissingIndex != Invalidindex)
							{
								std::swap(m_missingSequences[bMissingIndex], m_missingSequences.back());
								m_missingSequences.pop_back();
							}

							bUpdated = true;
						}
					}

					return bUpdated;
				}

				const LogSlot* GetOldestValidLogSlot()
				{
					for (uint64_t i = m_trackedSequences.Begin; i <= m_trackedSequences.End; ++i)
					{
						if (std::find(m_missingSequences.begin(), m_missingSequences.end(), i) != m_missingSequences.end())
						{
							LogSlot* slot = m_orderedLogBuffer.GetReservedSlot(i);
							if (slot != nullptr) return slot;
						}
					}

					return nullptr;
				}

				const LogSlot* GetTrackedSlot(uint64_t SequenceNumber)
				{
					return m_orderedLogBuffer.GetReservedSlot(SequenceNumber);
				}

				bool IsSequencesMissing()
				{
					return m_missingSequences.size() > 0;
				}

				uint64_t GetOldestMissingSequenceAfter(uint64_t SequenceNumber)
				{
					uint64_t missingSeq = InvalidSequenceNumber;
					for (auto mis : m_missingSequences)
					{
						if (mis > SequenceNumber)
						{
							missingSeq = std::min(missingSeq, mis);
						}
					}

					return missingSeq;
				}

				uint64_t GetLastReservedSequence()
				{
					return m_orderedLogBuffer.GetLastReservedSequenceNumber();
				}

				const TrackedSequenceNumbers& GetTrackedSequenceNumbers() const
				{
					return m_trackedSequences;
				}

				bool Flush(uint64_t StartSequenceNumber, uint64_t EndSequenceNumber, ILoggerSink& Sink)
				{
					assert((EndSequenceNumber < InvalidSequenceNumber &&
						StartSequenceNumber < InvalidSequenceNumber &&
						StartSequenceNumber <= EndSequenceNumber) && "Invalid Start or End Sequence");

					bool bFlushed = false;

					for (uint64_t seq = StartSequenceNumber; seq <= EndSequenceNumber; ++seq)
					{
						LogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);
						if (slot == nullptr)
						{
							std::cout << "Reserved Slot NOT FOUND for Seq: " << seq << std::endl;
							continue;
						}

						LogData* data = slot->Data;
						if (data == nullptr)
						{
							MissingDataTemplate.m_sequence = seq;
							data = &MissingDataTemplate;
							std::cout << "LOG DATA POINTER FOR SEQ: " << seq << " is a NULL_PTR" << std::endl;
						}

						Sink.Consume(std::move(*data));	// Safe only because consumer is responsible for Sink execution. If later the architecture changes, this needs to be taken into account.
					}

					for (auto& context : m_loggerStatePointer->m_threadContexts)
					{
						auto& logBuffer = context.m_logBuffer;

						while (!logBuffer.IsEmpty())
						{
							uint64_t peekSeq = logBuffer.Peek(logBuffer.FrontIndex()).m_sequence;

							if (peekSeq >= StartSequenceNumber && peekSeq <= EndSequenceNumber)
							{
								logBuffer.Dequeue();
								bFlushed = true;
							}
							else
							{
								break;
							}
						}
					}

					if (m_trackedSequences.Begin != StartSequenceNumber)
					{
						std::cout << "ERROR WITH TRACKED SEQUENCES" << std::endl;
						assert(false);
					}
					else if (m_trackedSequences.End == EndSequenceNumber && m_orderedLogBuffer.IsEmpty())
					{
						m_trackedSequences.Begin = InvalidSequenceNumber;
						m_trackedSequences.End = InvalidSequenceNumber;
					}
					else if (m_trackedSequences.End < EndSequenceNumber)
					{
						m_trackedSequences.Begin = EndSequenceNumber + 1;
					}
					else
					{
						std::cout << "ERROR WITH TRACKED SEQUENCES" << std::endl;
						assert(false);
					}

					if (!m_orderedLogBuffer.DereserveSequences(StartSequenceNumber, EndSequenceNumber))
					{
						std::cout << "ERROR WITH DERESERVING SEQUENCES" << std::endl;
						assert(false);
					}

					return bFlushed;
				}

			private:

				OrderedLogBuffer<Capacity> m_orderedLogBuffer;
				TrackedSequenceNumbers m_trackedSequences;

				std::vector<char> m_Arena;
				std::vector<uint64_t> m_missingSequences;
				std::size_t m_cursorStart = 0;
				std::size_t m_cursorEnd = 0;
				uint64_t m_highestReservedSequence = InvalidSequenceNumber;
				uint64_t m_OldestExpectedSequenceThisPass = InvalidSequenceNumber;

				LoggerState* m_loggerStatePointer = nullptr;

				LogData MissingDataTemplate{Neyrx::Logger::LogLevel::Warning, Neyrx::Logger::LogCategory::Temp, "MISSING SEQUENCE NUMBER", 0};
			};


			// Member Variables

			LoggerState m_loggerState;
			inline static thread_local ThreadContext* g_threadContext;
			std::atomic<size_t> m_QueueIndex{ 0 };
			const size_t m_MaxQueueCount = 4;

			OrderedWindow<ThreadQueueSize/5> m_orderedWindow;

			std::atomic<uint64_t> m_globalSequenceNumber{ 0 };
			std::atomic<uint64_t> m_lastConsumedSequenceNumber = InvalidSequenceNumber;
			std::counting_semaphore<1> m_semaphoreNextSequence;

			std::atomic<int> m_skippedLogs{ 0 };

			std::vector<ILoggerSink*> m_sinks;

			uint64_t ConsumeStartSequenceThisPass = InvalidSequenceNumber;
			uint64_t ConsumeEndSequenceThisPass = InvalidSequenceNumber;
		};
	}
};

static Neyrx::Logger::LoggerClass NX_Logger;




#define NX_LOG_INFO Neyrx::Logger::LogLevel::Info
#define NX_LOG_WARNING Neyrx::Logger::LogLevel::Warning
#define NX_LOG_ERROR Neyrx::Logger::LogLevel::Error
#define NX_LOG_FATAL Neyrx::Logger::LogLevel::Fatal

#define NX_LOG_TEMP Neyrx::Logger::LogCategory::Temp

#define NX_LOG(CATEGORY, LEVEL, LOGMSG)  NX_Logger.QueueMessage(CATEGORY, LEVEL, LOGMSG);