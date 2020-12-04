// *************os.c**************
// EE445M/EE380L.6 Labs 1, 2, 3, and 4 
// High-level OS functions
// Students will implement these functions as part of Lab
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 
// Jan 12, 2020, valvano@mail.utexas.edu


#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/PLL.h"
#include "../inc/LaunchPad.h"
#include "../inc/Timer4A.h"
#include "../inc/WTimer0A.h"
#include "../inc/WTimer1A.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/ADCT0ATrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/heap.h"

//#define TIMEPERIOD		TIME_500US
#define TIMEPERIOD		TIME_1MS
#define STACKSIZE			128
#define FIFOSIZE			64
#define THREAD_NUM		7

/*
struct TCB{
	int32_t* stackPt;
	struct TCB* nextTCB;
	struct TCB* previousTCB;
	bool listHead;
	bool active;
	uint32_t sleepTime;
	int32_t id;
	int32_t priority;
	int32_t blocked;
};
*/

//static global variables

struct TCB* SleepPt = NULL;
struct TCB ActiveThreads[PRIORITY_NUM];
struct TCB SleepingThreads;

struct TCB threadPool[THREAD_NUM];
int32_t stackPool[THREAD_NUM][STACKSIZE];

struct PCB Processes;

//dynamic global variables
struct TCB* RunPt = NULL;
int32_t** StackPt = NULL;
int threadId = 0;
int periodicThreadCount = 0;
struct PCB* PcbPt = NULL;
void* dataPt = NULL;


//Producer ISR -> Consumer Foreground FIFO
int isrToForegroundFIFO[FIFOSIZE];
Sema4Type dataAvailable;
int putIFFIndex;
int getIFFIndex;
int isrToForegroundFIFOCount;

// Foreground -> Foreground Mailbox
uint32_t Mailbox;
Sema4Type BoxFree;
Sema4Type DataValid;


//time keeping structures
bool timeStarted = false;

struct Time {
	uint32_t ms;
	uint32_t s;
	uint32_t min;
};

struct Time time = {
	.ms = 0,
	.s = 0,
	.min = 0
};


// Performance Measurements 
int32_t MaxJitter;             // largest time jitter between interrupts in usec
#define JITTERSIZE 64
uint32_t const JitterSize=JITTERSIZE;
uint32_t JitterHistogram[JITTERSIZE]={0,};


/*------------------------------------------------------------------------------
  Systick Interrupt Handler
  SysTick interrupt happens every 10 ms
  used for preemptive thread switch
 *------------------------------------------------------------------------------*/
void SysTick_Handler(void) {
	
	long sr = StartCritical();
	int curRunPtPriority = RunPt->priority;
  OS_Suspend();
	// this line is corrective measure for the scheduler
	// if there is a task 1 at priority 5 and task 2 at priority 6
	// scheduler would make RunPt task 2, because sequence is:
	// cache nextTCB -> remove runPt from active threads -> find next thread to run -> rechain RunPt
	// two threads with different priorities is corner case, similar to corner case of only one thread trying to context switch into itself
	if(RunPt->priority > curRunPtPriority && (ActiveThreads[curRunPtPriority].nextTCB != &ActiveThreads[curRunPtPriority])){
		RunPt = ActiveThreads[curRunPtPriority].nextTCB;
	}
	EndCritical(sr);
	
} // end SysTick_Handler

static void scheduler(){
	
	// move current thread to end of it's priority list
	
	// save RunPt's nextTCB
	// the case in which only one thread is running and needs to switch
	struct TCB* runPtNextTCB = RunPt->nextTCB;
	if(runPtNextTCB->listHead){
		runPtNextTCB = runPtNextTCB->nextTCB;
	}
	
	// first unchain RunPt
	RunPt->nextTCB->previousTCB = RunPt->previousTCB;
	RunPt->previousTCB->nextTCB = RunPt->nextTCB;
	
	// pick the first, highest priority active thread to run next
	for(int i = 0; i < PRIORITY_NUM; i++){
		if(ActiveThreads[i].nextTCB != &(ActiveThreads[i])){
			runPtNextTCB = ActiveThreads[i].nextTCB;
			break;
		}
	}
	
	// chain RunPt to end of it's priority linkedlist
	// placed here because if only one highest priority, then sequence of unchain -> chain -> find runPtNextTCB will result in itself
	RunPt->previousTCB = ActiveThreads[RunPt->priority].previousTCB;
	RunPt->nextTCB = &(ActiveThreads[RunPt->priority]);
	ActiveThreads[RunPt->priority].previousTCB->nextTCB = RunPt;
	ActiveThreads[RunPt->priority].previousTCB = RunPt;

	RunPt = runPtNextTCB;
}

unsigned long OS_LockScheduler(void){
  // lab 4 might need this for disk formating
	long sr = StartCritical();
	unsigned long old = NVIC_ST_CTRL_R;
	//NVIC_ST_CTRL_R = NVIC_ST_CTRL_ENABLE+NVIC_ST_CTRL_CLK_SRC;
	//NVIC_ST_CTRL_R = 7;
	NVIC_ST_CTRL_R = 6;
	EndCritical(sr);
	return old; 
}
void OS_UnLockScheduler(unsigned long previous){
  // lab 4 might need this for disk formating
	long sr = StartCritical();
	NVIC_ST_CTRL_R = previous; 
	EndCritical(sr);
}


