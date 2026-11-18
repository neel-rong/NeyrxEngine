#include "Neyrx/Core/Logger/Sink.h"




using namespace Neyrx::Logger;


// ==========================================================================================================
// Console Sink
// ==========================================================================================================

LogPublishState ConsoleSink::Write(LogArena& Arena)
{
	return LogPublishState::SUCCESS;
}




// ==========================================================================================================
// File Sink
// ==========================================================================================================

FileSink::FileSink(const char* FilePath)
{
	m_fileHandle = std::fopen(FilePath, "ab");

	assert(m_fileHandle != nullptr && "Logger: Failed to open log file");

	// Enlarge CRT write buffer to 64 KB for efficient NVMe sequential writes
	if (m_fileHandle)
	{
		std::setvbuf(m_fileHandle, nullptr, _IOFBF, 64 * 1024);
	}
}




FileSink::~FileSink()
{
	if (m_fileHandle)
	{
		std::fflush(m_fileHandle);
		std::fclose(m_fileHandle);
		m_fileHandle = nullptr;
	}
}




LogPublishState FileSink::Write(LogArena& Arena)
{
	const auto& data = Arena.GetData();

	if (m_fileHandle && !data.empty())
	{
		std::size_t sucessCount = std::fwrite(data.data(), 1, data.size(), m_fileHandle);

		if (sucessCount < 1) return LogPublishState::INVALID_INTERNAL_STATE;
	}

	return LogPublishState::SUCCESS;
}




void FileSink::FlushOS()
{
	if (m_fileHandle)
	{
		std::fflush(m_fileHandle);
	}
}