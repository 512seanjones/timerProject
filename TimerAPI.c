// Header Files
#include "TypeDefines.h"
#include "TimerMgrHeader.h"
#include "TimerAPI.h"
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

/*****************************************************
 * Global Variables
 *****************************************************
 */
// Timer Pool Global Variables
INT8U FreeTmrCount = 0;
RTOS_TMR *FreeTmrListPtr = NULL;

// Tick Counter
INT32U RTOSTmrTickCtr = 0;

// Hash Table
HASH_OBJ hash_table[HASH_TABLE_SIZE];

// Semaphore for Signaling the Timer Task
sem_t timer_task_sem;

// Mutex for Protecting Hash Table
pthread_mutex_t hash_table_mutex;

// Mutex for Protecting Timer Pool
pthread_mutex_t timer_pool_mutex;

/*****************************************************
 * Timer API Functions
 *****************************************************
 */

// Function to create a Timer
RTOS_TMR* RTOSTmrCreate(INT32U delay, INT32U period, INT8U option, RTOS_TMR_CALLBACK callback, void *callback_arg, INT8	*name, INT8U *err)
{
	RTOS_TMR *timer_obj = NULL;

	// Check the input Arguments for ERROR

	// Check for invalid delay

	if (option == RTOS_TMR_PERIODIC) {
		if (delay < 0){
			*err = RTOS_ERR_TMR_INVALID_DLY;
        	return NULL;
		}
		if (period < 1){
			*err = RTOS_ERR_TMR_INVALID_PERIOD;
        	return NULL;
		}
	}
	else if (option == RTOS_TMR_ONE_SHOT) {
		if (delay < 1){
			*err = RTOS_ERR_TMR_INVALID_DLY;
        	return NULL;
		}
	}
	else {
		*err = RTOS_ERR_TMR_INVALID_OPT;
        return NULL;
	}

	// Allocate a New Timer Obj
	timer_obj = alloc_timer_obj();

	if(timer_obj == NULL) {
		// Timers are not available
		*err = RTOS_ERR_TMR_NON_AVAIL;
		return NULL;
	}

	*err = RTOS_ERR_NONE;

	// Fill up the Timer Object with inputs
	timer_obj->RTOSTmrType = RTOS_TMR_TYPE;
	// Period and delay are being input in seconds
	// Need to convert them to ticks
	// Tick occurs every 100 ms so this will be 10 ticks per second
	timer_obj->RTOSTmrDelay = delay*10;
	timer_obj->RTOSTmrPeriod = period*10;
	timer_obj->RTOSTmrOpt = option;
	timer_obj->RTOSTmrCallback = callback;
	timer_obj->RTOSTmrCallbackArg = callback_arg;
	timer_obj->RTOSTmrName = name;

	// Set pointers and state
	timer_obj->RTOSTmrNext = NULL;
	timer_obj->RTOSTmrPrev = NULL;
	timer_obj->RTOSTmrState = RTOS_TMR_STATE_STOPPED;
	timer_obj->RTOSTmrMatch = 0;

	return timer_obj;
}

// Function to Delete a Timer
INT8U RTOSTmrDel(RTOS_TMR *ptmr, INT8U *perr)
{
	// ERROR Checking
    if(ptmr == NULL) {
        *perr = RTOS_ERR_TMR_INVALID;
        return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrType != RTOS_TMR_TYPE){
    	*perr = RTOS_ERR_TMR_INVALID_TYPE;
    	return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_UNUSED){
        *perr = RTOS_ERR_TMR_INACTIVE;
        return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState != RTOS_TMR_STATE_STOPPED && ptmr->RTOSTmrState != RTOS_TMR_STATE_RUNNING && ptmr->RTOSTmrState != RTOS_TMR_STATE_COMPLETED){
        *perr = RTOS_ERR_TMR_INVALID_STATE;
        return RTOS_FALSE;
    }
    *perr = RTOS_ERR_NONE;

	// Free Timer Object according to its State

	// Need to stop the timer if it is running
	if (ptmr->RTOSTmrState == RTOS_TMR_STATE_RUNNING){
		// Don't want a callback, just want to delete it
    	RTOSTmrStop(ptmr, RTOS_TMR_OPT_NONE, NULL, perr);
    	// Make sure the timer was stopped
    	if (perr != RTOS_ERR_NONE)
    		return RTOS_FALSE;
    }
    // Now we can delete it
	remove_hash_entry(ptmr);
	free_timer_obj(ptmr);

	return RTOS_TRUE;
}

