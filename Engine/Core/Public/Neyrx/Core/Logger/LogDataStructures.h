#pragma once

#include<string>
#include<chrono>
#include<array>

#include "Neyrx/Core/Container/Queue.h"


#include <stdio.h>			// Temp
#include <iostream>			// Temp
#include <thread>


// Temporary Global Variables
inline constexpr uint64_t InvalidSequenceNumber = std::numeric_limits<uint64_t>::max();
inline constexpr uint64_t Invalidindex = std::numeric_limits<uint64_t>::max();

inline constexpr std::size_t NumberOfThreads = 8;
inline constexpr std::size_t ThreadQueueSize = 16384;

inline constexpr std::size_t g_OrderedLogCapacity = ThreadQueueSize * NumberOfThreads;


namespace Neyrx::Logger
{
	// ==========================================================================================================
	// Enum Reserve State
	// ==========================================================================================================

	enum class LogReserveState : uint8_t
	{
		UNINITIALIZED,
		UNRESERVED,
		RESERVED,
		DERESERVED,
		DERESERVED_EMPTY,
		FAILURE_AT_CAPACITY,
		FAILURE_SEQUENCE_INVALID,
		FAILURE_SENTINAL,
		INVALID_INTERNAL_STATE,
	};




	// ==========================================================================================================
	// Enum Tracker State
	// ==========================================================================================================

	enum class LogTrackerState : uint8_t
	{
		UNINITIALIZED,
		TRACKED_ALL,
		TRACKED_WITH_MISSING,
		UNTRACKED,
		FAILURE_NO_RESERVE,
		FAILURE_EMPTY,
		FAILURE_SENTINAL,
		INVALID_STATE
	};




	// ==========================================================================================================
	// Enum Publish State
	// ==========================================================================================================

	enum class LogPublishState : uint8_t
	{
		SUCCESS,
		SUCCESS_WITH_MISSING,

		FAILURE_EMPTY,
		FAILURE_UNINITIALIZED,
		FAILURE_INVALID_SEQUENCE,

		INVALID_INTERNAL_STATE
	};




	// ==========================================================================================================
	// Log Levels
	// ==========================================================================================================

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




	// ==========================================================================================================
	// Log Category
	// ==========================================================================================================

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




	// ==========================================================================================================
	// Log Data
	// ==========================================================================================================

	struct LogData
	{
		LogData() {};

		LogData(const LogData&) = default;
		LogData(LogData&&) noexcept = default;

		LogData& operator=(const LogData&) = default;
		LogData& operator=(LogData&&) noexcept = default;

		LogData(std::thread::id InThreadID, uint64_t InSequenceNumber, LogCategory InCategory, LogLevel InLevel, std::string InLogMsg) :
			ThreadID(InThreadID),
			Sequence(InSequenceNumber),
			Category(InCategory),
			Level(InLevel),
			Message(std::move(InLogMsg)),
			Time(std::chrono::system_clock::now())
		{
		}

		inline void PrintData()	const	// Temporary
		{
			std::cout << "[ TheradID: " << ThreadID << "] " << "[ " << Sequence << " ] [ " << ToString(Category) << " ] [ " << ToString(Level) << "]  " << Message << std::endl;
		}

		std::thread::id ThreadID;
		uint64_t Sequence = InvalidSequenceNumber;
		LogCategory Category = LogCategory::Temp;
		LogLevel Level = LogLevel::Info;
		std::string Message = "";

		std::chrono::system_clock::time_point Time = std::chrono::system_clock::now();
	};




	// ==========================================================================================================
	// Log Thread Context
	// ==========================================================================================================

	struct LogThreadContext
	{
		SPSCRingBuffer<LogData, ThreadQueueSize> ThreadQueue{};
	};


	// ==========================================================================================================
	// Logger State
	// ==========================================================================================================

	struct LoggerThreadRegistry
	{
		LoggerThreadRegistry() = default;

		std::array<LogThreadContext, NumberOfThreads> m_threadContexts{};

