#include <conio.h>
#include <filesystem>

#include "Neyrx/Core/Logger/Logger.h"


using namespace Neyrx::Logger;


static std::atomic<bool> StopProducer = false;
static std::atomic<bool> StopConsumer = false;



void InputLog()
{
	NX_LOG(NX_LOG_TEMP, NX_LOG_INFO, "Hello from thread ID: %d", 1);
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
		for (int i = 0; i < 2048; ++i)
		{
			InputLog();
		}

		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
}

int main()
{
	Neyrx::Time::Initialize();

	std::cout << "Starting Logger Test...\n";

	NX_Logger.Initialize();

	std::filesystem::path root = PROJECT_ROOT_DIR;
	std::string filestr = root.string().c_str();
	filestr.append("/Log.txt");
	const char* file = filestr.c_str();
	FileSink testSink(file);
	NX_Logger.AddSink(&testSink);

	std::thread thread1(Worker);
	std::thread thread2(Worker);
	std::thread thread3(Worker);
	std::thread thread4(Worker);
	std::thread thread5(Worker);
	std::thread thread6(Worker);
	/*std::thread thread7(Worker);
	std::thread thread8(Worker);*/

	std::thread thread20(LogWorker);

	std::this_thread::sleep_for(std::chrono::seconds(300));

	StopProducer = true;

	thread1.join();
	thread2.join();
	thread3.join();
	thread4.join();
	thread5.join();
	thread6.join();
	/*thread7.join();
	thread8.join();*/

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
	uint64_t MsgCount = NX_Logger.GetSequenceCount();

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