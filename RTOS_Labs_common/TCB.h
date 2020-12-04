#ifndef __TCB_H
#define __TCB_H  1
#include <stdint.h>
#include <stdbool.h>


struct PCB{
	struct PCB* nextPCB;
	struct PCB* prevPCB;
	bool listHead;
	int32_t* data;
	int32_t* text;
	int32_t threadCount;
};

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
	// added for lab 5
	struct PCB* currentPCB;
};

#endif
