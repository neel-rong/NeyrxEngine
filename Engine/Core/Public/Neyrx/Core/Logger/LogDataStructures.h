#include<string>
#include<chrono>
#include<array>


#include <stdio.h>			// Temp
#include <iostream>			// Temp


// Temporary Global Variables
static constexpr uint64_t InvalidSequenceNumber = std::numeric_limits<uint64_t>::max();
static constexpr uint64_t Invalidindex = std::numeric_limits<uint64_t>::max();

static constexpr std::size_t g_OrderedLogCapacity = 64;


namespace Neyrx::Logger
{
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
		FAILURE_SENTINAL
	};


	// ==========================================================================================================
	// Enum Reserve State
	// ==========================================================================================================

	enum class LogReserveState : uint8_t
	{
		UNINITIALIZED,
		SUCCESS_RESERVED,
		FAILURE_AT_CAPACITY,
		FAILURE_SEQUENCE_INVALID,
		FAILURE_SENTINAL
	};




	// ==========================================================================================================
	// Enum Order Log Buffer State
	// ==========================================================================================================

	enum class OrderedLogBufferState : uint8_t
	{
		UNINITIALIZED,
		RESERVED_HAS_CAPACITY,
		RESERVED_FULL,
		EMPTY,
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

		LogData(uint64_t InSequenceNumber, LogCategory InCategory, LogLevel InLevel, std::string InLogMsg) :
			Sequence(InSequenceNumber),
			Category(InCategory),
			Level(InLevel),
			Message(std::move(InLogMsg)),
			Time(std::chrono::system_clock::now())
		{
		}

		inline void PrintData()	const	// Temporary
		{
			std::cout << "[ " << Sequence << " ] [ " << ToString(Category) << " ] [ " << ToString(Level) << "]  " << Message << std::endl;
		}

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
		LogThreadContext() : m_logBuffer() {}

		SPSCRingBuffer<LogData, ThreadQueueSize> m_logBuffer;
	};



	// ==========================================================================================================
	// Logger State
	// ==========================================================================================================

	struct LoggerThreadRegistry
	{
		LoggerThreadRegistry() = default;

		std::array<LogThreadContext, NumberOfThreads> m_threadContexts;

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
		uint64_t Sequence_Begin = InvalidSequenceNumber;
		uint64_t Sequence_End = InvalidSequenceNumber;

		std::array<uint64_t, g_OrderedLogCapacity> Sequence_Missing{};
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
				return ChunkIndex < OrderedLogBufferChunkCount &&
					Offset < Capacity;
			}
		};

		// =====================================
		// Functions
		// =====================================

		uint64_t GetLastReservedSequenceNumber() const;
		uint64_t GetOldestReservedSequenceNumber() const;

		std::size_t GetSize() const;
		bool IsEmpty() const;
		bool HasFreeChunk() const;		// Returns ture if the present write chunk or the next chunk is empty

		void IncrementBufferPosition(BufferPosition& OutBufferPosition);
		BufferPosition GetBufferPosition(const uint64_t SequenceNumber);
		OrderedLogSlot* GetReservedSlot(const uint64_t SequenceNumber);

		// Derserves the Buffer from the OldestReservedSequence to EndSequenceNumber
		bool DereserveSequences(uint64_t EndSequenceNumber);

		// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
		LogReserveState ReserveSequences(const uint64_t StartSequenceNumber);



	private:

		// =====================================
		// Member Parameters
		// =====================================

		static constexpr std::size_t OrderedLogBufferChunkCount = 3;

		OrderedLogBufferState LogBufferState = OrderedLogBufferState::UNINITIALIZED;

		std::array<std::array <OrderedLogSlot, Capacity>, OrderedLogBufferChunkCount> OrderedLogBuffers{ };

		BufferPosition BufferIndexStart{ 0 ,0 };							// Physical position of the oldest live reserved slot.
		BufferPosition BufferWriteCursor{ 0, 0 };							// Physical position of the next slot to reserve.
		std::array<std::size_t, OrderedLogBufferChunkCount> BufferSize{ };	// The present size of each buffer

		// Sequence number corresponding to BufferIndexStart.
		// Advances when the oldest reserved slots are removed.
		// Retains its last value when the buffer becomes empty.
		uint64_t OldestReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the actual reserved oldest sequnce. Set to invalid Sequnce Number when nothing is reserved
		uint64_t LastReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the last reserved sequence number. Retains the value even if nothing is stored.
	};




	// ==========================================================================================================
	// Ordered Log Window
	// ==========================================================================================================

	struct OrderedLogWindow
	{
	public:

		OrderedLogWindow() {}

		static constexpr std::size_t Capacity = g_OrderedLogCapacity;

		void Initialize(LoggerThreadRegistry* InLoggerState);	// Remove Sink

		LogReserveState ReserveLogSlotsForTracking();
		LogTrackerState TrackLogData();

		const OrderedLogSlot* GetOldestValidLogSlot()
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

		const TrackedSequenceNumbers& GetTrackedLogData() const
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
						std::cout << "Failed to Dequeue" << std::endl;
						break;
					}
				}
			}

			if (!m_orderedLogBuffer.DereserveSequences(StartSequenceNumber, EndSequenceNumber))
			{
				std::cout << "ERROR WITH DERESERVING SEQUENCES" << std::endl;
				assert(false);
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

			return bFlushed;
		}

		bool IsEmpty() const
		{
			return m_orderedLogBuffer.IsEmpty();
		}

	private:

		LogTrackerState m_trackerState;
		LogReserveState m_reserveState;

		TrackedLogData m_trackedData;

		OrderedLogBuffer m_orderedLogBuffer;

		std::size_t m_cursorStart = 0;
		std::size_t m_cursorEnd = 0;

		uint64_t m_StartSequenceThisPass = InvalidSequenceNumber;

		Neyrx::Logger::LoggerState* m_loggerStatePointer = nullptr;
		std::vector<ILoggerSink*>* m_SinksPtr;

		LogData MissingDataTemplate{ 0, Neyrx::Logger::LogCategory::Temp, Neyrx::Logger::LogLevel::Warning, "MISSING SEQUENCE NUMBER"};
	};
}