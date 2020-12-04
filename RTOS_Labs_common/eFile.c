// filename ************** eFile.c *****************************
// High-level routines to implement a solid-state disk 
// Students implement these functions in Lab 4
// Jonathan W. Valvano 1/12/20
#include <stdint.h>
#include <string.h>
#include "../inc/CortexM.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/UART0int.h"
#include <stdio.h>


// empty space directory entry will be '*' at the limit of files
#define FILE_DIRECTORY_LIMIT	510
#define BLOCK_SIZE						512
#define NUM_BLOCKS						255
// NOTE: directory is at sector 0 and FAT is at sector 1
#define FD_SECTOR_NUM					0
#define FAT_SECTOR_NUM				1

struct fileEntry{
	BYTE fileName[8];
	unsigned int sectorIndex;
	// availability of 1 means is available
	unsigned int available;
};

/*
// support only something like 2 MB of storage and 7 files
// 2 MB = 2000 kiloBytes = 4000 512byte chunks
uint8_t IndexTable[512];
uint8_t Directory[512];

directory follows this format:
Name of file 8 bytes
Starting sector number

sector follows this format:
first 2 bytes denote current sector usage
255 increments
0-2 times of 255 increments
*/

// TODO: label free space in directory

BYTE fileDataBuffer[512];
BYTE fileAllocationTable[512];
struct fileEntry fileDirectory[32];

bool initialized = false;
int currentFileStartingSectorNumber = 0;
int writeFileIndex = 0;
int readFileDataIndex = 0;
int readFileIndex = 0;

Sema4Type eFileMutex;


//---------- eFile_Init-----------------
// Activate the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure (already initialized)
int eFile_Init(void){ // initialize file system
	// initialize local buffers
	
	int errCode = 0;
	
	if(!initialized){
		for(int i = 0; i < BLOCK_SIZE; i++){
			fileAllocationTable[i] = 0;
			fileDataBuffer[i] = 0;
		}
		// set first two byte write indexes
		fileDataBuffer[0] = 2;
		fileDataBuffer[1] = 0;
		for(int i = 0; i < 32; i++){
			for(int i = 0; i < 8; i++){
				fileDirectory[i].fileName[i] = '\0';
			}
			fileDirectory[i].available = 1;
			fileDirectory[i].sectorIndex = 0;
		}
		initialized = true;
		unsigned long schedulerSuspend = OS_LockScheduler();
		errCode = eDisk_Init(0);
		OS_UnLockScheduler(schedulerSuspend);
		if(errCode){
			return errCode;
		}
		
		OS_InitSemaphore(&eFileMutex, 1);
		return 0;
	}
	
  return initialized;
}