void SysTick_Init(unsigned long period){
	NVIC_ST_CTRL_R = 0;         // disable SysTick during setup
  NVIC_ST_RELOAD_R = period-1;// reload value
  NVIC_ST_CURRENT_R = 0;      // any write to current clears it
  NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0x00FFFFFF)|0xE0000000; // priority 7
                              // enable SysTick with core clock and interrupts
  NVIC_ST_CTRL_R = 0x07;
}

/**
 * @details  Initialize operating system, disable interrupts until OS_Launch.
 * Initialize OS controlled I/O: serial, ADC, systick, LaunchPad I/O and timers.
 * Interrupts not yet enabled.
 * @param  none
 * @return none
 * @brief  Initialize OS
 */
void OS_Init(void){
  // put Lab 2 (and beyond) solution here
	
	// set the system clock
	PLL_Init(Bus80MHz);
	
	DisableInterrupts();
	
	//initializing static pointers
	
	for(int i = 0; i < PRIORITY_NUM; i++){
		ActiveThreads[i].nextTCB = &(ActiveThreads[i]);
		ActiveThreads[i].previousTCB = &(ActiveThreads[i]);
		ActiveThreads[i].listHead = true;
	}
	
	SleepingThreads.nextTCB = &SleepingThreads;
	SleepingThreads.previousTCB = &SleepingThreads;
	SleepingThreads.listHead = true;
	
	// added for Lab 5
	Processes.nextPCB = &Processes;
	Processes.prevPCB = &Processes;
	Processes.listHead = true;
	
	//Systick_Init belongs in OS_Launch
	//set PendSV priority to 7
	NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0xFF0FFFFF)|0x00E00000; // priority 7
	//set PendSV priority to 0
	//NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0xFF0FFFFF)|0x00000000; // priority 0
}; 

// ******** OS_InitSemaphore ************
// initialize semaphore 
// input:  pointer to a semaphore
// output: none
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value){
  // put Lab 2 (and beyond) solution here
	semaPt->Value = value;
	for(int i = 0; i < PRIORITY_NUM; i++){
		semaPt->blockedThreads[i].nextTCB = &(semaPt->blockedThreads[i]);
		semaPt->blockedThreads[i].previousTCB = &(semaPt->blockedThreads[i]);
		semaPt->blockedThreads[i].listHead = true;
	}
}; 

// ******** OS_Wait ************
// decrement semaphore 
// Lab2 spinlock
// Lab3 block if less than zero
// input:  pointer to a counting semaphore
// output: none
void OS_Wait(Sema4Type *semaPt){
  // put Lab 2 (and beyond) solution here
	
	long sr = StartCritical();
	semaPt->Value--;
	if(semaPt->Value < 0){
		//EnableInterrupts();
		//long sr2 = StartCritical();
		
		struct TCB* blockedThread = RunPt;		
		OS_Suspend();
		// blockedThread aka cached RunPt is now the last thread of its priority
		
		// unchain blockedThread from ActiveThreads priority list
		blockedThread->previousTCB->nextTCB = blockedThread->nextTCB;
		blockedThread->nextTCB->previousTCB = blockedThread->previousTCB;
		
		// chain blockedThread to blocked threads for this semaphore
		blockedThread->nextTCB = &(semaPt->blockedThreads[blockedThread->priority]);
		blockedThread->previousTCB = semaPt->blockedThreads[blockedThread->priority].previousTCB;
		semaPt->blockedThreads[blockedThread->priority].previousTCB->nextTCB = blockedThread;
		semaPt->blockedThreads[blockedThread->priority].previousTCB = blockedThread;
		
		//EndCritical(sr2);
		// OS_Suspend needs to execute here
		//DisableInterrupts();
	}
	//semaPt->Value--;
	EndCritical(sr);
	
}; 

// ******** OS_Signal ************
// increment semaphore 
// Lab2 spinlock
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a counting semaphore
// output: none
void OS_Signal(Sema4Type *semaPt){
  // put Lab 2 (and beyond) solution here
	
	long sr = StartCritical();
	semaPt->Value++;
	
	if(semaPt->Value <= 0){
		// put highest priority blocked thread back into active threads and reset Systick timer
		
		// find the first highest priority blocked thread
		struct TCB* unblockedThread = NULL;
		for(int i = 0; i < PRIORITY_NUM; i++){
			if(semaPt->blockedThreads[i].nextTCB != &(semaPt->blockedThreads[i])){
				unblockedThread = semaPt->blockedThreads[i].nextTCB;
				break;
			}
		}
		
		// unchained blocked thread
		unblockedThread->nextTCB->previousTCB = unblockedThread->previousTCB;
		unblockedThread->previousTCB->nextTCB = unblockedThread->nextTCB;
		
		// chain unblocked thread where it belongs
		unblockedThread->nextTCB = &(ActiveThreads[unblockedThread->priority]);
		unblockedThread->previousTCB = ActiveThreads[unblockedThread->priority].previousTCB;
		ActiveThreads[unblockedThread->priority].previousTCB->nextTCB = unblockedThread;
		ActiveThreads[unblockedThread->priority].previousTCB = unblockedThread;
		
		// if unblocked thread priority < RunPt priority, OS_Suspend and reset Systick timer
		if(unblockedThread->priority < RunPt->priority){
			// since OS_Suspend is lowest priority, should be safe to perform this operation
			OS_Suspend();
			// writing to NVIC_ST_CURRENT_R clears the current counter of Systick, effectively reseting the Systick counter
			NVIC_ST_CURRENT_R = 0;
		}
	}

	EndCritical(sr);

}; 

