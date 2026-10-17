#include "Neyrx/Core/Container/Queue.h"
#include "Neyrx/Core/Logger/Logger.h"
#include <conio.h>


static std::atomic<bool> StopLog = false;

class TestSink : public Neyrx::Logger::ILoggerSink
{
public:
    virtual void Consume(const Neyrx::Logger::LogData InLogData) override
    {
		uint64_t sequenceNumber = InLogData.m_sequence;

		if ((sequenceNumber < m_lastSequenceNumber.load(std::memory_order_relaxed)) && sequenceNumber != 0)
        {
            std::cout << "Error: Sequence number is not in order. Last: " << m_lastSequenceNumber << ", Current: " << sequenceNumber << "\n";
			bFail = true;
			m_inversionCount.fetch_add(1, std::memory_order_release);
			return;
		}
		else
		{
			std::cout << "Sequence number is in order. Last: " << m_lastSequenceNumber << ", Current: " << sequenceNumber << "\n";

			m_lastSequenceNumber.store(sequenceNumber, std::memory_order_relaxed);
			m_messageCount.fetch_add(1, std::memory_order_relaxed);
        }
	}

	const bool IsFailed() const
	{
		return bFail.load(std::memory_order_relaxed);
	}

	uint64_t GetMessageCount() const
	{
		return m_messageCount.load(std::memory_order_relaxed);
	}

	uint64_t GetMessageCount()
	{
		return m_messageCount.load(std::memory_order_relaxed);
	}

	int GetInversionCount()
	{
		return m_inversionCount.load(std::memory_order_relaxed);
	}

	int GetSkippedCount()
	{
		return NX_Logger.GetSkippedCount();
	}

private:
    std::atomic<uint64_t> m_lastSequenceNumber = 0;
	std::atomic<uint64_t> m_messageCount = 0;
    std::atomic<bool> bFail = false;
	std::atomic<int> m_inversionCount = 0;
};

struct MsgCount
{
	MsgCount() : count(0) {}

	int count = 0;
};

inline static thread_local MsgCount g_msgCount;



void InputLog(int& count)
{
	std::ostringstream oss;
	oss << "Hello from Thread ID: " << std::this_thread::get_id() << " | Msg Id: " << count;

	NX_LOG(Neyrx::Logger::LogCategory::Temp, Neyrx::Logger::LogLevel::Info, oss.str());

	count++;
}

void Logger()
{
	std::this_thread::sleep_for(2ms);
	while (!StopLog)
	{
		NX_Logger.ConsumeMessage();
	}
}

void Worker()
{
	NX_Logger.RegisterLogQueue();
	while (!StopLog)
	{
		InputLog(g_msgCount.count);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

int main()
{
	std::cout << "Starting Logger Test...\n";

	NX_Logger.Initialize();
	TestSink testSink;
	NX_Logger.AddSink(&testSink);


	std::thread thread1(Worker);
	std::thread thread2(Worker);
	std::thread thread3(Worker);
	std::thread thread4(Worker);


	std::thread thread5(Logger);

	while (!StopLog)
	{
		int stop = 0;
		std::cin >> stop;

		if (stop == 1)
		{
			StopLog.store(true, std::memory_order_release);
		}
	}

	StopLog = true;

	thread1.join();
	thread2.join();
	thread3.join();
	thread4.join();
	thread5.join();

	if (testSink.IsFailed())
	{
		std::cout << "\nLogger Test Failed.\n";
		std::cout << "\nInversion Count" << testSink.GetInversionCount() << std::endl;
		std::cout << "\nSkipped Count" << testSink.GetSkippedCount() << std::endl;

		return -1;
	}
	else
	{
		std::cout << "Logger Test Passed.\n";
		std::cout << "\nInversion Count" << testSink.GetInversionCount() << std::endl;
		std::cout << "\nSkipped Count" << testSink.GetSkippedCount() << std::endl;
	}

	std::cin;

	return 0;
}