		inline bool IsValidThreadIndex(std::size_t Index)
		{
			return Index >= 0 && Index < NumberOfThreads;
		}

		inline const std::size_t GetThreadIndex(const LogThreadContext* Context) const
		{
			std::size_t index = Invalidindex;

			for (std::size_t i = 0; i < m_threadContexts.size(); ++i)
			{
				if (&m_threadContexts[i] == Context)
				{
					index = i;
					break;
				}
			}

			return index;
		}
	};




	// ==========================================================================================================
	// Ordered Log Slot
	// ==========================================================================================================

	struct OrderedLogSlot
	{
		OrderedLogSlot() = default;

		OrderedLogSlot(std::size_t InOffset, std::size_t InLength, uint64_t InSequenceNumber, LogData* InData) :
			Offset(InOffset), Length(InLength), SequenceNumber(InSequenceNumber), Data(InData) {
		}

		OrderedLogSlot(uint64_t InSequenceNumber) : Offset(0), Length(0), SequenceNumber(InSequenceNumber), Data(nullptr) {}

		std::size_t Offset = 0;
		std::size_t Length = 0;
		uint64_t SequenceNumber = InvalidSequenceNumber;
		LogData* Data = nullptr;
	};




	// ==========================================================================================================
	// Tracked Log Data
	// ==========================================================================================================

	struct TrackedLogData
	{
		TrackedLogData()
		{
			Sequence_Missing.reserve(g_OrderedLogCapacity);
		}

		// Begin and End are set to InvalidSequenceNumber when nothing is tracked.
		// They are also invalid when UNINITIALIZED.

		// But if Begin and End are InvalidSequenceNumber,
		// while MaxTracked is not InvalidSequenceNumber
		// Then there is nothing being tracked, but the data strucutre has been initialized
		uint64_t Sequence_Begin = InvalidSequenceNumber;
		uint64_t Sequence_End = InvalidSequenceNumber;

		// Sequence_MaxTracked is a presistent Member variable that retains its value even after a flush
		// Keeps track of the Max Sequence Number that has been tracked since initialization
		uint64_t Sequence_MaxTracked = InvalidSequenceNumber;

		std::vector<uint64_t> Sequence_Missing{};

		inline LogTrackerState GetTrackerState() const
		{
			if (Sequence_MaxTracked == InvalidSequenceNumber) return LogTrackerState::UNINITIALIZED;

			if (Sequence_Begin == InvalidSequenceNumber && Sequence_End == InvalidSequenceNumber) return LogTrackerState::UNTRACKED;

			if (Sequence_Begin == InvalidSequenceNumber && Sequence_End != InvalidSequenceNumber) return LogTrackerState::INVALID_STATE;
			if (Sequence_Begin != InvalidSequenceNumber && Sequence_End == InvalidSequenceNumber) return LogTrackerState::INVALID_STATE;
			if (Sequence_Begin > Sequence_MaxTracked || Sequence_End != Sequence_MaxTracked) return LogTrackerState::INVALID_STATE;
			if (Sequence_Begin > Sequence_End) return LogTrackerState::INVALID_STATE;

			if (Sequence_Missing.size() != 0) return LogTrackerState::TRACKED_WITH_MISSING;
			if (Sequence_Missing.size() == 0) return LogTrackerState::TRACKED_ALL;

			return LogTrackerState::INVALID_STATE;
		}
	};




	// ==========================================================================================================
	// Ordered Log Buffer
	// ==========================================================================================================

	struct OrderedLogBuffer
	{
		static constexpr std::size_t Capacity = g_OrderedLogCapacity;

		// =====================================
		// Buffer Position
		// =====================================

		struct BufferPosition
		{
			BufferPosition() = default;
			BufferPosition(std::size_t InChunkIndex, std::size_t InOffset) :
				ChunkIndex(InChunkIndex),
				Offset(InOffset) {
			}

			std::size_t ChunkIndex = Invalidindex;
			std::size_t Offset = Invalidindex;


