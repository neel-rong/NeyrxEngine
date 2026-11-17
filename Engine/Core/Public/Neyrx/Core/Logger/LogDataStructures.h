#pragma once

#include <string>
#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <cstring>
#include <type_traits>
#include <charconv>
#include <stdexcept>
#include <cstdio>
#include <tuple>
#include <utility>
#include <concepts>
#include <functional>
#include <limits>
#include <span>
#include <bit>
#include <thread>

#include "Neyrx/Core/Container/Queue.h"
#include "Neyrx/Core/Container/Bitmap.h"
#include "Neyrx/Core/Container/ReorderBuffer.h"
#include "Neyrx/Core/Time/NX_Time.h"


#include <stdio.h>			// Temp
#include <iostream>			// Temp


// Temporary Global Variables
inline constexpr uint64_t InvalidSequenceNumber = (std::numeric_limits<uint64_t>::max)();
inline constexpr uint64_t InvalidIndex = (std::numeric_limits<uint64_t>::max)();

inline constexpr std::size_t NumberOfThreads = 6;
inline constexpr std::size_t ThreadQueueSize = 16384;

inline constexpr std::size_t g_maxLogArgumentCount = 8;
inline constexpr std::size_t g_maxStringArgumentLength = 1024;		// Longer strings are truncated
inline constexpr std::size_t g_maxLogFormatStringLength = 4096;
inline constexpr std::size_t g_OrderedLogCapacity = 4096;			// reorder window, not total in-flight

inline constexpr std::size_t g_logArenaCapacity = 512 * 1024;

static_assert(std::has_single_bit(g_OrderedLogCapacity) && g_OrderedLogCapacity >= 64,
	"g_OrderedLogCapacity must be a power of two >= 64");
static_assert(g_logArenaCapacity >= g_maxLogFormatStringLength + (g_maxStringArgumentLength * g_maxLogArgumentCount),
	"g_logArenaCapacity exceeded");