// ******** OS_bWait ************
// Lab2 spinlock, set to 0
// Lab3 block if less than zero
// input:  pointer to a binary semaphore
// output: none
void OS_bWait(Sema4Type *semaPt){
  // put Lab 2 (and beyond) solution here
	long sr = StartCritical();
	if(semaPt->Value == 0){
		
		struct TCB* blockedThread = RunPt;		
		OS_Suspend();
		// blockedThread aka cached RunPt is now the last thread of its priority
		
		// unchain blockedThread from ActiveThreads priority list
		blockedThread->previousTCB->nextTCB = blockedThread->nextTCB;
		blockedThread->nextTCB->previousTCB = blockedThread->previousTCB;
		
		// chain blockedThread to blocked threads for this semaphore
		blockedThread->nextTCB = &(semaPt->blockedThreads[blockedThread->priority]);
		blockedThread->previousTCB = semaPt->blockedThreads[blockedThread->priority].previousTCB;
		semaPt->blockedThreads[blockedThread->priority].previousTCB->nextTCB = blockedThread;
		semaPt->blockedThreads[blockedThread->priority].previousTCB = blockedThread;
	
	}
	semaPt->Value = 0;
	EndCritical(sr);

}; 

// ******** OS_bSignal ************
// Lab2 spinlock, set to 1
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a binary semaphore
// output: none
void OS_bSignal(Sema4Type *semaPt){
  // put Lab 2 (and beyond) solution here

	long sr = StartCritical();
	
	// put highest priority blocked thread back into active threads and reset Systick timer
	
	// find the first highest priority blocked thread
	struct TCB* unblockedThread = NULL;
	for(int i = 0; i < PRIORITY_NUM; i++){
		if(semaPt->blockedThreads[i].nextTCB != &(semaPt->blockedThreads[i])){
			unblockedThread = semaPt->blockedThreads[i].nextTCB;
			break;
		}
	}
	
	if(unblockedThread != NULL){
		// unchained blocked thread
		unblockedThread->nextTCB->previousTCB = unblockedThread->previousTCB;
		unblockedThread->previousTCB->nextTCB = unblockedThread->nextTCB;
		
		// chain unblocked thread where it belongs
		unblockedThread->nextTCB = &(ActiveThreads[unblockedThread->priority]);
		unblockedThread->previousTCB = ActiveThreads[unblockedThread->priority].previousTCB;
		ActiveThreads[unblockedThread->priority].previousTCB->nextTCB = unblockedThread;
		ActiveThreads[unblockedThread->priority].previousTCB = unblockedThread;
		
		// if unblocked thread priority > RunPt priority, OS_Suspend and reset Systick timer
		if(unblockedThread->priority > RunPt->priority){
			// since OS_Suspend is lowest priority, should be safe to perform this operation
			OS_Suspend();
			// writing to NVIC_ST_CURRENT_R clears the current counter of Systick, effectively reseting the Systick counter
			NVIC_ST_CURRENT_R = 0;
		}
	}else{
		semaPt->Value = 1;
	}
	
	EndCritical(sr);
}; 


//******** OS_AddThread *************** 
// add a foregound thread to the scheduler
// Inputs: pointer to a void/void foreground task
//         number of bytes allocated for its stack
//         priority, 0 is highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// stack size must be divisable by 8 (aligned to double word boundary)
// In Lab 2, you can ignore both the stackSize and priority fields
// In Lab 3, you can ignore the stackSize fields
int OS_AddThread(void(*task)(void), 
   uint32_t stackSize, uint32_t priority){
  // put Lab 2 (and beyond) solution here
		 
	long sr = StartCritical();
	
	// Lab 5 addition
	struct PCB* pcbEntry = RunPt->currentPCB;
	if(PcbPt){
		// coming from OS_AddProcess
		pcbEntry = PcbPt;
	}
	// if valid process trying to add a thread
	if(pcbEntry){
		pcbEntry->threadCount++;
	}
	
	
	int addThreadIndex = 0;
	for(; addThreadIndex < THREAD_NUM && threadPool[addThreadIndex].active == true; addThreadIndex++){
	}
	
	if(addThreadIndex == THREAD_NUM){
		EndCritical(sr);
		return 0;
	}
	
	//interrupts push R0-R3, R12, PC, LR, PSR -> 8 total registers
	threadPool[addThreadIndex].stackPt = &(stackPool[addThreadIndex][STACKSIZE-1]);
	
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x01000000;	//PSR
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)task;				//PC
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)task;				//LR
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x12121212;	//R12
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x03030303;	//R3
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x02020202;	//R2
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x01010101;	//R1
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x00000000;	//R0
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x11111111;	//R11
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x10101010;	//R10
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)dataPt;			//R9
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x08080808;	//R8
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x07070707;	//R7
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x06060606;	//R6
	*(threadPool[addThreadIndex].stackPt--) = (int32_t)0x05050505;	//R5
	*(threadPool[addThreadIndex].stackPt) 	= (int32_t)0x04040404;	//R4
	
	threadPool[addThreadIndex].active = true;
	threadPool[addThreadIndex].listHead = false;
	threadPool[addThreadIndex].id = threadId;
	threadPool[addThreadIndex].priority = priority;
	threadPool[addThreadIndex].currentPCB = pcbEntry;
	
	threadId++;
	
	// if HeadRunPt is NULL, this thread is now the head
	/*
	if(HeadRunPt == NULL){
		HeadRunPt = &(threadPool[addThreadIndex]);
		// circular linkedlist of 1
		HeadRunPt->nextTCB = HeadRunPt;
		HeadRunPt->previousTCB = HeadRunPt;
	}else{
		struct TCB* currentTCB = &(threadPool[addThreadIndex]);
		currentTCB->previousTCB = HeadRunPt->previousTCB;
		currentTCB->nextTCB = HeadRunPt;
		HeadRunPt->previousTCB->nextTCB = currentTCB;
		HeadRunPt->previousTCB = currentTCB;
	}
	*/
	
	/*
	ActiveThreads.previousTCB->nextTCB = &threadPool[addThreadIndex];
	ActiveThreads.nextTCB->previousTCB = &threadPool[addThreadIndex];
	threadPool[addThreadIndex].nextTCB = ActiveThreads.nextTCB;
	threadPool[addThreadIndex].previousTCB = ActiveThreads.previousTCB;
	ActiveThreads.previousTCB = &threadPool[addThreadIndex];
	*/
	
	threadPool[addThreadIndex].nextTCB = &(ActiveThreads[priority]);
	threadPool[addThreadIndex].previousTCB = ActiveThreads[priority].previousTCB;
	ActiveThreads[priority].previousTCB->nextTCB = &threadPool[addThreadIndex];
	ActiveThreads[priority].previousTCB = &(threadPool[addThreadIndex]);
	
	
	EndCritical(sr);
	
  return 1; // replace this line with solution
};


