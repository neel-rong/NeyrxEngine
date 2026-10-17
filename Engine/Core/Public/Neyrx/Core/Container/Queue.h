#pragma once
#include <array>
#include <cstddef>
#include <cassert>
#include <utility>
#include <atomic>
#include <iostream> // TODO - remove (TEMP)

namespace Neyrx
{
	// ==========================================================
	// RingBuffer Iterator
	// ==========================================================
	template <typename RingBufferType>
	class RingBufferIterator
	{
	public:

		using ValueType		= typename RingBufferType::ValueType;
		using PointerType	= ValueType*;
		using ReferenceType = ValueType&;


	public:

		RingBufferIterator(PointerType Pointer, std::size_t Head, std::size_t Index, std::size_t Capacity) :
			m_ptr(Pointer),
			m_head(Head),
			m_index(Index),
			m_capacity(Capacity) { }

		RingBufferIterator& operator++()
		{
			++m_index;

			return *this;
		}

		RingBufferIterator operator++(int)
		{
			RingBufferIterator iterator = *this;
			++(*this);

			return iterator;

		}

		RingBufferIterator& operator--()
		{
			assert(m_index > 0);
			--m_index;

			return *this;
		}

		RingBufferIterator operator--(int)
		{
			RingBufferIterator iterator = *this;
			--(*this);

			return iterator;

		}

		RingBufferIterator operator+(std::size_t Offset) const
		{
			RingBufferIterator iterator = *this;
			iterator.m_index += Offset;

			return iterator;
		}

		RingBufferIterator operator-(std::size_t Offset) const
		{
			assert(Offset <= m_index);

			RingBufferIterator iterator = *this;
			iterator.m_index -= Offset;

			return iterator;
		}

		RingBufferIterator& operator+=(std::size_t Offset)
		{
			m_index += Offset;

			return *this;
		}

		RingBufferIterator& operator-=(std::size_t Offset)
		{
			assert(Offset <= m_index);
			m_index -= Offset;

			return *this;
		}

		ReferenceType operator[](std::size_t Index) const
		{
			return m_ptr[(m_head + m_index + Index) % m_capacity];
		}

		PointerType operator->() const
		{
			return &m_ptr[(m_head + m_index) % m_capacity];
		}

		ReferenceType operator*() const
		{
			return m_ptr[(m_head + m_index) % m_capacity];
		}

		bool operator==(const RingBufferIterator& Other) const
		{
			return m_ptr == Other.m_ptr &&
					m_head == Other.m_head &&
					m_index == Other.m_index;
		}

		bool operator!=(const RingBufferIterator& Other) const
		{
			return !(*this == Other);
		}

		std::size_t GetIndex()
		{
			return m_index;
		}

	private:
		PointerType m_ptr = nullptr;	// Physical Storage
		std::size_t m_head = 0;			// Physical Location of Logical 0
		std::size_t m_index = 0;		// Logical Iterator Position
		std::size_t m_capacity = 0;		// Physical Capacity
	};



	// ==========================================================
	// Ringbuffer Iterator Snapshot
	// ==========================================================

	template <typename RingBufferType>
	struct RingBufferIteratorSnapshot
	{
		RingBufferIterator<RingBufferType> Begin;
		RingBufferIterator<RingBufferType> End;
	};




	// ==========================================================
	// Standard Ringbuffer
	// ==========================================================
	template<typename T, std::size_t Capacity>
	struct RingBuffer
	{
	public:
		using ValueType = T;
		using Iterator = RingBufferIterator<RingBuffer<T, Capacity>>;

	public:
		T& operator[](std::size_t Index)
		{
			assert(Index < GetSize());
			return m_data[(m_head + Index) % Capacity];
		}

		const T& operator[](std::size_t Index) const
		{
			assert(Index < GetSize());
			return m_data[(m_head + Index) % Capacity];
		}

		Iterator begin()
		{
			return Iterator(m_data.data(),
				m_head,
				0,
				Capacity);
		}

		Iterator end()
		{
			return Iterator(m_data.data(),
				m_head,
				GetSize(),
				Capacity);
		}


	public:
		// Returns the Front Physical Index
		std::size_t FrontIndex() const
		{
			return m_head;
		}

		// Returns the Back Physical Index
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