// Function to get the Name of a Timer
INT8* RTOSTmrNameGet(RTOS_TMR *ptmr, INT8U *perr)
{
	// ERROR Checking
	if(ptmr == NULL) {
		*perr = RTOS_ERR_TMR_INVALID;
		return NULL;
	}
	if(ptmr->RTOSTmrType != RTOS_TMR_TYPE){
    	*perr = RTOS_ERR_TMR_INVALID_TYPE;
    	return NULL;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_UNUSED){
        *perr = RTOS_ERR_TMR_INACTIVE;
        return NULL;
    }
    if(ptmr->RTOSTmrState != RTOS_TMR_STATE_STOPPED && ptmr->RTOSTmrState != RTOS_TMR_STATE_RUNNING && ptmr->RTOSTmrState != RTOS_TMR_STATE_COMPLETED){
        *perr = RTOS_ERR_TMR_INVALID_STATE;
        return NULL;
    }
	*perr = RTOS_ERR_NONE;

	// Return the Pointer to the String
	return ptmr->RTOSTmrName;
}

// To Get the Number of ticks remaining in time out
INT32U RTOSTmrRemainGet(RTOS_TMR *ptmr, INT8U *perr)
{
	// ERROR Checking
	if(ptmr == NULL) {
		*perr = RTOS_ERR_TMR_INVALID;
		return RTOS_FALSE;
	}
	if(ptmr->RTOSTmrType != RTOS_TMR_TYPE){
    	*perr = RTOS_ERR_TMR_INVALID_TYPE;
    	return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_UNUSED){
        *perr = RTOS_ERR_TMR_INACTIVE;
        return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState != RTOS_TMR_STATE_STOPPED && ptmr->RTOSTmrState != RTOS_TMR_STATE_RUNNING && ptmr->RTOSTmrState != RTOS_TMR_STATE_COMPLETED){
        *perr = RTOS_ERR_TMR_INVALID_STATE;
        return RTOS_FALSE;
    }
	*perr = RTOS_ERR_NONE;

	// Return the remaining ticks
	return ptmr->RTOSTmrMatch - RTOSTmrTickCtr;
}

// To Get the state of the Timer
INT8U RTOSTmrStateGet(RTOS_TMR *ptmr, INT8U *perr)
{
	// ERROR Checking
	if(ptmr == NULL) {
		*perr = RTOS_ERR_TMR_INVALID;
		return RTOS_FALSE;
	}
	if(ptmr->RTOSTmrType != RTOS_TMR_TYPE){
    	*perr = RTOS_ERR_TMR_INVALID_TYPE;
    	return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_UNUSED){
        *perr = RTOS_ERR_TMR_INACTIVE;
        return RTOS_TMR_STATE_UNUSED;
    }
    if(ptmr->RTOSTmrState != RTOS_TMR_STATE_STOPPED && ptmr->RTOSTmrState != RTOS_TMR_STATE_RUNNING && ptmr->RTOSTmrState != RTOS_TMR_STATE_COMPLETED){
        *perr = RTOS_ERR_TMR_INVALID_STATE;
        return RTOS_FALSE;
    }
	*perr = RTOS_ERR_NONE;

	// Return State
	return ptmr->RTOSTmrState;
}

// Function to start a Timer
INT8U RTOSTmrStart(RTOS_TMR *ptmr, INT8U *perr)
{
	// ERROR Checking
	if(ptmr == NULL) {
		*perr = RTOS_ERR_TMR_INVALID;
		return RTOS_FALSE;
	}
	if(ptmr->RTOSTmrType != RTOS_TMR_TYPE){
    	*perr = RTOS_ERR_TMR_INVALID_TYPE;
    	return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_UNUSED){
        *perr = RTOS_ERR_TMR_INACTIVE;
        return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState != RTOS_TMR_STATE_STOPPED && ptmr->RTOSTmrState != RTOS_TMR_STATE_RUNNING && ptmr->RTOSTmrState != RTOS_TMR_STATE_COMPLETED){
        *perr = RTOS_ERR_TMR_INVALID_STATE;
        return RTOS_FALSE;
    }
	*perr = RTOS_ERR_NONE;

	// Based on the Timer State, update the RTOSTmrMatch using RTOSTmrTickCtr, RTOSTmrDelay and RTOSTmrPeriod
	// You may use the Hash Table to Insert the Running Timer Obj

	ptmr->RTOSTmrState = RTOS_TMR_STATE_RUNNING;

	// If the timer is configured for PERIODIC mode, delay is the first timeout to wait for
	// before the timer starts entering periodic mode
	// If delay is zero, will just start in periodic
	if (ptmr->RTOSTmrOpt == RTOS_TMR_PERIODIC && ptmr->RTOSTmrDelay == 0)
        ptmr->RTOSTmrDelay = ptmr->RTOSTmrPeriod;
	ptmr->RTOSTmrMatch = RTOSTmrTickCtr + ptmr->RTOSTmrDelay;
    ptmr->RTOSTmrDelay = 0;

    insert_hash_entry(ptmr);

    return RTOS_TRUE;
}

