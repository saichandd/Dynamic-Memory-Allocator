/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

size_t getBlockSize(size_t size);
int getIndexFromBlockSize(size_t block_size);
void InsertToFreeList(sf_block *block, size_t block_size);
void initilizeFreeListArr();
sf_block * GetBlockFromFreeLists(size_t block_size);
sf_block *SplitAndAddToListsIfPossible(sf_block *block, size_t block_size);
sf_block *growMemory(size_t size);
void clearBlockFromFL(sf_block *block);

sf_block * LeftCoal(sf_block *block);
sf_block *RightCoal(sf_block *block,size_t block_size);

void setPrevBlockAllocatedToNoInNextBlock(sf_block *block);
int prevallocZeroButAllocInHeadNotZero(sf_block* block);

void *sf_realloc(void *pp, size_t rsize) {

    debug("FREEEEEE-------------------------");
    //check if ptr is valid
    if(pp == NULL){
        sf_errno = EINVAL;
        return NULL;
    }
    sf_block * block = (sf_block *)(pp - 16);


    //allocated bit no set
    if((block->header & THIS_BLOCK_ALLOCATED) == 0){
        sf_errno = EINVAL;
        return NULL;
    }

    //Size not VALID
    size_t block_size = block->header & BLOCK_SIZE_MASK;
    if(block_size < 32){
        sf_errno = EINVAL;
        return NULL;
    }
    // debug("--");

    sf_footer *block_footer= (sf_footer*)((void*)block + block_size);
    //footer not same as header
    if((*block_footer ^ sf_magic()) != block->header ){
        sf_errno = EINVAL;
        return NULL;
        // return;
    }

    //PREV BLOCK AND OTHER ERRORS
    size_t prev_block_size = (block->prev_footer^sf_magic()) & BLOCK_SIZE_MASK;
    sf_block *prev_block = (sf_block*)((void*)block - prev_block_size);


    if(pp < ((void*)sf_mem_start()+40)){
        sf_errno = EINVAL;
        return NULL;
    }

    if((block->header & PREV_BLOCK_ALLOCATED) == 0 && ((prev_block->header & THIS_BLOCK_ALLOCATED) !=0)){
        sf_errno = EINVAL;
        return NULL;
    }

    if((sf_mem_end() - 8) <= ((void*)block+block_size)){
        sf_errno = EINVAL;
        return NULL;
    }

    //same size retrun sae

    // The prev_alloc field is 0, indicating that the previous block is free,
    // but the alloc field of the previous block header is not 0
    if(prevallocZeroButAllocInHeadNotZero(block) == 1){
        sf_errno = EINVAL;
        return NULL;
    }

    //Errors end



    //HERE IT BEGINS PROPERLY
    //is rsize == 0
    if(rsize == 0){
        sf_free(pp);
        return NULL;
    }

    if(rsize == block_size-16){
        return pp;
    }

    //HERE IS IT
    if(rsize > block_size-16){
        void* new_block = sf_malloc(rsize);
        if(new_block == NULL){
            return NULL;
        }
        memcpy(new_block, pp, block_size-16);
        sf_free(pp);
        return new_block;
    }
    else{
        //rsize <= block_size-16
        return (void *)SplitAndAddToListsIfPossible(block, getBlockSize(rsize)) + 2*(sizeof(sf_header));
    }

    return NULL;

}

void sf_free(void *pp){

    debug("FREEEEEE-------------------------");
    //check if ptr is valid
    if(pp == NULL){
        abort();
        return;
    }

    if(pp-8 < ((void *)sf_mem_start()+40)){
        abort();
        return;
    }

    sf_block * block = (sf_block *)(pp - 16);

    //Allocated bit is 0
    if((block->header & THIS_BLOCK_ALLOCATED) == 0){
        abort();
        return;
    }

    //Size not VALID
    size_t block_size = block->header & BLOCK_SIZE_MASK;
    if(block_size < 32){
        abort();
        return;
    }

    sf_footer *block_footer= (sf_footer*)((void*)block + block_size);
    //footer not same as header
    if((*block_footer ^ sf_magic()) != block->header ){
        abort();
        return;
    }

    // The prev_alloc field is 0, indicating that the previous block is free,
    // but the alloc field of the previous block header is not 0
    if(prevallocZeroButAllocInHeadNotZero(block) == 1){
        abort();
        return;
    }

    //header is before prologue or footer after

    //PREV BLOCK AND OTHER ERRORS
    size_t prev_block_size = (block->prev_footer^sf_magic()) & BLOCK_SIZE_MASK;
    sf_block *prev_block = (sf_block*)((void*)block - prev_block_size);

    if (((void *)sf_mem_end() - 8) <= (((void *)block + block_size))) {
        abort();
        return;
    }


    if(((block->header & PREV_BLOCK_ALLOCATED)==0) && ((prev_block->header & THIS_BLOCK_ALLOCATED)!=0)){
        abort();
        return;
    }

    if(block->header != ((*(sf_footer *)((void *)block + block_size))^sf_magic())){
        abort();
        return;
    }


    // block->header = block->header & (BLOCK_SIZE_MASK | PREV_BLOCK_ALLOCATED);
    block->header &= ~0x02;
    //change block footer
    *(sf_footer *)((void *)block + block_size) = block->header ^ sf_magic();



    //set next block header and footer properly
    setPrevBlockAllocatedToNoInNextBlock(block);

    // sf_show_heap();
    block = LeftCoal(block);
    // sf_show_heap();
    block = RightCoal(block, (block->header & BLOCK_SIZE_MASK));
    // sf_show_heap();

    block_size = block->header & BLOCK_SIZE_MASK;
    InsertToFreeList(block, block_size);
    // sf_show_heap();

    debug("END FREEE-----------------------");
}


