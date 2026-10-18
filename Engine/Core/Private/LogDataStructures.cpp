#include <cassert>

#include "Neyrx/Core/Logger/LogDataStructures.h"


using namespace Neyrx::Logger;



// ==========================================================================================================
// Ordered Log Buffer
// ==========================================================================================================

LogReserveState OrderedLogBuffer::GetReserveState()
{
	return m_logReserveState;
}




uint64_t OrderedLogBuffer::GetStartReservedSequenceNumber() const
{
	return m_startReservedSequenceNumber;
}




uint64_t OrderedLogBuffer::GetEndReservedSequenceNumber() const
{
	return m_endReservedSequenceNumber;
}




uint64_t OrderedLogBuffer::GetMaxReservedSequenceNumber() const
{
	return m_maxReservedSequenceNumber;
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
	if (IsEmpty() || m_maxReservedSequenceNumber == InvalidSequenceNumber) return BufferPosition();

	// Return null if InSequenceNumber is a stale sequence number
	if (InSequenceNumber < m_startReservedSequenceNumber) return BufferPosition();

	std::size_t totalBufferSize = GetSize();

	uint64_t sequenceOffset = InSequenceNumber - m_startReservedSequenceNumber;

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

	if (!bpos.IsValid()) return nullptr;

	const OrderedLogSlot* slot = &OrderedLogBuffers[bpos.ChunkIndex][bpos.Offset];

	return slot;
}




LogReserveState OrderedLogBuffer::DereserveSequences(uint64_t EndSequenceNumber)
{
	if (EndSequenceNumber == InvalidSequenceNumber ||
		EndSequenceNumber < m_startReservedSequenceNumber
		|| EndSequenceNumber > m_endReservedSequenceNumber)	// TODO - Error Handling
	{
		std::cout << "Invalid End Sequence" << std::endl;
		return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	uint64_t Start = m_startReservedSequenceNumber;	// Set Start to Oldest Sequence Number
	uint64_t End = EndSequenceNumber;

	std::size_t size = GetSize();		// If later Dereserve Is more linient in respect of Start this will be useful
	std::size_t offset = Start - m_startReservedSequenceNumber;
	std::size_t count = End - Start + 1;

	if (offset >= size || offset + count > size)
	{
		std::cout << "Sequences Out of Range" << std::endl;
		return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	BufferPosition bufPos = GetBufferPosition(Start);

	if (!bufPos.IsValid()) return LogReserveState::INVALID_INTERNAL_STATE;

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
		m_startReservedSequenceNumber = InvalidSequenceNumber;
		m_bufferIndexStart = BufferPosition{ 0, 0 };
		m_bufferWriteCursor = BufferPosition{ 0, 0 };

		m_logReserveState = LogReserveState::SUCCESS_DERESERVED_EMPTY;
	}
	else
	{
		m_startReservedSequenceNumber = End + 1;

		m_logReserveState = LogReserveState::SUCCESS_DERESERVED;
	}

	return m_logReserveState;
}




LogReserveState OrderedLogBuffer::ReserveSequences(const uint64_t InStartSequenceNumber)
{
	if (!HasFreeChunk())				// Not enough capacity to reserve all sequences
	{
		m_logReserveState = LogReserveState::FAILURE_AT_CAPACITY;
		return LogReserveState::FAILURE_AT_CAPACITY;
	}

	if (InStartSequenceNumber == InvalidSequenceNumber ||
		InvalidSequenceNumber - InStartSequenceNumber < Capacity)
	{
		return LogReserveState::FAILURE_SENTINAL;
	}


	if (m_logReserveState == LogReserveState::UNINITIALIZED)				// First ReserveSlots function call, no prior ReserveSlots function call was made
	{
		m_startReservedSequenceNumber = InStartSequenceNumber;
	}
	else	// Do not reserve Sequences that are already reserved, and reject gaps between last reserved sequence number
	{
		if (InStartSequenceNumber != m_maxReservedSequenceNumber + 1) return LogReserveState::FAILURE_SEQUENCE_INVALID;
	}

	uint64_t sequence = InvalidSequenceNumber;

	// Reserves Capacity(value of) consecutive sequence numbers in the ordered buffer.
	// Reservations continue from BufferWriteCursor, the next physical write
	// position. A full chunk advances the cursor to the next chunk at offset 0.
	// The next chunk must be empty before it can be used.
	for (std::size_t i = 0; i < Capacity; ++i)						// No more than single chunks capacity should be reserved during a pass
	{
		if (m_bufferWriteCursor.Offset >= Capacity)					// If the present chunk is full, move to the next chunk
		{
			m_bufferWriteCursor.ChunkIndex = (m_bufferWriteCursor.ChunkIndex + 1) % m_orderedLogBufferChunkCount;

			assert(m_bufferSize[m_bufferWriteCursor.ChunkIndex] == 0 && "Log Buffer Chunk is not Empty");		// If the next chunk is not empty, then old data was not flushed.

			m_bufferWriteCursor.Offset = 0;
		}

		sequence = InStartSequenceNumber + static_cast<uint64_t>(i);
		OrderedLogBuffers[m_bufferWriteCursor.ChunkIndex][m_bufferWriteCursor.Offset].SequenceNumber = sequence;
		++m_bufferWriteCursor.Offset;
		++m_bufferSize[m_bufferWriteCursor.ChunkIndex];
	}

	m_endReservedSequenceNumber = sequence;
	m_maxReservedSequenceNumber = sequence;

	if (IsEmpty())
	{
		m_logReserveState = LogReserveState::SUCCESS_DERESERVED_EMPTY;
	}
	else
	{
		m_logReserveState = LogReserveState::SUCCESS_DERESERVED;
	}

	return m_logReserveState;
}




// ==========================================================================================================
// Ordered Log Window
// ==========================================================================================================

void OrderedLogWindow::Initialize(LoggerThreadRegistry* InLoggerState)
{
	m_loggerRegistryPointer = InLoggerState;
	m_staleLogBuffer.reserve(Capacity);
}




const OrderedLogSlot* OrderedLogWindow::GetFirstAccountedLogSlot() const
{
	for (uint64_t i = m_trackedData.Sequence_Begin; i <= m_trackedData.Sequence_End; ++i)
	{
		if (std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), i) != m_trackedData.Sequence_Missing.end())
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
		if (std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), m_trackedData.Sequence_Missing)
			!= m_trackedData.Sequence_Missing.end())
		{
			return seq;
		}
	}

	return InvalidSequenceNumber;
}




