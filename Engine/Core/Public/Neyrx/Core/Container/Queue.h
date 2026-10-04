#pragma once
#include <array>
#include <cstddef>
#include <cassert>
#include <utility>
#include <atomic>
#include <iostream> // TODO - remove (TEMP)

namespace Neyrx
{
	// Standard Ringbuffer
	template<typename T, std::size_t Capacity>
	struct RingBuffer
	{
		// Returns the Front Logical Index
		std::size_t FrontIndex() const
		{
			return m_head;
		}

		// Returns the Back Logical Index
		std::size_t BackIndex() const
		{
			return (m_tail + Capacity - 1) % Capacity;
		}

		// Returns the Object by reference at the Physical Index of the Buffer
		T& Get(std::size_t Index)
		{
			assert(Index < Capacity); // TODO - Error Handling
			return m_data[Index];
		}

		// Returns the Object by const reference at the Physical Index of the Buffer
		const T& Get(std::size_t Index) const
		{
			assert(Index < Capacity); // TODO - Error Handling
			return m_data[Index];
		}

		// Returns the Object by reference at the Front Logical Index of the Buffer
		T& Front()
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[m_head];
		}

		// Returns the Object by const reference at the Front Logical Index of the Buffer
		const T& Front() const
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[m_head];
		}

		// Returns the Object by reference at the Back Logical Index of the Buffer
		T& Back()
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[BackIndex()];
		}

		// Returns the Object by const reference at the Back Logical Index of the Buffer
		const T& Back() const
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[BackIndex()];
		}

		// Returns true if the Queue is Full, false otherwise
		bool IsFull() const
		{
			return (m_tail + 1) % Capacity == m_head;
		}

		// Returns true if the Queue is Empty, false otherwise
		bool IsEmpty() const
		{
			return m_tail == m_head;
		}

		// Copies an LValue in the Queue
		void Enqueue(const T& InObject)
		{
			if (IsFull()) return;

			m_data[m_tail] = InObject;

			m_tail = (m_tail + 1) % Capacity;
		}

		// Moves an RValue in to the Queue
		void Enqueue(T&& InObject)
		{
			if (IsFull()) return;

			m_data[m_tail] = std::move(InObject);

			m_tail = (m_tail + 1) % Capacity;
		}

		// Increments the Head and frees the index at the Front of the Queue
		// Returns true if the object was successfully dequeued, false if the queue is empty
		bool Dequeue()
		{
			if (IsEmpty()) return false;

			m_head = (m_head + 1) % Capacity;

			return true;
		}

	private:

		std::size_t m_head = 0;				// index of the front element
		std::size_t m_tail = 0;				// index of the next insertion
		std::array<T, Capacity> m_data;		// Stored Array of Objects
	};




	// Sinlge Producer Single Consumer Ring Buffer
	// uses atomic operations to ensure thread safety for a single producer and a single consumer
	// avoids using locks and mutexes, which can be expensive in terms of performance
	template<typename T, std::size_t Capacity>
		requires(Capacity >= 2)
	struct SPSCRingBuffer
	{
	public:

		// Returns true if the Queue is Full, false otherwise
		bool IsFull() const
		{
			return (m_tail.load(std::memory_order_acquire) + 1) % Capacity == m_head.load(std::memory_order_acquire);
		}


		// Returns true if the Queue is Empty, false otherwise
		bool IsEmpty() const
		{
			return m_tail.load(std::memory_order_acquire) == m_head.load(std::memory_order_acquire);
		}

		// Copies an LValue in the Queue
		// Returns true if the object was successfully enqueued, false if the queue is full
		bool  Enqueue(const T& Object)
		{
			std::size_t tail = m_tail.load(std::memory_order_relaxed);
			std::size_t next = (tail + 1) % Capacity;

			if (next == m_head.load(std::memory_order_acquire)) return false;

			m_data[tail] = Object;
			m_tail.store(next, std::memory_order_release);

			return true;
		}

		// Moves an RValue in to the Queue
		// Returns true if the object was successfully enqueued, false if the queue is full
		bool Enqueue(T&& InObject)
		{
			std::size_t tail = m_tail.load(std::memory_order_relaxed);
			std::size_t next = (tail + 1) % Capacity;

			if (next == m_head.load(std::memory_order_acquire)) return false;

			m_data[tail] = std::move(InObject);
			m_tail.store(next, std::memory_order_release);

			return true;
		}

		// Moves the front object into the OutObject and increments the Head to release the index at the Front of the Queue
		// Returns true if the object was successfully dequeued, false if the queue is empty
		bool Dequeue(T& OutObject)
		{
			std::size_t head = m_head.load(std::memory_order_relaxed);

			if (head == m_tail.load(std::memory_order_acquire))
			{
				//std::cout << "\nQueue is empty, cannot dequeue object.\n";
				return false; // TODO - TEMP
			}

			OutObject = std::move(m_data[head]);

			auto next = (head + 1) % Capacity;
			m_head.store(next, std::memory_order_release);

			return true;
		}

	private:

		// Member Variables
		std::atomic<std::size_t> m_head{ 0 };			// index of the front element
		std::atomic<std::size_t> m_tail{ 0 };			// index of the next insertion
		std::array<T, Capacity> m_data;					// Stored Array of Objects
	};
}