			inline bool IsValid() const
			{
				return ChunkIndex < m_orderedLogBufferChunkCount &&
					Offset < Capacity;
			}
		};


		struct ReservedLogData
		{
			ReservedLogData() = default;

			// Sequence number corresponding to BufferIndexStart.
			// Advances when the oldest reserved slots are removed.
			// Retains its last value when the buffer becomes empty.
			uint64_t SequenceNumber_Start = InvalidSequenceNumber;	// Denotes the present reserved start sequence number. Set to invalid Sequnce Number when nothing is reserved
			uint64_t SequenceNumber_End = InvalidSequenceNumber;	// Denotes the present reserved end sequence number. Set to invalid Sequnce Number when nothing is reserved
			uint64_t SequenceNumber_Max = InvalidSequenceNumber;	// Denotes the last Max Sequence Number that has been reserved. Value does not change when nothing is reserved

			LogReserveState GetLogReserveState() const
			{
				if (SequenceNumber_Max == InvalidSequenceNumber) return LogReserveState::UNINITIALIZED;

				if (SequenceNumber_Start == InvalidSequenceNumber && SequenceNumber_End == InvalidSequenceNumber) return LogReserveState::UNRESERVED;

				if (SequenceNumber_Start != InvalidSequenceNumber && SequenceNumber_End == InvalidSequenceNumber) return LogReserveState::INVALID_INTERNAL_STATE;
				if (SequenceNumber_Start == InvalidSequenceNumber && SequenceNumber_End != InvalidSequenceNumber) return LogReserveState::INVALID_INTERNAL_STATE;
				if (SequenceNumber_Start != InvalidSequenceNumber && SequenceNumber_End != InvalidSequenceNumber
					&& SequenceNumber_Max == InvalidSequenceNumber) return LogReserveState::INVALID_INTERNAL_STATE;
				if (SequenceNumber_Start > SequenceNumber_Max || SequenceNumber_End > SequenceNumber_Max) return LogReserveState::INVALID_INTERNAL_STATE;

				if (SequenceNumber_Start <= SequenceNumber_End) return LogReserveState::RESERVED;

				return LogReserveState::INVALID_INTERNAL_STATE;
			}
		};

		// =====================================
		// Functions
		// =====================================

		LogReserveState GetReserveState();

		uint64_t GetEndReservedSequenceNumber() const;
		uint64_t GetStartReservedSequenceNumber() const;
		uint64_t GetMaxReservedSequenceNumber() const;

		std::size_t GetSize() const;
		bool IsEmpty() const;
		bool HasFreeChunk() const;		// Returns ture if the present write chunk or the next chunk is empty

		void IncrementBufferPosition(BufferPosition& OutBufferPosition);
		BufferPosition GetBufferPosition(const uint64_t SequenceNumber) const;

		OrderedLogSlot* GetReservedSlot(const uint64_t SequenceNumber);
		const OrderedLogSlot* GetReservedSlot(const uint64_t SequenceNumber) const;

		// Derserves the Buffer from the OldestReservedSequence to EndSequenceNumber
		LogReserveState DereserveSequences(uint64_t EndSequenceNumber);

		// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
		LogReserveState ReserveSequences(const uint64_t StartSequenceNumber);



	private:

		// =====================================
		// Member Parameters
		// =====================================

		static constexpr std::size_t m_orderedLogBufferChunkCount = 3;

		std::array<std::array <OrderedLogSlot, Capacity>, m_orderedLogBufferChunkCount> OrderedLogBuffers{ };

		BufferPosition m_bufferIndexStart{ 0 ,0 };								// Physical position of the oldest live reserved slot.
		BufferPosition m_bufferWriteCursor{ 0, 0 };								// Physical position of the next slot to reserve.
		std::array<std::size_t, m_orderedLogBufferChunkCount> m_bufferSize{ };	// The present size of each buffer

		ReservedLogData m_reservedLogData;
	};




	// ==========================================================================================================
	// Ordered Log Window
	// ==========================================================================================================

