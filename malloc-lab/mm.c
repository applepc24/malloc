#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

// 기본 설정
static void *heap_listp = NULL;
static void *free_listp = NULL;

// 내부 함수 선언
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

// 매크로 정의
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define WSIZE 8
#define DSIZE 16
#define CHUNKSIZE (1<<12)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p)) = (val)
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define PRED(bp) (*(void **)(bp))
#define SUCC(bp) (*(void **)((char *)(bp) + WSIZE))

// 초기 힙 설정
int mm_init(void) {
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *)-1) return -1;

    PUT(heap_listp, 0);                                  // Alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));       // Prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));       // Prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));           // Epilogue header
    heap_listp += (2 * WSIZE);

    void *bp = extend_heap(CHUNKSIZE / WSIZE);
    if (bp == NULL) return -1;
    if (extend_heap(4) == NULL) return -1;

    bp = coalesce(bp);
    free_listp = bp;
    return 0;
}

// 힙 확장
static void *extend_heap(size_t words) {
    char *bp;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    PUT(HDRP(bp), PACK(size, 0));         // Header
    PUT(FTRP(bp), PACK(size, 0));         // Footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // New epilogue header

    return coalesce(bp);
}

// 메모리 해제
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

// 블록 병합
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    void *prev = PREV_BLKP(bp);
    void *next = NEXT_BLKP(bp);

    if (!prev_alloc) {
        remove_free_block(prev);
        size += GET_SIZE(HDRP(prev));
        bp = prev;
    }
    if (!next_alloc) {
        remove_free_block(next);
        size += GET_SIZE(HDRP(next));
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    insert_free_block(bp);
    return bp;
}

// 메모리 할당
void *mm_malloc(size_t size) {
    size_t asize, extendsize;
    char *bp;
    if (size == 0) return NULL;

    asize = (size <= DSIZE) ? (2 * DSIZE) : DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE);

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

// 블록 배치 및 분할
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_free_block(bp);

    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(csize - asize, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, 0));
        PRED(next_bp) = NULL;
        SUCC(next_bp) = NULL;
        insert_free_block(next_bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

// best-fit 가용 블록 탐색
static void *find_fit(size_t asize) {
    void *bp, *best_bp = NULL;
    size_t best_size = (size_t)-1;
    for (bp = free_listp; bp != NULL; bp = SUCC(bp)) {
        size_t bsize = GET_SIZE(HDRP(bp));
        if (asize <= bsize && bsize < best_size) {
            best_size = bsize;
            best_bp = bp;
            if (bsize == asize) break;
        }
    }
    return best_bp;
}

// realloc 구현
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size_t oldsize = GET_SIZE(HDRP(ptr));
    size_t asize = (size <= DSIZE) ? (2 * DSIZE) : DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE);
    if (asize <= oldsize) return ptr;

    void *next = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(next)) && (oldsize + GET_SIZE(HDRP(next))) >= asize) {
        remove_free_block(next);
        size_t newsize = oldsize + GET_SIZE(HDRP(next));
        PUT(HDRP(ptr), PACK(newsize, 1));
        PUT(FTRP(ptr), PACK(newsize, 1));
        return ptr;
    }

    void *newptr = mm_malloc(size);
    if (newptr == NULL) return NULL;
    size_t copySize = oldsize - DSIZE;
    if (size < copySize) copySize = size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}

// 가용 블록 삽입 (free list 맨 앞)
static void insert_free_block(void *bp) {
    if (bp == NULL) return;
    if (bp == free_listp) return;
    SUCC(bp) = free_listp;
    PRED(bp) = NULL;
    if (free_listp != NULL)
        PRED(free_listp) = bp;
    free_listp = bp;
}

// 가용 블록 제거
static void remove_free_block(void *bp) {
    if (PRED(bp) != NULL)
        SUCC(PRED(bp)) = SUCC(bp);
    else
        free_listp = SUCC(bp);
    if (SUCC(bp) != NULL)
        PRED(SUCC(bp)) = PRED(bp);
}