int prevallocZeroButAllocInHeadNotZero(sf_block* block){
    sf_footer prev_footer = *(sf_footer*)((void*)block);
    size_t prev_block_size = (prev_footer^sf_magic()) & BLOCK_SIZE_MASK;

    sf_header prev_header = *(sf_header*)((void*)block-prev_block_size+8);

    if(((prev_footer^sf_magic()) & THIS_BLOCK_ALLOCATED) != (prev_header & THIS_BLOCK_ALLOCATED)){
        return 1;
    }
    return 0;
}

void setPrevBlockAllocatedToNoInNextBlock(sf_block *block){
    size_t block_size = block->header & BLOCK_SIZE_MASK;

    sf_block * next_block = (sf_block*)((void*)block + block_size);;
    size_t next_block_size = next_block->header & BLOCK_SIZE_MASK;

    // next_block->header = next_block->header & (BLOCK_SIZE_MASK | THIS_BLOCK_ALLOCATED);
    next_block->header &= ~0x1;
    if(next_block_size != 0){
        *(sf_footer *)((void *)next_block + next_block_size) = next_block->header ^ sf_magic();
    }

    return;
}

sf_block *RightCoal(sf_block *block,size_t block_size){

    sf_block * next_block = (sf_block*)((void*)block + block_size);

    debug("next block allocated %ld", (next_block->header & THIS_BLOCK_ALLOCATED));
    if((next_block->header & THIS_BLOCK_ALLOCATED) == 2){
        debug("next is allocated");
        return block;
    }
    debug("--");
    clearBlockFromFL(next_block);
    sf_block *new_block = block;
    size_t new_block_size = block_size+(next_block->header & BLOCK_SIZE_MASK);
    new_block->prev_footer = block->prev_footer;
    new_block->header = new_block_size;

    if(((new_block->prev_footer ^sf_magic()) & THIS_BLOCK_ALLOCATED ) == 2)
        new_block->header |= PREV_BLOCK_ALLOCATED;

    *(sf_footer *)((void *)new_block + new_block_size) = new_block->header ^ sf_magic();

    return RightCoal(new_block, new_block_size);
}

