#pragma once
#include <cstdio>
#include <cassert>

#include "Neyrx/Core/Logger/LogDataStructures.h"

namespace Neyrx::Logger
{
	// ==========================================================================================================
	// Logger Sink
	// ==========================================================================================================

	class ILoggerSink
	{
	public:

		virtual ~ILoggerSink() = default;

		virtual LogPublishState Write(LogArena& Arena) = 0;
		virtual void FlushOS() = 0;
	};




	// ==========================================================================================================
	// Console Sink
	// ==========================================================================================================

	class ConsoleSink : public ILoggerSink
	{
	public:

		virtual ~ConsoleSink() = default;

		virtual LogPublishState Write(LogArena& Arena) override;
	};




	// ==========================================================================================================
	// File Sink
	// ==========================================================================================================

	class FileSink : public ILoggerSink
	{
	public:

		FileSink(const char* FilePath);
		virtual ~FileSink() override;

		virtual LogPublishState Write(LogArena& Arena) override;
		virtual void FlushOS() override;

	private:

		std::FILE* m_fileHandle = nullptr;
	};
}