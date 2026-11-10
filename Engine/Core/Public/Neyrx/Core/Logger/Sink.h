#pragma once
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

		virtual LogPublishState Publish(LogArena& Arena) = 0;
	};




	// ==========================================================================================================
	// Console Sink
	// ==========================================================================================================

	class IConsoleSink : public ILoggerSink
	{
	public:

		virtual ~IConsoleSink() = default;

		inline virtual LogPublishState Publish(LogArena& Arena) override
		{
			return LogPublishState::SUCCESS;
		}
	};
}