static void killProcess(struct PCB* killedPCB){
	
	unsigned long sr = StartCritical();
	
	Heap_Free(killedPCB->data);
	Heap_Free(killedPCB->text);
	
	// unchain the killedPCB from list of PCBs
	killedPCB->nextPCB->prevPCB = killedPCB->prevPCB;
	killedPCB->prevPCB->nextPCB = killedPCB->nextPCB;
	
	Heap_Free(killedPCB);

	EndCritical(sr);
}
//******** OS_AddProcess *************** 
// add a process with foregound thread to the scheduler
// Inputs: pointer to a void/void entry point
//         pointer to process text (code) segment
//         pointer to process data segment
//         number of bytes allocated for its stack
//         priority (0 is highest)
// Outputs: 1 if successful, 0 if this process can not be added
// This function will be needed for Lab 5
// In Labs 2-4, this function can be ignored
int OS_AddProcess(void(*entry)(void), void *text, void *data, 
  unsigned long stackSize, unsigned long priority){
  // put Lab 5 solution here
		
	// save the text and data pointers in a pcb 
	// PCB needs entries for two int32_t pointers, total count of threads in the process
	// create reference of PCB, and add reference to TCB, so that when another thread of this process is added, the pcb can manage its threads
	// set r9 in the thread's stack to the data pointer
	// create the thread with addThread
		
	unsigned long sr = StartCritical();
		
	struct PCB* newPCB = Heap_Malloc(sizeof(struct PCB));
	if(!newPCB){
		EndCritical(sr);
		return 0;
	}
	newPCB->data = data;
	newPCB->text = text;
	newPCB->threadCount = 0;
		
	// add new process to the processes list
	newPCB->nextPCB = &Processes;
	newPCB->prevPCB = &Processes;
	Processes.prevPCB->nextPCB = newPCB;
	Processes.prevPCB = newPCB;
	
	dataPt = data;
	struct PCB* pcbPtSaver = PcbPt;
	PcbPt = newPCB;
	// temporarily change the global PcbPt to "pass" in the pointer to OS_AddThread
	int addThreadRes = OS_AddThread(entry, stackSize, priority);
	// PcbPt back to NULL
	PcbPt = pcbPtSaver;
	
	if(!addThreadRes){
		// kill the process here
		killProcess(newPCB);
		EndCritical(sr);
		return 0;
	}
	
  EndCritical(sr);
  return 1; // replace this line with Lab 5 solution
}


//******** OS_Id *************** 
// returns the thread ID for the currently running thread
// Inputs: none
// Outputs: Thread ID, number greater than zero 
uint32_t OS_Id(void){
  // put Lab 2 (and beyond) solution here
  return RunPt->id;
};


//******** OS_AddPeriodicThread *************** 
// add a background periodic task
// typically this function receives the highest priority
// Inputs: pointer to a void/void background function
//         period given in system time units (12.5ns)
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// You are free to select the time resolution for this function
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal   OS_AddThread
// This task does not have a Thread ID
// In lab 1, this command will be called 1 time
// In lab 2, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, this command will be called 0 1 or 2 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddPeriodicThread(void(*task)(void), 
   uint32_t period, uint32_t priority){
  // put Lab 2 (and beyond) solution here
	periodicThreadCount++;
	switch(periodicThreadCount){
	 case 1:
		WideTimer0A_Init(task, period, priority);
		break;
	 case 2:
		WideTimer1A_Init(task, period, priority);
		break;
	}
	
  return 0; // replace this line with solution
};


/*----------------------------------------------------------------------------
  PF1 Interrupt Handler
 *----------------------------------------------------------------------------*/

void (*AddSW1Task)(void);
void (*AddSW2Task)(void);

static void GPIOPortF_4DebounceTask(){
	OS_Sleep(20);
	GPIO_PORTF_ICR_R = 0x10;	// clear the PF4 interrupt flag
	GPIO_PORTF_IM_R |= 0x10;	// rearm PF4 interrupts
	OS_Kill();
}

static void GPIOPortF_0DebounceTask(){
	OS_Sleep(20);
	GPIO_PORTF_ICR_R = 0x01;	// clear the PF0 interrupt flag
	GPIO_PORTF_IM_R |= 0x01;	// rearm PF0 interrupts
	OS_Kill();
}

