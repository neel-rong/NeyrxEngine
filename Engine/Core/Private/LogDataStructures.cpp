#include <cassert>

#include "Neyrx/Core/Logger/LogDataStructures.h"


using namespace Neyrx::Logger;



// ==========================================================================================================
// Ordered Log Buffer
// ==========================================================================================================

uint64_t OrderedLogBuffer::GetLastReservedSequenceNumber() const
{
	return LastReservedSequenceNumber;
}




uint64_t OrderedLogBuffer::GetOldestReservedSequenceNumber() const
{
	return OldestReservedSequenceNumber;
}




std::size_t OrderedLogBuffer::GetSize() const
{
	std::size_t result = 0;

	for (auto size : BufferSize)
	{
		result += size;
	}

	return result;
}




bool OrderedLogBuffer::IsEmpty() const
{
	return GetSize() == 0;
}




bool OrderedLogBuffer::HasFreeChunk() const
{
	// The write buffer offset will reset to 0 on a flush if the Chunk is emtpty.
	// Else it will retain it's last position
	// So the result here should never return true when the write chunk is empty while a cursor is not at 0
	bool result = BufferSize[BufferWriteCursor.ChunkIndex] == 0;

	if (result)
	{
		assert(BufferWriteCursor.Offset == 0 && "Orderer Log Bufer Chunk is Empty, while the Write Cursor offset is not 0");
	}

	if (!result)
	{
		return BufferSize[(BufferWriteCursor.ChunkIndex + 1) % OrderedLogBufferChunkCount] == 0;
	}

	return result;
}




void OrderedLogBuffer::IncrementBufferPosition(BufferPosition& InBufferPosition)
{
	++InBufferPosition.Offset;

	if (InBufferPosition.Offset >= Capacity)
	{
		InBufferPosition.ChunkIndex = (InBufferPosition.ChunkIndex + 1) % OrderedLogBufferChunkCount;
		InBufferPosition.Offset = 0;
	}
}




OrderedLogBuffer::BufferPosition OrderedLogBuffer::GetBufferPosition(const uint64_t InSequenceNumber)
{
	// Return null if the buffer is empty, or no reserve functional call has been made yet
	if (IsEmpty() || OldestReservedSequenceNumber == InvalidSequenceNumber) return BufferPosition();

	// Return null if InSequenceNumber is a stale sequence number
	if (InSequenceNumber < OldestReservedSequenceNumber) return BufferPosition();

	std::size_t totalBufferSize = GetSize();

	uint64_t sequenceOffset = InSequenceNumber - OldestReservedSequenceNumber;

	// Return null if InSequenceNumber is not within reserved range
	if (sequenceOffset >= totalBufferSize) return BufferPosition();

	const std::size_t offset = static_cast<std::size_t>(sequenceOffset);

	std::size_t bindex = BufferIndexStart.Offset + offset;
	std::size_t cindex = BufferIndexStart.ChunkIndex;

	if (bindex >= Capacity)
	{
		std::size_t linearOffset = BufferIndexStart.Offset + offset;
		std::size_t chunkAdvance = linearOffset / Capacity;
		bindex = linearOffset % Capacity;
		cindex = (BufferIndexStart.ChunkIndex + chunkAdvance) % OrderedLogBufferChunkCount;
	}

	return BufferPosition(cindex, bindex);
}




OrderedLogSlot* OrderedLogBuffer::GetReservedSlot(const uint64_t InSequenceNumber)
{
	BufferPosition bpos = GetBufferPosition(InSequenceNumber);

	if (!bpos.IsValid()) return nullptr;

	OrderedLogSlot* slot = &OrderedLogBuffers[bpos.ChunkIndex][bpos.Offset];

	return slot;
}




