/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static char *heap_listp = NULL;

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
// word size = 4byte
#define DSIZE 8
// double word size = 8byte
#define CHUNKSIZE (1<<12)
// 최대 힙 블록의 바이트 수 (258바이트)
#define MAX(x, y) ((x) > (y)? (x) : (y))
// x > y 이 참이면 x, 거짓이면 y
#define PACK(size, alloc) ((size) | (alloc))
// 힙 블럭의 크기와 가용여부를 비트로 결합
#define GET(p) (*(unsigned int *)(p))
// p의 값을 읽어옴
#define PUT(p, val) (*(unsigned int *)(p)) = (val)
// p의 값을 val 값으로 할당함
#define GET_SIZE(p) (GET(p) & ~0x7)
// 블럭의 메타데이터 빼고 payload의 값만 추출
#define GET_ALLOC(p) ((GET(p) & 0x1))
// 블럭의 메타데이터 값만 추출
#define HDRP(bp) ((char *)(bp) - WSIZE)
// payload의 시작주소를 받아 그 주소의 헤더값을 추출
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
// payload 의 시작주소를 받아 현재 푸터의 값을 추출

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
// 현재 블럭의 다음 블럭 페이로드 주소를 추출
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))
// 현재 블럭의 이전 블럭 페이로드 주소를 추출
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
// 힙영역 만들기
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
    // 만약 heap_listp가 실패한다면
        return -1;
        // 리턴 -1
    }
    PUT(heap_listp, 0);
    // 힙에 0을 넣는다(패딩공간 확보 4바이트)
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
    // 힙리스트 +4 바이트 떨어진 곳에 할당된 블럭을 만든다.(프롤로그 헤더)
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
    // 힙리스트 +8 바이트 떨어진 곳에 할당된 블럭을 만든다.(프롤로그 풋터)
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));
    // 힙리스트 12바이트 떨어진곳에 할당된 더미블럭을 만든다.(에필로그 블럭)
    heap_listp += (2*WSIZE);
    //heap_listp 포인터는 초기 더미 페이로드 주소를 가리킨다

    if(extend_heap(CHUNKSIZE/WSIZE) == NULL){
        return -1;
    }
    // 힙영역을 확장에 실패하면 리턴 1
    return 0;
}

static void *extend_heap(size_t words){
    char *bp;
    size_t size;

    size = (words % 2) ? (words +1) * WSIZE : words * WSIZE;
    // 들어오는 워드 사이즈가 홀수면 1더해줘서 4바이트 곱하고 아니면 그냥 4바이트 곱하기
    if((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }
    // 힙을 사이즈만큼 확장하는데 실패하면 리턴 NULL

    PUT(HDRP(bp), PACK(size, 0));
    //bp 블록에 free로 헤더저장
    PUT(FTRP(bp), PACK(size, 0));
    //bp 블록에 free로 풋터저장
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0 , 1));
    //만들어진 free 블럭 뒤에 새로운 에필로그 헤더를 만듬

    return coalesce(bp);
    //주변의 free 블록들과 병합한 후 최종 포인터를 리턴
}
/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr){
    size_t size = GET_SIZE(HDRP(ptr));
    // 현재 블럭의 사이즈가 가져오기

    PUT(HDRP(ptr), PACK(size, 0));
    // 현재 블럭의 헤더 값을 0으로 변경
    PUT(FTRP(ptr) , PACK(size, 0));
    // 현재 블럭의 풋터 값을 0으로 변경
    coalesce(ptr);
    // 주변의 free 블럭을 확인해서 병합
}

