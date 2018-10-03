/* 
 * Simple, 32-bit and 64-bit clean allocator based on a doubly linked explicit free list,
 * first fit placement, and boundary tag coalescing. 
 *
 * Blocks are aligned to double-word boundaries.  This yields 
 * 8-byte aligned blocks on a 32-bit processor, and 16-byte aligned
 * blocks on a 64-bit processor.  However, 16-byte alignment is stricter
 * than necessary; the assignment only requires 8-byte alignment.  The
 * minimum block size is four words.
 *
 * This allocator uses the size of a pointer, e.g., sizeof(void *), to
 * define the size of a word.  This allocator also uses the standard
 * type uintptr_t to define unsigned integers that are the same size
 * as a pointer, i.e., sizeof(uintptr_t) == sizeof(void *).
 *
 * ANATOMY OF BLOCKS:
 * Free block:		HEADER | PREV FREE | NEXT FREE | OLD DATA | FOOTER
 * Allocated block:	HEADER |--------------DATA----------------| FOOTER
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
	/* Team name */
	"201401403 and 201401439",
	/* First member's full name */
	"Anishi Mehta",
	/* First member's email address */
	"201401439@daiict.ac.in",
	/* Second member's full name (leave blank if none) */
	"Aarushi Sanghani",
	/* Second member's email address (leave blank if none) */
	"201401403@daiict.ac.in"
};

/* Basic constants and macros: */
#define WSIZE      sizeof(void *) /* Word and header/footer size (bytes) */
#define DSIZE      (2 * WSIZE)    /* Doubleword size (bytes) */
#define CHUNKSIZE  (1 << 12)      /* Extend heap by this amount (bytes) */

#define MAX(x, y)  ((x) > (y) ? (x) : (y))  

/* Pack a size and allocated bit into a word. */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p. */
#define GET(p)       (*(uintptr_t *)(p))
#define PUT(p, val)  (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p. */
#define GET_SIZE(p)   (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)  (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer. */
#define HDRP(bp)  ((char *)(bp) - WSIZE)
#define FTRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks. */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given ptr bp in free list, get next and previous ptr in the list. */
/* Since minimum block size is 4 * WSIZE, we can store the address of previous next block in the list through pointers. */
#define GET_NEXT_PTR(bp)  (*(char **)(bp + WSIZE))
#define GET_PREV_PTR(bp)  (*(char **)(bp))

/* Puts pointers in the next and previous elements of free list */
#define SET_NEXT_PTR(bp, qp) (GET_NEXT_PTR(bp) = qp)
#define SET_PREV_PTR(bp, qp) (GET_PREV_PTR(bp) = qp)

/* Global variables: */
static char *heap_listp = 0; /* Pointer to first block */
static char *free_listp = 0;  

/* Function prototypes for internal helper routines: */
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/* Function prototypes for maintaining free list*/
static void insert_in_free_list(void *bp); 
static void remove_from_free_list(void *bp);

/* Function prototypes for heap consistency checker routines: */
static void checkblock(void *bp);
static void checkheap(bool verbose);
static void printblock(void *bp); 

/* 
 * Requires:
 *   None.
 * 
 * Effects:
 *   Initialize the memory manager.  Returns 0 if the memory manager was
 *   successfully initialized and -1 otherwise.
 *	 The initial heap looks like this: 
 * 	 ||PADDING|PROLOGUE HEADER (2 * DSIZE/1)|PROLOGUE PREV PTR (0)|PROLOGUE NEXT PTR (0)|PROLOGUE FOOTER (2 * DSIZE/1)|EPILOGUE (0/1)||
 * 	 Each part is of 8 bytes.
 * 	 EPILOGUE signals the end of the heap.
 * 	 PROLOGUE PREV PTR signals the end of the free list.
 */
