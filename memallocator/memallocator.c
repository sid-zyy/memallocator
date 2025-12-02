#include <sys/mman.h>//used for mmap
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "memallocator.h"

struct block{
    size_t size;//size of memory
    struct block* next;//pointer to next block
    int free;//int to check if block is free
    int mmap;//checking if block is mmap or sbrk allocated
};

const size_t BLOCK_SIZE = sizeof(struct block);
const size_t mmapthreshold = 1024 * 128;//above this threshold mmap used instead of sbrk
struct block *free_list = NULL;//header of LL


struct block* request_space(struct block* last, size_t size){
    void *request = sbrk(size+BLOCK_SIZE);//asking OS for memory
    if(request == (void*)-1){//sbrk returns void*-1 on failure
        return NULL;
    }
    struct block *newblock = (struct block*) request;//casting request to be struct block* type
    newblock->size = size;
    newblock->next = NULL;
    newblock->free = 0;
    newblock->mmap = 0;
    if(last){//add to the end of the list
        last->next = newblock;
    }
    return newblock;
}

struct block* find_free(struct block **last, size_t size){//we pass last to 
    struct block* current = free_list;

    while(current && !(current->free && current->size >= size)){
        *last = current;
        current = current->next;
    }
    return current;//returns NULL if there is no pre existing free memory else returns block with required memory or above
}

void split_block(struct block *newblock,size_t size){
    if(newblock->size >= size+BLOCK_SIZE+sizeof(void*)){
        struct block* newblock2 = (struct block*)((char*)(newblock+1)+size);//calculates where newblock2 should begin
        newblock2->size = newblock->size - size - BLOCK_SIZE;
        newblock2->free = 1;
        newblock2->next = newblock->next;//inserting newblock2 between newblock and its next block
        newblock2->mmap = 0;

        newblock->size = size;
        newblock->next =  newblock2;
    }
}

void coalesce(struct block* block){//merging this free block with adjacent free blocks to combine into a larger block
    if(block->next && block->next->free){
        block->size += block->next->size + BLOCK_SIZE;
        block->next = block->next->next;
    }

    struct block* current = free_list;
    while(current && current->next != block){
        current = current->next;
    }

    if(current && current->next->free){
        current->size += block->size + BLOCK_SIZE;
        current->next =  block->next;
    }
}

void *mymalloc(size_t size){
    struct block* newblock;

    if(size == 0){
        return NULL;
    }
    
    size = (size + sizeof(void*)-1) & ~(sizeof(void*)-1); 
    //its used to align the size to the next multiple of sizeof(void*)
    //size of void* is the same as number of bytes in a word
    //this function automatically goes to the next word
    //ex : size is 12 bytes we add 7 bytes = 19 bytes then use & function with complement of 7 we get 16

    if(size >= mmapthreshold){//case of mmap being used
        void *ptr =  mmap(NULL,size + BLOCK_SIZE,PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1,0);
        //NULL means kernel chooses any suitable virtual address
        //length of the memory region (size asked + internal block header)
        //PROT read and write means mmemory can be both read and written
        //MAP_ANONOYMOUS means dont load a file into memory rather just give me the raw memory
        //MAP_PRIVATE the mapping is private to this process
        if(ptr == MAP_FAILED){
            return NULL;
        }
        newblock = (struct block*)ptr; //intitializing values of header of memory block
        newblock->size = size;
        newblock->next = NULL;
        newblock->free = 0;
        newblock->mmap = 1;
    }
    else{//using sbrk()
        if(!free_list){//first element in LL case
            newblock = request_space(NULL,size);
            if(!newblock){
                return NULL;
            }
            free_list = newblock;
        }
        else{
            struct block *last = free_list;
            newblock = find_free(&last, size);
            if(!newblock){//if there is no such pre existing free block
                newblock = request_space(last,size);//we will initialize a new block after the last block
                if(!newblock){//allocation failed
                    return NULL;
                }
            }
            else{//if existing block is found with space greater than required
                split_block(newblock,size);//split the block into required and extra
                newblock->free = 0;
            }
        }
    }
    return (newblock+1);
}

void myfree(void* ptr){
    if(!ptr) return;
    struct block* block = (struct block*)ptr-1;//we cast ptr to struct block*
    //we use pointer arithmetic so pointer moves from start of user data to start of header of the block

    if(block->mmap){
        munmap(block,block->size + BLOCK_SIZE);//munmap permanently returns the memory back to the OS
    }
    else{
        block->free = 1;
        coalesce(block);
    }
}

