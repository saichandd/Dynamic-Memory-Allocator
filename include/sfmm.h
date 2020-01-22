/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#ifndef SFMM_H
#define SFMM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/*

                                 Format of an allocated memory block
    +-----------------------------------------------------------------------------------------+
    |                                       64-bits wide                                      |
    +-----------------------------------------------------------------------------------------+

    +---------------------------------------------------------------------+---------+---------+ <- header
    |                                       block_size                    |  alloc  |prv alloc|
    |                                  (2 LSB's implicitly 0)             |   (1)   |  (0/1)  |
    |                                         64 bits                     |  1 bit  |  1 bit  |
    +---------------------------------------------------------------------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                   Payload and Padding                                   |
    |                                     (N Memory Rows)                                     |
    |                                                                                         |
    |                                                                                         |
    +--------------------------------------------+------------------------+---------+---------+ <- footer
    |                                       block_size                    |  alloc  |prv alloc|
    |                                  (2 LSB's implicitly 0)             |   (1)   |  (0/1)  |
    |                                         64 bits                     |  1 bit  |  1 bit  |
    +---------------------------------------------------------------------+---------+---------+

    NOTE: Footer contents must always be identical to header contents XOR'ed with sf_magic().
*/

/*
                                     Format of a free memory block
    +---------------------------------------------------------------------+---------+---------+ <- header
    |                                       block_size                    |  alloc  |prv alloc|
    |                                  (2 LSB's implicitly 0)             |   (0)   |  (0/1)  |
    |                                         64 bits                     |  1 bit  |  1 bit  |
    +--------------------------------------------+------------------------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                Pointer to next free block                               |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                               Pointer to previous free block                            |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                                         Unused                                          |
    |                                     (N Memory Rows)                                     |
    |                                                                                         |
    |                                                                                         |
    +--------------------------------------------+------------------------+---------+---------+ <- footer
    |                                       block_size                    |  alloc  |prv alloc|
    |                                  (2 LSB's implicitly 0)             |   (0)   |  (0/1)  |
    |                                         64 bits                     |  1 bit  |  1 bit  |
    +---------------------------------------------------------------------+---------+---------+

    NOTE: Footer contents must always be identical to header contents XOR'ed with sf_magic().
*/

#define PREV_BLOCK_ALLOCATED  0x1
#define THIS_BLOCK_ALLOCATED  0x2
#define BLOCK_SIZE_MASK 0xfffffffc

typedef size_t sf_header;
typedef size_t sf_footer;

/* Structure of a block. */
typedef struct sf_block {
    sf_footer prev_footer;  // NOTE: This actually belongs to the *previous* block.
    sf_header header;
    union {
        /* A free block contains links to other blocks in a free list. */
        struct {
            struct sf_block *next;
            struct sf_block *prev;
        } links;
        /* An allocated block contains a payload (aligned), starting here. */
        char payload[0];   // Length varies according to block size.
    } body;
} sf_block;

/*
 * The heap is designed to keep the payload area of each block aligned to a double row (16-byte)
 * boundary.  The header of a block precedes the payload area, and is only single-row (8-byte)
 * aligned.  The heap starts with a "prologue" that consists of padding (to achieve the desired
 * alignment) and an allocated block with just a header and a footer and a minimum-size payload
 * area (which is unused).  The heap ends with an "epilogue" that consists only of an allocated
 * header.  The prologue and epilogue are never freed, and they serve as sentinels that eliminate
 * edge cases in coalescing that would otherwise have to be treated.
 */

/*
                                         Format of the heap
    +-----------------------------------------------------------------------------------------+
    |                                       64-bits wide                                      |
    +-----------------------------------------------------------------------------------------+

                                                                                                   heap start
    +-----------------------------------------------------------------------------------------+ <- (aligned)
    |                                                                                         |
    |                                         unused                                          | padding
    |                                         64 bits                                         |
    +--------------------------------------------+------------------------+---------+---------+
    |                                     block_size (= 16)               |  alloc  |prv alloc| prologue
    |                                  (2 LSB's implicitly 0)             |   (1)   |   (1)   | header
    |                                         64 bits                     |  1 bit  |  1 bit  |
    +--------------------------------------------+------------------------+---------+---------+ <- (aligned)
    |                                                                                         |
    |                                         unused                                          | padding
    |                                         64 bits                                         |
    +--------------------------------------------+--------------------------------------------+
    |                                                                                         |
    |                                         unused                                          | padding
    |                                         64 bits                                         |
    +--------------------------------------------+------------------------+---------+---------+ <- (aligned)
    |                                     block_size (= 16)               |  alloc  |prv alloc|
    |                                  (2 LSB's implicitly 0)             |   (1)   |   (1)   | prologue
    |                                         64 bits                     |  1 bit  |  1 bit  | footer
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    |                                 Allocated and free blocks                               |
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    +---------------------------------------------------------------------+---------+---------+
    |                                     block_size (= 0)                |  alloc  |prv alloc|
    |                                  (2 LSB's implicitly 0)             |   (1)   |  (0/1)  | epilogue
    |                                         64 bits                     |  1 bit  |  1 bit  |
    +---------------------------------------------------------------------+---------+---------+ <- heap end
                                                                                                   (aligned)
    NOTE: Prologue footer contents must always be identical to prologue header contents XOR'ed
    with sf_magic().
*/

/* Structure of the prologue. */
typedef struct sf_prologue {
    sf_footer padding1;
    sf_header header;
    void *unused1;
    void *unused2;
    sf_footer footer;
} sf_prologue;

/* Structure of the epilogue. */
typedef struct sf_epilogue {
    sf_header header;
} sf_epilogue;

/*
 * Free blocks are maintained in a set of circular, doubly linked lists, segregated by
 * size class.  The first list holds blocks of the minimum size M.  The second list holds
 * blocks of size 2M.  The third list holds blocks whose size is in the interval (2M, 4M].
 * The fourth list holds blocks whose size is in the interval (4M, 8M], and so on.
 * This continues up to the interval (64M, 128M], and then the last list holds all blocks
 * of size greater than 128M.
 *
 * Each of the circular, doubly linked lists has a "dummy" block used as the list header.
 * This dummy node is always linked between the last and the first element of the list.
 * In an empty list, the next and free pointers of the list header point back to itself.
 * In a list with something in it, the next pointer of the header points to the first node
 * in the list and the previous pointer of the header points to the last node in the list.
 * The header itself is never removed from the list and it contains no data (only the link
 * fields are used).  The reason for doing things this way is to avoid edge cases in insertion
 * and deletion of nodes from the list.
 */

#define NUM_FREE_LISTS 9
struct sf_block sf_free_list_heads[NUM_FREE_LISTS];

/* sf_errno: will be set on error */
int sf_errno;

/*
 * This is your implementation of sf_malloc. It acquires uninitialized memory that
 * is aligned and padded properly for the underlying system.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If size is 0, then NULL is returned without setting sf_errno.
 * If size is nonzero, then if the allocation is successful a pointer to a valid region of
 * memory of the requested size is returned.  If the allocation is not successful, then
 * NULL is returned and sf_errno is set to ENOMEM.
 */
void *sf_malloc(size_t size);

/*
 * Resizes the memory pointed to by ptr to size bytes.
 *
 * @param ptr Address of the memory region to resize.
 * @param size The minimum size to resize the memory to.
 *
 * @return If successful, the pointer to a valid region of memory is
 * returned, else NULL is returned and sf_errno is set appropriately.
 *
 *   If sf_realloc is called with an invalid pointer sf_errno should be set to EINVAL.
 *   If there is no memory available sf_realloc should set sf_errno to ENOMEM.
 *
 * If sf_realloc is called with a valid pointer and a size of 0 it should free
 * the allocated block and return NULL without setting sf_errno.
 */
void *sf_realloc(void *ptr, size_t size);

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */
void sf_free(void *ptr);

/* sfutil.c: Helper functions already created for this assignment. */

/*
 * Any program using the sfmm library must call this function ONCE
 * before issuing any allocation requests. This function DOES NOT
 * allocate any space to your allocator.
 */
void sf_mem_init();

/*
 * Any program using the sfmm library must call this function ONCE
 * after all allocation requests are complete. If implemented cleanly,
 * your program should have no memory leaks in valgrind after this function
 * is called.
 */
void sf_mem_fini();

/*
 * This function increases the size of your heap by adding one page of
 * memory to the end.
 *
 * @return On success, this function returns a pointer to the start of the
 * additional page, which is the same as the value that would have been returned
 * by get_heap_end() before the size increase.  On error, NULL is returned
 * and sf_errno is set to ENOMEM.
 */
void *sf_mem_grow();

/* The size of a page of memory returned by sf_mem_grow(). */
#define PAGE_SZ 4096

/*
 * @return The starting address of the heap for your allocator.
 */
void *sf_mem_start();

/*
 * @return The ending address of the heap for your allocator.
 */
void *sf_mem_end();

/*
 * @return The "magic number" used to obfuscate footer contents to make it
 * difficult to free a block without having first succesfully malloc'ed one.
 */
uint64_t sf_magic();

/*
 * Display the contents of the heap in a human-readable form.
 */
void sf_show_block(sf_block *bp);
void sf_show_blocks();
void sf_show_free_list(int index);
void sf_show_free_lists();
void sf_show_heap();

#endif
