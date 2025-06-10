#ifndef RI_RINAGE_ALLOC_H
#define RI_RINAGE_ALLOC_H

#include "RITypes.h"
#include "system/Types.h"
#include <array>
#include <cassert>

struct RISegmentAllocDesc_s{
	uint32_t numElements;
	uint16_t elementStride;
	uint16_t numSegments;
	uint16_t maxElements; 
};

struct RISegmentReq_s {
	uint16_t elementStride;
	uint32_t elementOffset;
	uint32_t numElements;
};

template<size_t N> 
struct RISegmentAlloc {
  static constexpr uint32_t SEGMENTS = N;
  RISegmentAlloc() {}
  RISegmentAlloc(struct RISegmentAllocDesc_s* desc);
  bool request(uint32_t frameIndex, size_t numElements, struct RISegmentReq_s *req); 

  uint16_t elementStride = 0;
 	uint16_t numSegments = 0;
  uint16_t maxElements = 0;

  // data
  int16_t tail = 0;
  int16_t head = 1;
  uint16_t numElements = 0;
  size_t elementOffset = 0;
  struct Segment {
    uint64_t frameNum;
    size_t numElements; // size of the generation
  };
  std::array<Segment, N> segment;
};

template<size_t N> 
RISegmentAlloc<N>::RISegmentAlloc(struct RISegmentAllocDesc_s* desc) {
	  assert( desc );
	  elementStride = desc->elementStride;
	  tail = 0;
	  head = 1;
	  numSegments = desc->numSegments;
	  maxElements = desc->maxElements;
	  assert(numSegments <= N);
	  assert( elementStride > 0 );
}
template <size_t N>
bool RISegmentAlloc<N>::request(uint32_t frameIndex, size_t numElements,
                                struct RISegmentReq_s *req) {
  // reclaim segments that are unused
  while (tail != head && frameIndex >= (segment[tail].frameNum + numSegments)) {
    assert(alloc->numElements >= segment[tail].numElements);
    numElements -= segment[tail].numElements;
    elementOffset = (elementOffset + segment[tail].numElements) % maxElements;
    segment[tail].numElements = 0;
    segment[tail].frameNum = 0;
    tail = (tail + 1) % segment.size();
  }

  // the frame has change
  if (frameIndex != segment[head].frameNum) {
    head = (head + 1) % ARRAY_COUNT(segment);
    segment[head].frameNum = frameIndex;
    segment[head].numElements = 0;
    assert(head != tail); // this shouldn't happen
  }

  size_t elmentEndOffset = (elementOffset + numElements) % maxElements;
  assert(alloc->elementOffset < alloc->maxElements);
  assert(elmentEndOffset < alloc->maxElements);
  // we don't have enough space to fit into the end of the buffer give up the
  // remaning and move the cursor to the start
  if (elementOffset < elmentEndOffset &&
      elmentEndOffset + numElements > maxElements) {
    const uint32_t remaining = (maxElements - elmentEndOffset);
    segment[head].numElements += remaining;
    numElements += remaining;
    elmentEndOffset = 0;
    assert((alloc->elementOffset + alloc->numElements) % alloc->maxElements ==
           0);
  }
  size_t remainingSpace = 0;
  if (elmentEndOffset < elementOffset) { // the buffer has wrapped around
    remainingSpace = elementOffset - elmentEndOffset;
  } else {
    remainingSpace = maxElements - elmentEndOffset;
  }
  assert(remainingSpace <= alloc->maxElements);

  // there is not enough avalaible space we need to reallocate
  if (numElements > remainingSpace) {
    return false;
  }
  segment[head].numElements += numElements;
  numElements += numElements;

  req->elementOffset = elmentEndOffset;
  req->elementStride = elementStride;
  req->numElements =
      numElements; // includes the padding on the end of the buffer
  return true;
}

static inline bool IsRISegmentBufferContinous(size_t currentOffset,
                                              size_t currentNumElements,
                                              size_t nextOffset) {
  return currentOffset + currentNumElements == nextOffset;
}

#endif