//---------- eFile_Format-----------------
// Erase all files, create blank directory, initialize free space manager
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Format(void){ // erase disk, add format
	
	OS_bWait(&eFileMutex);
	
	int errCode = 0;
	
	for(int i = 0; i < BLOCK_SIZE; i++){
		fileAllocationTable[i] = 0;
		fileDataBuffer[i] = 0;
	}
	// set first two byte write indexes
	fileDataBuffer[0] = 2;
	fileDataBuffer[1] = 0;
	for(int i = 0; i < 32; i++){
		for(int i = 0; i < 8; i++){
			fileDirectory[i].fileName[i] = '\0';
		}
		fileDirectory[i].available = 1;
		fileDirectory[i].sectorIndex = 0;
	}
	
	// clear 512 sectors 0-511
	unsigned long schedulerSuspend = OS_LockScheduler();
	int sectorIndex = 0;
	for(; sectorIndex < 512 && errCode == 0; sectorIndex++){
		errCode = eDisk_WriteBlock(fileDataBuffer, sectorIndex);
	}
	OS_UnLockScheduler(schedulerSuspend);
	
	if(errCode){
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	// set free space entry in directory
	// free space entry name is '*' and following space is starting free block of 2
	// disk sector 0 is file directory
	// disk sector 1 is file allocation table
	strcpy((char*)fileDirectory[31].fileName, "*");
	fileDirectory[31].sectorIndex = 2;
	
	// link up the empty space in the FAT
	// only use up 256 spots, because max value of unsigned char is 256
	for(int i = 2; i < 255; i++){
		fileAllocationTable[i] = i+1;
	}
	// null terminate where the free space ends
	fileAllocationTable[255] = 0;
	
	// store back the formatted FAT and file Directory
	schedulerSuspend = OS_LockScheduler();
	errCode = eDisk_WriteBlock((BYTE*)fileDirectory, FD_SECTOR_NUM);
	errCode = eDisk_WriteBlock((BYTE*)fileAllocationTable, FAT_SECTOR_NUM);
	OS_UnLockScheduler(schedulerSuspend);

	OS_bSignal(&eFileMutex);
  
	return errCode;   // replace
}

//---------- eFile_Mount-----------------
// Mount the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure
int eFile_Mount(void){ // initialize file system
	
	OS_bWait(&eFileMutex);
	
	int errCode = 0;
	/*
	load up the directory and index table to local buffers
	*/
	unsigned long schedulerSuspend = OS_LockScheduler();
	errCode = eDisk_ReadBlock((BYTE*)fileDirectory, FD_SECTOR_NUM);
	if(errCode){
		OS_UnLockScheduler(schedulerSuspend);
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	errCode = eDisk_ReadBlock(fileAllocationTable, FAT_SECTOR_NUM);
	OS_UnLockScheduler(schedulerSuspend);
  
	OS_bSignal(&eFileMutex);
	
	return errCode;   // replace
}


//---------- eFile_Create-----------------
// Create a new, empty file with one allocated block
// Input: file name is an ASCII string up to seven characters 
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Create( const char name[]){  // create new file, make it empty 
	
	OS_bWait(&eFileMutex);
	
	if(fileDirectory[31].sectorIndex == 0){
		// no more space available
		OS_bSignal(&eFileMutex);
		return 1;
	}
	
	// clear fileDataBuffer
	for(int i = 0; i < 512; i++){
		fileDataBuffer[i] = 0;
	}
	
	// allocate entry in fileDirectory
	int freeFileEntryIndex = -1;
	for(int i = 0; i < 31; i++){
		// check to see if there is already a file with the same name is in use
		if(strcmp((char*)fileDirectory[i].fileName, name) == 0 && 
			 fileDirectory[i].available == 0){
			break;
		}
		if(fileDirectory[i].available){
			freeFileEntryIndex = i;
			break;
		}
	}
	
	int errCode = 1;
	
	// if no space in directory or no space in FAT or file with matching name is already in use, return
	if(freeFileEntryIndex == -1 || fileDirectory[31].sectorIndex == 0){
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	
	// set fileEntry to using
	fileDirectory[freeFileEntryIndex].available = 0;
	// update fileEntry's name
	strcpy((char*)fileDirectory[freeFileEntryIndex].fileName, name);
	// set location for the sector of the new file in disk
	BYTE freeSectorIndex = fileDirectory[31].sectorIndex;
	fileDirectory[freeFileEntryIndex].sectorIndex = freeSectorIndex;
	// update the free sector linkedlist
	fileDirectory[31].sectorIndex = fileAllocationTable[freeSectorIndex];
	// update the file allocation table
	fileAllocationTable[freeSectorIndex] = 0;
	
	
	unsigned long schedulerSuspend = OS_LockScheduler();
	// read the newly allocated sector from disk
	errCode = eDisk_ReadBlock(fileDataBuffer, freeSectorIndex);
	// clear the new block of memory
	for(int i = 0; i < 512; i++){
		fileDataBuffer[i] = 0;
	}
	fileDataBuffer[0] = 2;
	fileDataBuffer[1] = 0;
	// write it back into disk
	errCode = eDisk_WriteBlock(fileDataBuffer, freeSectorIndex);
	OS_UnLockScheduler(schedulerSuspend);
  
	OS_bSignal(&eFileMutex);
	
	return errCode;   // replace
}


//---------- eFile_WOpen-----------------
// Open the file, read into RAM last block
// Input: file name is an ASCII string up to seven characters
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WOpen( const char name[]){      // open a file for writing 
	
	OS_bWait(&eFileMutex);
	
	// look for the file
	int errCode = 1;
	// set current file status as write
	readFileDataIndex = 0;
	
	int foundFileEntryIndex = -1;
	for(int i = 0; i < 31; i++){
		// check if fileName matches a file currently in use
		if(strcmp((char*)fileDirectory[i].fileName, name) == 0 &&
			 fileDirectory[i].available == 0){
			foundFileEntryIndex = i;
			break;
		}
	}
	
	if(foundFileEntryIndex == -1){
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	
	unsigned long schedulerSuspend = OS_LockScheduler();
	// find and yoink the corresponding sector in disk into RAM
	int diskSectorIndex = fileDirectory[foundFileEntryIndex].sectorIndex;
	errCode = eDisk_ReadBlock(fileDataBuffer, diskSectorIndex);
	
	// update current sector number
	currentFileStartingSectorNumber = diskSectorIndex;
	writeFileIndex = currentFileStartingSectorNumber;
	OS_UnLockScheduler(schedulerSuspend);
  
	OS_bSignal(&eFileMutex);
	
	return errCode;   // replace  
}

//---------- eFile_Write-----------------
// save at end of the open file
// Input: data to be saved
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Write( const char data){
	
		OS_bWait(&eFileMutex);
	
		int errCode = 0;
		unsigned long schedulerSuspend = OS_LockScheduler();
		int currentFileByteIndex = fileDataBuffer[0];
		currentFileByteIndex += 256 * fileDataBuffer[1];
		if(currentFileByteIndex >= 512){
			// store current fileDataBuffer back to corresponding sector
			errCode = eDisk_WriteBlock(fileDataBuffer, writeFileIndex);
			if(errCode){
				OS_UnLockScheduler(schedulerSuspend);
				OS_bSignal(&eFileMutex);
				return errCode;
			}
			// check if last block of file has space
			// traverse to end of the current file's sector list
			int localStartingIndex = currentFileStartingSectorNumber;
			while(fileAllocationTable[localStartingIndex]){
				localStartingIndex = fileAllocationTable[localStartingIndex];
			}
			// read the last block of file from memory
			errCode = eDisk_ReadBlock(fileDataBuffer,localStartingIndex);
			if((fileDataBuffer[0] + 256*fileDataBuffer[1]) >= 512){
				// no more space in FAT to allocate
				if(fileDirectory[31].sectorIndex == 0){
					OS_UnLockScheduler(schedulerSuspend);
					OS_bSignal(&eFileMutex);
					return 1;
				}else{
					// if last block of file has no more space, allocate more storage
					int freeSectorIndex = fileDirectory[31].sectorIndex;
					// link current end sector to new ending sector
					fileAllocationTable[localStartingIndex] = freeSectorIndex;
					// update the free index of the free space list
					fileDirectory[31].sectorIndex = fileAllocationTable[freeSectorIndex];
					// null terminate the current end sector
					fileAllocationTable[freeSectorIndex] = 0;
					// read the new block from memory
					errCode = eDisk_ReadBlock(fileDataBuffer,freeSectorIndex);
					// update the currentIndex to place data
					currentFileByteIndex = fileDataBuffer[0] + 256 * fileDataBuffer[1];
					writeFileIndex = freeSectorIndex;
				}
			}else{
				// last block of file has more storage
				writeFileIndex = localStartingIndex;
				currentFileByteIndex = fileDataBuffer[0] + 256 * fileDataBuffer[1];
			}
		}
		OS_UnLockScheduler(schedulerSuspend);
		
		fileDataBuffer[currentFileByteIndex] = data;
		fileDataBuffer[0] = (fileDataBuffer[0]+1)%256;
		if(fileDataBuffer[0] == 0){
			fileDataBuffer[1]++;
		}
		
		OS_bSignal(&eFileMutex);
		
    return errCode;   // replace
}

//---------- eFile_WClose-----------------
// close the file, left disk in a state power can be removed
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WClose(void){ // close the file for writing
  
	OS_bWait(&eFileMutex);
	
	int errCode = 1;
	
	// store whatever sector was open, back onto disk
	unsigned long schedulerSuspend = OS_LockScheduler();
	errCode = eDisk_WriteBlock(fileDataBuffer, writeFileIndex);
	OS_UnLockScheduler(schedulerSuspend);
	
	// set status as no longer writing 
	writeFileIndex = 0;
	
	OS_bSignal(&eFileMutex);
  
	return errCode;   // replace
}


//---------- eFile_ROpen-----------------
// Open the file, read first block into RAM 
// Input: file name is an ASCII string up to seven characters
// Output: 0 if successful and 1 on failure (e.g., trouble read to flash)
int eFile_ROpen( const char name[]){      // open a file for reading 
	
	OS_bWait(&eFileMutex);
	
	// look for the file
	int errCode = 1;
	
	// set status as read
	writeFileIndex = 0;
	
	int foundFileEntryIndex = -1;
	for(int i = 0; i < 31; i++){
		// check if fileName matches a file currently in use
		if(strcmp((char*)fileDirectory[i].fileName, name) == 0 &&
			 fileDirectory[i].available == 0){
			foundFileEntryIndex = i;
			break;
		}
	}
	
	if(foundFileEntryIndex == -1){
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	
	unsigned long schedulerSuspend = OS_LockScheduler();
	// find and yoink the corresponding sector in disk into RAM
	int diskSectorIndex = fileDirectory[foundFileEntryIndex].sectorIndex;
	errCode = eDisk_ReadBlock(fileDataBuffer, diskSectorIndex);
	OS_UnLockScheduler(schedulerSuspend);
	
	// update current sector number
	currentFileStartingSectorNumber = diskSectorIndex;
	
	// update the indexes for currently opened file
	readFileIndex = currentFileStartingSectorNumber;
	// first two bytes are write information
	readFileDataIndex = 2;
	
	OS_bSignal(&eFileMutex);
	
  return errCode;   // replace   
}
 
//---------- eFile_ReadNext-----------------
// retreive data from open file
// Input: none
// Output: return by reference data
//         0 if successful and 1 on failure (e.g., end of file)
int eFile_ReadNext( char *pt){       // get next byte 
  
	OS_bWait(&eFileMutex);
	
	int errCode = 0;
	*pt = fileDataBuffer[readFileDataIndex];
	readFileDataIndex = (readFileDataIndex+1)%511;
	if(readFileDataIndex == 0){
		if(fileAllocationTable[readFileIndex] == 0){
			OS_bSignal(&eFileMutex);
			return 1;
		}
		readFileIndex = fileAllocationTable[readFileIndex];
		unsigned long schedulerSuspend = OS_LockScheduler();
		errCode = eDisk_ReadBlock(fileDataBuffer, readFileIndex);
		OS_UnLockScheduler(schedulerSuspend);
		readFileDataIndex = 2;
	}
	
	OS_bSignal(&eFileMutex);
	
  return errCode;   // replace
}
    
//---------- eFile_RClose-----------------
// close the reading file
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_RClose(void){ // close the file for writing
  
	OS_bWait(&eFileMutex);
	
	// add check for open/closed file?
	unsigned long schedulerSuspend = OS_LockScheduler();
	DRESULT errCode = eDisk_WriteBlock(fileDataBuffer, readFileIndex);
	OS_UnLockScheduler(schedulerSuspend);
	
	OS_bSignal(&eFileMutex);
	
  return errCode;   // replace
}


//---------- eFile_Delete-----------------
// delete this file
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Delete( const char name[]){  // remove this file 
	
	OS_bWait(&eFileMutex);
	
	int errCode = 0;
	
	int fileDeleteIndex = -1;
	for(int i = 0; i < 31; i++){
		// find a file with matching name that is currently in use
		if(strcmp((char*)fileDirectory[i].fileName, name) == 0 &&
			 fileDirectory[i].available == 0){
			fileDeleteIndex = i;
			break;
		}
	}
	
	if(fileDeleteIndex == -1){
		OS_bSignal(&eFileMutex);
		return 1;
	}
	
	// set up fileDataBuffer to clear up all the freed memory in disk
	for(int i = 0; i < 512; i++){
		fileDataBuffer[i] = 0;
	}
	// set the write pointer bytes
	fileDataBuffer[0] = 2;
	fileDataBuffer[1] = 0;
	
	// mark file as available
	fileDirectory[fileDeleteIndex].available = 1;
	
	// chain the used space to the end of the free sector list
	int fileFreeSectorListHead = fileDirectory[fileDeleteIndex].sectorIndex;
	int freeSectorListHead = fileDirectory[31].sectorIndex;
	while(fileAllocationTable[freeSectorListHead]){
		freeSectorListHead = fileAllocationTable[freeSectorListHead];
	}
	fileAllocationTable[freeSectorListHead] = fileFreeSectorListHead;
	
	// mark file as empty
	fileDirectory[fileDeleteIndex].sectorIndex = 0;
	
	// clear each of the blocks used by this file, in disk
	/*
	errCode = eDisk_WriteBlock(fileDataBuffer, fileFreeSectorListHead);
	while(fileAllocationTable[fileFreeSectorListHead]){
		eDisk_WriteBlock(fileDataBuffer, fileFreeSectorListHead);
		fileFreeSectorListHead = fileAllocationTable[fileFreeSectorListHead];
	}
	errCode = eDisk_WriteBlock(fileDataBuffer, freeSectorListHead);
	*/
	unsigned long schedulerSuspend = OS_LockScheduler();
	while(fileFreeSectorListHead && errCode == 0){
		errCode = eDisk_WriteBlock(fileDataBuffer, fileFreeSectorListHead);
		fileFreeSectorListHead = fileAllocationTable[fileFreeSectorListHead];
	}
	OS_UnLockScheduler(schedulerSuspend);
	
	OS_bSignal(&eFileMutex);
	
  return errCode;   // replace
}                             


//---------- eFile_DOpen-----------------
// Open a (sub)directory, read into RAM
// Input: directory name is an ASCII string up to seven characters
//        (empty/NULL for root directory)
// Output: 0 if successful and 1 on failure (e.g., trouble reading from flash)
int eFile_DOpen( const char name[]){ // open directory
	
	// do some other time
  return 0;   // replace
}
  
//---------- eFile_DirNext-----------------
// Retreive directory entry from open directory
// Input: none
// Output: return file name and size by reference
//         0 if successful and 1 on failure (e.g., end of directory)
int eFile_DirNext( char *name[], unsigned long *size){  // get next entry 
	
	// do some other time
  return 0;   // replace
}

//---------- eFile_DClose-----------------
// Close the directory
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_DClose(void){ // close the directory
	
	// do some other time
  return 0;   // replace
}


//---------- eFile_Unmount-----------------
// Unmount and deactivate the file system
// Input: none
// Output: 0 if successful and 1 on failure (not currently mounted)
int eFile_Unmount(void){ 
  
	OS_bWait(&eFileMutex);
	
	int errCode = 0;
	
	unsigned long schedulerSuspend = OS_LockScheduler();
	errCode = eDisk_WriteBlock(fileAllocationTable, FAT_SECTOR_NUM);
	if(errCode){
		OS_UnLockScheduler(schedulerSuspend);
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	
	if(writeFileIndex){
		errCode = eDisk_WriteBlock(fileDataBuffer, writeFileIndex);
	}
	if(errCode){
		OS_UnLockScheduler(schedulerSuspend);
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	
	errCode = eDisk_WriteBlock((BYTE*)fileDirectory, FD_SECTOR_NUM);
	if(errCode){
		OS_UnLockScheduler(schedulerSuspend);
		OS_bSignal(&eFileMutex);
		return errCode;
	}
	OS_UnLockScheduler(schedulerSuspend);
	
	OS_bSignal(&eFileMutex);
  
	return errCode;   // replace
}

// trash coupled code but idk how else to simultaneously support both uart and disk management cleanly
// these methods are called only by interpreter
// be warned that since interpreter is typically low priority, priority inversion can easily happen !

BYTE tempDataBuffer[512];


// Output: 0 if successful and 1 on failure (trouble reading from disk)
static int fileSizeCounter(int directoryEntry, int* numBytes, int* numSectors){

	int errCode = 0;
	
	int startingSector = fileDirectory[directoryEntry].sectorIndex;
	while(startingSector && !errCode){
		errCode = eDisk_ReadBlock(tempDataBuffer, startingSector);
		int currentByteUsage = 0;
		currentByteUsage += tempDataBuffer[0];
		currentByteUsage += 256 * tempDataBuffer[1];
		
		*numBytes = *numBytes + currentByteUsage;
		*numSectors = *numSectors + 1;
		startingSector = fileAllocationTable[startingSector];
	}
	
	return errCode;
}

//---------- eFile_PrintDirectory-----------------
// Used by interpreter to print out directory
// Input: none
// Output: none
void eFile_PrintDirectory(void){
	
	OS_bWait(&eFileMutex);
	
	UART_OutString("\n\r");
	
	for(int i = 0; i < 31; i++){
		if(fileDirectory[i].available == 0){
			int numBytes = 0;
			int numSectors = 0;
			int errCode = fileSizeCounter(i, &numBytes, &numSectors);
			if(errCode){
				OS_bSignal(&eFileMutex);
				UART_OutString("trouble reading from directory");
				return;
			}
			// file is currently in use and is valid
			UART_OutString((char*)fileDirectory[i].fileName);
			UART_OutString(" number of sectors: ");
			UART_OutUDec(numSectors);
			UART_OutString(" number of bytes: ");
			UART_OutUDec(numBytes);
			UART_OutString("\n\r");
		}
	}
	
	OS_bSignal(&eFileMutex);
	
}

static int filePrinter(int directoryEntry){

	int errCode = 0;
	
	int startingSector = fileDirectory[directoryEntry].sectorIndex;
	while(startingSector && !errCode){
		errCode = eDisk_ReadBlock(tempDataBuffer, startingSector);
		int startingIndex = 2;
		int endingIndex = 0;
		endingIndex += tempDataBuffer[0];
		endingIndex += 256 * tempDataBuffer[1];
		for(; startingIndex < endingIndex; startingIndex++){
			UART_OutChar(tempDataBuffer[startingIndex]);
		}
		startingSector = fileAllocationTable[startingSector];
	}
	
	UART_OutString("\n\r");
	return errCode;
}

//---------- eFile_PrintFile-----------------
// Used by interpreter to print out eFile file
// Input: name of file to be printed out
// Output: none
void eFile_PrintFile(char* fileName){
	
	OS_bWait(&eFileMutex);
	
	UART_OutString("\n\r");
	
	int foundFileIndex = -1;
	for(int i = 0; i < 31; i++){
		// look for file with the same name that's active
		if(strcmp((char*)fileDirectory[i].fileName, fileName) == 0 &&
			 fileDirectory[i].available == 0){
			foundFileIndex = i;
			break;
		}
	}
	
	if(foundFileIndex == -1){
		UART_OutString("file : ");
		UART_OutString(fileName);
		UART_OutString(" can't be printed because it doesn't exist");
		OS_bSignal(&eFileMutex);
		UART_OutString("\n\r");
		return;
	}
	
	int errCode = filePrinter(foundFileIndex);
	if(errCode){
		UART_OutString("trouble reading file");
		OS_bSignal(&eFileMutex);
		UART_OutString("\n\r");
		return;
	}
	
	UART_OutString("\n\r");
	
	OS_bSignal(&eFileMutex);
	
}

//---------- eFile_DeleteFile-----------------
// Used by interpreter to delete File file
// Input: name of file to be deleted
// Output: none
void eFile_DeleteFile(char* fileName){

	int errCode = eFile_Delete(fileName);
	
	OS_bWait(&eFileMutex);
	
	UART_OutString("\n\r");
	
	if(errCode){
		UART_OutString("trouble deleting file");
	}else{
		UART_OutString("deleted: ");
		UART_OutString(fileName);
	}
	
	UART_OutString("\n\r");
	
	OS_bSignal(&eFileMutex);
	
}

//---------- eFile_FormatDisk-----------------
// Used by interpreter to format eDisk
// Input: none
// Output: none
void eFile_FormatDisk(void){
	
	int errCode = eFile_Format();
	
	OS_bWait(&eFileMutex);
	
	UART_OutString("\n\r");
	
	if(errCode){
		UART_OutString("trouble formatting eDisk");
	}else{
		UART_OutString("eDisk formatted");
	}
	
	UART_OutString("\n\r");
	
	OS_bSignal(&eFileMutex);
	
}

//---------- eFile_CreateFile-----------------
// Used by interpreter to create file on eDisk
// Input: name of file to create
// Output: none
void eFile_CreateFile(char* fileName){
	
	int errCode = eFile_Create(fileName);
	
	OS_bWait(&eFileMutex);
	
	UART_OutString("\n\r");
	
	if(errCode){
		UART_OutString("trouble creating file");
	}else{
		UART_OutString("created: ");
		UART_OutString(fileName);
	}
	
	UART_OutString("\n\r");
	
	OS_bSignal(&eFileMutex);
	
}

//---------- eFile_WriteToFile-----------------
// Used by interpreter to write string to file on eDisk
// Input: name of file to create, string to write to file
// Output: none
void eFile_WriteToFile(char* fileName, char* stringToWrite){
	UART_OutString("\n\r");
	unsigned long lockScheduler = OS_LockScheduler();
	OS_RedirectToFile(fileName);
	printf(stringToWrite);
	OS_EndRedirectToFile();
	OS_UnLockScheduler(lockScheduler);
}

void eFile_UnmountFS(void){
	UART_OutString("\n\r");
	
	int errCode = eFile_Unmount();
	if(errCode){
		UART_OutString("trouble unmounting file system");
	}else{
		UART_OutString("unmounted file system");
	}
	
	UART_OutString("\n\r");
}




