// filename *************************heap.c ************************
// Implements memory heap for dynamic memory allocation.
// Follows standard malloc/calloc/realloc/free interface
// for allocating/unallocating memory.

// Jacob Egner 2008-07-31
// modified 8/31/08 Jonathan Valvano for style
// modified 12/16/11 Jonathan Valvano for 32-bit machine
// modified August 10, 2014 for C99 syntax

/* This example accompanies the book
   "Embedded Systems: Real Time Operating Systems for ARM Cortex M Microcontrollers",
   ISBN: 978-1466468863, Jonathan Valvano, copyright (c) 2015

 Copyright 2015 by Jonathan W. Valvano, valvano@mail.utexas.edu
    You may use, edit, run or distribute this file
    as long as the above copyright notice remains

 THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 VALVANO SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL,
 OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 For more information about my classes, my research, and my books, see
 http://users.ece.utexas.edu/~valvano/
 */


#include <stdint.h>
#include "../RTOS_Labs_common/heap.h"

#define HEAP_SIZE	2000	//8000 int32_t units == 4*8000 bytes

static int32_t heap[HEAP_SIZE];

/*

header and trailer implementation



*/


//******** Heap_Init *************** 
// Initialize the Heap
// input: none
// output: always 0
// notes: Initializes/resets the heap to a clean state where no memory
//  is allocated.
int32_t Heap_Init(void){
  
	for(int i = 0; i < HEAP_SIZE; i++){
		heap[i] = 0;
	}
	
	// heap is getting tracked by number of 4 byte increments
	// header and trailer each use 1 4 byte block
	heap[0] = (-1)*(HEAP_SIZE-2);
	heap[HEAP_SIZE-1] = (-1)*(HEAP_SIZE-2);
	
  return 0;   // replace
}


//******** Heap_Malloc *************** 
// Allocate memory, data not initialized
// input: 
//   desiredBytes: desired number of bytes to allocate
// output: void* pointing to the allocated memory or will return NULL
//   if there isn't sufficient space to satisfy allocation request
void* Heap_Malloc(int32_t desiredBytes){
	
	// convert byte amount to number of blocks needed
	int32_t desiredBlocks = (desiredBytes + 3)/4;

	// walk through the heap using the pointers
	long heapIdx = 0;
	while(heapIdx < HEAP_SIZE){
		int32_t availability = heap[heapIdx];
		int32_t availabilityAbs = availability;
		if(availability < 0){
			availabilityAbs *= -1;
		}
		// if found enough available space, can allocate space
		if(availability < 0 && (availabilityAbs >= desiredBlocks)){
			// need to check bounds, if added overhead exceeds bounds
			// then it's clear that the space can't be split
			int32_t blockEndIdx = heapIdx + availabilityAbs + 1;
			// 4 units of overhead + blocks allocated
			if((availabilityAbs - desiredBlocks - 3) > 0){
				// if space left, can split
				heap[heapIdx] = desiredBlocks;
				heap[heapIdx + desiredBlocks + 1] = desiredBlocks;
				heap[heapIdx + desiredBlocks + 2] = (-1)*(availabilityAbs - desiredBlocks - 2);
				heap[blockEndIdx] = (-1)*(availabilityAbs - desiredBlocks - 2);
			}else{
				// no space left, can't split, give all space away
				heap[heapIdx] = availabilityAbs;
				heap[blockEndIdx] = availabilityAbs;
			}
			return(&heap[heapIdx+1]);
		}else{
			// get next header index, which is availabilityAbs + 2 blocks of overhead away
			heapIdx += availabilityAbs + 2;
		}
	}
	
  return 0;   // NULL
}


//******** Heap_Calloc *************** 
// Allocate memory, data are initialized to 0
// input:
//   desiredBytes: desired number of bytes to allocate
// output: void* pointing to the allocated memory block or will return NULL
//   if there isn't sufficient space to satisfy allocation request
//notes: the allocated memory block will be zeroed out
void* Heap_Calloc(int32_t desiredBytes){  
  
	int32_t* retPtr = Heap_Malloc(desiredBytes);
	
	if(!retPtr){
		return retPtr;
	}
	
	int32_t blocksToClear = *(retPtr-1);
	
	for(int i = 0; i < blocksToClear; i++){
		retPtr[i] = 0;
	}
	
  return retPtr; 
}


//******** Heap_Realloc *************** 
// Reallocate buffer to a new size
//input: 
//  oldBlock: pointer to a block
//  desiredBytes: a desired number of bytes for a new block
// output: void* pointing to the new block or will return NULL
//   if there is any reason the reallocation can't be completed
// notes: the given block may be unallocated and its contents
//   are copied to a new block if growing/shrinking not possible
void* Heap_Realloc(void* oldBlock, int32_t desiredBytes){
	
	int32_t* oldBlockPtr = oldBlock;
	
	// invalid oldBlockPtr if, not within heap address space
	if((oldBlockPtr - 1) < heap){
		return 0;
	}
	int32_t allocatedSpaceHeader = *(oldBlockPtr-1);
	// if header value is negative, pointer isn't pointing to allocated space
	if(allocatedSpaceHeader < 0){
		return 0;
	}
	// invalid oldBlockPtr if not within heap address space 
	if((oldBlockPtr + allocatedSpaceHeader) > &heap[HEAP_SIZE-1]){
		return 0;
	}
	int32_t allocatedSpaceTrailer = *(oldBlockPtr + allocatedSpaceHeader);
	// trailer value must match header value, otherwise corrupted heap
	if(allocatedSpaceTrailer != allocatedSpaceHeader){
		return 0;
	}
	
	// try looking for available space
	void* reallocSpace = Heap_Malloc(desiredBytes);
	if(!reallocSpace){
		// couldn't realloc
		return 0;
	}
	
	// copy data over to the new allocated space and free the previous memory
	// can realloc be used to reallocate less memory?? apparently yes lol
	int32_t dataAmount = allocatedSpaceHeader;
	int32_t* allocPtr = reallocSpace;
	int32_t reallocAmount = *((int32_t*)reallocSpace - 1);
	
	int32_t overWriteAmount = dataAmount;
	if(dataAmount > reallocAmount){
		overWriteAmount = reallocAmount;
	}
	
	for(int i = 0; i < overWriteAmount; i++){
		allocPtr[i] = oldBlockPtr[i];
	}
	
	if(Heap_Free(oldBlockPtr)){
		// heap free failed
		return 0;
	}

	return reallocSpace;
}