// Function to Stop the Timer
INT8U RTOSTmrStop(RTOS_TMR *ptmr, INT8U opt, void *callback_arg, INT8U *perr)
{
	// ERROR Checking
	if(ptmr == NULL) {
		*perr = RTOS_ERR_TMR_INVALID;
		return RTOS_FALSE;
	}
	if(ptmr->RTOSTmrType != RTOS_TMR_TYPE){
    	*perr = RTOS_ERR_TMR_INVALID_TYPE;
    	return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_UNUSED){
        *perr = RTOS_ERR_TMR_INACTIVE;
        return RTOS_TRUE;
    }
	if(opt != RTOS_TMR_OPT_NONE && opt != RTOS_TMR_OPT_CALLBACK && opt != RTOS_TMR_OPT_CALLBACK_ARG){
        *perr = RTOS_ERR_TMR_INVALID_OPT;
        return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrState == RTOS_TMR_STATE_STOPPED){
        *perr = RTOS_ERR_TMR_STOPPED;
        return RTOS_TRUE;
    }
    if(ptmr->RTOSTmrState != RTOS_TMR_STATE_RUNNING && ptmr->RTOSTmrState != RTOS_TMR_STATE_COMPLETED){
        *perr = RTOS_ERR_TMR_INVALID_STATE;
        return RTOS_FALSE;
    }
    if(ptmr->RTOSTmrCallback == NULL && opt == RTOS_TMR_OPT_CALLBACK){
        *perr = RTOS_ERR_TMR_NO_CALLBACK;
        return RTOS_FALSE;
    }
    if(callback_arg == NULL && opt == RTOS_TMR_OPT_CALLBACK_ARG){
        *perr = RTOS_ERR_TMR_NO_CALLBACK;
        return RTOS_FALSE;
    }
	*perr = RTOS_ERR_NONE;

	// Remove the Timer from the Hash Table List
	remove_hash_entry(ptmr);

	// Change the State to Stopped
	ptmr->RTOSTmrState = RTOS_TMR_STATE_STOPPED;

	// Call the Callback function if required
	if(opt == RTOS_TMR_OPT_CALLBACK){
        ptmr->RTOSTmrCallback(ptmr->RTOSTmrCallbackArg);
    }
    if(opt == RTOS_TMR_OPT_CALLBACK_ARG){
        ptmr->RTOSTmrCallback(callback_arg);
    }
    return RTOS_TRUE;
}

// Function called when OS Tick Interrupt Occurs which will signal the RTOSTmrTask() to update the Timers
void RTOSTmrSignal(int signum)
{
	// Received the OS Tick
	// Send the Signal to Timer Task using the Semaphore
	sem_post(&timer_task_sem);
}

/*****************************************************
 * Internal Functions
 *****************************************************
 */

// Create Pool of Timers
INT8U Create_Timer_Pool(INT32U timer_count)
{
	// Create the Timer pool using Dynamic Memory Allocation
	// You can imagine of LinkedList Creation for Timer Obj
	FreeTmrCount = timer_count;

	for (int i=0; i<timer_count; i++){
		RTOS_TMR *tmr = malloc(sizeof(RTOS_TMR));
		tmr->RTOSTmrType = RTOS_TMR_TYPE;
		tmr->RTOSTmrCallback = NULL;
		tmr->RTOSTmrCallbackArg = NULL;
		tmr->RTOSTmrMatch = 0;
		tmr->RTOSTmrDelay = 0;
		tmr->RTOSTmrPeriod = 0;
		tmr->RTOSTmrName = NULL;
		tmr->RTOSTmrOpt = 0;
		tmr->RTOSTmrState = RTOS_TMR_STATE_UNUSED;
		tmr->RTOSTmrNext = NULL;
		if (i == 0) {
			tmr->RTOSTmrPrev = NULL;
			tmr->RTOSTmrNext = NULL;
			FreeTmrListPtr = tmr;
		} 
		else {
			tmr->RTOSTmrNext = FreeTmrListPtr;
			tmr->RTOSTmrPrev = NULL;
			FreeTmrListPtr->RTOSTmrPrev = tmr;
			FreeTmrListPtr = tmr;
		}
	}

	return RTOS_SUCCESS;
}

