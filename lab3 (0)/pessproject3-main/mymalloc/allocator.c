#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "./allocator_interface.h"
#include "./memlib.h"

#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

#define WSIZE      8
#define DSIZE      16
#define CHUNKSIZE  (1<<12)

#define PACK(size, alloc)  ((size) | (alloc))
#define GET(p)             (*(size_t *)(p))
#define PUT(p, val)        (*(size_t *)(p) = (val))
#define GET_SIZE(p)        (GET(p) & ~0x7)
#define GET_ALLOC(p)       (GET(p) & 0x1)

#define HDRP(bp)           ((char *)(bp) - WSIZE)
#define FTRP(bp)           ((char *)(bp) + GET_SIZE(HDRP(bp)) - (2 * WSIZE))
#define NEXT_BLKP(bp)      ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)      ((char *)(bp) - GET_SIZE(((char *)(bp) - (2 * WSIZE))))

static char *heap_listp = NULL;

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        return bp;
    } else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if (size < DSIZE) size = DSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

static void *find_fit(size_t asize) {
    void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
    return NULL;
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    if ((csize - asize) >= DSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

int my_init() {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp += (2 * WSIZE);
    return 0;
}

void* my_malloc(size_t size) {
    size_t asize;
    char *bp;
    if (size == 0) return NULL;
    asize = ALIGN(size + 2 * WSIZE);
    if (asize < DSIZE) asize = DSIZE;

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    size_t extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) return NULL;
    place(bp, asize);
    return bp;
}

void my_free(void* ptr) {
    if (!ptr) return;
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

void* my_realloc(void* ptr, size_t size) {
    if (!ptr) return my_malloc(size);
    if (size == 0) { my_free(ptr); return NULL; }
    size_t old_size = GET_SIZE(HDRP(ptr));
    if (size <= old_size - 2*WSIZE) return ptr;
    void *newptr = my_malloc(size);
    if (!newptr) return NULL;
    size_t copy_size = (size < old_size - 2*WSIZE) ? size : old_size - 2*WSIZE;
    memcpy(newptr, ptr, copy_size);
    my_free(ptr);
    return newptr;
}

int my_check() { return 0; }