void GPIOPortF_Handler(void){
	
	// disable Port F interrupts
	// clear interrupt flag
	// start new thread that sleeps and will reenable Port F interrupts
	
	// NOTE: PF4 -> SW1
	// NOTE: PF0 -> SW2
	
	// is it possible for RIS to change servicing interrupt and when interrupt occured?
	// PF4
	if(GPIO_PORTF_RIS_R&0x10){
		GPIO_PORTF_ICR_R = 0x10;												// clear the PF4 interrupt flag
		GPIO_PORTF_IM_R &= ~0x10;												// disarm PF4 interrupts
		int retVal = OS_AddThread(&GPIOPortF_4DebounceTask, 128, 0);
		AddSW1Task();
	}
	// PF0
	if(GPIO_PORTF_RIS_R&0x01){
		GPIO_PORTF_ICR_R = 0x01;;												// clear the PF0 interrupt flag
		GPIO_PORTF_IM_R &= ~0x01;												// disarm PF0 interrupts
		int retVal = OS_AddThread(&GPIOPortF_0DebounceTask, 128, 0);
		AddSW2Task();
	}
}

//******** OS_AddSW1Task *************** 
// add a background task to run whenever the SW1 (PF4) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal   OS_AddThread
// This task does not have a Thread ID
// In labs 2 and 3, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW1Task(void(*task)(void), uint32_t priority){
  // put Lab 2 (and beyond) solution here
	
	AddSW1Task = task;
	SYSCTL_RCGCGPIO_R |= 0x00000020;													// activate clock for port F
	GPIO_PORTF_LOCK_R = 0x4C4F434B;														// unlock GPIO Port F						
	GPIO_PORTF_CR_R = 0x1F;																		// allow changes to PF4-0
	GPIO_PORTF_DIR_R &= ~0x10;																// make PF4, PF0 in
	GPIO_PORTF_DEN_R |= 0x10;																	// enable digital I/O on PF4, PF0
	GPIO_PORTF_PUR_R |= 0x10;																	// pull-up on PF4, PF0
	GPIO_PORTF_IS_R &= ~0x10;																	// PF4, PF0 are edge sensitive
	GPIO_PORTF_IBE_R &= ~0x10;
	GPIO_PORTF_IEV_R &= ~0x10;
	//GPIO_PORTF_IBE_R |= 0x11;																	// PF4, PF0 are both edges
	GPIO_PORTF_ICR_R = 0x10;																	// clear flags
	GPIO_PORTF_IM_R |= 0x10;																	// arm interrupts on PF4, PF0
	NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|(priority << 21);	// priority 
	NVIC_EN0_R = 0x40000000;																	// enable interrupt 30 in NVIC
	/*
	AddSW1Task = task;
	SYSCTL_RCGCGPIO_R |= 0x20;
	GPIO_PORTF_DIR_R	&= ~0x10;
	GPIO_PORTF_AFSEL_R	&= ~0x10;
	GPIO_PORTF_DEN_R	|= 0x10;
	GPIO_PORTF_PCTL_R	&= ~0x000F0000;
	GPIO_PORTF_AMSEL_R	&= ~0x10;
	GPIO_PORTF_PUR_R	|= 0x10;
	GPIO_PORTF_IS_R	&= ~0x10;
	GPIO_PORTF_IBE_R	&= ~0x10;
	GPIO_PORTF_IEV_R	&= ~0x10;
	GPIO_PORTF_ICR_R	= 0x10;
	GPIO_PORTF_IM_R	|= 0x10;
	NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|(priority << 21);
	NVIC_EN0_R = 0x40000000;
  return 0; // replace this line with solution
	*/
	return 0;
};

//******** OS_AddSW2Task *************** 
// add a background task to run whenever the SW2 (PF0) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is highest, 5 is lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed user task will run to completion and return
// This task can not spin block loop sleep or kill
// This task can call issue OS_Signal, it can call OS_AddThread
// This task does not have a Thread ID
// In lab 2, this function can be ignored
// In lab 3, this command will be called will be called 0 or 1 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW2Task(void(*task)(void), uint32_t priority){
  // put Lab 2 (and beyond) solution here
	
	AddSW2Task = task;
	SYSCTL_RCGCGPIO_R |= 0x00000020;													// activate clock for port F
	GPIO_PORTF_LOCK_R = 0x4C4F434B;														// unlock GPIO Port F						
	GPIO_PORTF_CR_R = 0x1F;																		// allow changes to PF4-0
	GPIO_PORTF_DIR_R &= ~-0x01;																// make PF4, PF0 in
	GPIO_PORTF_DEN_R |= 0x01;																	// enable digital I/O on PF4, PF0
	GPIO_PORTF_PUR_R |= 0x01;																	// pull-up on PF4, PF0
	GPIO_PORTF_IS_R &= ~0x01;																	// PF4, PF0 are edge sensitive
	GPIO_PORTF_IBE_R &= ~0x01;
	GPIO_PORTF_IEV_R &= ~0x01;
	//GPIO_PORTF_IBE_R |= 0x01;																	// PF4, PF0 are both edges
	GPIO_PORTF_ICR_R |= 0x01;																	// clear flags
	GPIO_PORTF_IM_R |= 0x01;																	// arm interrupts on PF4, PF0
	NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|(priority << 21);	// priority 
	NVIC_EN0_R = 0x40000000;																	// enable interrupt 30 in NVIC
  return 0; // replace this line with solution
	
};