using namespace Neyrx::Time;
using namespace Neyrx::Container;


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
		UNINITIALIZED,

		SUCCESS,
		SUCCESS_WITH_MISSING,

		FAILURE_EMPTY,
		FAILURE_UNINITIALIZED,
		FAILURE_INVALID_SEQUENCE,

		INVALID_INTERNAL_STATE
	};





	// ==========================================================================================================
	// Enum Format State
	// ==========================================================================================================

	enum class LogFormatState : uint8_t
	{
		SUCCESS,

		FAILURE_CAPACITY,
		INVALID_INTERNAL_STATE
	};




	// ==========================================================================================================
	// Format Operation Type
	// ==========================================================================================================
	enum class LogFormatOpType
	{
		FormatLiteral,
		SignedInteger,
		UnsignedInteger,
		Float,
		Double,
		Boolean,
		Character,
		ArgumentLiteral,
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
X(Info)						\
X(Warning)					\
X(Error)					\
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
X(Temp)							\


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
	// Fixed String
	// ==========================================================================================================

	// Preflighted entry formatter
	struct StringRef
	{
		const char* Ptr;
		std::size_t Length;
	};


	static StringRef MakeStringRef(const char* String) noexcept
	{
		if (String == nullptr) return { "(null)", 6 };

		const void* end = std::memchr(String, 0, g_maxStringArgumentLength);

		std::size_t len = end
			? static_cast<std::size_t>(static_cast<const char*>(end) - String)
			: g_maxStringArgumentLength;			// truncate long strings

		return { String, len };
	}




	// ==========================================================================================================
	// Log Format Operations
	// ==========================================================================================================

	struct LogFormatOperation
	{
		LogFormatOpType Type = LogFormatOpType::FormatLiteral;
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
		for (std::size_t i = 0; i < Format.size();)
		{
			if (Format.Value[i] != '%') { ++i; continue; }
			if (i + 1 > Format.size()) throw "Invalid log format";
			if (Format.Value[i + 1] != '%') ++count;		// % not included in argument count

			i += 2;
		}

		return count;
	}




	// ==========================================================================================================
	// Get To Chars Buffer Size
	// ==========================================================================================================

	template<typename T>
	constexpr std::size_t GetMaxFormattedSize()
	{
		using Type = std::remove_cvref_t<T>;

		if constexpr (std::same_as<Type, bool>)
		{
			return 5;
		}
		else if constexpr (std::same_as<Type, char>)
		{
			return 1;
		}
		else if constexpr (std::integral<Type>)
		{
			return std::numeric_limits<Type>::digits10 + 3;
		}
		else if constexpr (std::same_as<Type, float>)
		{
			return 64;
		}
		else if constexpr (std::same_as<Type, double>)
		{
			return 128;
		}
		else if constexpr (std::is_pointer_v<Type>)
		{
			return 18;
		}
		else
		{
			static_assert(!sizeof(Type), "Unsupported numeric type");
		}
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
		std::size_t LogSize = 0;
		std::size_t StringArgumentCount = 0;
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
		constexpr std::array<std::size_t, sizeof...(Args)> argumentSizes{
					GetMaxFormattedSize<Args>()...
		};

		std::size_t offset = 0;

		std::size_t index = 0;

		while (index < Format.size())
		{
			if (Format.Value[index] != '%')
			{
				++metadata.LogSize;
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
					LogFormatOpType::FormatLiteral,
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

				const auto argumentType = arguments[metadata.ArgumentCount];

				LogFormatOpType type;

				if (specifier == 'd' && argumentType == LogArgumentType::SignedInteger)
				{
					type = LogFormatOpType::SignedInteger;
				}
				else if (specifier == 'u' && argumentType == LogArgumentType::UnsignedInteger)
				{
					type = LogFormatOpType::UnsignedInteger;
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
				else if (specifier == 'c' && argumentType == LogArgumentType::Character)
				{
					type = LogFormatOpType::Character;
				}
				else if (specifier == 's' && argumentType == LogArgumentType::StringLiteral)
				{
					type = LogFormatOpType::ArgumentLiteral;
				}
				else if (specifier == 'p' && argumentType == LogArgumentType::Pointer)
				{
					type = LogFormatOpType::Pointer;
				}
				else
				{
					throw "Invalid specifier";
				}


				// Account for the actual string length in runtime preflight
				if (argumentType != LogArgumentType::StringLiteral)
				{
					metadata.LogSize += argumentSizes[metadata.ArgumentCount];
				}
				else
				{
					++metadata.StringArgumentCount;
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

				++metadata.LogSize;
			}

			index += 2;
		}

		if (offset < Format.size())
		{
			metadata.Operations[metadata.OperationCount++] =
			{
				LogFormatOpType::FormatLiteral,
				offset,
				Format.size() - offset,
				0
			};

			metadata.LogSize += Format.size() - offset;
		}

		++metadata.LogSize; // Increment for line ending

		if (metadata.LogSize > g_maxLogFormatStringLength)
		{
			throw "Log format exceeds maximum estimated log string size";
		}

		if (metadata.ArgumentCount != sizeof...(Args))
		{
			throw "Invalid log format: Argument count mismatch";
		}

		if (metadata.ArgumentCount > g_maxLogArgumentCount)
		{
			throw "Invalid log format: Argument count exceeds the limit";
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
				const std::size_t InArgumentCount,
				const std::size_t InLogSize,
				const std::size_t InStringArgumentCount) :
			ThreadID(std::this_thread::get_id()),
			Sequence(InSequenceNumber),
			Category(InCategory),
			Level(InLevel),
			LogFormatString(InLogStr),
			OperationCount(InOperationCount),
			ArgumentCount(InArgumentCount),
			LogSize(InLogSize),
			StringArgumentCount(InStringArgumentCount),
			Time(NX_Clock::now())
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
				const std::size_t InLogSize,
				const std::size_t InStringArgumentCount,
				Args&&... InArguments)	:
			Sequence(InSequenceNumber),
			Category(InCategory),
			Level(InLevel),
			LogFormatString(InLogStr),
			LogOperations(InLogFormatOperations),
			OperationCount(InOperationCount),
			ArgumentCount(InArgumentCount),
			LogSize(InLogSize),
			StringArgumentCount(InStringArgumentCount),
			ThreadID(std::this_thread::get_id()),
			Time(NX_Clock::now())
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
			else if constexpr (std::is_same_v<Type, std::string>)
			{
				destination.ArgType = LogArgumentType::StringLiteral;
				destination.ArgValue.StringLiteral = InArgument;
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
		std::array<LogArgument, g_maxLogArgumentCount> LogArguments{};

		std::size_t OperationCount = 0;
		std::size_t ArgumentCount = 0;
		std::size_t LogSize = 0;
		std::size_t StringArgumentCount = 0;

		NX_TimePoint Time = NX_Clock::now();
	};




	// ==========================================================================================================
	// Log Arena
	// ==========================================================================================================

	class LogArena
	{
	public:

		LogArena() : m_data(std::make_unique_for_overwrite<char[]>(g_logArenaCapacity))
		{
			// One time pre-fault so the first fill doesn't pay page faults on the hot path.
			std::memset(m_data.get(), 0, g_logArenaCapacity);
		}

		LogArena(const LogArena&) = delete;
		LogArena& operator=(LogArena&) = delete;
		LogArena(LogArena&&) noexcept = default;
		LogArena& operator=(LogArena&&) noexcept= default;

		// State ---------------------------------------------------------------------------------
		[[nodiscard]] inline std::size_t GetSize() const noexcept { return m_cursor; }
		[[nodiscard]] inline std::size_t GetCapacity() const noexcept{ return g_logArenaCapacity; }
		[[nodiscard]] inline std::size_t GetRemaining() const noexcept { return g_logArenaCapacity - m_cursor; }
		[[nodiscard]] inline bool IsEmpty() const noexcept { return m_cursor == 0; }

		// Overflow-safe: compares against the remainder, never adds to the cursor.
		[[nodiscard]] bool HasCapacityFor(uint64_t Length) const noexcept
		{
			return GetRemaining() >= Length;
		}

		// The contiguous block of formatted data
		[[nodiscard]] std::span<const char> GetData() const noexcept { return { m_data.get(), m_cursor }; }

		void Clear() noexcept { m_cursor = 0; }

		uint64_t GetMark() const noexcept { return m_cursor; }

		// Truncate to Mark
		void Truncate(uint64_t Mark)
		{
			assert(Mark <= m_cursor);

			m_cursor = Mark;
		}

		// Appends ---------------------------------------------------------------------------------

		// Append after check
		bool AppendToArena_Literal(const char* InData, std::size_t InLength);

		// Append without a check, caller ensures to preflight capacity
		void AppendUncheked(const char* Data, std::size_t  Length) noexcept;
		void AppendCharUncheked(char Character) noexcept;


		// Formats into the arena without any scratch buffer or a second copy.
		// to_chars is parsed with the end of the arena, so it is inherently bounds safe:
		// a value that doesn't fit returns false and writes nothing we keep.
		template<typename Number>
			requires (
		(std::integral<std::remove_cvref_t<Number>> &&
			!std::same_as<std::remove_cvref_t<Number>, bool>) ||
			std::floating_point<std::remove_cvref_t<Number>>)
			bool AppendToArena_Numeric(
				const Number& InNumber,
				const int Base = 10,
				const std::chars_format Format = std::chars_format::general)
		{
			using Type = std::remove_cvref_t<Number>;

			static_assert(GetMaxFormattedSize<Type>() <= g_logArenaCapacity);

			char* start = m_data.get() + m_cursor;
			char* end = m_data.get() + g_logArenaCapacity;

			std::to_chars_result result{};

			if constexpr (std::integral<Type>)
			{
				assert(Base >= 2 && Base <= 36);
				if (Base < 2 || Base > 36) return false;

				result = std::to_chars(start,
					end,
					InNumber,
					Base);
			}
			else if constexpr (std::floating_point<Type>)
			{
				result = std::to_chars(start,
					end,
					InNumber,
					Format);
			}

			if (result.ec != std::errc{})
			{
				__debugbreak();
				return false;
			}

			m_cursor += static_cast<std::size_t>(result.ptr - start);
			return true;
		}


	private:

		std::unique_ptr<char[]> m_data;
		std::size_t m_cursor = 0;
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
			std::size_t index = InvalidIndex;

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
	// Ordered Log Window
	// ==========================================================================================================

	struct OrderedLogWindow
	{
	public:

		OrderedLogWindow() = default;

		// Public Member -------------------------------------------------------------------------------------------------------------
		static constexpr std::size_t Capacity = g_OrderedLogCapacity;


		// Consume Pass Result -------------------------------------------------------------------------------------------------------

		struct ConsumePassResult
		{
			LogPublishState State = LogPublishState::UNINITIALIZED;
			uint64_t PublishedCount = 0;		// Entries formatted and published this pass
			bool bGapActive = false;			// Head is missing, later sequences exist
			bool bQueuesEmpty = false;			// All thread context queues are empty
		};


		// Public Functions ----------------------------------------------------------------------------------------------------------

		void Initialize(LoggerThreadRegistry* InLoggerState);

		uint64_t GetSkippedCount();
		uint64_t GetInvertedCount();
		NX_TimePoint GetGapSince();
		std::chrono::microseconds GetGapTimeout();
		LogArena& GetLogArena();
		LogFormatState FormatLogDataToArena(LogData* Data);


		// ---------------------------------------------------------------------------------------------------------------------------
		// Template Functions --------------------------------------------------------------------------------------------------------
		// ---------------------------------------------------------------------------------------------------------------------------

		// Execute Log Format Operations ---------------------------------------------------------------------------------------------

		LogFormatState ExecuteLogFormatOperations(
			const char* FormatString,
			LogArgument* InArguments,
			const LogFormatOperation* FormatOperations,
			std::size_t OperationCount,
			std::size_t StaticSize,
			std::size_t StringArgumentCount)
		{
			// Preflight: one strlen per string argument, lengths kept for the write pass

			std::array<StringRef, g_maxLogArgumentCount> strings;
			std::size_t need = StaticSize;

			if (StringArgumentCount > 0)
			{
				std::size_t k = 0;

				for (std::size_t i = 0; i < OperationCount; ++i)
				{
					if (FormatOperations[i].Type != LogFormatOpType::ArgumentLiteral) continue;

					StringRef ref = MakeStringRef(InArguments[FormatOperations[i].ArgumentIndex].ArgValue.StringLiteral);
					need += ref.Length;
					++k;
				}
			}

			if (!m_logArena.HasCapacityFor(need)) return LogFormatState::FAILURE_CAPACITY;

			const size_t mark = m_logArena.GetMark();
			std::size_t stringIdx = 0;
			bool result = true;

			for (std::size_t OperationIndex = 0; OperationIndex < OperationCount; ++OperationIndex)
			{
				const auto& op = FormatOperations[OperationIndex];

				switch (op.Type)
				{
				case LogFormatOpType::FormatLiteral:
				{
					m_logArena.AppendUncheked(FormatString + op.Offset, op.Length);
					break;
				}

				case LogFormatOpType::SignedInteger :
				{
					result = m_logArena.AppendToArena_Numeric(InArguments[op.ArgumentIndex].ArgValue.SignedInteger);
					break;
				}

				case LogFormatOpType::UnsignedInteger :
				{
					result = m_logArena.AppendToArena_Numeric(InArguments[op.ArgumentIndex].ArgValue.UnsignedInteger);
					break;
				}

				case LogFormatOpType::Float :
				{
					result = m_logArena.AppendToArena_Numeric(InArguments[op.ArgumentIndex].ArgValue.FloatValue);
					break;
				}

				case LogFormatOpType::Double :
				{
					result = m_logArena.AppendToArena_Numeric(InArguments[op.ArgumentIndex].ArgValue.DoubleValue);
					break;
				}

				case LogFormatOpType::Boolean :
				{
					const bool value = InArguments[op.ArgumentIndex].ArgValue.BooleanValue;

					m_logArena.AppendUncheked(
						value ? "true" : "false",
						value ? 4 : 5);
					break;
				}

				case LogFormatOpType::Character :
				{
					const auto& character = InArguments[op.ArgumentIndex].ArgValue.Character;

					result = m_logArena.AppendToArena_Literal(&character, 1);
					break;
				}

				case LogFormatOpType::ArgumentLiteral :
				{
					const auto& argument = InArguments[op.ArgumentIndex];
					const StringRef stringRef = MakeStringRef(argument.ArgValue.StringLiteral);

					m_logArena.AppendUncheked(stringRef.Ptr, stringRef.Length);

					break;
				}

				case LogFormatOpType::Pointer :
				{
					auto& argument = InArguments[op.ArgumentIndex];

					if (argument.ArgValue.Pointer == nullptr)
					{
						m_logArena.AppendUncheked("nullptr", 7);
					}
					else
					{
						const auto pointer = reinterpret_cast<std::uintptr_t>(
							argument.ArgValue.Pointer
							);

						m_logArena.AppendUncheked("0x", 2);
						result = m_logArena.AppendToArena_Numeric(pointer, 16);
					}

					break;
				}

				case LogFormatOpType::EscapedPercent :
				{
					m_logArena.AppendUncheked("%", 1);
					break;
				}

				default :
				{
					result = false;
					break;
				}
				}
			}

			if (!result)
			{
				m_logArena.Truncate(mark);
				return LogFormatState::INVALID_INTERNAL_STATE;
			}


			return LogFormatState::SUCCESS;
		}




		// Flush Arena ---------------------------------------------------------------------------------------------------------------

		template<typename Publisher>
		bool FlushArena(Publisher&& Publish)
		{
			if (m_logArena.IsEmpty()) return true;
			if (Publish(m_logArena) != LogPublishState::SUCCESS) return false;

			m_logArena.Clear();
			return true;
		}


		// FormatOrFlush -------------------------------------------------------------------------------------------------------------

		// For entries formatted outside the ordered drain (late/stale entries).
		template<typename Publisher>
		bool FormatOrFlush(LogData& Data, Publisher&& Publish)
		{
			LogFormatState state = FormatLogDataToArena(&Data);
			if (state == LogFormatState::FAILURE_CAPACITY)
			{
				if (!FlushArena(Publish)) return false;
				state = FormatLogDataToArena(&Data);
			}

			return state == LogFormatState::SUCCESS;
		}


		// Ingest Queues ------------------------------------------------------------------------------------------------------------

		template<typename Publisher>
		std::size_t IngestQueues(Publisher&& Publish, bool& bOutAllEmtpty, bool& bOutFailed)
		{
			std::size_t ingested = 0;
			bOutAllEmtpty = true;
			bOutFailed = false;

			for (auto& context : m_loggerThreadRegistryPtr->m_threadContexts)
			{
				auto& queue = context.ThreadQueue;

				while (!queue.IsEmpty())
				{
					bOutAllEmtpty = false;
					LogData& data = queue.PeekLogical(0);
					const uint64_t seq = data.Sequence;

					if (seq == InvalidSequenceNumber) { queue.Dequeue(); continue; }

					SequenceStatus status = m_buffer.Insert(seq, std::move(data));

					if (status == SequenceStatus::BeyondCapacity)
					{
						// Exceeds the ordered ring capacity,
						// leave the rest of the sequences to be dealt with in the next pass
						break;
					}

					switch (status)
					{
					case SequenceStatus::Accepted:
					{
						if (m_maxSeenSequence == InvalidSequenceNumber ||
							seq > m_maxSeenSequence)
						{
							m_maxSeenSequence = seq;
						}

						++ingested;
						break;
					}

					case SequenceStatus::Duplicate:
					{
						++m_duplicantCount;
						break;
					}

					case SequenceStatus::Stale:
					{
						// Directly publish using the dedicated by-pass function
						if (!FormatOrFlush(data, Publish)) { bOutFailed = true; return ingested; };
						if (m_skippedLogCount > 0) --m_skippedLogCount;
						++m_invertedCount;
						break;
					}

					default:
					{
						throw "Invalid sequence status";
					}
					}

					// Pay load already lives in the ordered ring
					queue.Dequeue();
				}
			}

			return ingested;
		}


		// Consume Pass -------------------------------------------------------------------------------------------------------------

		template<typename Publisher>
		requires (std::invocable<Publisher, LogArena&> &&
				std::same_as<std::invoke_result_t<Publisher, LogArena&>, LogPublishState>)
		ConsumePassResult ConsumePass(Publisher&& Publish)
		{
			bool bAllEmpty = true;
			bool bFailed = false;

			IngestQueues(Publish, bAllEmpty, bFailed);
			if (bFailed) return { LogPublishState::INVALID_INTERNAL_STATE, 0, false, false };

			// Gap handling: head missing but later sequences exist
			const bool bGap = m_buffer.GetReadyCount() == 0 &&
				m_maxSeenSequence != InvalidSequenceNumber &&
				m_maxSeenSequence > m_buffer.GetHeadSequence();

			if (bGap)
			{
				auto timeNow = NX_Clock::now();

				if (!m_bGapActive)
				{
					m_bGapActive = true;
					m_gapSince = timeNow;		// caller : spin/yeild and call again
				}
				else if (timeNow - m_gapSince > g_gapTimeout)
				{
					while (m_buffer.GetReadyCount() == 0 &&
						m_maxSeenSequence <= m_buffer.GetHeadSequence())
					{
						m_buffer.SkipMissingHead();
						++m_skippedLogCount;
					}

					m_bGapActive = false;
				}
			}
			else
			{
				m_bGapActive = false;
			}


			// Drain contiguous logs
			// intermitently flush whenever the arena is full
			bool bArenaFull = false;
			bool bInternalError = false;

			while (m_buffer.GetReadyCount() > 0)
			{
				bArenaFull = false;

				m_buffer.DrainWhile([&](uint64_t, LogData& Data)
					{
						const LogFormatState formatState = FormatLogDataToArena(&Data);

						if (formatState == LogFormatState::SUCCESS) return true;
						if (formatState == LogFormatState::FAILURE_CAPACITY) bArenaFull = true;
						else bInternalError = true;

						return false;
					});

				if (bInternalError)
				{
					return { LogPublishState::INVALID_INTERNAL_STATE,
						m_lastPublishedSequence - m_buffer.GetHeadSequence(),
						false, false };
				}

				if (bArenaFull)
				{
					if (m_logArena.IsEmpty() || !FlushArena(Publish))
					{
						return { LogPublishState::INVALID_INTERNAL_STATE,
							m_lastPublishedSequence - m_buffer.GetHeadSequence(),
							false, false };
					}
				}
			}

			if (!FlushArena(Publish))
			{
				return { LogPublishState::INVALID_INTERNAL_STATE,
						m_lastPublishedSequence - m_buffer.GetHeadSequence(),
						false, false };
			}

			m_lastPublishedSequence = m_buffer.GetHeadSequence() - 1;

			LogPublishState state = bGap ? LogPublishState::SUCCESS_WITH_MISSING : LogPublishState::SUCCESS;

			return { state,
				m_lastPublishedSequence - m_buffer.GetHeadSequence(),
				false, false };
		}


	private:

		// =====================================
		// Member Parameters
		// =====================================

		LoggerThreadRegistry* m_loggerThreadRegistryPtr;
		ReorderBuffer<LogData, g_OrderedLogCapacity> m_buffer;
		LogArena m_logArena;

		uint64_t m_maxSeenSequence = InvalidSequenceNumber;
		uint64_t m_lastPublishedSequence = InvalidSequenceNumber;
		uint64_t m_duplicantCount = 0;
		uint64_t m_skippedLogCount = 0;
		uint64_t m_invertedCount = 0;

		bool m_bGapActive = false;
		NX_TimePoint m_gapSince;

		static constexpr auto g_gapTimeout = std::chrono::microseconds(200);
	};
}