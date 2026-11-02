#pragma once

#include<string>
#include<chrono>
#include<array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <charconv>
#include <stdexcept>
#include <cstdio>
#include <tuple>
#include <utility>
#include <concepts>


#include "Neyrx/Core/Container/Queue.h"


#include <stdio.h>			// Temp
#include <iostream>			// Temp
#include <thread>


// Temporary Global Variables
inline constexpr uint64_t InvalidSequenceNumber = std::numeric_limits<uint64_t>::max();
inline constexpr uint64_t Invalidindex = std::numeric_limits<uint64_t>::max();

inline constexpr std::size_t NumberOfThreads = 8;
inline constexpr std::size_t ThreadQueueSize = 16384;

inline constexpr std::size_t MaxLogArguments = 8;

inline constexpr std::size_t g_OrderedLogCapacity = ThreadQueueSize * NumberOfThreads;
inline constexpr std::size_t g_estimatedLogStringSize = 256;


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
	// Format Operation Type
	// ==========================================================================================================
	enum class LogFormatOpType
	{
		Integer,
		Float,
		Double,
		Boolean,
		Literal,
		Pointer,
		EscapedPercent

	};




	// ==========================================================================================================
	// Log Argument Type
	// ==========================================================================================================
	enum class LogArgumentType
	{
		SignedInteger,
		UnsignedInteger,
		Float,
		Double,
		Boolean,
		Character,
		StringLiteral,
		Pointer,
		Invalid
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
	// Fixed String
	// ==========================================================================================================

	template<std::size_t N>
	struct FixedString
	{
		// Always needs compile time evaluation
		// for compile time format-string validation later in the pipeline
		consteval FixedString(const char(&String)[N])
		{
			for (std::size_t i = 0; i < N; ++i)
			{
				Value[i] = String[i];
			}
		}


		constexpr std::size_t size() const
		{
			return N - 1;
		}

		constexpr std::string_view StringView() const
		{
			return { Value, N - 1 };
		}

		char Value[N];
	};




	// ==========================================================================================================
	// Log Format Operations
	// ==========================================================================================================

	struct LogFormatOperation
	{
		LogFormatOpType Type = LogFormatOpType::Literal;
		std::size_t Offset = 0;
		std::size_t Length = 0;
		std::size_t ArgumentIndex = 0;
	};




	// ==========================================================================================================
	// Get Operations Count
	// ==========================================================================================================

	template<FixedString Format>
	consteval std::size_t GetOperationsCount()
	{
		std::size_t count = 0;
		std::size_t index = 0;
		std::size_t offset = 0;

		while (index < Format.size())
		{
			// iterate till a format specifier is found
			if (Format.Value[index] != '%')
			{
				++index;
				continue;
			}

			if (index + 1 >= Format.size())
			{
				throw "Invalid log format";
			}

			// If there is a string literal before the format specifier, increment count
			if (index > offset)
			{
				++count;
			}

			// Increment count for format specifier
			++count;

			// Increment index and update offset to skip the format specifier
			index += 2;
			offset = index;
		}

		// Increment for trailing literal, if any
		if (offset < Format.size()) ++count;

		return count;
	}




	// ==========================================================================================================
	// Get Specifier Count
	// ==========================================================================================================

	template<FixedString Format>
	consteval std::size_t GetSpecifierCount()
	{
		std::size_t count = 0;
		for (std::size_t i = 0; i < Format.size(); ++i)
		{
			if (Format.Value[i] != '%')
			{
				continue;
			}
			else if (i + 1 > Format.size())
			{
				throw "Invalid Log format";
			}

			++count;
		}

		return count;
	}




	// ==========================================================================================================
	// Log Metadata
	// ==========================================================================================================

	template<FixedString Format>
	struct LogMetadata
	{
		std::array<LogFormatOperation, GetOperationsCount<Format>()> Operations{};
		std::array<LogArgumentType, GetSpecifierCount<Format>()> ArgumentTypes{};
		std::size_t OperationCount = 0;
		std::size_t ArgumentCount = 0;
	};




	// ==========================================================================================================
	// Get Log Argument Type
	// ==========================================================================================================

	template <typename T>
	consteval LogArgumentType GetLogArgumentType()
	{
		using Type = std::remove_cvref_t<T>;


		if constexpr (std::is_same_v<Type, bool>)
		{
			return LogArgumentType::Boolean;
		}
		else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>)
		{
			return LogArgumentType::SignedInteger;
		}
		else if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>)
		{
			return LogArgumentType::UnsignedInteger;
		}
		else if constexpr (std::is_same_v<Type, float>)
		{
			return LogArgumentType::Float;
		}
		else if constexpr (std::is_same_v<Type, double>)
		{
			return LogArgumentType::Double;
		}
		else if constexpr (std::is_same_v<Type, char>)
		{
			return LogArgumentType::Character;
		}
		else if constexpr (std::is_same_v<Type, const char*>)
		{
			return LogArgumentType::StringLiteral;
		}
		else if constexpr (std::is_pointer_v<Type>)
		{
			return LogArgumentType::Pointer;
		}
		else
		{
			static_assert(!sizeof(Type), "Unsupported log argument type");
			return LogArgumentType::Invalid; // Default return to satisfy compiler
		}
	}




	// ==========================================================================================================
	// Parse Log Format
	// ==========================================================================================================

	template <FixedString Format, typename... Args>
	consteval auto ParseFormat()
	{
		LogMetadata<Format> metadata;
		constexpr std::array<LogArgumentType, sizeof...(Args)> arguments{
			GetLogArgumentType<Args>()...
		};

		std::size_t offset = 0;

		std::size_t index = 0;

		while (index < Format.size())
		{
			if (Format.Value[index] != '%')
			{
				++index;
				continue;
			}

			if (index + 1 >= Format.size())
			{
				throw "Invalid log format";
			}

			if (index > offset)
			{
				metadata.Operations[metadata.OperationCount++] =
				{
					LogFormatOpType::Literal,
					offset,
					index - offset,
					0
				};
			}

			offset = index + 2;

			const char specifier = Format.Value[index + 1];

			if (specifier != '%')
			{
				if (metadata.ArgumentCount >= sizeof...(Args))
				{
					throw "Invalid log format: Argument count mismatch";
				}

				auto argumentType = std::remove_cvref_t<decltype(arguments[metadata.ArgumentCount])>();

				LogFormatOpType type;

				if ((specifier == 'd' && argumentType == LogArgumentType::SignedInteger) ||
					(specifier == 'u' && argumentType == LogArgumentType::UnsignedInteger))
				{
					type = LogFormatOpType::Integer;
				}
				else if (specifier == 'f' && argumentType == LogArgumentType::Float)
				{
					type = LogFormatOpType::Float;
				}
				else if (specifier == 'l' && argumentType == LogArgumentType::Double)
				{
					type = LogFormatOpType::Double;
				}
				else if (specifier == 'b' && argumentType == LogArgumentType::Boolean)
				{
					type = LogFormatOpType::Boolean;
				}
				else if ((specifier == 's' && argumentType == LogArgumentType::StringLiteral) ||
					(specifier == 'c' && argumentType == LogArgumentType::Character))
				{
					type = LogFormatOpType::Literal;
				}
				else if (specifier == 'p' && argumentType == LogArgumentType::Pointer)
				{
					type = LogFormatOpType::Pointer;
				}
				else
				{
					throw "Invalid specifier";
				}

				metadata.Operations[metadata.OperationCount++] =
				{
					type,
					index,
					2,
					metadata.ArgumentCount
				};

				metadata.ArgumentTypes[metadata.ArgumentCount++] = argumentType;
			}
			else
			{
				metadata.Operations[metadata.OperationCount++] =
				{
					LogFormatOpType::EscapedPercent,
					index,
					2,
					0
				};
			}

			if (index + 2 >= Format.size())
			{
				break;
			}

			index += 2;
		}

		if (offset < Format.size())
		{
			metadata.Operations[metadata.OperationCount++] =
			{
				LogFormatOpType::Literal,
				index,
				Format.size() - offset,
				0
			};
		}

		return metadata;
	}




	// ==========================================================================================================
	// Log Format Metadata Holder
	// ==========================================================================================================

	template<FixedString Format, typename... Args>
	struct LogFormatMetadataHolder
	{
		static constexpr auto FormatString = Format;
		static constexpr auto Metadata = ParseFormat<Format, Args...>();
	};




	// ==========================================================================================================
	// Log Argument Value
	// ==========================================================================================================

	union LogArgumentValue
	{
		int64_t SignedInteger;
		uint64_t UnsignedInteger;
		float FloatValue;
		double DoubleValue;
		bool BooleanValue;
		char Character;
		const char* StringLiteral;
		void* Pointer;

		constexpr LogArgumentValue() :
			UnsignedInteger(0) { }
	};




	// ==========================================================================================================
	// Log Argument
	// ==========================================================================================================

	struct LogArgument
	{
		LogArgumentType ArgType = LogArgumentType::UnsignedInteger;
		LogArgumentValue ArgValue{};
	};




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

		LogData(uint64_t InSequenceNumber,
				LogCategory InCategory,
				LogLevel InLevel,
				const char* InLogStr,
				const std::size_t InOperationCount,
				const std::size_t InArgumentCount) :
			ThreadID(std::this_thread::get_id()),
			Sequence(InSequenceNumber),
			Category(InCategory),
			Level(InLevel),
			LogFormatString(InLogStr),
			OperationCount(InOperationCount),
			ArgumentCount(InArgumentCount),
			Time(std::chrono::system_clock::now())
		{
		}

		template <typename... Args>
		LogData(uint64_t InSequenceNumber,
				LogCategory InCategory,
				LogLevel InLevel,
				const char* InLogStr,
				const LogFormatOperation* InLogFormatOperations,
				const std::size_t InOperationCount,
				const std::size_t InArgumentCount,
				Args&&... InArguments)	:
			Sequence(InSequenceNumber),
			Category(InCategory),
			Level(InLevel),
			LogFormatString(InLogStr),
			LogOperations(InLogFormatOperations),
			OperationCount(InOperationCount),
			ArgumentCount(InArgumentCount),
			ThreadID(std::this_thread::get_id()),
			Time(std::chrono::system_clock::now())
		{
			std::size_t index = 0;
			(StoreArguments(index++, std::forward<Args>(InArguments)), ...);
		}

		template<typename Arg>
		void StoreArguments(std::size_t Index, Arg&& InArgument)
		{
			using Type = std::remove_cvref_t<Arg>;
			auto& destination = LogArguments[Index];

			if constexpr (std::is_same_v<Type, bool>)
			{
				destination.ArgType = LogArgumentType::Boolean;
				destination.ArgValue.BooleanValue = InArgument;
			}
			else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>)
			{
				destination.ArgType = LogArgumentType::SignedInteger;
				destination.ArgValue.SignedInteger = InArgument;
			}
			else if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>)
			{
				destination.ArgType = LogArgumentType::UnsignedInteger;
				destination.ArgValue.UnsignedInteger = InArgument;
			}
			else if constexpr (std::is_same_v<Type, float>)
			{
				destination.ArgType = LogArgumentType::Float;
				destination.ArgValue.FloatValue = InArgument;
			}
			else if constexpr (std::is_same_v<Type, double>)
			{
				destination.ArgType = LogArgumentType::Double;
				destination.ArgValue.DoubleValue = InArgument;
			}
			else if constexpr (std::is_same_v<Type, char>)
			{
				destination.ArgType = LogArgumentType::Character;
				destination.ArgValue.Character = InArgument;
			}
			else
			{
				static_assert(!sizeof(Type), "Unsupported log argument type");
			}
		}

		std::thread::id ThreadID;
		uint64_t Sequence = InvalidSequenceNumber;
		LogCategory Category = LogCategory::Temp;
		LogLevel Level = LogLevel::Info;

		const char* LogFormatString = nullptr;
		const LogFormatOperation* LogOperations = nullptr;
		std::array<LogArgument, MaxLogArguments> LogArguments{};

		std::size_t OperationCount = 0;
		std::size_t ArgumentCount = 0;

		std::chrono::system_clock::time_point Time = std::chrono::system_clock::now();
	};




	// ==========================================================================================================
	// Log Arena
	// ==========================================================================================================

	struct LogArena
	{
	public:

		LogArena() = default;

		void Initialize();
		void Reset();

		std::size_t GetSize() const;
		std::size_t GetArenaStart() const;
		std::size_t GetArenaEnd() const;
		bool IsEmpty() const;
		bool IsFull() const;

		bool WriteToArena(const char* InData, std::size_t InLength, std::size_t& OutOffset);
		char* GetHeadData();

	private:

		bool HasCapacityFor(std::size_t InLength) const;

		// Private Members

		using BufferType = RingBuffer<char, g_OrderedLogCapacity* g_estimatedLogStringSize>;

		std::unique_ptr<BufferType> m_arenaBuffer = std::make_unique_for_overwrite<BufferType>();

		bool bInitialized = 0;
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

		// Execute Log Format Operations Recurssive ----------------------------------------------------------------------------------

		template<typename Tuple>
		bool ExecuteLogFormatOperations_Recurssive(const char* FormatString,
			Tuple Arguments,
			const LogFormatOperation* FormatOperations,
			std::size_t OperationCount,
			std::size_t ArgumentCount,
			std::size_t OperationIndex = 0)
		{

		}




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



		// Format Log Data -----------------------------------------------------------------------------------------------------------





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
					/*MissingDataTemplate.Sequence = expected;
					PublishFunction(MissingDataTemplate);*/
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
		LogArena m_logArena;

		std::size_t m_cursorStart = 0;
		std::size_t m_cursorEnd = 0;

		uint64_t m_startSequenceThisPass = InvalidSequenceNumber;

		LoggerThreadRegistry* m_loggerRegistryPointer = nullptr;

		//LogData MissingDataTemplate{0, Neyrx::Logger::LogCategory::Temp, Neyrx::Logger::LogLevel::Warning, "MISSING SEQUENCE NUMBER"};

		uint64_t m_lastPublishSequence = InvalidSequenceNumber;
		uint64_t m_invertedLogCount = 0;
		uint64_t m_skippedLogCount = 0;
	};
}