int 
mm_init(void) 
{
	/* Create the initial empty heap. */
	if ((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1)
		return (-1);
	PUT(heap_listp, 0);                            		/* Alignment padding */
	PUT(heap_listp + (1 * WSIZE), PACK(2 * DSIZE, 1)); 	/* Prologue header */ 
	PUT(heap_listp + (2 * WSIZE), 0);					/* Prologue previous pointer */
	PUT(heap_listp + (3 * WSIZE), 0);					/*Prologue next pointer */
	PUT(heap_listp + (4 * WSIZE), PACK(2 * DSIZE, 1)); 	/* Prologue footer */ 
	PUT(heap_listp + (5 * WSIZE), PACK(0, 1));     		/* Epilogue header */
	heap_listp += (2 * WSIZE);							/* heap_listp now points to payload of prologue. */
	free_listp = heap_listp;							/* Setting end of free list as prologue. */

	/* Extend the empty heap with a free block of CHUNKSIZE bytes. */
	if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
		return (-1);
	return (0);
}

/* 
 * Requires:
 *   size of memory asked by the programmer.
 *
 * Effects:
 *   Allocate a block with at least "size" bytes of payload, unless "size" is
 *   zero.  Returns the address of this block if the allocation was successful
 *   and NULL otherwise.
 */
void *
mm_malloc(size_t size) 
{
	size_t asize;      /* Adjusted block size */
	size_t extendsize; /* Amount to extend heap if no fit */
	void *bp;

	/* Ignore spurious requests. */
	if (size <= 0)
		return (NULL);

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);

	/* Search the free list for a fit. */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return (bp);
	}

	/* No fit found.  Get more memory and place the block. */
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)  
		return (NULL);
	place(bp, asize);
	return (bp);
} 

/* 
 * Requires:
 *   "bp" is either the address of an allocated block or NULL.
 *
 * Effects:
 *   Free a block and coalesce.
 */
void
mm_free(void *bp)
{
	size_t size;

	/* Ignore spurious requests. */
	if (bp == NULL)
		return;

	/* Free and coalesce the block. */
	size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

/*
 * Requires:
 *   "ptr" is either the address of an allocated block or NULL.
 *
 * Effects:
 *   Reallocates the block "ptr" to a block with at least "size" bytes of payload, unless "size" is zero.  
 *	 If "size" is zero, frees the block "ptr" and returns NULL.  
 * 	 If the block "ptr" is already a block with at least "size" bytes of payload, then "ptr" is returned.
 *   If "size" is more than the size of "ptr" block,
 * 	 if the next block is free, extract space from that if possible, 
 *	 else a new block is allocated and the contents of the old block "ptr" are copied to that new block.
 *   Returns the address of this new block if the allocation was successful and NULL otherwise.
 */
void * 
mm_realloc(void * bp, size_t size)
{
  if((int)size < 0) 
    return NULL; 
  
  else if((int)size == 0){ 
    mm_free(bp); 
    return NULL; 
  } 

else if(size > 0){ 
  	/* If bp is NULL, then this is just malloc. */
    if(bp == NULL) {
		return mm_malloc(size);
	}

    size_t oldsize = GET_SIZE(HDRP(bp)); 
    size_t asize;							/* Valid requested block size */

    /* Aligning and adding overheads. */
    if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);

    /* If newsize is less than or equal to oldsize, return the pointer. */
    if(asize <= oldsize){ 
        return bp; 
    }
    
    /* Now, asize is greater than oldsize */ 
    else { 
        size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); 
        size_t csize = oldsize + GET_SIZE(HDRP(NEXT_BLKP(bp)));
      	/* 
      	 * If next block is free and the size of the two blocks is greater than 
         * or equal to the new size, then we only need to combine both the blocks.  
         */ 
        if(!next_alloc && csize >= asize){ 
        	remove_from_free_list(NEXT_BLKP(bp)); 
            PUT(HDRP(bp), PACK(csize, 1)); 
            PUT(FTRP(bp), PACK(csize, 1)); 
            return bp; 
        }
        /* If it couldn't fit, create a new block. */
        else {  
            void * new_ptr = mm_malloc(size);  
            memcpy(new_ptr, bp, oldsize); 
            mm_free(bp); 
            return new_ptr; 
        } 
    }
}
else 
    return NULL;
} 

/*
 * The following routines are internal helper routines.
 */

/*
 * Requires:
 *   "bp" is the address of a newly freed block.
 *
 * Effects:
 *   Perform boundary tag coalescing. Updates free list accordingly. 
 *	 It removes all the original free blocks under consideration, merges them when applicable,
 *	 and inserts the newly created free block into the list.
 *	 Returns the address of the coalesced block.
 */