uint64_t OrderedLogWindow::GetMaxReservedSequence() const
{
	return m_orderedLogBuffer.GetMaxReservedSequenceNumber();
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
	uint64_t lastRes = m_orderedLogBuffer.GetEndReservedSequenceNumber();

	if (m_orderedLogBuffer.GetReserveState() != LogReserveState::UNINITIALIZED &&			// Sentinal Gaurd
		lastRes >= InvalidSequenceNumber - Capacity)
	{
		return LogReserveState::FAILURE_SENTINAL;
	}

	// If Tracker is uninitialized, then this is the first function call, use startRes as 0
	uint64_t startRes = m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED
		? 0
		: lastRes + 1;

	return m_orderedLogBuffer.ReserveSequences(startRes);
}




LogTrackerState OrderedLogWindow::TrackLogData()
{
	uint64_t oldestSeq = m_orderedLogBuffer.GetStartReservedSequenceNumber();

	if (m_orderedLogBuffer.GetReserveState() == LogReserveState::UNINITIALIZED)
	{
		return LogTrackerState::FAILURE_NO_RESERVE;
	}

	if (m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED &&
		m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED)
	{
		m_startSequenceThisPass = 0;
	}
	else
	{
		// This cannot result in a InvalidSequenceNumber
		// since the OrderLogBuffer reserve plicy wont allow resering any slot that equals InvalidSequenceNumber
		m_startSequenceThisPass = m_trackedData.Sequence_MaxTracked + 1;
	}

	bool bAllQueueEmpty = true;

	for (LogThreadContext& contex : m_loggerRegistryPointer->m_threadContexts)
	{
		auto& buffer = contex.m_logBuffer;

		if (buffer.IsEmpty())
		{
			continue;
		}
		else
		{
			bAllQueueEmpty = false;
		}

		auto ItSnap = buffer.GetRingBufferIteratorSnapshot();

		for (auto it = ItSnap.Begin; it != ItSnap.End; ++it)
		{
			const LogData* data = &buffer.Peek(it.GetIndex());

			if (data == nullptr)
			{
				std::cout << "Data is Null in SPSC Queue" << std::endl;
				assert(false && "Data is Null in SPSC Queue");
				continue;
			}

			const uint64_t seq = data->Sequence;

			if (seq == InvalidSequenceNumber)
			{
				std::cout << "INVALID Sequence Number FOUND in Context Queue" << std::endl;
				continue;
			}

			if (seq < oldestSeq) // Stale sequence use the bypass function || This is temporary
			{
				m_staleLogBuffer.push_back(data);
			}

			// If the sequence is such that it exceeds the max Sequence Number that can be reserved this pass,
			// break and conitnue with the next thread context.
			// This thread context connot contain a smaller sequence number
			const uint64_t rangeStart = m_startSequenceThisPass == InvalidSequenceNumber ? 0 : m_startSequenceThisPass;

			if (seq > rangeStart)			// seq if not greater than range start, is greater than oldestRserved, and is a missing data. Should bypass this check
			{
				const uint64_t offset = seq - rangeStart;
				if (offset >= Capacity) break;
			}

			// If no log has been consumed/reserved then use expected as 0. This results in the index of the incoming sequence passing the threashold.
			uint64_t expected = m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED ? 0 : m_trackedData.Sequence_End + 1;

			if (expected == InvalidSequenceNumber)
			{
				return LogTrackerState::FAILURE_SENTINAL;
			}

			std::size_t bMissingIndex = Invalidindex;

			// If expected > seq, then check if the seq is found in m_missingSequences
			// if expected < seq, reserve all Sequence Numbers between expected and seq
			if (seq < expected)
			{// If seq is not a missing tracked sequence, the seq has already been reserved in the previous pass and is waiting flush
				if (m_trackedData.Sequence_Missing.size() != 0)
				{
					auto it = std::find(m_trackedData.Sequence_Missing.begin(), m_trackedData.Sequence_Missing.end(), seq);

					if (it == m_trackedData.Sequence_Missing.end() && m_trackedData.Sequence_Missing.size())
					{
						assert(false && "OrderLogWindow Missing Sequence Bug");
					}

					// If seq is a missing sequence, store the index, and pop only if successfully reserved
					bMissingIndex = static_cast<std::size_t>(std::distance(m_trackedData.Sequence_Missing.begin(), it));
				}
			}
			else if (seq > expected)
			{
				// if seq > expected, mark all in between sequences as missing
				// and push back into m_missingSequences
				if (seq > expected)
				{
					for (uint64_t missing = expected; missing < seq; ++missing)
					{
						m_trackedData.Sequence_Missing.push_back(missing);
					}
				}
			}

			OrderedLogSlot* slot = m_orderedLogBuffer.GetReservedSlot(seq);

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

			*slot = OrderedLogSlot(0, 0, seq, const_cast<LogData*>(data));

			if (m_trackedData.Sequence_End == InvalidSequenceNumber || m_trackedData.Sequence_End < seq)
			{
				m_trackedData.Sequence_End = seq;
				m_trackedData.Sequence_MaxTracked = seq;
			}

			// If seq is a missing index, swap and pop from m_trackedData.SequenceMissing
			if (bMissingIndex != Invalidindex)
			{
				std::swap(m_trackedData.Sequence_Missing[bMissingIndex], m_trackedData.Sequence_Missing.back());
				m_trackedData.Sequence_Missing.pop_back();
			}
		}
	}

	if (m_trackedData.GetTrackerState() == LogTrackerState::UNINITIALIZED || m_trackedData.GetTrackerState() == LogTrackerState::UNTRACKED)
	{
		if (bAllQueueEmpty)
		{
			return LogTrackerState::FAILURE_EMPTY;
		}
	}

	return m_trackedData.GetTrackerState();
}




bool OrderedLogWindow::IsStaleDataPending() const
{
	return m_staleLogBuffer.size() != 0;
}