//******** Heap_Free *************** 
// return a block to the heap
// input: pointer to memory to unallocate
// output: 0 if everything is ok, non-zero in case of error (e.g. invalid pointer
//     or trying to unallocate memory that has already been unallocated
int32_t Heap_Free(void* pointer){
	
	int32_t* allocationLocation = pointer;
	
	// invalid allocationLocation if, not within heap address space
	if((allocationLocation - 1) < heap){
		return 1;
	}
	int32_t allocatedSpaceHeader = *(allocationLocation-1);
	// if header value is negative, pointer isn't pointing to allocated space
	if(allocatedSpaceHeader < 0){
		return 1;
	}
	// invalid allocationLocation if not within heap address space 
	if((allocationLocation + allocatedSpaceHeader) > &heap[HEAP_SIZE-1]){
		return 1;
	}
	int32_t allocatedSpaceTrailer = *(allocationLocation + allocatedSpaceHeader);
	// trailer value must match header value, otherwise corrupted heap
	if(allocatedSpaceTrailer != allocatedSpaceHeader){
		return 1;
	}
	
	// valid pointer here, free the memory
	// can merge above only, below only, or both
	
	// check if trailer above allocated space is negative and 
	// if header below allocated space is negative
	
	// how can one even check that the system isn't totally corrupted ?
	
	int32_t previousTrailer = 0;
	if((allocationLocation - 2) > heap){
		previousTrailer = *(allocationLocation - 2);
	}
	
	int32_t previousHeader = 0;
	if(previousTrailer < 0){
		int32_t* previousHeaderLocation = allocationLocation - 2 + previousTrailer - 1;
		if(previousHeaderLocation >= heap){
			// header can be first block of heap
			previousHeader = *(previousHeaderLocation);
		}
	}
	
	int32_t nextHeader = 0;
	if((allocationLocation + allocatedSpaceHeader + 1) < &heap[HEAP_SIZE-1]){
		nextHeader = *(allocationLocation + allocatedSpaceHeader + 1);
	}
	
	int32_t nextTrailer = 0;
	if(nextHeader < 0){
		int32_t* nextTrailerLocation = allocationLocation + allocatedSpaceHeader + 2 + (-1 * nextHeader);
		if(nextTrailerLocation <= &(heap[HEAP_SIZE-1])){
			// trailer can be the last block of the heap
			nextTrailer = *(nextTrailerLocation);
		}
	}
	
	int32_t* startingFreeLocation = allocationLocation - 1;
	int32_t* endingFreeLocation = allocationLocation + allocatedSpaceTrailer;
	int32_t freedBlocks = allocatedSpaceHeader;

	// if previous trailer shows that there is a mergeable area above current free-ing location
	// stretch the startingFreeLocation
	
	if((previousTrailer == previousHeader) && (previousHeader < 0)){
		// mergeable space above
		startingFreeLocation = allocationLocation - 2 + previousHeader - 1;
		freedBlocks += (-1 * previousHeader) + 2;
	}
	
	// if next header shows that there is a mergeable area below current free-ing location
	// stretch the endingFreeLocation
	
	if((nextTrailer == nextHeader) && (nextHeader < 0)){
		// mergeable space below
		endingFreeLocation = allocationLocation + allocatedSpaceHeader + 2 + (-1 * nextTrailer);
		freedBlocks += (-1 * nextHeader) + 2;
	}
	
	*(startingFreeLocation) = (-1 * freedBlocks);
	*(endingFreeLocation) = (-1 * freedBlocks);
	
  return 0;   // replace
}


//******** Heap_Stats *************** 
// return the current status of the heap
// input: reference to a heap_stats_t that returns the current usage of the heap
// output: 0 in case of success, non-zeror in case of error (e.g. corrupted heap)
int32_t Heap_Stats(heap_stats_t *stats){
	
	
	uint32_t heapSize = 0;
	uint32_t usedBytes = 0;
	uint32_t freeBytes = 0;
	int32_t heapIdx = 0;
	
	
	while(heapIdx < HEAP_SIZE){
		int32_t headerValue = heap[heapIdx];
		int32_t headerValueAbs = headerValue;
		
		if(headerValue < 0){
			headerValueAbs *= -1;
			freeBytes += headerValueAbs * 4;
		}else{
			usedBytes += headerValue * 4;
		}
		
		// each segment has 2 blocks of 4 bytes of overhead
		usedBytes += 8;
	
		heapIdx += headerValueAbs + 1;
		if(heap[heapIdx] != headerValue){
			// if header and trailer values are not equal, then heap corruption
			return 1;
		}
		// move index to the next header after current trailer
		heapIdx++;
	}
	
	heapSize = HEAP_SIZE * 4;
	
	stats->free = freeBytes;
	stats->size = heapSize;
	stats->used = usedBytes;
	
  return 0;   // replace
}