static void *
coalesce(void * bp)
{
  	bool NEXT_ALLOC = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  	bool PREV_ALLOC = GET_ALLOC(FTRP(PREV_BLKP(bp)));
 	size_t size = GET_SIZE(HDRP(bp));
  
 	/* If no adjacent blocks are free, add the block to free list and return the pointer. */
 	if(PREV_ALLOC && NEXT_ALLOC){
 		insert_in_free_list(bp);
 		return (bp);
 	}
 	
 	/* Only the next block is free. */   
  	else if (PREV_ALLOC && !NEXT_ALLOC) {                  
    	size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    	remove_from_free_list(NEXT_BLKP(bp));
    	PUT(HDRP(bp), PACK(size, 0));
    	PUT(FTRP(bp), PACK(size, 0));
  	}
  
  	/* Only the previous block is free. */  
  	else if (!PREV_ALLOC && NEXT_ALLOC) {               
    	size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    	bp = PREV_BLKP(bp);
    	remove_from_free_list(bp);
    	PUT(HDRP(bp), PACK(size, 0));
    	PUT(FTRP(bp), PACK(size, 0));
  	}
  
  	/* Both adjacent blocks are free. */ 
  	else if (!PREV_ALLOC && !NEXT_ALLOC) {                
    	size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
    	remove_from_free_list(PREV_BLKP(bp));
    	remove_from_free_list(NEXT_BLKP(bp));
    	bp = PREV_BLKP(bp);
    	PUT(HDRP(bp), PACK(size, 0));
    	PUT(FTRP(bp), PACK(size, 0));
  	}

  	/* Insert the updated freed block into free list. */
  	insert_in_free_list(bp);
  	return bp;
}

/* 
 * Requires:
 *   The number of words by which the heap is to be extended.
 *
 * Effects:
 *   Extend the heap with a free block and return that block's address.
 */
static void * 
extend_heap(size_t words) 
{
	void *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment. */
	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
	
	if ((bp = mem_sbrk(size)) == (void *)-1)  
		return (NULL);

	/* Initialize free block header/footer and the epilogue header. */
	PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
	PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

	/* Coalesce if the previous block was free. */
	return (coalesce(bp));
}

/*
 * Requires:
 *   Size of the block to be found.
 *
 * Effects:
 *   Find a fit for a block with "asize" bytes from the free list. 
 * 	 Returns that block's address or NULL if no suitable block was found. 
 */
static void *
find_fit(size_t asize)
{
	void * bp;

	/* Search for the first fit in the free list. */
	for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = GET_NEXT_PTR(bp)){
		if (asize <= GET_SIZE(HDRP(bp)))
			return (bp);
	}
	/* No fit was found. */
	return (NULL);
}

/* 
 * Requires:
 *   "bp" is the address of a free block that is at least "asize" bytes.
 *
 * Effects:
 *   Place a block of "asize" bytes at the start of the free block "bp" and
 *   split that block if the remainder would be at least the minimum block
 *   size. It removes the original free block from the list and adds the newly created
 *	 free block, and coalesces the newly formed free block, if formed.
 */
static void 
place(void * bp, size_t asize){
  
  size_t csize = GET_SIZE(HDRP(bp));

  /* If the next block would be a valid block, split. */
  if ((csize - asize) >= 4 * WSIZE) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    remove_from_free_list(bp);
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize-asize, 0));
    PUT(FTRP(bp), PACK(csize-asize, 0));
    coalesce(bp);
  }
  /* If the remaining space was too less to form a block, simply place block. */
  else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
    remove_from_free_list(bp);
  }
}

/* 
 * Requires:
 * 	The address "bp" of the block to be inserted.
 * 
 * Effects:
 * 	Inserts the free block pointer from the free list in the LIFO manner.
 * 	The new block will be added to the beginning of the list.
 *  The last element of the free list will be the previous pointer of the prologue.
 */
static void
insert_in_free_list(void * bp){
	/* Updating the pointers. */
  	SET_NEXT_PTR(bp, free_listp); 
  	SET_PREV_PTR(free_listp, bp); 
  	SET_PREV_PTR(bp, NULL); 
  	free_listp = bp; 						// Shifting the list pointer to new first block
}

/* 
 * Requires:
 * 	The address "bp" of the block to be removed.
 * 
 * Effects:
 * 	Removes a block from the free list.
 */
static void
remove_from_free_list(void * bp){
  	if (GET_PREV_PTR(bp))
    	SET_NEXT_PTR(GET_PREV_PTR(bp), GET_NEXT_PTR(bp));
    /* If removing the first block, update list pointer. */
  	else
    	free_listp = GET_NEXT_PTR(bp);
  	SET_PREV_PTR(GET_NEXT_PTR(bp), GET_PREV_PTR(bp));
}

/* 
 * The remaining routines are heap consistency checker routines. 
 */

/*
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Perform a minimal check on the block "bp".
 */
