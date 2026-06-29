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
#define DSIZE      32 
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

#define PRED_PTR(bp)       ((char *)(bp))
#define SUCC_PTR(bp)       ((char *)(bp) + WSIZE)
#define GET_PRED(bp)       (*(char **)(PRED_PTR(bp)))
#define GET_SUCC(bp)       (*(char **)(SUCC_PTR(bp)))
#define SET_PRED(bp, pred) (*(char **)(PRED_PTR(bp)) = (char *)(pred))
#define SET_SUCC(bp, succ) (*(char **)(SUCC_PTR(bp)) = (char *)(succ))

static char *free_listp = NULL;

static void insert_to_list(void *bp) {
    SET_SUCC(bp, free_listp);
    SET_PRED(bp, NULL);
    if (free_listp != NULL) SET_PRED(free_listp, bp);
    free_listp = (char *)bp;
}

static void remove_from_list(void *bp) {
    char *prev = GET_PRED(bp), *succ = GET_SUCC(bp);
    if (prev) SET_SUCC(prev, succ);
    else free_listp = succ;
    if (succ) SET_PRED(succ, prev);
}

static void *coalesce(void *bp) {
    size_t prev_alloc = ((char*)PREV_BLKP(bp) < (char*)mem_heap_lo()) ? 1 : GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = ((char*)NEXT_BLKP(bp) > (char*)mem_heap_hi()) ? 1 : GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) { }
    else if (prev_alloc && !next_alloc) {
        remove_from_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        remove_from_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else {
        remove_from_list(PREV_BLKP(bp));
        remove_from_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    insert_to_list(bp);
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
    char *bp;
    for (bp = free_listp; bp != NULL; bp = GET_SUCC(bp)) {
        if (asize <= GET_SIZE(HDRP(bp))) return bp;
    }
    return NULL;
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_from_list(bp);

    if ((csize - asize) >= DSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(csize - asize, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, 0));
        insert_to_list(next_bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

int my_init() {
    free_listp = NULL;
    char *start = (char *)mem_sbrk(4 * WSIZE);
    if (start == (void *)-1) return -1;
    PUT(start, 0);
    PUT(start + WSIZE, PACK(2*WSIZE, 1));
    PUT(start + 2*WSIZE, PACK(2*WSIZE, 1));
    PUT(start + 3*WSIZE, PACK(0, 1));
    return 0;
}

void* my_malloc(size_t size) {
    if (size == 0) return NULL;
    size_t asize = ALIGN(size + 2 * WSIZE);
    if (asize < DSIZE) asize = DSIZE;

    char *bp;
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
