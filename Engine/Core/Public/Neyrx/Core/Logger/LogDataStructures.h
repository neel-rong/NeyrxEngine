#include<string>
#include<chrono>
#include<array>


#include <stdio.h>			// Temp
#include <iostream>			// Temp


// Temporary Global Variables
static constexpr uint64_t InvalidSequenceNumber = std::numeric_limits<uint64_t>::max();
static constexpr uint64_t Invalidindex = std::numeric_limits<uint64_t>::max();

static constexpr std::size_t NumberOfThreads = 4;
static constexpr std::size_t ThreadQueueSize = 256;

static constexpr std::size_t g_OrderedLogCapacity = 64;


namespace Neyrx::Logger
{
	// ==========================================================================================================
	// Enum Reserve State
	// ==========================================================================================================

	enum class LogReserveState : uint8_t
	{
		UNINITIALIZED,
		SUCCESS_RESERVED,
		SUCCESS_DERESERVED,
		SUCCESS_DERESERVED_EMPTY,
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

		LogReserveState m_logReserveState = LogReserveState::UNINITIALIZED;

		std::array<std::array <OrderedLogSlot, Capacity>, m_orderedLogBufferChunkCount> OrderedLogBuffers{ };

		BufferPosition m_bufferIndexStart{ 0 ,0 };							// Physical position of the oldest live reserved slot.
		BufferPosition m_bufferWriteCursor{ 0, 0 };							// Physical position of the next slot to reserve.
		std::array<std::size_t, m_orderedLogBufferChunkCount> m_bufferSize{ };	// The present size of each buffer

		// Sequence number corresponding to BufferIndexStart.
		// Advances when the oldest reserved slots are removed.
		// Retains its last value when the buffer becomes empty.
		uint64_t m_startReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the present reserved start sequence number. Set to invalid Sequnce Number when nothing is reserved
		uint64_t m_endReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the present reserved end sequence number. Set to invalid Sequnce Number when nothing is reserved
		uint64_t m_maxReservedSequenceNumber = InvalidSequenceNumber;	// Denotes the last Max Sequence Number that has been reserved. Value does not change when nothing is reserved
	};




	// ==========================================================================================================
	// Ordered Log Window
	// ==========================================================================================================

	struct OrderedLogWindow
	{
	public:

		OrderedLogWindow() {}

		// Public Member
		static constexpr std::size_t Capacity = g_OrderedLogCapacity;


		// Public Functions

		void Initialize(LoggerThreadRegistry* InLoggerState);

		const OrderedLogSlot* GetFirstAccountedLogSlot() const;
		uint64_t GetFirstMissingSequenceAfter(uint64_t SequenceNumber) const;
		uint64_t GetFirstValidSequenceAfter(uint64_t SequenceNumber) const;
		uint64_t GetMaxReservedSequence() const;

		bool IsSequencesMissing();
		bool IsEmpty() const;

		const OrderedLogSlot* GetTrackedSlot(uint64_t SequenceNumber) const;
		const TrackedLogData& GetTrackedLogData() const;

		LogReserveState ReserveLogSlotsForTracking();
		LogTrackerState TrackLogData();

		bool IsStaleDataPending() const;


		// ---------------------------------------------------------------------------------------------------------------------------
		// Template Functions --------------------------------------------------------------------------------------------------------
		// ---------------------------------------------------------------------------------------------------------------------------

