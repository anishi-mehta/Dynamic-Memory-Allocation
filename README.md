# DYNAMIC MEMORY ALLOCATION

Aarushi Sanghani 	: 201401403
Anishi Mehta		: 201401439


BASIC APPROACH: 

We have used the approach of an explicit free list. We maintain a doubly linked list of all the free blocks of the heap. The pointers for the next and the previous blocks of the list are added to the payload of the free blocks themselves. The free list saves on the time taken to look for a desired free block as the traversal only takes place among all the free blocks. An implicit free list implementation of malloc requires traversal through all the blocks present in the heap, allocated and free, which results in a longer time. Thus, explicit free list improves the throughput considerably.


IMPLEMENTATION:

 1. STRUCTURE OF BLOCKS:

    Free block:		    HEADER | PREV FREE | NEXT FREE | OLD DATA | FOOTER
    Allocated block:	HEADER |--------------DATA----------------| FOOTER

    As the linked list pointers are added to the payload of the free blocks, the minimum size of a single block increases by (2 * WSIZE).
    Thus, the minimum block size to create a valid block must be (4 * WSIZE).

 2. FREE LIST FUNCTIONS:

 	Four macros are added to access and modify the pointers of the free blocks.
 	We added two functions, insert_into_free_list() and remove_from_free_list(). These functions are used to perform basic doubly linked list operations of inserting into and removing blocks from the list. We follow LIFO mode of insertion.

 3. MM_INIT():

 	The initial heap looks like this: 
 	|| PADDING | PROLOGUE HEADER (2 * DSIZE/1) | PROLOGUE PREV PTR (0) | PROLOGUE NEXT PTR (0) | PROLOGUE FOOTER (2 * DSIZE/1) | EPILOGUE (0/1) ||

 	Initial heap size: (6 * WSIZE)

 	The free_listp is initialised with the payload of the prologue. We follow LIFO mode of insertion and thus, the prologue will always be the last entry in the free list.
 	Although the prologue is an allocated block, the previous and next pointers for the free list have been added to it. This is because when a free block is added to an empty free list, the previous pointer of the prologue would change. Had these pointers not been added, this would corrupt the data present in the footer of the prologue.

 4. MM_REALLOC():

 	When the requested reallocated size is less than the old size of the block, we simply return the pointer to the block without changing anything. This reduces external fragmentation than splitting the block, and hence improves memory utilisation.
 	When the requested size is more than the old size, we check if the next block is a free block. If this was found to be true, and total size is more than the requested size, then these are merged to form a single block. However, when the next block is allocated, a new malloc is called, the data of the old block is copied to the new block and the old block is freed.

 5. FIND_FIT():

 	To look for a suitable free block, the traversal takes place directly in the free list and not the entire heap, as was the case before. This improves search time and in turn, throughput.

 6. CHECKHEAP():

 	This functions checks the heap for the following inconsistencies that might have occured during execution.

 	a. Checks if all blocks in free list are indeed free.
 	b. Checks coalescing i.e. if adjacent blocks are allocated.
 	c. Checks if all the free blocks have been added to the free list
 	d. Checks if the pointers of the free blocks point to valid free blocks.

 7. CHECKBLOCK():

 	This functions checks if the pointers of the free block lie within the heap bounds.
