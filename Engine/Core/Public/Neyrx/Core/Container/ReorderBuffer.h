#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <cassert>
#include <utility>
#include <type_traits>

#include "Neyrx/Core/Container/SequenceStatus.h"
#include "Neyrx/Core/Container/Bitmap.h"




namespace Neyrx::Container
{
	// ---- Release policies: what happens to a slot's payload when it is retired --------------------------
	// NoRelease: do nothing. Slots are constructed once with the buffer and live until it is destroyed.
	//            The bitmap alone decides whether a slot is valid. Consumers should move out of the slot.
	struct NoRelease
	{
		template<typename T>
		void operator()(T&) const noexcept {}
	};

	// ResetToDefault: assign T{} on retire (frees owned memory immediately, costs a write per slot).
	struct ResetToDefault
	{
		template<typename T>
		void operator()(T& Value) const
		{
			Value = T{};
		}
	};


	// ==========================================================================================================
	// Ordered Ring
	// ==========================================================================================================

	template<typename T, std::size_t Capacity = 4096, typename ReleasePolicy = NoRelease>
		requires (std::has_single_bit(Capacity) && Capacity >= 64 &&
			std::is_default_constructible_v<T> && std::is_move_assignable_v<T>)
	class ReorderBuffer
	{
	public:

		static constexpr std::size_t Mask = Capacity - 1;
		static constexpr bool bReleaseOnRetire = !std::is_same_v<ReleasePolicy, NoRelease>;

		ReorderBuffer() : m_slots(std::make_unique<T[]>(Capacity)) {}


		// O(WordCount) with NoRelease. No slot is destroyed or constructed.
		void Reset(uint64_t InStartSequence)
		{
			if constexpr (bReleaseOnRetire)
			{
				for (std::size_t i = 0; i < Capacity; ++i)
				{
					m_release(m_slots[i]);
				}
			}
			m_bitmap.Reset(InStartSequence);
		}


		SequenceStatus Insert(uint64_t Sequence, T&& Data)
		{
			SequenceStatus status = m_bitmap.MarkReceived(Sequence);

			if (!IsAccepted(status)) return status;

			m_slots[Sequence & Mask] = std::move(Data);
			return status;
		}


		// Returns nullptr if the sequence hasn't arrived (or is outside the window).
		[[nodiscard]] T* Get(uint64_t Sequence)
		{
			return m_bitmap.IsReceived(Sequence) ? &m_slots[Sequence & Mask] : nullptr;
		}


		// Returns nullptr if the sequence hasn't arrived (or is outside the window).
		[[nodiscard]] const T* Get(uint64_t Sequence) const
		{
			return m_bitmap.IsReceived(Sequence) ? &m_slots[Sequence & Mask] : nullptr;
		}


		[[nodiscard]] uint64_t GetReadyCount() const noexcept
		{
			return m_bitmap.GetContiguousReadyCount();
		}


		[[nodiscard]] uint64_t GetHeadSequence() const noexcept
		{
			return m_bitmap.GetHead();
		}


		// Advances the window past Count consumed, contiguous slots.
		void Retire(uint64_t Count)
		{
			assert(Count <= GetReadyCount());

			if constexpr (bReleaseOnRetire)
			{
				uint64_t start = GetHeadSequence();

				for (uint64_t i = 0; i < Count; ++i)
				{
					m_release(m_slots[(start + i) & Mask]);
				}
			}
			m_bitmap.AdvanceWindow(Count);
		}


		// Hands up to MaxCount ready entries, in order, to Consume(seq, T&), then retires them.
		// The callback should move the payload out.
		template<typename Fn>
		uint64_t Drain(Fn&& Consumer, uint64_t MaxCount = Capacity)
		{
			const uint64_t count = std::min<uint64_t>(GetReadyCount(), MaxCount);
			const uint64_t start = GetHeadSequence();

			for (uint64_t i = 0; i < count; ++i)
			{
				Consumer(m_slots[(start + i) & Mask]);
			}

			Retire(count);
			return count;
		}


		// Like Drain, but the consumer returns false to stop (e.g. arena full).
		// Only entries actually consumed are retired, so nothing is silently dropped.
		template<typename Fn>
		uint64_t DrainWhile(Fn&& Consumer, uint64_t MaxCount = Capacity)
		{
			const uint64_t count = std::min<uint64_t>(GetReadyCount(), MaxCount);
			const uint64_t start = GetHeadSequence();

			uint64_t consumed = 0;

			while (consumed < count
				&& Consumer(start + consumed, m_slots[(start + consumed) & Mask]))
			{
				++consumed;
			}

			Retire(consumed);
			return consumed;
		}


		// Give up on a head sequence that never arrived. A late arrival then classifies as Stale.
		void SkipMissingHead()
		{
			assert(!m_bitmap.IsReceived(m_bitmap.GetHead()));
			m_bitmap.AdvanceWindow(1);
		}


	private:
		std::unique_ptr<T[]> m_slots;
		SlidingBitmap<Capacity> m_bitmap;
		[[msvc::no_unique_address]] ReleasePolicy m_release{};
	};
}