		// Returns the Object by reference at the Front Index of the Buffer
		T& Front()
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[m_head];
		}

		// Returns the Object by const reference at the Front Index of the Buffer
		const T& Front() const
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[m_head];
		}

		// Returns the Object by reference at the Back Index of the Buffer
		T& Back()
		{
			assert(!IsEmpty());	// TODO - Error Handling
			return m_data[BackIndex()];
		}

		// Returns the Object by const reference at the Back Index of the Buffer
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
		bool Enqueue(const T& InObject)
		{
			if (IsFull()) return false;

			m_data[m_tail] = InObject;

			m_tail = (m_tail + 1) % Capacity;

			return true;
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

			// TODO - Use Trivially destructible

			m_head = (m_head + 1) % Capacity;

			return true;
		}

		// Returns a const reference to the Logical Index of the Buffer
		T& Peek(std::size_t Index)
		{
			assert(Index > 0 && Index < GetSize());

			return m_data[LogicalIndexToPhysicalIndex(Index)];
		}

		const T& Peek(std::size_t Index) const
		{
			assert(Index > 0 && Index < GetSize());

			return m_data[LogicalIndexToPhysicalIndex(Index)];
		}

		std::size_t LogicalIndexToPhysicalIndex(std::size_t Index)
		{
			return (m_head + Index) % Capacity;
		}

		std::size_t GetSize() const
		{
			return (m_tail + Capacity - m_head) % Capacity;
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

		using ValueType = T;
		using Iterator = RingBufferIterator<SPSCRingBuffer<T, Capacity>>;
		using IteratorSnapshot = RingBufferIteratorSnapshot<SPSCRingBuffer<T, Capacity>>;

	public:

		IteratorSnapshot GetRingBufferIteratorSnapshot()
		{
			const std::size_t head = m_head.load(std::memory_order_relaxed);
			const std::size_t tail = m_tail.load(std::memory_order_acquire);
			const std::size_t size = (tail + Capacity - head) % Capacity;

			return {
				Iterator(m_data.data(), head, 0, Capacity),
				Iterator(m_data.data(), head, size, Capacity)
			};
		}
	public:

		// Returns the Front Physical Index
		std::size_t FrontIndex() const
		{
			return m_head.load(std::memory_order_relaxed);
		}

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

		// Moves the front object into the OutObject and increments the Head to release the index at the Front of the Queue
		// Returns true if the object was successfully dequeued, false if the queue is empty
		bool Dequeue()
		{
			std::size_t head = m_head.load(std::memory_order_relaxed);

			if (head == m_tail.load(std::memory_order_acquire))
			{
				//std::cout << "\nQueue is empty, cannot dequeue object.\n";
				return false; // TODO - TEMP
			}

			auto next = (head + 1) % Capacity;
			m_head.store(next, std::memory_order_release);

			return true;
		}

		// Takes the physical index and returns the data at the physcial adress
		T& Peek(std::size_t Index)
		{
			if (Index >= Capacity)
			{
				assert(Index < Capacity && "Invalid Queue Acess");
			}

			return m_data[Index];
		}

		// Takes the physical index and returns the data at the physcial adress
		const T& Peek(std::size_t Index) const
		{
			if (Index >= Capacity)
			{
				assert(Index < Capacity && "Invalid Queue Acess");
			}

			return m_data[Index];
		}

		// Takes the logical index and returns the data at the physcial adress
		T& PeekLogical(std::size_t Index)
		{
			assert(Index < GetSize());

			return m_data[LogicalIndexToPhysicalIndex(Index)];
		}

		const T& PeekLogical(std::size_t Index) const
		{
			assert(Index < GetSize());

			return m_data[LogicalIndexToPhysicalIndex(Index)];
		}

		std::size_t LogicalIndexToPhysicalIndex(std::size_t Index) const
		{
			return (m_head + Index) % Capacity;
		}

		std::size_t GetSize() const
		{
			return (m_tail.load(std::memory_order_acquire) + Capacity - m_head.load(std::memory_order_relaxed)) % Capacity;
		}

	private:

		// Member Variables
		std::atomic<std::size_t> m_head{ 0 };			// index of the front element
		std::atomic<std::size_t> m_tail{ 0 };			// index of the next insertion
		std::array<T, Capacity> m_data;					// Stored Array of Objects
	};
}