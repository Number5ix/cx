#include <cxmem/xalloc_core/xalloc_core.h>

// Returns: size of memory at ptr
// In normal (max performance mode), this will have a lower bound at the number of bytes that
// were required by xaAlloc, but may be more. If more, it is safe to use the extra space
// without reallocating.
// In secure mode, this will always return the exact number of bytes that were requested by
// the original allocation. The reason is that in secure mode, any slack space in the allocation
// is filled with padding and used to check for buffer overruns.
// See also xaOptSize(), which is useful for determining the optimal size to allocate in
// secure mode to allow room for buffer growth.
size_t xaSize(void *ptr);

// Returns the optimal size to allocate to fit a given number of bytes. This returns the
// underlying size class that fits the given number of bytes.
// This is most useful when the allocator is in secure mode to determine how much bigger
// a buffer can be allocated without moving to the next size class, since in that mode
// the difference between the requested size and actual allocated size is NOT usable by
// the caller due to secure padding.
size_t xaOptSize(size_t sz);

// Flushes any deferred free() operations and returns as much memory to the OS as possible
void xaFlush();
