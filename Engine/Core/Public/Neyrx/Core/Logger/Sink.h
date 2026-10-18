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

		virtual void Publish(const LogData& InLogData) = 0;
	};




	// ==========================================================================================================
	// Console Sink
	// ==========================================================================================================

	class IConsoleSink : public ILoggerSink
	{
	public:

		virtual ~IConsoleSink() = default;

		inline virtual void Publish(const LogData& InLogData) override
		{
			InLogData.PrintData();
		}
	};
}