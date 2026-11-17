#include <cassert>
#include <intrin.h>

#include "Neyrx/Core/Logger/LogDataStructures.h"


using namespace Neyrx::Logger;



// ==========================================================================================================
// Log Arena
// ==========================================================================================================

bool LogArena::AppendToArena_Literal(const char* Data, std::size_t Length)
{
	if (!HasCapacityFor(Length)) return false;

	std::memcpy(m_data.get() + m_cursor, Data, Length);
	m_cursor += Length;
	return true;
}



void LogArena::AppendUncheked(const char* Data, std::size_t  Length) noexcept
{
	assert(HasCapacityFor(Length));

	std::memcpy(m_data.get() + m_cursor, Data, Length);
	m_cursor += Length;
}




void LogArena::AppendCharUncheked(char Character) noexcept
{
	assert(HasCapacityFor(1));
	m_data[m_cursor++] = Character;
}




// ==========================================================================================================
// Ordered Log Window
// ==========================================================================================================

void OrderedLogWindow::Initialize(LoggerThreadRegistry* InLoggerState)
{
	m_loggerThreadRegistryPtr = InLoggerState;
}




uint64_t OrderedLogWindow::GetSkippedCount()
{
	return m_skippedLogCount;
}




uint64_t OrderedLogWindow::GetInvertedCount()
{
	return m_invertedCount;
}




NX_TimePoint OrderedLogWindow::GetGapSince()
{
	return m_gapSince;
}




std::chrono::microseconds OrderedLogWindow::GetGapTimeout()
{
	return g_gapTimeout;
}




LogArena& OrderedLogWindow::GetLogArena()
{
	return m_logArena;
}




LogFormatState OrderedLogWindow::FormatLogDataToArena(LogData* Data)
{
	std::string_view categoryStringView = ToString(Data->Category);
	const std::size_t categorySize = categoryStringView.length();

	std::string_view levelStringView = ToString(Data->Level);
	const std::size_t levelSize = levelStringView.length();

	const double logTimestamp = GetDurationMilliseconds(Data->Time);
	constexpr std::size_t timeStampSize = GetMaxFormattedSize<double>();

	// "[ " + " ] [ " + " ] [ " + "] "
	// Need not count line ending, LogSize already accounts for that
	constexpr std::size_t headerLiteralSize = 14;

	const std::size_t requiredSize =
		headerLiteralSize +
		categorySize + levelSize +
		timeStampSize +
		Data->LogSize;

	if (!m_logArena.AppendToArena_Literal("[ ", 2)) return LogFormatState::INVALID_INTERNAL_STATE;
	if (!m_logArena.AppendToArena_Literal(categoryStringView.data(), categorySize)) return LogFormatState::INVALID_INTERNAL_STATE;
	if (!m_logArena.AppendToArena_Literal(" ] [ ", 5)) return LogFormatState::INVALID_INTERNAL_STATE;
	if (!m_logArena.AppendToArena_Literal(levelStringView.data(), levelSize)) return LogFormatState::INVALID_INTERNAL_STATE;
	if (!m_logArena.AppendToArena_Literal(" ] [ ", 5)) return LogFormatState::INVALID_INTERNAL_STATE;
	if (!m_logArena.AppendToArena_Numeric(logTimestamp)) return LogFormatState::INVALID_INTERNAL_STATE;
	if (!m_logArena.AppendToArena_Literal("] ", 2)) return LogFormatState::INVALID_INTERNAL_STATE;

	LogFormatState state = ExecuteLogFormatOperations(Data->LogFormatString,
		Data->LogArguments.data(),
		Data->LogOperations,
		Data->OperationCount,
		requiredSize, Data->StringArgumentCount);

	if (state != LogFormatState::SUCCESS) return state;
	if (!m_logArena.AppendToArena_Literal("\n", 1)) return LogFormatState::INVALID_INTERNAL_STATE;

	return LogFormatState::SUCCESS;
}