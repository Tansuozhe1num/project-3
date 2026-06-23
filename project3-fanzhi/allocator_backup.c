/**
 * Copyright (c) 2015 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "./allocator_interface.h"
#include "./memlib.h"

// Don't call libc malloc!
#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)

#ifndef ALIGNMENT
  #define ALIGNMENT 8
#endif

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE SIZE_T_SIZE
#define DSIZE (2 * WSIZE)
#define CHUNKSIZE (1 << 12)
#define LISTS 16

#define OVERHEAD (2 * WSIZE)
#define MIN_BLOCK_SIZE (ALIGN(OVERHEAD + 2 * sizeof(void*)))

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(size_t*)(p))
#define PUT(p, val) (*(size_t*)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~(size_t)0x7)
#define GET_ALLOC(p) (GET(p) & (size_t)0x1)

#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))

#define NEXT_FREEP(bp) (*(void**)(bp))
#define PREV_FREEP(bp) (*(void**)((char*)(bp) + sizeof(void*)))

static char* heap_listp = NULL;
static void* free_lists[LISTS];

static size_t adjust_block_size(size_t size) {
  return MAX(MIN_BLOCK_SIZE, ALIGN(size + OVERHEAD));
}

static int list_index(size_t size) {
  int index = 0;
  size_t bound = MIN_BLOCK_SIZE;
  while (index < LISTS - 1 && size > bound) {
    bound <<= 1;
    index++;
  }
  return index;
}

static void insert_free_block(void* bp) {
  int index = list_index(GET_SIZE(HDRP(bp)));
  void* head = free_lists[index];

  NEXT_FREEP(bp) = head;
  PREV_FREEP(bp) = NULL;
  if (head != NULL) {
    PREV_FREEP(head) = bp;
  }
  free_lists[index] = bp;
}

static void remove_free_block(void* bp) {
  int index = list_index(GET_SIZE(HDRP(bp)));
  void* prev = PREV_FREEP(bp);
  void* next = NEXT_FREEP(bp);

  if (prev != NULL) {
    NEXT_FREEP(prev) = next;
  } else {
    free_lists[index] = next;
  }
  if (next != NULL) {
    PREV_FREEP(next) = prev;
  }
}

static void* coalesce(void* bp) {
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));

  if (prev_alloc && next_alloc) {
    insert_free_block(bp);
    return bp;
  }

  if (prev_alloc && !next_alloc) {
    void* next_bp = NEXT_BLKP(bp);
    remove_free_block(next_bp);
    size += GET_SIZE(HDRP(next_bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    insert_free_block(bp);
    return bp;
  }

  if (!prev_alloc && next_alloc) {
    void* prev_bp = PREV_BLKP(bp);
    remove_free_block(prev_bp);
    size += GET_SIZE(HDRP(prev_bp));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(prev_bp), PACK(size, 0));
    insert_free_block(prev_bp);
    return prev_bp;
  }

  void* prev_bp = PREV_BLKP(bp);
  void* next_bp = NEXT_BLKP(bp);
  remove_free_block(prev_bp);
  remove_free_block(next_bp);
  size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
  PUT(HDRP(prev_bp), PACK(size, 0));
  PUT(FTRP(next_bp), PACK(size, 0));
  insert_free_block(prev_bp);
  return prev_bp;
}

static void* extend_heap(size_t size) {
  size_t asize = ALIGN(size);
  if (asize < MIN_BLOCK_SIZE) {
    asize = MIN_BLOCK_SIZE;
  }

  char* bp = mem_sbrk(asize);
  if (bp == (void*) - 1) {
    return NULL;
  }

  PUT(HDRP(bp), PACK(asize, 0));
  PUT(FTRP(bp), PACK(asize, 0));
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
  return coalesce(bp);
}

static void place(void* bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));
  remove_free_block(bp);

  if (csize - asize >= MIN_BLOCK_SIZE) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));

    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize - asize, 0));
    PUT(FTRP(bp), PACK(csize - asize, 0));
    insert_free_block(bp);
    return;
  }

  PUT(HDRP(bp), PACK(csize, 1));
  PUT(FTRP(bp), PACK(csize, 1));
}

static void* find_fit(size_t asize) {
  int index = list_index(asize);

  for (; index < LISTS; index++) {
    void* bp = free_lists[index];
    while (bp != NULL) {
      if (GET_SIZE(HDRP(bp)) >= asize) {
        return bp;
      }
      bp = NEXT_FREEP(bp);
    }
  }
  return NULL;
}

int my_check() {
  int free_blocks_heap = 0;
  int free_blocks_lists = 0;
  char* bp;

  if (heap_listp == NULL) {
    printf("Heap is not initialized.\n");
    return -1;
  }

  if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))) {
    printf("Bad prologue block.\n");
    return -1;
  }

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (((uintptr_t)bp % ALIGNMENT) != 0) {
      printf("Misaligned block at %p.\n", bp);
      return -1;
    }

    if (GET(HDRP(bp)) != GET(FTRP(bp))) {
      printf("Header/footer mismatch at %p.\n", bp);
      return -1;
    }

    if (!GET_ALLOC(HDRP(bp))) {
      free_blocks_heap++;
      if (!GET_ALLOC(HDRP(NEXT_BLKP(bp))) && GET_SIZE(HDRP(NEXT_BLKP(bp))) > 0) {
        printf("Uncoalesced neighbors at %p.\n", bp);
        return -1;
      }
    }
  }

  if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
    printf("Bad epilogue block.\n");
    return -1;
  }

  for (int index = 0; index < LISTS; index++) {
    void* free_bp = free_lists[index];
    while (free_bp != NULL) {
      if (GET_ALLOC(HDRP(free_bp))) {
        printf("Allocated block in free list at %p.\n", free_bp);
        return -1;
      }
      if (list_index(GET_SIZE(HDRP(free_bp))) != index) {
        printf("Free list bucket mismatch at %p.\n", free_bp);
        return -1;
      }
      if (NEXT_FREEP(free_bp) != NULL && PREV_FREEP(NEXT_FREEP(free_bp)) != free_bp) {
        printf("Broken next link at %p.\n", free_bp);
        return -1;
      }
      free_blocks_lists++;
      free_bp = NEXT_FREEP(free_bp);
    }
  }

  if (free_blocks_heap != free_blocks_lists) {
    printf("Free block count mismatch: heap=%d list=%d.\n",
           free_blocks_heap, free_blocks_lists);
    return -1;
  }

  return 0;
}

int my_init() {
  for (int i = 0; i < LISTS; i++) {
    free_lists[i] = NULL;
  }

  heap_listp = mem_sbrk(4 * WSIZE);
  if (heap_listp == (void*) - 1) {
    return -1;
  }

  PUT(heap_listp, 0);
  PUT(heap_listp + WSIZE, PACK(DSIZE, 1));
  PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1));
  PUT(heap_listp + 3 * WSIZE, PACK(0, 1));
  heap_listp += 2 * WSIZE;

  return extend_heap(CHUNKSIZE) != NULL ? 0 : -1;
}

void* my_malloc(size_t size) {
  size_t asize;
  size_t extendsize;
  void* bp;

  if (size == 0) {
    return NULL;
  }

  asize = adjust_block_size(size);
  bp = find_fit(asize);
  if (bp != NULL) {
    place(bp, asize);
    return bp;
  }

  extendsize = MAX(asize, CHUNKSIZE);
  bp = extend_heap(extendsize);
  if (bp == NULL) {
    return NULL;
  }

  place(bp, asize);
  return bp;
}

void my_free(void* ptr) {
  size_t size;

  if (ptr == NULL) {
    return;
  }

  size = GET_SIZE(HDRP(ptr));
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
  coalesce(ptr);
}

void* my_realloc(void* ptr, size_t size) {
  size_t asize;
  size_t oldsize;
  size_t next_size;
  void* next_bp;
  void* newptr;

  if (ptr == NULL) {
    return my_malloc(size);
  }
  if (size == 0) {
    my_free(ptr);
    return NULL;
  }

  asize = adjust_block_size(size);
  oldsize = GET_SIZE(HDRP(ptr));

  if (asize <= oldsize) {
    if (oldsize - asize >= MIN_BLOCK_SIZE) {
      PUT(HDRP(ptr), PACK(asize, 1));
      PUT(FTRP(ptr), PACK(asize, 1));
      next_bp = NEXT_BLKP(ptr);
      PUT(HDRP(next_bp), PACK(oldsize - asize, 0));
      PUT(FTRP(next_bp), PACK(oldsize - asize, 0));
      coalesce(next_bp);
    }
    return ptr;
  }

  next_bp = NEXT_BLKP(ptr);
  if (!GET_ALLOC(HDRP(next_bp))) {
    next_size = GET_SIZE(HDRP(next_bp));
    if (oldsize + next_size >= asize) {
      remove_free_block(next_bp);
      PUT(HDRP(ptr), PACK(oldsize + next_size, 1));
      PUT(FTRP(ptr), PACK(oldsize + next_size, 1));
      if (oldsize + next_size - asize >= MIN_BLOCK_SIZE) {
        size_t combined = oldsize + next_size;
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));
        next_bp = NEXT_BLKP(ptr);
        PUT(HDRP(next_bp), PACK(combined - asize, 0));
        PUT(FTRP(next_bp), PACK(combined - asize, 0));
        insert_free_block(next_bp);
      }
      return ptr;
    }
  }

  newptr = my_malloc(size);
  if (newptr == NULL) {
    return NULL;
  }

  oldsize -= OVERHEAD;
  if (size < oldsize) {
    oldsize = size;
  }
  memcpy(newptr, ptr, oldsize);
  my_free(ptr);
  return newptr;
}
