#include <cassert>
#include <intrin.h>

#include "Neyrx/Core/Logger/LogDataStructures.h"


using namespace Neyrx::Logger;



// ==========================================================================================================
// Ordered Log Buffer
// ==========================================================================================================

LogReserveState OrderedLogBuffer::GetReserveState()
{
	return m_reservedLogData.GetLogReserveState();
}




uint64_t OrderedLogBuffer::GetStartReservedSequenceNumber() const
{
	return m_reservedLogData.SequenceNumber_Start;
}




uint64_t OrderedLogBuffer::GetEndReservedSequenceNumber() const
{
	return m_reservedLogData.SequenceNumber_End;
}




uint64_t OrderedLogBuffer::GetMaxReservedSequenceNumber() const
{
	return m_reservedLogData.SequenceNumber_Max;
}




std::size_t OrderedLogBuffer::GetSize() const
{
	std::size_t result = 0;

	for (auto size : m_bufferSize)
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
	bool result = m_bufferSize[m_bufferWriteCursor.ChunkIndex] == 0;

	if (result)
	{
		assert(m_bufferWriteCursor.Offset == 0 && "Orderer Log Bufer Chunk is Empty, while the Write Cursor offset is not 0");
	}

	if (!result)
	{
		return m_bufferSize[(m_bufferWriteCursor.ChunkIndex + 1) % m_orderedLogBufferChunkCount] == 0;
	}

	return result;
}




void OrderedLogBuffer::IncrementBufferPosition(BufferPosition& InBufferPosition)
{
	++InBufferPosition.Offset;

	if (InBufferPosition.Offset >= Capacity)
	{
		InBufferPosition.ChunkIndex = (InBufferPosition.ChunkIndex + 1) % m_orderedLogBufferChunkCount;
		InBufferPosition.Offset = 0;
	}
}