sf_block *LeftCoal(sf_block *block){

    size_t prev_block_allocated = (block->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED;
    if(prev_block_allocated == 2){

        // sf_show_block(block);
        debug("prev is allocated");
        // block->header =
        //returning the given block
        return block;
    }

    debug("--");

    size_t prev_block_size = (block->prev_footer ^ sf_magic()) & BLOCK_SIZE_MASK;
    sf_block *new_block = (sf_block *)((void*)block - prev_block_size); //DONT KNOW IF THIS WORKS BECAUSE OF POINTER TYPES

    clearBlockFromFL(new_block);

    // size_t block_size = block->header & BLOCK_SIZE_MASK;
    // debug("size of try to left coal %lu", block_size);
    //check prev_footer is free

    size_t new_block_size = prev_block_size + (block->header & BLOCK_SIZE_MASK);
    new_block->header = new_block_size;
    new_block->header |= PREV_BLOCK_ALLOCATED;
    *(sf_footer *)((void *)new_block + new_block_size) = new_block->header ^ sf_magic();

    return LeftCoal(new_block);

}

void *sf_malloc(size_t size) {

    if(size == 0){
        return NULL;
    }

    size_t block_size = 0;

    //determine actual size for block

    debug("%s -- %lu", "Required size",size);
    block_size = getBlockSize(size);

    //first time(initilize heap)
    debug("%lu", block_size);
    if(sf_mem_start() == sf_mem_end()){

        debug("first time?");
        void *grow_ptr = sf_mem_grow();
        // if no memory
        // return

        //PROLOGUE
        sf_prologue *prologue = grow_ptr;
        prologue->header = 32;
        prologue->header = prologue->header | PREV_BLOCK_ALLOCATED;
        prologue->header = prologue->header | THIS_BLOCK_ALLOCATED;
        prologue->footer = prologue->header ^ sf_magic();

        //EPILOGUE
        sf_epilogue *epilogue = (sf_epilogue*)(sf_mem_end()-8);
        epilogue->header = 0;
        epilogue->header = epilogue->header | THIS_BLOCK_ALLOCATED;

        //rest of new memory
        sf_block *block_piece = (sf_block *)&(prologue->footer);
        block_piece->header = 4096-40-8; //4048
        block_piece->header = block_piece->header | PREV_BLOCK_ALLOCATED;
        *(sf_footer *)((void *)block_piece + 4048) = block_piece->header ^ sf_magic();

        debug("before Inserting to FL");
        initilizeFreeListArr();
        InsertToFreeList(block_piece, 4096-40-8);
    }
    // sf_show_heap();
    // get for free list to return
    sf_block *free_block = GetBlockFromFreeLists(block_size);

    debug("got free block");
    // sf_show_heap();

    if(free_block == NULL){

        return NULL;
    }
    // sf_show_heap();
    //split and return the right thing
    sf_block *assign_block = SplitAndAddToListsIfPossible(free_block, block_size);
    // sf_show_heap();
    return (void*) (assign_block)+16;
}

sf_block *SplitAndAddToListsIfPossible(sf_block *block, size_t block_size){

    size_t um_block_header = block->header & BLOCK_SIZE_MASK;
    debug("%ld - %ld",um_block_header, block_size);
    // sf_show_heap();

    //SHOULD CHANGE THIS
    if(um_block_header - block_size < 32){

        //go to next block and set PREV_BLOCK_ALLOCATED
        sf_block *next_block = (sf_block *)((void *)block + um_block_header);
        size_t um_next_block_size = next_block->header & BLOCK_SIZE_MASK;

        next_block->header = next_block->header | PREV_BLOCK_ALLOCATED;
        debug("prev block allocated");
        if(um_next_block_size != 0){
            debug("Not epilogue");
            *(sf_footer *)((void *)next_block + um_next_block_size) = next_block->header ^ sf_magic();
        }

        //change this block
        block->header = block->header | THIS_BLOCK_ALLOCATED;
        *(sf_footer *)((void *)block + um_block_header) = block->header ^ sf_magic();
        // you can check the below line, it should be allocated
        // block->header = block->header | PREV_BLOCK_ALLOCATED;
        return block;
    }

    sf_block *left_block = block;
    left_block->header = block_size;

    //Is previous block always allocated ?
    left_block->header = left_block->header | THIS_BLOCK_ALLOCATED;
    if(((left_block->prev_footer ^sf_magic()) & THIS_BLOCK_ALLOCATED ) == 2)
        left_block->header |= PREV_BLOCK_ALLOCATED;

    *(sf_footer *)((void *)left_block + block_size) = left_block->header ^ sf_magic();


    sf_block *right_block = (sf_block *)((void *)left_block + block_size);
    right_block->header = um_block_header - block_size;
    right_block->header = right_block->header | PREV_BLOCK_ALLOCATED;
    *(sf_footer *)((void *)right_block + um_block_header - block_size) = right_block->header ^ sf_magic();


    debug("--");
    sf_block * to_coal;
    debug("--");
    to_coal = LeftCoal(right_block);
    debug("--");
    to_coal = RightCoal(to_coal, to_coal->header & BLOCK_SIZE_MASK);
    debug("--");
    InsertToFreeList(to_coal, to_coal->header & BLOCK_SIZE_MASK);

    return left_block;
}

sf_block * GetBlockFromFreeLists(size_t block_size){

    debug("get blocks from free lists ------------------------");
    // sf_show_heap();
    // int nothing_free = 0;
    int index = getIndexFromBlockSize(block_size);
    sf_block *block;
    int got_block = 0;
    while(index<NUM_FREE_LISTS){
        //empty list
        if(sf_free_list_heads[index].body.links.next == &sf_free_list_heads[index]){
            // debug("%d", index);
            index++;
        }
        else{
            sf_block *blk = sf_free_list_heads[index].body.links.next;

            while(blk != &sf_free_list_heads[index]){

                size_t um_blk_header = blk->header & BLOCK_SIZE_MASK;
                if(um_blk_header >= block_size){
                    //block size big enough
                    got_block = 1;
                    block = blk;
                    break;
                }
                blk = blk->body.links.next;
            }
            //if block is found, break
            if(got_block == 1){
                break;
            }
            else{
                index++;
            }
        }
    }
    //exhausted all the lists, get new memory
    if(got_block == 0){

        debug("exhausted");
        // void *present_end = sf_mem_end();
        // debug("block_size required %lu", block_size);

        // debug("--");
        block = growMemory(block_size);

        if(block == NULL){
            return NULL;
        }

    }

    else{
        // debug("in elsee");
        // sf_show_heap();
        // sf_show_block(block);
        // sf_block *block = sf_free_list_heads[index].body.links.next;
        // sf_block *second_block = block->body.links.next;
        // sf_free_list_heads[index].body.links.next = second_block;
        // second_block->body.links.prev = &sf_free_list_heads[index];

        clearBlockFromFL(block);
    }

    // sf_show_heap();
    debug("returinign blocks");
    return block;
}


void clearBlockFromFL(sf_block *block){

    sf_block *prev_block = block->body.links.prev;
    sf_block *next_block = block->body.links.next;
    prev_block->body.links.next = next_block;
    next_block->body.links.prev = prev_block;
}

sf_block *growMemory(size_t size){

    sf_block *new_block;
    sf_block *full_block;
    int not_enough = 0;

    while(not_enough == 0){

        debug("called");
        void * prev_end = sf_mem_end();
        void * new_mem = sf_mem_grow();

        if(new_mem == NULL){
            InsertToFreeList(full_block, full_block->header & BLOCK_SIZE_MASK);
            sf_errno = ENOMEM;
            return NULL;
        }

        debug("--");
        //Create Epilogue
        sf_epilogue *epilogue = sf_mem_end()-8;
        epilogue->header = 0;
        epilogue->header = epilogue->header | THIS_BLOCK_ALLOCATED;

        debug("--");
        //Initilize free_block
        new_block = (sf_block*)(prev_end -8-8); //8 for epilogue and 8 for footer
        new_block->header = (4096);
        if(((new_block->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED) == 2){
            new_block->header |= PREV_BLOCK_ALLOCATED;
        }
        sf_footer *new_footer = (sf_footer *)((void *)new_block + 4096);
        *new_footer = new_block->header ^ sf_magic();
        // sf_show_heap();

        debug("-- %lu", new_block->header);
        // coal free_block with left_block and take that block
        full_block = LeftCoal(new_block);

        debug("--x");
        if((full_block->header & BLOCK_SIZE_MASK) >= size){
            //satistfied with stize
            debug("Enough size");
            not_enough = 1;
        }

    }

    return full_block;
}


void InsertToFreeList(sf_block *block, size_t block_size){
    debug("InsertToFreeList");
    int index = getIndexFromBlockSize(block_size);

    sf_block *old_first = sf_free_list_heads[index].body.links.next;
    // sf_block *old_last = sf_free_list_heads[index].body.links.next;
    old_first->body.links.prev = block;

    sf_free_list_heads[index].body.links.next = block;

    block->body.links.prev = &sf_free_list_heads[index];
    block->body.links.next = old_first;

    // sf_show_free_lists();
    debug("--");
    return;
}

void initilizeFreeListArr(){
    for(int i = 0; i< 9; i++){
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }
}

// void initilizeProAndEpi(){

// }

int getIndexFromBlockSize(size_t block_size){

    int index = -1;
    int M = 32;
    if(block_size == 32){
        return 0;
    }
    else if(block_size > M && block_size <= 2*M){
        return 1;
    }
    else if(block_size > 2*M && block_size <= 4*M){
        return 2;
    }
    else if(block_size > 4*M && block_size <= 8*M){
        return 3;
    }
    else if(block_size > 8*M && block_size <= 16*M){
        return 4;
    }
    else if(block_size > 16*M && block_size <= 32*M){
        return 5;
    }
    else if(block_size > 32*M && block_size <= 64*M){
        return 6;
    }
    else if(block_size > 64*M && block_size <= 128*M){
        return 7;
    }
    else{
        return 8;
    }

    return index;
}

size_t getBlockSize(size_t size){
    //size should be multiple of 16
    size_t block_size = 8+8;

    while(1){
        if(size <= 16){
            block_size += 16;
            break;
        }
        block_size += 16;
        size -= 16;
    }

    return block_size;
}

// void sf_free(void *pp) {
//     return;
// }