bool OrderedLogBuffer::DereserveSequences(uint64_t EndSequenceNumber)
{
	if (EndSequenceNumber == InvalidSequenceNumber ||
		EndSequenceNumber < OldestReservedSequenceNumber
		|| EndSequenceNumber > LastReservedSequenceNumber)	// TODO - Error Handling
	{
		std::cout << "Invalid End Sequence" << std::endl;
		return false;
	}

	uint64_t Start = OldestReservedSequenceNumber;	// Set Start to Oldest Sequence Number
	uint64_t End = EndSequenceNumber;

	std::size_t size = GetSize();		// If later Dereserve Is more linient in respect of Start this will be useful
	std::size_t offset = Start - OldestReservedSequenceNumber;
	std::size_t count = End - Start + 1;

	if (offset >= size || offset + count > size)
	{
		std::cout << "Sequences Out of Range" << std::endl;
		return false;
	}

	BufferPosition bufPos = GetBufferPosition(Start);

	if (!bufPos.IsValid()) return false;

	for (uint64_t seq = Start;; ++seq)
	{
		OrderedLogSlot& slot = OrderedLogBuffers[bufPos.ChunkIndex][bufPos.Offset];

		assert(slot.SequenceNumber == seq && "Unexpected Sequence Number in Reserved Slot");

		const std::size_t slotChunkIndex = bufPos.ChunkIndex;

		slot.SequenceNumber = InvalidSequenceNumber;
		slot.Data = nullptr;

		assert(BufferSize[slotChunkIndex] > 0);
		--BufferSize[slotChunkIndex];

		IncrementBufferPosition(BufferIndexStart);
		IncrementBufferPosition(bufPos);

		if (seq == End) break;
	}

	if (IsEmpty())
	{
		OldestReservedSequenceNumber = InvalidSequenceNumber;
		BufferIndexStart = BufferPosition{ 0, 0 };
		BufferWriteCursor = BufferPosition{ 0, 0 };
	}
	else
	{
		OldestReservedSequenceNumber = End + 1;
	}

	return true;
}