// Initialize the Hash Table
void init_hash_table(void)
{	
	// Make sure everything is empty
	for (int i=0; i<HASH_TABLE_SIZE; i++){
		hash_table[i].timer_count = 0;
		hash_table[i].list_ptr = NULL;
	}
}

// Insert a Timer Object in the Hash Table
void insert_hash_entry(RTOS_TMR *timer_obj)
{
	// Calculate the index using Hash Function
	int idx = (RTOSTmrTickCtr + timer_obj->RTOSTmrDelay) % HASH_TABLE_SIZE;
	
	// Lock the Resources
	pthread_mutex_lock(&hash_table_mutex);

	// Add the Entry
	if (!hash_table[idx].list_ptr)
		hash_table[idx].list_ptr = timer_obj;
	else{
		// Just place timer at beginning of list
		// TmrTask will perform a linear search
		RTOS_TMR *temp = hash_table[idx].list_ptr;
		timer_obj->RTOSTmrNext = hash_table[idx].list_ptr;
		hash_table[idx].list_ptr->RTOSTmrPrev = timer_obj;
		timer_obj->RTOSTmrPrev = NULL;
		hash_table[idx].list_ptr = timer_obj;		
	}
	hash_table[idx].timer_count++;

	// Unlock the Resources
	pthread_mutex_unlock(&hash_table_mutex);
}

// Remove the Timer Object entry from the Hash Table
void remove_hash_entry(RTOS_TMR *timer_obj)
{
	// Calculate the index using Hash Function
	int idx = (RTOSTmrTickCtr + timer_obj->RTOSTmrDelay) % HASH_TABLE_SIZE;

	// Lock the Resources
	pthread_mutex_lock(&hash_table_mutex);

	// Remove the Timer Obj
	if (hash_table[idx].timer_count == 1){
		hash_table[idx].list_ptr == NULL;
		hash_table[idx].timer_count = 0;
	}
	else {
		// timer_obj is first entry
		if (timer_obj == hash_table[idx].list_ptr){
			timer_obj->RTOSTmrNext->RTOSTmrPrev = NULL;
			hash_table[idx].list_ptr = timer_obj->RTOSTmrNext;
		}
		// timer_obj is last entry
		else if (timer_obj->RTOSTmrNext == NULL){
			timer_obj->RTOSTmrPrev->RTOSTmrNext = NULL;
		}
		else {
			timer_obj->RTOSTmrNext->RTOSTmrPrev = timer_obj->RTOSTmrPrev;
			timer_obj->RTOSTmrPrev->RTOSTmrNext = timer_obj->RTOSTmrNext;
		}
		hash_table[idx].timer_count--;
	}

	// Unlock the Resources
	pthread_mutex_unlock(&hash_table_mutex);
}

// Timer Task to Manage the Running Timers
void *RTOSTmrTask(void *temp)
{
	RTOS_TMR *tmr;
	INT8U perr;
	INT32U ret;
	while(1) {
		// Wait for the signal from RTOSTmrSignal()
		sem_wait(&timer_task_sem);
		// Once got the signal, Increment the Timer Tick Counter
		RTOSTmrTickCtr++;

		// Check the whole List associated with the index of the Hash Table
		int idx = RTOSTmrTickCtr % HASH_TABLE_SIZE;

		// Compare each obj of linked list for Timer Completion
		// If the Timer is completed, Call its Callback Function, Remove the entry from Hash table
		// If the Timer is Periodic then again insert it in the hash table
		// Change the State
		tmr = hash_table[idx].list_ptr;
		while(tmr != NULL){
			ret = RTOSTmrRemainGet(tmr, &perr);
			if (perr != RTOS_ERR_NONE)
				continue;
			if (ret == 0){
				RTOSTmrStop(tmr, RTOS_TMR_OPT_CALLBACK, NULL, &perr);
				if (perr != RTOS_ERR_NONE)
					continue;
				tmr->RTOSTmrState = RTOS_TMR_STATE_COMPLETED;
	            if(tmr->RTOSTmrOpt == RTOS_TMR_PERIODIC){
	                RTOSTmrStart(tmr, &perr);
	                if (perr != RTOS_ERR_NONE)
						continue;
	            }
			}
			tmr = tmr->RTOSTmrNext;
		}
	}
	return temp;
}