// ******** OS_Sleep ************
// place this thread into a dormant state
// input:  number of msec to sleep
// output: none
// You are free to select the time resolution for this function
// OS_Sleep(0) implements cooperative multitasking
void OS_Sleep(uint32_t sleepTime){
  // put Lab 2 (and beyond) solution here
	
	// shouldn't be allowed to context switch when in here
	long sr = StartCritical();
	
	RunPt->sleepTime = sleepTime;
	/*
	
	// unchaining RunPt from active threads
	RunPt->previousTCB->nextTCB = RunPt->nextTCB;
	RunPt->nextTCB->previousTCB = RunPt->previousTCB;
	
	struct TCB* nextRunPt = RunPt->nextTCB;
	if(nextRunPt->listHead){
		nextRunPt = nextRunPt->nextTCB;
	}
	
	// chain RunPt to sleeping threads
	RunPt->previousTCB = SleepingThreads.previousTCB;
	RunPt->nextTCB = &(SleepingThreads);
	SleepingThreads.previousTCB->nextTCB = RunPt;
	SleepingThreads.previousTCB = RunPt;
	
	// set RunPt to next valid active thread
	RunPt = nextRunPt;
	*/
	
	struct TCB* sleepingThread = RunPt;
	// OS_Suspend will LSL TCB list and update RunPt with next valid thread
	OS_Suspend();
	
	// remove sleepingThread from its list and append to SleepingThreads list
	
	// unchaining sleepingThread from active threads
	sleepingThread->previousTCB->nextTCB = sleepingThread->nextTCB;
	sleepingThread->nextTCB->previousTCB = sleepingThread->previousTCB;
	
	// chain sleepingThread to sleeping threads
	sleepingThread->previousTCB = SleepingThreads.previousTCB;
	sleepingThread->nextTCB = &(SleepingThreads);
	SleepingThreads.previousTCB->nextTCB = sleepingThread;
	SleepingThreads.previousTCB = sleepingThread;
	
	EndCritical(sr);
	//OS_Suspend();
};  

// ******** OS_Kill ************
// kill the currently running thread, release its TCB and stack
// input:  none
// output: none
void OS_Kill(void){
  // put Lab 2 (and beyond) solution here
	DisableInterrupts();
	
	struct TCB* killThread = RunPt;
	// OS_Suspend will LSL TCB list and update RunPt with next valid thread
	OS_Suspend();
	
	// unchaining killThread from active threads
	killThread->previousTCB->nextTCB = killThread->nextTCB;
	killThread->nextTCB->previousTCB = killThread->previousTCB;
	
	// mark as inactive for addThread to overwrite TCB
	killThread->active = false;
	
	// Lab 5 addition for processes
	killThread->currentPCB->threadCount--;
	if(killThread->currentPCB->threadCount == 0){
		killProcess(killThread->currentPCB);
	}
	// reset the PCB pointer
	killThread->currentPCB = NULL;

  EnableInterrupts();   // end of atomic section 
  //for(;;){};        // can not return
    
}; 

// ******** OS_Suspend ************
// suspend execution of currently running thread
// scheduler will choose another thread to execute
// Can be used to implement cooperative multitasking 
// Same function as OS_Sleep(0)
// input:  none
// output: none
void OS_Suspend(void){
  // put Lab 2 (and beyond) solution here
	
	long sr = StartCritical();
	/*
	RunPt = RunPt->nextTCB;
	if(RunPt->listHead){
		RunPt = RunPt->nextTCB;
	}
	*/
	scheduler();
	
	INTCTRL = 0x10000000;
	// reset systick timer here:
	NVIC_ST_CURRENT_R = 0;
	
	EndCritical(sr);
	
	//INTCTRL = 0x10000000;

};
  
// ******** OS_Fifo_Init ************
// Initialize the Fifo to be empty
// Inputs: size
// Outputs: none 
// In Lab 2, you can ignore the size field
// In Lab 3, you should implement the user-defined fifo size
// In Lab 3, you can put whatever restrictions you want on size
//    e.g., 4 to 64 elements
//    e.g., must be a power of 2,4,8,16,32,64,128
void OS_Fifo_Init(uint32_t size){
  // put Lab 2 (and beyond) solution here
	OS_InitSemaphore(&dataAvailable, 0);			// data is not initially available
	getIFFIndex = 0;
	putIFFIndex = 0;
	isrToForegroundFIFOCount = 0;
};

// ******** OS_Fifo_Put ************
// Enter one data sample into the Fifo
// Called from the background, so no waiting 
// Inputs:  data
// Outputs: true if data is properly saved,
//          false if data not saved, because it was full
// Since this is called by interrupt handlers 
//  this function can not disable or enable interrupts
int OS_Fifo_Put(uint32_t data){
  // put Lab 2 (and beyond) solution here
	if(isrToForegroundFIFOCount == FIFOSIZE){
		return 0;
	}
	
	isrToForegroundFIFO[putIFFIndex] = data;
	putIFFIndex = (putIFFIndex+1)%FIFOSIZE;
	isrToForegroundFIFOCount++;
	
	OS_Signal(&dataAvailable);
	
	return 1;
};  

// ******** OS_Fifo_Get ************
// Remove one data sample from the Fifo
// Called in foreground, will spin/block if empty
// Inputs:  none
// Outputs: data 
uint32_t OS_Fifo_Get(void){
  // put Lab 2 (and beyond) solution here
	
	OS_Wait(&dataAvailable);
	
	long sr = StartCritical();
	int fifoVal = isrToForegroundFIFO[getIFFIndex];
	isrToForegroundFIFOCount--;
	getIFFIndex = (getIFFIndex+1)%FIFOSIZE;
	EndCritical(sr);
  
	return fifoVal; // replace this line with solution
};