OrderedLogBuffer::BufferPosition OrderedLogBuffer::GetBufferPosition(const uint64_t InSequenceNumber) const
{
	// Return null if the buffer is empty, or no reserve functional call has been made yet
	if (IsEmpty() || m_reservedLogData.GetLogReserveState() == LogReserveState::UNINITIALIZED) return BufferPosition();

	// Return null if InSequenceNumber is a stale sequence number
	if (InSequenceNumber < m_reservedLogData.SequenceNumber_Start) return BufferPosition();

	std::size_t totalBufferSize = GetSize();

	uint64_t sequenceOffset = InSequenceNumber - m_reservedLogData.SequenceNumber_Start;

	// Return null if InSequenceNumber is not within reserved range
	if (sequenceOffset >= totalBufferSize) return BufferPosition();

	const std::size_t offset = static_cast<std::size_t>(sequenceOffset);

	std::size_t bindex = m_bufferIndexStart.Offset + offset;
	std::size_t cindex = m_bufferIndexStart.ChunkIndex;

	if (bindex >= Capacity)
	{
		std::size_t linearOffset = m_bufferIndexStart.Offset + offset;
		std::size_t chunkAdvance = linearOffset / Capacity;
		bindex = linearOffset % Capacity;
		cindex = (m_bufferIndexStart.ChunkIndex + chunkAdvance) % m_orderedLogBufferChunkCount;
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




const OrderedLogSlot* OrderedLogBuffer::GetReservedSlot(const uint64_t InSequenceNumber) const
{
	BufferPosition bpos = GetBufferPosition(InSequenceNumber);

	if (!bpos.IsValid())
	{
		__debugbreak();
		return nullptr;
	}

	const OrderedLogSlot* slot = &OrderedLogBuffers[bpos.ChunkIndex][bpos.Offset];

	return slot;
}




LogReserveState OrderedLogBuffer::DereserveSequences(uint64_t EndSequenceNumber)
{
	if (EndSequenceNumber == InvalidSequenceNumber ||
		EndSequenceNumber < m_reservedLogData.SequenceNumber_Start
		|| EndSequenceNumber > m_reservedLogData.SequenceNumber_End)	// TODO - Error Handling
	{
		__debugbreak();
		std::cout << "Invalid End Sequence" << std::endl;
		return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	uint64_t Start = m_reservedLogData.SequenceNumber_Start;	// Set Start to Oldest Sequence Number
	uint64_t End = EndSequenceNumber;

	std::size_t size = GetSize();		// If later Dereserve Is more linient in respect of Start this will be useful
	std::size_t offset = Start - m_reservedLogData.SequenceNumber_Start;
	std::size_t count = End - Start + 1;

	if (offset >= size || offset + count > size)
	{
		__debugbreak();
		std::cout << "Sequences Out of Range" << std::endl;
		return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	BufferPosition bufPos = GetBufferPosition(Start);

	if (!bufPos.IsValid())
	{
		__debugbreak();
		return LogReserveState::INVALID_INTERNAL_STATE;
	}

	for (uint64_t seq = Start;; ++seq)
	{
		OrderedLogSlot& slot = OrderedLogBuffers[bufPos.ChunkIndex][bufPos.Offset];

		assert(slot.SequenceNumber == seq && "Unexpected Sequence Number in Reserved Slot");

		const std::size_t slotChunkIndex = bufPos.ChunkIndex;

		slot.SequenceNumber = InvalidSequenceNumber;
		slot.Data = nullptr;

		assert(m_bufferSize[slotChunkIndex] > 0);
		--m_bufferSize[slotChunkIndex];

		IncrementBufferPosition(m_bufferIndexStart);
		IncrementBufferPosition(bufPos);

		if (seq == End) break;
	}

	if (IsEmpty())
	{
		m_reservedLogData.SequenceNumber_Start = InvalidSequenceNumber;
		m_reservedLogData.SequenceNumber_End = InvalidSequenceNumber;
		m_bufferIndexStart = BufferPosition{ 0, 0 };
		m_bufferWriteCursor = BufferPosition{ 0, 0 };

		return LogReserveState::DERESERVED_EMPTY;
	}
	else
	{
		m_reservedLogData.SequenceNumber_Start = End + 1;

		return LogReserveState::DERESERVED;
	}

	return LogReserveState::INVALID_INTERNAL_STATE;
}




LogReserveState OrderedLogBuffer::ReserveSequences(const uint64_t InStartSequenceNumber)
{
	if (!HasFreeChunk())				// Not enough capacity to reserve all sequences
	{
		return LogReserveState::FAILURE_AT_CAPACITY;
	}

	if (InStartSequenceNumber == InvalidSequenceNumber ||
		InvalidSequenceNumber - InStartSequenceNumber < Capacity)
	{
		return LogReserveState::FAILURE_SENTINAL;
	}

	LogReserveState state = m_reservedLogData.GetLogReserveState();

	if (state == LogReserveState::INVALID_INTERNAL_STATE) return state;

	if (state != LogReserveState::UNINITIALIZED && InStartSequenceNumber != m_reservedLogData.SequenceNumber_Max + 1)
	{
		return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	const uint64_t end = InStartSequenceNumber + Capacity; // exclusive end

	// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
	// Reservations continue from BufferWriteCursor, the next physical write
	// position. A full chunk advances the cursor to the next chunk at offset 0.
	// The next chunk must be empty before it can be used.
	for (uint64_t sequence = InStartSequenceNumber; sequence < end; ++sequence)						// No more than single chunks capacity should be reserved during a pass
	{
		if (m_bufferWriteCursor.Offset >= Capacity)					// If the present chunk is full, move to the next chunk
		{
			m_bufferWriteCursor.ChunkIndex = (m_bufferWriteCursor.ChunkIndex + 1) % m_orderedLogBufferChunkCount;

			assert(m_bufferSize[m_bufferWriteCursor.ChunkIndex] == 0 && "Log Buffer Chunk is not Empty");		// If the next chunk is not empty, then old data was not flushed.

			m_bufferWriteCursor.Offset = 0;
		}

		OrderedLogBuffers[m_bufferWriteCursor.ChunkIndex][m_bufferWriteCursor.Offset].SequenceNumber = sequence;
		++m_bufferWriteCursor.Offset;
		++m_bufferSize[m_bufferWriteCursor.ChunkIndex];
	}

	if (state != LogReserveState::RESERVED)
	{
		m_reservedLogData.SequenceNumber_Start = InStartSequenceNumber;
	}

	m_reservedLogData.SequenceNumber_End = end - 1;
	m_reservedLogData.SequenceNumber_Max = end - 1;

	return m_reservedLogData.GetLogReserveState();
}




// ==========================================================================================================
// Ordered Log Window
// ==========================================================================================================

void OrderedLogWindow::Initialize(LoggerThreadRegistry* InLoggerState)
{
	m_loggerRegistryPointer = InLoggerState;
}




const OrderedLogSlot* OrderedLogWindow::GetFirstAccountedLogSlot() const
{
	for (uint64_t i = m_trackedData.Sequence_Begin; i <= m_trackedData.Sequence_End; ++i)
	{
		auto it = std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), i);
		if (it != m_trackedData.Sequence_Missing.end())
		{
			const OrderedLogSlot* slot = m_orderedLogBuffer.GetReservedSlot(i);
			if (slot != nullptr) return slot;
		}
	}

	return nullptr;
}




uint64_t OrderedLogWindow::GetFirstMissingSequenceAfter(uint64_t SequenceNumber) const
{
	uint64_t missingSeq = InvalidSequenceNumber;
	for (auto mis : m_trackedData.Sequence_Missing)
	{
		if (mis > SequenceNumber)
		{
			missingSeq = std::min(missingSeq, mis);
		}
	}

	return missingSeq;
}

uint64_t OrderedLogWindow::GetFirstValidSequenceAfter(uint64_t SequenceNumber) const
{
	if (SequenceNumber >= m_trackedData.Sequence_End) return InvalidSequenceNumber;

	for (uint64_t seq = SequenceNumber + 1; seq <= m_trackedData.Sequence_End; ++seq)
	{
		auto it = std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), seq);
		if (it == m_trackedData.Sequence_Missing.end() && m_orderedLogBuffer.GetReservedSlot(seq)->Data != nullptr)
		{
			return seq;
		}
	}

	return InvalidSequenceNumber;
}

const OrderedLogSlot* OrderedLogWindow::GetFirstValidLogSlotAfter(uint64_t SequenceNumber) const
{
	if (SequenceNumber >= m_trackedData.Sequence_End) return nullptr;

	for (uint64_t seq = SequenceNumber + 1; seq <= m_trackedData.Sequence_End; ++seq)
	{
		auto it = std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), seq);
		if (it == m_trackedData.Sequence_Missing.end())
		{
			const OrderedLogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);

			if (slot->Data != nullptr) return slot;
		}
	}

	return nullptr;
}