		// Publish Log Data ----------------------------------------------------------------------------------------------------------
		template <typename Publisher>
		LogPublishState PublishLogData(uint64_t EndSequenceNumber, Publisher&& PublishFunction)
		{
			if (m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED) return LogPublishState::FAILURE_UNINITIALIZED;

			if (m_orderedLogBuffer.IsEmpty()) return LogPublishState::FAILURE_EMPTY;

			if (m_trackedData.Sequence_Begin > EndSequenceNumber || EndSequenceNumber == InvalidSequenceNumber) return LogPublishState::FAILURE_INVALID_SEQUENCE;

			LogPublishState publishState = LogPublishState::SUCCESS;

			// Publish to Sinks
			for (uint64_t seq = m_trackedData.Sequence_Begin; seq <= EndSequenceNumber; ++seq)
			{
				OrderedLogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);
				if (slot == nullptr)		// Should not hit this state unless there is bug
				{
					std::cout << "Reserved Slot NOT FOUND for Seq: " << seq << std::endl;	// TODO - Temp
					assert(false);
					return LogPublishState::INVALID_INTERNAL_STATE;
				}

				LogData* data = slot->Data;
				if (data == nullptr)
				{
					MissingDataTemplate.Sequence = seq;
					data = &MissingDataTemplate;
					std::cout << "LOG DATA POINTER FOR SEQ: " << seq << " is a NULL_PTR" << std::endl; // TODO Temp
					publishState = LogPublishState::SUCCESS_WITH_MISSING;
				}
				// Publish is safe only because consumer is responsible for Sink execution.
				// If later the architecture changes, this needs to be taken into account.
				PublishFunction(*data);
			}

			// Dequeue the ThreadContext Queues
			for (auto& context : m_loggerRegistryPointer->m_threadContexts)
			{
				auto& logBuffer = context.m_logBuffer;

				while (!logBuffer.IsEmpty())
				{
					uint64_t peekSeq = logBuffer.Peek(logBuffer.FrontIndex()).m_sequence;

					if (peekSeq >= m_trackedData.Sequence_Begin && peekSeq <= m_trackedData.Sequence_End)
					{
						logBuffer.Dequeue();
					}
					else
					{
						return LogPublishState::INVALID_INTERNAL_STATE;
					}
				}
			}

			// Dereserve the m_orderedLogBuffer
			LogReserveState reserveState = m_orderedLogBuffer.DereserveSequences(EndSequenceNumber);

			if (reserveState != LogReserveState::SUCCESS_DERESERVED || reserveState != LogReserveState::SUCCESS_DERESERVED_EMPTY)
			{
				std::cout << "ERROR WITH DERESERVING SEQUENCES" << std::endl;
				assert(false);
				return LogPublishState::INVALID_INTERNAL_STATE;
			}

			// Update the m_trackedData
			if (m_trackedData.Sequence_End == EndSequenceNumber && m_orderedLogBuffer.IsEmpty())
			{
				m_trackedData.Sequence_Begin = InvalidSequenceNumber;
				m_trackedData.Sequence_End = InvalidSequenceNumber;

				// Clear the Missing Sequences from the Tracked Data, older than the last published data
				for (std::size_t bMissingIndex = 0; bMissingIndex < m_trackedData.Sequence_Missing.size() - 1; ++bMissingIndex)
				{
					uint64_t missingSeq = m_trackedData.Sequence_Missing[i];

					if (missingSeq > EndSequenceNumber) continue;

					std::swap(m_trackedData.Sequence_Missing[bMissingIndex], m_trackedData.Sequence_Missing.back());
					m_trackedData.Sequence_Missing.pop_back();
				}
			}
			else if (m_trackedData.Sequence_End < EndSequenceNumber)
			{
				m_trackedData.Sequence_Begin = EndSequenceNumber + 1;
			}

			return publishState;
		}



		// Publish Stale Data --------------------------------------------------------------------------------------------------------
		template <typename Publisher>
		LogPublishState PublishStaleData(Publisher&& PublishFunction)
		{
			if (!IsStaleDataPending()) return LogPublishState::FAILURE_EMPTY;

			for (const LogData* data : m_staleLogBuffer)
			{
				PublishFunction(data);
			}

			m_staleLogBuffer.clear();
		}

	private:

		// =====================================
		// Member Parameters
		// =====================================

		TrackedLogData m_trackedData;

		OrderedLogBuffer m_orderedLogBuffer;
		std::vector<const LogData*> m_staleLogBuffer{ };

		std::size_t m_cursorStart = 0;
		std::size_t m_cursorEnd = 0;

		uint64_t m_startSequenceThisPass = InvalidSequenceNumber;

		LoggerThreadRegistry* m_loggerRegistryPointer = nullptr;

		LogData MissingDataTemplate{ 0, Neyrx::Logger::LogCategory::Temp, Neyrx::Logger::LogLevel::Warning, "MISSING SEQUENCE NUMBER"};
	};
}