	struct OrderedLogWindow
	{
	public:

		OrderedLogWindow() = default;

		// Public Member
		static constexpr std::size_t Capacity = g_OrderedLogCapacity;


		// Public Functions

		void Initialize(LoggerThreadRegistry* InLoggerState);

		const OrderedLogSlot* GetFirstAccountedLogSlot() const;
		uint64_t GetFirstMissingSequenceAfter(uint64_t SequenceNumber) const;
		uint64_t GetFirstValidSequenceAfter(uint64_t SequenceNumber) const;
		const OrderedLogSlot* GetFirstValidLogSlotAfter(uint64_t SequenceNumber) const;
		uint64_t GetMaxReservedSequence() const;

		uint64_t GetInvertedCount() const;
		uint64_t GetSkippedCount() const;

		bool IsSequencesMissing();
		bool IsEmpty() const;

		const OrderedLogSlot* GetTrackedSlot(uint64_t SequenceNumber) const;
		const TrackedLogData& GetTrackedLogData() const;

		LogReserveState ReserveLogSlotsForTracking();



		// ---------------------------------------------------------------------------------------------------------------------------
		// Template Functions --------------------------------------------------------------------------------------------------------
		// ---------------------------------------------------------------------------------------------------------------------------

		// Track Log Data ------------------------------------------------------------------------------------------------------------

		LogTrackerState TrackLogData()
		{
			LogReserveState state = m_orderedLogBuffer.GetReserveState();

			if (state == LogReserveState::UNINITIALIZED ||
				state != LogReserveState::RESERVED ||
				state == LogReserveState::UNRESERVED)
			{
				if (ReserveLogSlotsForTracking() != LogReserveState::RESERVED)
				{
					return LogTrackerState::FAILURE_NO_RESERVE;
				}
			}

			if (m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED)
			{
				m_startSequenceThisPass = 0;
			}
			else
			{
				// This cannot result in a InvalidSequenceNumber
				// since the OrderLogBuffer reserve plicy wont allow resering any slot that equals InvalidSequenceNumber
				m_startSequenceThisPass = m_trackedData.Sequence_MaxTracked + 1;
			}

			uint64_t start_reserveSeq = m_orderedLogBuffer.GetStartReservedSequenceNumber();

			bool bAllQueueEmpty = true;

			for (std::size_t threadIndex = 0; threadIndex < NumberOfThreads; ++threadIndex)
			{
				LogThreadContext& context = m_loggerRegistryPointer->m_threadContexts[threadIndex];
				auto& buffer = context.ThreadQueue;

				if (buffer.IsEmpty())
				{
					continue;
				}
				else
				{
					bAllQueueEmpty = false;
				}

				auto ItSnap = buffer.GetRingBufferIteratorSnapshot();

				for (auto it = ItSnap.Begin; it != ItSnap.End; ++it)
				{
					// If no log has been consumed/reserved then use expected as 0.
					// This results in the index of the incoming sequence passing the threashold.
					uint64_t expected = m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED
						? 0
						: m_trackedData.Sequence_MaxTracked + 1;

					if (expected == InvalidSequenceNumber)
					{
						return LogTrackerState::FAILURE_SENTINAL;
					}

					const LogData* queueBufferData = &(*it);

					if (queueBufferData == nullptr)
					{
						std::cout << "Data is Null in SPSC Queue" << "\n";
						assert(false && "Data is Null in SPSC Queue");
						continue;
					}

					const uint64_t seq = queueBufferData->Sequence;

					if (seq == InvalidSequenceNumber)
					{
						std::cout << "INVALID Sequence Number FOUND in Context Queue" << "\n";
						continue;
					}

					if (m_lastPublishSequence != InvalidSequenceNumber && seq <= m_lastPublishSequence)		// Stale sequences bypass tracking
					{
						continue;
					}

					if (seq > m_orderedLogBuffer.GetEndReservedSequenceNumber()) // Sequence is out of Reserved range
					{
						break;
					}

					std::size_t bMissingIndex = Invalidindex;
					OrderedLogSlot* orderedSlot = m_orderedLogBuffer.GetReservedSlot(seq);
					if (orderedSlot == nullptr)
					{
						std::cout << "Invalid Slot. The Sequence Number was possibly not reserved" << "\n";
						assert(false);
						continue;
					}

					LogData* orderedData = orderedSlot->Data;

					// If the slot returned by the LogBuffer, already contains a pointer to valid data,
					// It is a data reserved in the last pass, and is waiting flush
					if (orderedData != nullptr)
					{
						continue;
					}

					// If expected > seq, then check if the seq is found in m_missingSequences
					if (expected > seq)
					{// If seq is not a missing tracked sequence, the seq has already been reserved in the previous pass and is waiting flush
						if (m_trackedData.Sequence_Missing.size() > 0)
						{
							auto it = std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), seq);

							if (it == m_trackedData.Sequence_Missing.end())
							{
								continue;
							}

							// If seq is a missing sequence, store the index, and pop only if successfully reserved
							bMissingIndex = static_cast<std::size_t>(std::distance(m_trackedData.Sequence_Missing.begin(), it));
						}
						else
						{
							__debugbreak();
							assert(false);
						}
					}

					*orderedSlot = OrderedLogSlot(0, 0, seq, const_cast<LogData*>(queueBufferData));

					// if expected < seq, reserve all Sequence Numbers between expected and seq
					if (expected < seq)
					{
						// if seq > expected, mark all in between sequences as missing
						// and push back into m_missingSequences
						for (uint64_t missing = expected; missing < seq; ++missing)
						{
							m_trackedData.Sequence_Missing.push_back(missing);
						}
					}

					if (m_trackedData.Sequence_End == InvalidSequenceNumber || m_trackedData.Sequence_End < seq)
					{
						m_trackedData.Sequence_End = seq;
						m_trackedData.Sequence_MaxTracked = seq;
					}

					if (m_trackedData.Sequence_Begin == InvalidSequenceNumber)
					{
						m_trackedData.Sequence_Begin = m_startSequenceThisPass;
					}

					// If seq is a missing index, swap and pop from m_trackedData.SequenceMissing
					if (bMissingIndex != Invalidindex)
					{
						std::swap(m_trackedData.Sequence_Missing[bMissingIndex], m_trackedData.Sequence_Missing.back());
						m_trackedData.Sequence_Missing.pop_back();
					}
				}
			}

