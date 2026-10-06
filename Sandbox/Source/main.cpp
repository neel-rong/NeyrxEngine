#include <iostream>
#include <thread>
#include "Neyrx/Core/Container/Queue.h"
#include "Neyrx/Core/Logger.h"
#include <sstream>
#include <semaphore>

//static std::atomic<bool> StopLog = false;
//
//struct MsgCount
//{
//	MsgCount() : count(0) {}
//
//	int count = 0;
//};
//
//inline static thread_local MsgCount g_msgCount;
//
//
//
//void InputLog(int& count)
//{
//	std::ostringstream oss;
//	oss << "Hello from Thread ID: " << std::this_thread::get_id() << " | Msg Id: " << count;
//
//	NX_LOG(Neyrx::Logger::LogCategory::Temp, Neyrx::Logger::LogLevel::Info, oss.str());
//
//	count++;
//}
//
//void Logger()
//{
//	while (!StopLog)
//	{
//		Neyrx::Logger::NX_Logger.ConsumeMessage();
//	}
//}
//
//void Worker()
//{
//	Neyrx::Logger::NX_Logger.RegisterLogQueue();
//	while(!StopLog)
//	{
//			InputLog(g_msgCount.count);
//			std::this_thread::sleep_for(std::chrono::milliseconds(2));
//	}
//}
//
//int main()
//{
//	std::thread thread1(Worker);
//	std::thread thread2(Worker);
//	std::thread thread3(Worker);
//	std::thread thread4(Worker);
//
//	std::thread thread5(Logger);
//
//	if (std::cin.get()) StopLog = true;
//
//	thread1.join();
//	thread2.join();
//	thread3.join();
//	thread4.join();
//	thread5.join();
//
//	return 0;
//}

using namespace std::chrono_literals;

int main()
{
	std::counting_semaphore<1> semaphore(0);

	std::thread worker([&]()
		{
			std::cout << "Worker waiting..." << std::endl;

			if (semaphore.try_acquire_for(500us))
			{
				std::cout << "Worker Relased..." << std::endl;
			}
			else
			{
				std::cout << "Acquie failed. Continue other work";
			}
		}
	);

	std::cout << "Main Thread Working" << std::endl;

	worker.join();

	return 0;
}