// ******** OS_Fifo_Size ************
// Check the status of the Fifo
// Inputs: none
// Outputs: returns the number of elements in the Fifo
//          greater than zero if a call to OS_Fifo_Get will return right away
//          zero or less than zero if the Fifo is empty 
//          zero or less than zero if a call to OS_Fifo_Get will spin or block
int32_t OS_Fifo_Size(void){
  // put Lab 2 (and beyond) solution here
	
	return isrToForegroundFIFOCount;
};


// ******** OS_MailBox_Init ************
// Initialize communication channel
// Inputs:  none
// Outputs: none
void OS_MailBox_Init(void){
  // put Lab 2 (and beyond) solution here
  
	OS_InitSemaphore(&DataValid, 0);	// data should not be immediately available
	OS_InitSemaphore(&BoxFree, 1);		// mailbox should be immediately available

  // put solution here
};

// ******** OS_MailBox_Send ************
// enter mail into the MailBox
// Inputs:  data to be sent
// Outputs: none
// This function will be called from a foreground thread
// It will spin/block if the MailBox contains data not yet received 
void OS_MailBox_Send(uint32_t data){
  // put Lab 2 (and beyond) solution here
  // put solution here
	
	OS_bWait(&BoxFree);
	Mailbox = data;
	OS_bSignal(&DataValid);
	
};

// ******** OS_MailBox_Recv ************
// remove mail from the MailBox
// Inputs:  none
// Outputs: data received
// This function will be called from a foreground thread
// It will spin/block if the MailBox is empty 
uint32_t OS_MailBox_Recv(void){
  // put Lab 2 (and beyond) solution here
	
	uint32_t mailboxVal = 0;
	
	OS_bWait(&DataValid);
	mailboxVal = Mailbox;
	OS_bSignal(&BoxFree);
	
  return mailboxVal; // replace this line with solution
};

// ******** OS_Time ************
// return the system time 
// Inputs:  none
// Outputs: time in 12.5ns units, 0 to 4294967295
// The time resolution should be less than or equal to 1us, and the precision 32 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_TimeDifference have the same resolution and precision 
uint32_t OS_Time(void){
  // put Lab 2 (and beyond) solution here
	
	uint32_t ticks = TIMER5_TAV_R;
	//uint32_t ticks = 0;
	
	uint32_t msTicks = time.ms;
	msTicks *= TIMEPERIOD;
	
	uint32_t sTicks = time.s;
	sTicks *= 1000;
	sTicks *= TIMEPERIOD;
	
	uint32_t minTicks = time.min;
	minTicks *= 60;
	minTicks *= 1000;
	minTicks *= TIMEPERIOD;
	
	uint32_t totalTicks = (ticks + msTicks + sTicks + minTicks);

  return totalTicks; // output is in 12.5ns units
};

// ******** OS_TimeDifference ************
// Calculates difference between two times
// Inputs:  two times measured with OS_Time
// Outputs: time difference in 12.5ns units 
// The time resolution should be less than or equal to 1us, and the precision at least 12 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_Time have the same resolution and precision 
uint32_t OS_TimeDifference(uint32_t start, uint32_t stop){
  // put Lab 2 (and beyond) solution here
	
	if(stop > start){
		return stop - start;
	}
	
	uint32_t timerOverflowVal = 4294967295 - start + stop;
	
  return timerOverflowVal; // replace this line with solution
};

void OS_TimerIncrement(void){
	// following line needs launchpad_init() or else will hardfault!
	//PF2 ^= 0x04;
	//currently using  1ms timer
	if(time.ms == 999){
		time.ms = 0;
		if(time.s == 59){
			time.s = 0;
			if(time.min == 59){
				time.s = 59;
				time.ms = 999;
			}else{
				time.min++;
			}
		}else{
			time.s++;
		}
	}else{
		time.ms++;
	}
	
	//	Update sleeping thread timers
	//  current highest priority interrupt at priority 5
	
	long sr = StartCritical();
	
	struct TCB* sleepingThreadsTail = &SleepingThreads;
	struct TCB* sleepingThreadsPt = SleepingThreads.nextTCB;
	/*
	int currentThreadPriority = RunPt->priority;
	int highestWokenThreadPriority = 8;
	*/
	
	while(sleepingThreadsPt != sleepingThreadsTail){
		sleepingThreadsPt->sleepTime--;
		struct TCB* nextSleepingThread = sleepingThreadsPt->nextTCB;
		if(sleepingThreadsPt->sleepTime == 0){
			/*
			if(sleepingThreadsPt->priority < highestWokenThreadPriority){
				highestWokenThreadPriority = sleepingThreadsPt->priority;
			}
			*/
			// remove from sleeping list and back into active threads
			sleepingThreadsPt->nextTCB->previousTCB = sleepingThreadsPt->previousTCB;
			sleepingThreadsPt->previousTCB->nextTCB = sleepingThreadsPt->nextTCB;
			// place woken up thread back into active threads list
			sleepingThreadsPt->nextTCB = &(ActiveThreads[sleepingThreadsPt->priority]);
			sleepingThreadsPt->previousTCB = ActiveThreads[sleepingThreadsPt->priority].previousTCB;
			ActiveThreads[sleepingThreadsPt->priority].previousTCB->nextTCB = sleepingThreadsPt;
			ActiveThreads[sleepingThreadsPt->priority].previousTCB = sleepingThreadsPt;
		}
		sleepingThreadsPt = nextSleepingThread;
	}
	/*
	if(highestWokenThreadPriority < currentThreadPriority){
		OS_Suspend();
	}
	*/
	EndCritical(sr);
	
}

