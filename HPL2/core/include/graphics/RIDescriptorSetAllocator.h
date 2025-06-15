#ifndef R_DESCRIPTOR_POOL_H
#define R_DESCRIPTOR_POOL_H

#include <cstddef>
#include <stdint.h>

#include "graphics/RITypes.h"

#define RESERVE_BLOCK_SIZE 1024
#define ALLOC_HASH_RESERVE 256
#define DESCRIPTOR_MAX_SIZE 64
#define DESCRIPTOR_RESERVED_SIZE 64

struct RIDescriptorSetSlot {
	uint32_t hash;
	uint32_t frameCount;
	// queue
	struct RIDescriptorSetSlot *quNext;
	struct RIDescriptorSetSlot *quPrev;

	// hash
	struct RIDescriptorSetSlot *hNext;
	struct RIDescriptorSetSlot *hPrev;
	union {
#if ( DEVICE_IMPL_VULKAN )
		struct {
			VkDescriptorPool pool;
			VkDescriptorSet handle;
		} vk;
#endif
	};
};

struct RIDescriptorPoolAllocSlot {
		union {
#if ( DEVICE_IMPL_VULKAN )
			struct {
				VkDescriptorPool handle;
			} vk;
#endif
		};

};

struct RIDescriptorSetAlloc;
typedef void ( *RIDescriptorSetAlloc_Create )( struct RIDevice_s *device, struct RIDescriptorSetAlloc *alloc );

struct RIDescriptorSetAlloc {
	RIDescriptorSetAlloc_Create descriptor_alloc_handle;
	uint8_t framesInFlight; // the number of frames in flight 

	struct RIDescriptorSetSlot *hash_slots[ALLOC_HASH_RESERVE];
	struct RIDescriptorSetSlot *queue_begin;
	struct RIDescriptorSetSlot *queue_end;

	struct RIDescriptorSetSlot **reservedSlots; // stb arrays
	struct RIDescriptorPoolAllocSlot* pools; // stb arrays
	struct RIDescriptorSetSlot **blocks;
	size_t blockIndex;
};

struct RIDescriptorSetResult {
	bool found;
	struct RIDescriptorSetSlot *set; // the associated slot
};

struct RIDescriptorSetResult resolveDescriptorSetAlloc( struct RIDevice_s *device,
													 struct RIDescriptorSetAlloc *alloc,
													 uint32_t frameCount,
													 uint32_t hash );
void free_descriptor_set_alloc( struct RIDevice_s *device, struct RIDescriptorSetAlloc *alloc );

// utility
struct RIDescriptorSetSlot *alloc_descriptor_set_slot( struct RIDescriptorSetAlloc *alloc );
void attach_descriptor_slot( struct RIDescriptorSetAlloc *alloc, struct RIDescriptorSetSlot *slot );
void detach_descriptor_slot( struct RIDescriptorSetAlloc *alloc, struct RIDescriptorSetSlot *slot );

#endif