LogReserveState OrderedLogBuffer::ReserveSequences(const uint64_t InStartSequenceNumber)
{
	if (!HasFreeChunk()) return LogReserveState::FAILURE_AT_CAPACITY;										// Not enough capacity to reserve all sequences

	if (InStartSequenceNumber == InvalidSequenceNumber ||
		InvalidSequenceNumber - InStartSequenceNumber < Capacity)
	{
		return LogReserveState::FAILURE_SENTINAL;
	}

	if (OldestReservedSequenceNumber == InvalidSequenceNumber)				// First ReserveSlots function call, no prior ReserveSlots function call was made
	{
		OldestReservedSequenceNumber = InStartSequenceNumber;
	}
	else	// Do not reserve Sequences that are already reserved, and reject gaps between last reserved sequence number
	{
		if (InStartSequenceNumber != OldestReservedSequenceNumber + GetSize()) return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	uint64_t sequence = InvalidSequenceNumber;

	// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
	// Reservations continue from BufferWriteCursor, the next physical write
	// position. A full chunk advances the cursor to the next chunk at offset 0.
	// The next chunk must be empty before it can be used.
	for (std::size_t i = 0; i < Capacity; ++i)						// No more than single chunks capacity should be reserved during a pass
	{
		if (BufferWriteCursor.Offset >= Capacity)					// If the present chunk is full, move to the next chunk
		{
			BufferWriteCursor.ChunkIndex = (BufferWriteCursor.ChunkIndex + 1) % OrderedLogBufferChunkCount;

			assert(BufferSize[BufferWriteCursor.ChunkIndex] == 0 && "Log Buffer Chunk is not Empty");		// If the next chunk is not empty, then old data was not flushed.

			BufferWriteCursor.Offset = 0;
		}

		sequence = InStartSequenceNumber + static_cast<uint64_t>(i);
		OrderedLogBuffers[BufferWriteCursor.ChunkIndex][BufferWriteCursor.Offset].SequenceNumber = sequence;
		++BufferWriteCursor.Offset;
		++BufferSize[BufferWriteCursor.ChunkIndex];
	}

	LastReservedSequenceNumber = sequence;

	return LogReserveState::SUCCESS_RESERVED;
}




// ==========================================================================================================
// Ordered Log Window
// ==========================================================================================================

void OrderedLogWindow::Initialize(LoggerThreadRegistry* InLoggerState)
{
	m_loggerStatePointer = InLoggerState;
}




LogReserveState OrderedLogWindow::ReserveLogSlotsForTracking()
{
	uint64_t lastRes = m_orderedLogBuffer.GetLastReservedSequenceNumber();

	if (m_reserveState != LogReserveState::UNINITIALIZED &&			// Sentinal Gaurd
		lastRes >= InvalidSequenceNumber - Capacity)
	{
		return LogReserveState::FAILURE_SENTINAL;
	}

	// If Tracker is uninitialized, then this is the first function call, use startRes as 0
	uint64_t startRes = m_trackerState == LogTrackerState::UNINITIALIZED
		? 0
		: lastRes + 1;

	return m_orderedLogBuffer.ReserveSequences(startRes);
}




LogTrackerState OrderedLogWindow::TrackLogData()
{
	uint64_t oldestSeq = m_orderedLogBuffer.GetOldestReservedSequenceNumber();

	if (m_reserveState == LogReserveState::UNINITIALIZED) return LogTrackerState::FAILURE_NO_RESERVE;

	if (m_trackerState != LogTrackerState::UNINITIALIZED &&
		m_trackedData.Sequence_End != InvalidSequenceNumber)
	{
		m_StartSequenceThisPass == m_trackedData.Sequence_End;
	}
	else
	{
		m_StartSequenceThisPass = 0;
	}

	for (LogThreadContext& contex : m_loggerStatePointer->m_threadContexts)
	{
		auto& buffer = contex.m_logBuffer;

		if (buffer.IsEmpty()) continue;

		auto ItSnap = buffer.GetRingBufferIteratorSnapshot();

		for (auto it = ItSnap.Begin; it != ItSnap.End; ++it)
		{
			const LogData* data = &buffer.Peek(it.GetIndex());

			if (data == nullptr)
			{
				std::cout << "Data is Null in SPSC Queue" << std::endl;
				continue;
			}

			uint64_t seq = data->Sequence;

			if (seq == InvalidSequenceNumber)
			{
				std::cout << "INVALID Sequence Number FOUND in Context Queue" << std::endl;
				continue;
			}

			if (seq < oldestSeq) // Stale sequence use the bypass function || This is temporary
			{
				for (auto& sink : *m_SinksPtr)
				{
					sink->Consume(*data);
				}
			}

			// If the sequence is such that it exceeds the max Sequence Number that can be reserved this pass,
			// break and conitnue with the next thread context.
			// This thread context connot contain a smaller sequence number
			const uint64_t rangeStart = m_OldestExpectedSequenceThisPass == InvalidSequenceNumber ? 0 : m_OldestExpectedSequenceThisPass;

			if (seq > rangeStart)			// seq if not greater than range start, is greater than oldestRserved, and is a missing data. Should bypass this check
			{
				const uint64_t offset = seq - rangeStart;
				if (offset >= Capacity) break;
			}

			// If no log has been consumed/reserved then use expected as 0. This results in the index of the incoming sequence passing the threashold.
			uint64_t expected = m_highestReservedSequence == InvalidSequenceNumber ? 0 : m_highestReservedSequence + 1;

			std::size_t bMissingIndex = Invalidindex;

			// If expected > seq, then check if the seq is found in m_missingSequences
			// if expected < seq, reserve all Sequence Numbers between expected and seq
			if (seq < expected)
			{
				auto it = std::find(m_missingSequences.begin(), m_missingSequences.end(), seq);

				if (it == m_missingSequences.end())
				{
					if (bGracePeriodCheck)
					{
						continue; // If seq is not a missing tracked sequence, the seq has already been reserved in the previous pass and is waiting flush
					}
					else
					{
						assert(false && "Reserve Slot and Updated Order Log Bug");
						continue;
					}
				}

				// If seq is a missing sequence, store the index, and pop only if successfully reserved
				bMissingIndex = static_cast<std::size_t>(std::distance(m_missingSequences.begin(), it));
			}
			else if (seq > expected) // Gaurd against sentinal overflow and Sequence Gap
			{
				// if seq > expected, mark all in between sequences as missing
				// and push back into m_missingSequences
				if (seq > expected)
				{
					for (uint64_t missing = expected; missing < seq; ++missing)
					{
						m_missingSequences.push_back(missing);
					}
				}
			}

			LogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);

			if (slot == nullptr)
			{
				std::cout << "Invalid Slot. The Sequence Number was possibly not reserved" << std::endl;
				assert(false);
				continue;
			}


			// If the slot returned by the LogBuffer, already contains a pointer to valid data,
			// It is an old or stale data that was not flushed.
			// The eariler condition statement should have prohinited such conditions to exist
			// If the asset below is hit, check the validity of condition statements for rejecting sequences first
			if (slot->Data != nullptr)
			{
				std::cout << "Encountered a reserved slot with valid a data pointer" << std::endl;
				assert(false);
				continue;
			}

			*slot = LogSlot(0, 0, seq, const_cast<LogData*>(data));

			if (m_highestReservedSequence == InvalidSequenceNumber || m_highestReservedSequence < seq)
			{
				m_highestReservedSequence = seq;
			}

			// If seq is a missing index, swap and pop from m_missingSequences
			if (bMissingIndex != Invalidindex)
			{
				std::swap(m_missingSequences[bMissingIndex], m_missingSequences.back());
				m_missingSequences.pop_back();
			}

			bUpdated = true;
		}
	}

	return bUpdated;
}