#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cassert>

#include "Neyrx/Core/Container/SequenceStatus.h"

namespace Neyrx::Container
{
	// ======================================================================================================
	// Sliding Reorder Bitmap (consumer-thread only, O(1) mark, word-wise scan/clear)
	//
	// Indexing is ABSOLUTE: sequence S always lives at word (S / 64) & WordMask, bit S & 63.
	// The head can sit at any bit offset; nothing is rotated when the window advances.
	// ======================================================================================================

	template<std::size_t WindowSize = 4096>
	requires (std::has_single_bit(WindowSize) && WindowSize >= 64)
	class SlidingBitmap
	{
	public:

		static constexpr std::size_t WordCount = WindowSize / 64;
		static constexpr std::size_t WordMask = WordCount - 1;

		SlidingBitmap() noexcept
		{
			Reset(0);
		}


		void Reset(uint64_t InStartSequence) noexcept
		{
			m_windowHead = InStartSequence;
			m_words.fill(0);
		}

		// Returns true if Sequence is inside the window.
		[[nodiscard]] bool IsInsideWindow(uint64_t Sequence) const noexcept
		{
			// Unsigned wrap makes Sequence < head produce a huge value, so one compare covers both ends.
			return static_cast<uint64_t>(Sequence - m_windowHead) < WindowSize;
		}


		// Classifies a sequence against the window without modifying anything.
		//   Stale          : behind the head (already retired). Always wins over Duplicate.
		//   BeyondCapacity : at or past head + WindowSize.
		//   Duplicate      : inside the window and already marked.
		//   Accepted       : inside the window and not yet marked.
		[[nodiscard]] constexpr SequenceStatus Classify(uint64_t Sequence) const noexcept
		{
			uint64_t diff = Sequence - m_windowHead;

			if (diff >= WindowSize)
			{
				return (diff >= HalfRange) ? SequenceStatus::Stale : SequenceStatus::BeyondCapacity;
			}

			return IsReceived(Sequence) ? SequenceStatus::Duplicate : SequenceStatus::Accepted;
		}

		// Marks a sequence as received. The bit is only set when the result is Accepted.
		[[nodiscard]] SequenceStatus MarkReceived(uint64_t Sequence) noexcept
		{
			const SequenceStatus status = Classify(Sequence);

			if (status != SequenceStatus::Accepted) return status;

			m_words[(Sequence >> 6) & WordMask] |= 1ULL << (Sequence & 63);

			return status;
		}


		// Returns true if Sequence State is set to true.
		[[nodiscard]]  bool IsReceived(uint64_t Sequence) const noexcept
		{
			return IsInsideWindow(Sequence) && TestBit(Sequence);
		}


		// Number of consecutive received sequences starting at the head (0..WindowSize).
		[[nodiscard]] uint64_t GetContiguousReadyCount() const noexcept
		{
			uint64_t ready = 0;
			uint64_t seq = m_windowHead;

			while(ready < WindowSize)
			{
				uint64_t wordIdx = (seq >> 6) & WordMask;
				uint64_t bitOffset = seq & 63;

				// Shift bit offsets below seq; vacated high bits are zero, so run <= 64 - bitOffset.
				uint64_t word = m_words[wordIdx] >> bitOffset;

				uint64_t run = static_cast<uint64_t>(std::countr_one(word));

				// Never count past one full window (the scan can wrap back around over the the head's word).
				run = std::min<uint64_t>(run, WindowSize - ready);

				ready += run;
				seq += run;

				// Run ended before the word boundary -> Gap found
				if (bitOffset + run < 64) break;
			}

			return ready;
		}


		// Retires Count sequences from the head, clearing their bits a word at a time.
		void AdvanceWindow(uint64_t Count) noexcept
		{
			assert(Count <= WindowSize);

			while (Count > 0)
			{
				std::size_t wordIdx = (m_windowHead >> 6) & WordMask;
				std::size_t bitOffset = m_windowHead & 63;

				std::size_t bits = std::min<uint64_t>(Count, 64 - bitOffset);

				std::size_t mask = (bits == 64) ? ~0ULL : (((1ULL << bits) - 1ULL) << bitOffset);
				m_words[wordIdx] &= ~mask;

				m_windowHead += bits;
				Count -= bits;
			}
		}


		[[nodiscard]] uint64_t GetHead() const noexcept
		{
			return m_windowHead;
		}


	private:

		static constexpr uint64_t HalfRange = 1ULL << 63;

		// Unchecked called garuntees sequence is inside the window
		[[nodiscard]] bool TestBit(uint64_t Sequence) const noexcept
		{
			return (m_words[(Sequence >> 6) & WordMask] >> (Sequence & 63)) & 1ULL;
		}

		std::array<uint64_t, WordCount> m_words{};
		uint64_t m_windowHead = 0;
	};
}