void (*PeriodicTask5)(void);   // user function

void Timer5A_Init(void(*task)(void), uint32_t period, uint32_t priority){
  SYSCTL_RCGCTIMER_R |= 0x20;   // 0) activate TIMER5
  PeriodicTask5 = task;         // user function
  TIMER5_CTL_R = 0x00000000;    // 1) disable TIMER5A during setup
  TIMER5_CFG_R = 0x00000000;    // 2) configure for 32-bit mode
  TIMER5_TAMR_R = 0x00000002;   // 3) configure for periodic mode, default down-count settings
  TIMER5_TAILR_R = period-1;    // 4) reload value
  TIMER5_TAPR_R = 0;            // 5) bus clock resolution
  TIMER5_ICR_R = 0x00000001;    // 6) clear TIMER5A timeout flag
  TIMER5_IMR_R = 0x00000001;    // 7) arm timeout interrupt
  NVIC_PRI23_R = (NVIC_PRI23_R&0xFFFFFF00)|(priority<<5); // priority 
// interrupts enabled in the main program after all devices initialized
// vector number 108, interrupt number 92
  NVIC_EN2_R = 1<<28;           // 9) enable IRQ 92 in NVIC
  TIMER5_CTL_R = 0x00000001;    // 10) enable TIMER5A
}

void Timer5A_Handler(void){
  TIMER5_ICR_R = TIMER_ICR_TATOCINT;// acknowledge TIMER5A timeout
  (*PeriodicTask5)();               // execute user task
}
void Timer5_Stop(void){
  NVIC_DIS2_R = 1<<28;          // 9) disable interrupt 92 in NVIC
  TIMER5_CTL_R = 0x00000000;    // 10) disable timer5A
}

// ******** OS_ClearMsTime ************
// sets the system time to zero (solve for Lab 1), and start a periodic interrupt
// Inputs:  none
// Outputs: none
// You are free to change how this works
void OS_ClearMsTime(void){
  // put Lab 1 solution here
	// interrupt every 5ms
	if(timeStarted){
		Timer5_Stop();
	}
	
	time.ms = 0;
	time.s = 0;
	time.min = 0;
	
	//1ms == 1000000 ns
	//ticks = 1000000 / (12.5ns/tick)
	//ticks = 80000
	//Timer5A_Init(OS_TimerIncrement, 80000, 5);
	
	//testing out 40000 ticks, aka 2Khz
	Timer5A_Init(OS_TimerIncrement, TIMEPERIOD, 5);
	timeStarted = true;
	
};

// ******** OS_MsTime ************
// reads the current time in msec (solve for Lab 1)
// Inputs:  none
// Outputs: time in ms units
// You are free to select the time resolution for this function
// For Labs 2 and beyond, it is ok to make the resolution to match the first call to OS_AddPeriodicThread
uint32_t OS_MsTime(void){
  // put Lab 1 solution here
	uint32_t timeMuliplier = 0;
	
	//uint32_t timerTickMs = (TIMER5_TAV_R * 12.5)/1000000;
	uint32_t timerTickMs = 0;
	uint32_t timerMs = time.ms;
	uint32_t timerS = time.s * 1000;
	uint32_t timerMin = time.min * 60 * 1000;
	
	if(TIMEPERIOD > TIME_1MS){
		timeMuliplier = TIMEPERIOD/TIME_1MS;
		timerTickMs *= timeMuliplier;
		timerMs *= timeMuliplier;
		timerMin *= timeMuliplier;
		timerMin *= timeMuliplier;
	}else{
		timeMuliplier = TIME_1MS/TIMEPERIOD;
		timerTickMs /= timeMuliplier;
		timerMs /= timeMuliplier;
		timerMin /= timeMuliplier;
		timerMin /= timeMuliplier;
	}
	
	uint32_t total_ms = timerTickMs + timerMs + timerS + timerMin;
	return total_ms;
};


//******** OS_Launch *************** 
// start the scheduler, enable interrupts
// Inputs: number of 12.5ns clock cycles for each time slice
//         you may select the units of this parameter
// Outputs: none (does not return)
// In Lab 2, you can ignore the theTimeSlice field
// In Lab 3, you should implement the user-defined TimeSlice field
// It is ok to limit the range of theTimeSlice to match the 24-bit SysTick
void OS_Launch(uint32_t theTimeSlice){
  // put Lab 2 (and beyond) solution here
	
	// set RunPt here to startOS
	// be careful about removing all threads because OS_Addthread will not save you
	// add a idle thread to get around this. OS_Addthread should not interact with RunPt
	SysTick_Init(theTimeSlice);
	
	// pick the first thread with the highest priority
	int firstActiveThreadIndex = 0;
	for(int i = 0; i < PRIORITY_NUM; i++){
		if(ActiveThreads[i].nextTCB != &(ActiveThreads[i])){
			firstActiveThreadIndex = i;
			break;
		}
	}
	
	RunPt = ActiveThreads[firstActiveThreadIndex].nextTCB;
	StackPt = &(ActiveThreads[firstActiveThreadIndex].nextTCB->stackPt);
	
  //RunPt = ActiveThreads.nextTCB;
	//StackPt = &(ActiveThreads.nextTCB->stackPt);
	
	// interrupt bookkeeping:
	// Timer5A -> priority 5
	// Systick -> priority 7
	// PendSV  -> priority 7
	StartOS();

};