			if (m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED || m_trackedData.GetTrackerState() == LogTrackerState::UNTRACKED)
			{
				if (bAllQueueEmpty)
				{
					return LogTrackerState::FAILURE_EMPTY;
				}
			}

			return m_trackedData.GetTrackerState();
		}




		// Publish Log Data ----------------------------------------------------------------------------------------------------------
		template <typename Publisher>
		LogPublishState PublishLogData(uint64_t EndSequenceNumber, Publisher&& PublishFunction)
		{
			if (m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED) return LogPublishState::FAILURE_UNINITIALIZED;

			if (m_orderedLogBuffer.IsEmpty()) return LogPublishState::FAILURE_EMPTY;

			if (m_trackedData.Sequence_Begin < m_orderedLogBuffer.GetStartReservedSequenceNumber()) return LogPublishState::INVALID_INTERNAL_STATE;

			if (m_trackedData.Sequence_Begin > EndSequenceNumber || EndSequenceNumber == InvalidSequenceNumber) return LogPublishState::FAILURE_INVALID_SEQUENCE;

			LogPublishState publishState = LogPublishState::SUCCESS;

			uint64_t expected = m_lastPublishSequence == InvalidSequenceNumber
				? 0
				: m_lastPublishSequence + 1;

			while (expected <= EndSequenceNumber)
			{
				OrderedLogSlot* slot = m_orderedLogBuffer.GetReservedSlot(expected);

				if (slot == nullptr)
				{
					std::cout << "Null Slot found in OrderedLogBuffer" << std::endl;
					assert(false);
					++expected;
					continue;
				}

				LogData* data = slot->Data;

				if (data == nullptr)
				{
					//std::cout << "Null Data found in OrderedLogBuffer" << std::endl;
					MissingDataTemplate.Sequence = expected;
					PublishFunction(MissingDataTemplate);
					++m_skippedLogCount;
				}
				else
				{
					PublishFunction(*data);
				}

				m_lastPublishSequence = expected;
				++expected;
			}

			uint64_t begin = m_trackedData.Sequence_Begin;

			// Publish to Sinks and Dequeue the ThreadContext Queues
			for (auto& context : m_loggerRegistryPointer->m_threadContexts)
			{
				auto& logBuffer = context.ThreadQueue;

				if (m_skippedLogCount > 0)
				{
					while (!logBuffer.IsEmpty() && logBuffer.PeekLogical(0).Sequence <= EndSequenceNumber)
					{
						uint64_t seq = logBuffer.PeekLogical(0).Sequence;
						if (seq < begin)
						{
							PublishFunction(logBuffer.PeekLogical(0));
							--m_skippedLogCount;
							++m_invertedLogCount;
						}
						else if (std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), seq) !=
							m_trackedData.Sequence_Missing.end())
						{
							PublishFunction(logBuffer.PeekLogical(0));
							--m_skippedLogCount;
							++m_invertedLogCount;
						}

						logBuffer.Dequeue();
					}
				}
				else
				{
					while (!logBuffer.IsEmpty() && logBuffer.PeekLogical(0).Sequence <= EndSequenceNumber)
					{

						logBuffer.Dequeue();
					}
				}
			}


			// Dereserve the m_orderedLogBuffer
			LogReserveState reserveState = m_orderedLogBuffer.DereserveSequences(EndSequenceNumber);

			if (reserveState != LogReserveState::DERESERVED && reserveState != LogReserveState::DERESERVED_EMPTY)
			{
				std::cout << "ERROR WITH DERESERVING SEQUENCES" << std::endl;
				assert(false);
				return LogPublishState::INVALID_INTERNAL_STATE;
			}


			// Update the m_trackedData
			if (m_trackedData.Sequence_End == EndSequenceNumber)
			{
				m_trackedData.Sequence_Begin = InvalidSequenceNumber;
				m_trackedData.Sequence_End = InvalidSequenceNumber;
			}
			else
			{
				m_trackedData.Sequence_Begin = EndSequenceNumber + 1;
			}


			// Clear the Missing Sequences from the Tracked Data, older than the last published data
			std::size_t MissingIndex = 0;

			if (m_trackedData.Sequence_Missing.size() != 0)
			{
				while (MissingIndex < m_trackedData.Sequence_Missing.size())
				{
					uint64_t missingSeq = m_trackedData.Sequence_Missing[MissingIndex];

					if (missingSeq <= EndSequenceNumber)
					{
						std::swap(m_trackedData.Sequence_Missing[MissingIndex], m_trackedData.Sequence_Missing.back());
						m_trackedData.Sequence_Missing.pop_back();
					}
					else
					{
						++MissingIndex;
					}
				}
			}

			return publishState;
		}

	private:

		// =====================================
		// Member Parameters
		// =====================================

		TrackedLogData m_trackedData;

		OrderedLogBuffer m_orderedLogBuffer;

		std::size_t m_cursorStart = 0;
		std::size_t m_cursorEnd = 0;

		uint64_t m_startSequenceThisPass = InvalidSequenceNumber;

		LoggerThreadRegistry* m_loggerRegistryPointer = nullptr;

		LogData MissingDataTemplate{ std::this_thread::get_id(), 0, Neyrx::Logger::LogCategory::Temp, Neyrx::Logger::LogLevel::Warning, "MISSING SEQUENCE NUMBER"};

		uint64_t m_lastPublishSequence = InvalidSequenceNumber;
		uint64_t m_invertedLogCount = 0;
		uint64_t m_skippedLogCount = 0;
	};
}