static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    // 현재 블록의 바로 앞 블록이 할당되어있는지 확인함
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    // 현재 블록의 바로 뒤 블록이 할당되어있는지 확인함
    size_t size = GET_SIZE(HDRP(bp));
    // 현재 블록의 사이즈를 구함
    if(prev_alloc && next_alloc){
        return bp;
    // 앞뒤 블럭이 전부 사용중이면 현재 블럭 주소 리턴
    }else if(prev_alloc && !next_alloc){
        // 만약 뒤 블록이 free 블록이라면
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        // 현재 블럭 사이즈에 다음 블럭 사이즈를 더함
        PUT(HDRP(bp), PACK(size, 0));
        // 현재 블록 헤더에 더해진 사이즈의 정보를 저장
        PUT(FTRP(bp), PACK(size, 0));
        // 현재 블록 풋터에 더해진 사이즈의 정보를 저장
    }else if(!prev_alloc && next_alloc){
        // 만약 앞 블럭이 free 블럭이라면
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        // 현재 사이즈 앞 블럭 사이즈랑 현제 사이즈를 더함
        PUT(FTRP(bp), PACK(size, 0));
        // 현재 블록 풋터에 병합된 크기(size)를 저장
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        //현재 앞블록의 헤더에 병합된 크기를 저장
        bp = PREV_BLKP(bp);
        // bp 포인터를 앞 블럭으로 옮김
    }else{
        //앞뒤 블럭 전부 사용가능 하다면
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        // 현재 사이즈에 앞쪽 블록 사이즈와 뒤쪽 사이즈를 더한다.
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        // 앞 블럭 헤더값에 앞뒤가 더해진 현재 사이즈를 저장
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        // 뒤쪽 블럭 풋터 값을 앞뒤가 더해진 현재 사이즈로 저장
        bp= PREV_BLKP(bp);
        // bp 포인터를 앞쪽 블럭으로 이동
    }
    return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */

void *mm_malloc(size_t size){
    size_t asize;
    //실제로 힙에 할당해야 하는 사이즈
    size_t extendsize;
    // free 블록을 못찾았을때 새로 요청할 크기
    char *bp;

    if(size == 0){
        return NULL;
    }
    // 만약 사용자가 0을 요청하면 NULL 반환
    if(size <= DSIZE){
        asize = 2*DSIZE;
        // 만약 요청 사이즈가 8바이트보다 작으면 실제 사이즈는 16바이트
    }else{
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1))/ DSIZE);
        // 8바이트 보다 클때 8의 배수 처리
    }

    if((bp = find_fit(asize)) != NULL){
        // 만약 실제 사이즈에 맞는 힙 공간을 찾으면
        place(bp, asize);
        // 실제 사이즈에 맞게 블럭 조정
        return bp;
        // bp 포인터 리턴
    }

    extendsize = MAX(asize, CHUNKSIZE);
    //요청 크기(asize)와 기본 확장 단위(CHUNKSIZE) 중 더 큰 값을 선택해서 힙 확장 크기로 설정
    if((bp = extend_heap(extendsize/WSIZE)) == NULL){
    //extendsize 만큼 확장을 실패하면
        return NULL;
        // 리턴 NULL
    }
    place(bp, asize);
    // 확장에 성공하면 블럭 분할해서 저장
    return bp;
    //현재 블럭 포인터 리턴
}

static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));
    // csize는 bp가 가리키는 현재 블럭의 전체사이즈

    if((csize - asize) >= (2*DSIZE)) {
    // 만약 (현재 블럭 사이즈 - 요청 사이즈)가 16바이트보다 크거나 같으면
        PUT(HDRP(bp), PACK(asize , 1));
        // 현재 블록헤더에 사이즈를 요청 사이즈로 변경
        PUT(FTRP(bp), PACK(asize , 1));
        // 현재 블록풋터에 사이즈 요청 사이즈 로 변경
        bp = NEXT_BLKP(bp);
        // 포인터는 다음 블록 페이로드 주소를 가리킴
        PUT(HDRP(bp), PACK(csize-asize, 0));
        // 다음 블록 헤더에 사이즈를 전에 자르고 남은 사이즈로 바꿈
        PUT(FTRP(bp), PACK(csize-asize, 0));
        // 다음 블록 푸터에 사이즈를 전에 자르고 남은 사이즈로 바꿈
    }else{
        //만약 남는 공간 16비트보다 작아서 쪼갤 수 없으면
        PUT(HDRP(bp), PACK(csize, 1));
        // 블록 전체 크기를 그냥 사용(헤더에 사이즈 저장)
        PUT(FTRP(bp), PACK(csize, 1));
        // (풋터에 저장)
    }
}
static void *find_fit(size_t asize){
    //요청 사이즈에 맞는 블럭을 찾는 함수
    void *bp;

    for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp= NEXT_BLKP(bp)){
        // 힙리스트 처음부터 크기가 0인 에필로그 블록을 만날때까지 다음 블록으로 이동
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
        // 만약 블록이 free이거나 요청 사이즈가 현재 블록 사이즈보다 같거나 작으면
            return bp;
            //현재 포인터 반환
        }
    }
    return NULL;
    //맞는 공간을 못찾으면 리턴 NULL
}

void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}