// Timer Initialization Function
void RTOSTmrInit(void)
{
	INT32U timer_count = 0;
	INT8U	retVal;
	pthread_attr_t attr;

	fprintf(stdout,"\n\nPlease Enter the number of Timers required in the Pool for the OS ");
	scanf("%d", &timer_count);

	// Create Timer Pool
	retVal = Create_Timer_Pool(timer_count);

	// Check the return Value
	if (retVal != RTOS_SUCCESS){
		fprintf(stdout, "Error creating the timer pool\n");
		return;
	}

	// Init Hash Table
	init_hash_table();

	fprintf(stdout, "\n\nHash Table Initialized Successfully\n");

	// Initialize Semaphore for Timer Task
	sem_init(&timer_task_sem, 0, 0);

	// Initialize Mutex if any
	pthread_mutex_init(&hash_table_mutex, NULL);
	pthread_mutex_init(&timer_pool_mutex, NULL);

	// Create any Thread if required for Timer Task
	pthread_create(&thread, NULL, RTOSTmrTask, NULL);

	fprintf(stdout,"\nRTOS Initialization Done...\n");
}

// Allocate a timer object from free timer pool
RTOS_TMR* alloc_timer_obj(void)
{
	// Lock the Resources
	pthread_mutex_lock(&timer_pool_mutex);

	// Check for Availability of Timers
	if (FreeTmrCount == 0){
		fprintf(stdout, "Failed to allocated timer object, no free timers\n");
		return NULL;
	}

	// Assign the Timer Object
	RTOS_TMR *timer_obj = FreeTmrListPtr;

	if (FreeTmrCount == 1){
        FreeTmrListPtr = NULL;
    }
    else {
        FreeTmrListPtr = FreeTmrListPtr->RTOSTmrNext;
        FreeTmrListPtr->RTOSTmrPrev = NULL;
    }

    FreeTmrCount--;

	// Unlock the Resources
	pthread_mutex_unlock(&timer_pool_mutex);

	return timer_obj;
}

// Free the allocated timer object and put it back into free pool
void free_timer_obj(RTOS_TMR *ptmr)
{
	// Lock the Resources
	pthread_mutex_lock(&timer_pool_mutex);

	// Clear the Timer Fields
	ptmr->RTOSTmrPeriod = 0;
	ptmr->RTOSTmrDelay = 0;

	// Change the State
	ptmr->RTOSTmrState = RTOS_TMR_STATE_UNUSED;

	// Return the Timer to Free Timer Pool
	ptmr->RTOSTmrNext = FreeTmrListPtr;
	ptmr->RTOSTmrPrev = NULL; // timer will be placed at head of the list
	FreeTmrListPtr->RTOSTmrPrev = ptmr;
	FreeTmrListPtr = ptmr;
	FreeTmrCount++;

	// Unlock the Resources
	pthread_mutex_unlock(&timer_pool_mutex);
}

// Function to Setup the Timer of Linux which will provide the Clock Tick Interrupt to the Timer Manager Module
void OSTickInitialize(void) {	
	timer_t timer_id;
	struct itimerspec time_value;

	// Setup the time of the OS Tick as 100 ms after 3 sec of Initial Delay
	time_value.it_interval.tv_sec = 0;
	time_value.it_interval.tv_nsec = RTOS_CFG_TMR_TASK_RATE;

	time_value.it_value.tv_sec = 0;
	time_value.it_value.tv_nsec = RTOS_CFG_TMR_TASK_RATE;

	// Change the Action of SIGALRM to call a function RTOSTmrSignal()
	signal(SIGALRM, &RTOSTmrSignal);

	// Create the Timer Object
	timer_create(CLOCK_REALTIME, NULL, &timer_id);

	// Start the Timer
	timer_settime(timer_id, 0, &time_value, NULL);
}