uint64_t OrderedLogWindow::GetMaxReservedSequence() const
{
	return m_orderedLogBuffer.GetMaxReservedSequenceNumber();
}




uint64_t OrderedLogWindow::GetInvertedCount() const
{
	return m_invertedLogCount;
}




uint64_t OrderedLogWindow::GetSkippedCount() const
{
	return m_skippedLogCount;
}




bool OrderedLogWindow::IsEmpty() const
{
	return m_orderedLogBuffer.IsEmpty();
}




bool OrderedLogWindow::IsSequencesMissing()
{
	return m_trackedData.Sequence_Missing.size() > 0;
}




const OrderedLogSlot* OrderedLogWindow::GetTrackedSlot(uint64_t SequenceNumber) const
{
	return m_orderedLogBuffer.GetReservedSlot(SequenceNumber);
}




const TrackedLogData& OrderedLogWindow::GetTrackedLogData() const
{
	return m_trackedData;
}




LogReserveState OrderedLogWindow::ReserveLogSlotsForTracking()
{
	LogReserveState state = m_orderedLogBuffer.GetReserveState();
	uint64_t maxRes = m_orderedLogBuffer.GetMaxReservedSequenceNumber();
	uint64_t startRes = InvalidSequenceNumber;

	if (state == LogReserveState::INVALID_INTERNAL_STATE) return state;

	if (state != LogReserveState::UNINITIALIZED)
	{
		if (maxRes >= InvalidSequenceNumber - Capacity)	// Sentinal Gaurd
		{
			return LogReserveState::FAILURE_SENTINAL;
		}

		startRes = maxRes + 1;
	}
	else
	{
		startRes = 0;
	}

	return m_orderedLogBuffer.ReserveSequences(startRes);
}