static void
checkblock(void * bp) 
{
	if ((uintptr_t)bp % DSIZE)
		printf("Error: %p is not doubleword aligned\n", bp);
	if (GET(HDRP(bp)) != GET(FTRP(bp)))
		printf("Error: header and footer of block %p do not match\n", bp);

	/* To check if the next and prev pointers for free blocks are within heap bounds. */
	if(GET_ALLOC(bp) == 0)
		if ((void *)GET_NEXT_PTR(bp) < mem_heap_lo() || (void *)GET_NEXT_PTR(bp) > mem_heap_hi())
			printf("Error: next pointer %p is not within heap bounds \n", GET_NEXT_PTR(bp));
		if(GET_PREV_PTR(bp) != NULL)
			if ((void *)GET_PREV_PTR(bp) < mem_heap_lo() || (void *)GET_PREV_PTR(bp) > mem_heap_hi())
				printf("Error: prev pointer %p is not within heap bounds \n", GET_PREV_PTR(bp));
}

/* 
 * Requires:
 *   None.
 *
 * Effects:
 *   Perform a minimal check of the heap for consistency. 
 */
void
checkheap(bool verbose) 
{
	void * bp;
	void * bp2;
	int flag = 0;

	if (verbose)
		printf("Heap (%p):\n", heap_listp);

	/* Checking prologue */
	if (GET_SIZE(HDRP(heap_listp)) != 2 * DSIZE || !GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header\n");
	checkblock(heap_listp);
	printblock(heap_listp);


	for (bp = free_listp; bp!=heap_listp; bp = GET_NEXT_PTR(bp)) {
		/* To check if all blocks in free list are indeed free. */
		if(GET_ALLOC(HDRP(bp))!=0){
			printf("The allocated block %p has been added to the free list\n", bp);
		}

		/* To check coalescing, check if adjacent blocks are allocated. */
		if(GET_ALLOC(PREV_BLKP(bp))!=1 || GET_ALLOC(NEXT_BLKP(bp))!=1)
			printf("The free block %p has escaped coalescing\n", bp);

		/* To check if the pointers of the free blocks point to valid free blocks. */
		if(bp != free_listp){
			if(GET_ALLOC((uintptr_t *)GET_PREV_PTR(HDRP(bp))) !=0 ){
				printf("The previous pointer of %p does not point to a free block\n", bp);
			}
		}
		if(GET_ALLOC((uintptr_t *)GET_NEXT_PTR(HDRP(bp)))!=0){
			printf("The next pointer of %p does not point to a free block\n", bp);
			}
	}
	/* Checking if prologue points to a valid free block. */
	if(heap_listp != free_listp && GET_ALLOC((uintptr_t *)GET_PREV_PTR(HDRP(heap_listp))) != 0){
		printf("The previous pointer of prologue %p does not point to a free block\n", heap_listp);
	}

	/* 
	 * Traversing through heap to check each block.
	 * We start with the first block after the prologue since the previous block for prologue
	 * does not exist. Also, prologue has been checked above. 
	 */
	for(bp = NEXT_BLKP(heap_listp) ; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
		flag=0;

		if (verbose)
			printblock(bp);
		checkblock(bp);
	
		/* 
		 * To check if all the free blocks have been added to the free list. 
		 * Traverse the heap. Once you find a free block, find that block in the free list.
		 * If this was not found, it implies that the free block has not been added.
		 */
		if(GET_ALLOC(HDRP(bp))==0){
			for(bp2 = free_listp; bp2 != heap_listp; bp2 = GET_NEXT_PTR(bp2)){
				if(bp2 == bp){
					flag = 1;
					break;
				}
			}
			if(flag == 0)
				printf("The free block %p has not been added to the free list\n", bp);
		}
	}

	if (verbose)
		printblock(bp);

	if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp)))
		printf("Bad epilogue header\n");
}

/*
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Print the block "bp".
 */
static void
printblock(void * bp) 
{
	bool halloc, falloc;
	size_t hsize, fsize;

	checkheap(false);
	hsize = GET_SIZE(HDRP(bp));
	halloc = GET_ALLOC(HDRP(bp));  
	fsize = GET_SIZE(FTRP(bp));
	falloc = GET_ALLOC(FTRP(bp));  

	if (hsize == 0) {
		printf("%p: end of heap\n", bp);
		return;
	}

	if(halloc == 1)
		printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp, 
	    	hsize, (halloc ? 'a' : 'f'), 
	    	fsize, (falloc ? 'a' : 'f'));
	else
		printf("%p: header: [%zu:%c] prev_ptr: %p next_ptr: %p footer: [%zu:%c]\n", bp, 
	    	hsize, (halloc ? 'a' : 'f'), 
	    	GET_PREV_PTR(bp),
	    	GET_NEXT_PTR(bp),
	    	fsize, (falloc ? 'a' : 'f'));
}

/*
 * The last lines of this file configures the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
