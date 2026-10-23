#include <conio.h>

#include "Neyrx/Core/Logger/Logger.h"


using namespace Neyrx::Logger;


static std::atomic<bool> StopProducer = false;
static std::atomic<bool> StopConsumer = false;

class TestSink : public ILoggerSink
{
public:

	virtual ~TestSink() = default;

    inline virtual void Publish(const Neyrx::Logger::LogData& InLogData) override
    {
		++m_messageCount;
	}

	uint64_t GetMessageCount() const
	{
		return m_messageCount;
	}

private:
    uint64_t m_lastSequenceNumber = InvalidSequenceNumber;
	uint64_t m_messageCount = 0;
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

void LogWorker()
{
	while (!StopConsumer)
	{
		NX_Logger.TryAndPublishLogData();
	}
}

void Worker()
{
	NX_Logger.RegisterLogQueue();
	while (!StopProducer)
	{
		for (int i = 0; i < 8192; ++i)
		{
			InputLog(g_msgCount.count);
		}
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
	std::thread thread5(Worker);
	std::thread thread6(Worker);
	std::thread thread7(Worker);
	std::thread thread8(Worker);
	std::thread thread9(Worker);
	std::thread thread10(Worker);


	std::thread thread20(LogWorker);

	/*int end = 0;
	while (end != 1)
	{
		std::cin >> end;
	}*/

	std::this_thread::sleep_for(std::chrono::seconds(15));

	StopProducer = true;

	thread1.join();
	thread2.join();
	thread3.join();
	thread4.join();
	thread5.join();
	thread6.join();
	thread7.join();
	thread8.join();
	thread9.join();
	thread10.join();

	std::cout << "__PRODUCER THREADS JOINED__" << std::endl;

	while (!NX_Logger.IsIdle())
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	StopConsumer = true;


	thread20.join();
	std::cout << "__Consumer THREADS JOINED__" << std::endl;

	uint64_t inversionCount = NX_Logger.GetInvertedCount();
	uint64_t skippedCount = NX_Logger.GetSkippedCount();
	uint64_t MsgCount = testSink.GetMessageCount();

	int result = -1;

	if (inversionCount == 0 && skippedCount == 0)
	{
		std::cout << "Logger Test Passed.\n";
		std::cout << "\nInversion Count" << inversionCount << std::endl;
		std::cout << "\nSkipped Count" << skippedCount << std::endl;
		std::cout << "\nMessage Count" << MsgCount << std::endl;

		result = 0;
	}
	else
	{
		std::cout << "\nLogger Test Failed.\n";
		std::cout << "\nInversion Count" << inversionCount << std::endl;
		std::cout << "\nSkipped Count" << skippedCount << std::endl;
		std::cout << "\nMessage Count" << MsgCount << std::endl;

		result = -1;
	}

	getch();

	return result;
}