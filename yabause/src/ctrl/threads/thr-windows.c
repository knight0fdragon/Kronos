/*  src/thr-windows.c: Windows thread functions
    Copyright 2013 Theo Berkau. Based on code by Andrew Church and Lawrence Sebald.

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file thr-windows.c
    \brief Windows port's threading functions
*/

#include <windows.h>
#include <WinDef.h>
#include "core.h"
#include "threads.h"

struct thd_s {
   int running;
   HANDLE thd;
   void* (*func)(void *);
   void *arg;
   CRITICAL_SECTION mutex;
   HANDLE cond;
};

static struct thd_s thread_handle[YAB_NUM_THREADS];
static int hnd_key;
static int hnd_key_once=FALSE;

//////////////////////////////////////////////////////////////////////////////

static DWORD wrapper(void *hnd)
{
   struct thd_s *hnds = (struct thd_s *)hnd;

   EnterCriticalSection(&hnds->mutex);

   /* Set the handle for the thread, and call the actual thread function. */
   TlsSetValue(hnd_key, hnd);
   hnds->func(hnds->arg);

   LeaveCriticalSection(&hnds->mutex);

   return 0;
}

int YabThreadStart(unsigned int id, void* (*func)(void *), void *arg)
{
   if (!hnd_key_once)
   {
      hnd_key=TlsAlloc();
      hnd_key_once = 1;
   }

   if (thread_handle[id].running)
   {
      fprintf(stderr, "YabThreadStart: thread %u is already started!\n", id);
      return -1;
   }

   // Create CS and condition variable for thread
   InitializeCriticalSection(&thread_handle[id].mutex);
   if ((thread_handle[id].cond = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL)
   {
      perror("CreateEvent");
   	  return -1;
   }

   thread_handle[id].func = func;
   thread_handle[id].arg = arg;

   if ((thread_handle[id].thd = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)wrapper, &thread_handle[id], 0, NULL)) == NULL)
   {
      perror("CreateThread");
      return -1;
   }

   thread_handle[id].running = 1;

   return 0;
}

void YabThreadWait(unsigned int id)
{
   if (!thread_handle[id].thd)
      return;  // Thread wasn't running in the first place

   WaitForSingleObject(thread_handle[id].thd,INFINITE);
   CloseHandle(thread_handle[id].thd);
   thread_handle[id].thd = NULL;
   thread_handle[id].running = 0;
   if (thread_handle[id].cond)
   	   CloseHandle(thread_handle[id].cond);
}

void YabThreadYield(void)
{
	SleepEx(0, 0);
}

u32 YabThreadUSleep( u32 stime )
{
	SleepEx(stime/1000, 0);
  return stime%1000;
}

void YabThreadWake(unsigned int id)
{
   if (!thread_handle[id].thd)
      return;  // Thread wasn't running in the first place

   SetEvent(thread_handle[id].cond);
}

typedef struct YabEventQueue_win32
{
	void** buffer;
	int capacity;
	int size;
	int in;
	int out;
	SRWLOCK mutex;
	CONDITION_VARIABLE cond_full;
	CONDITION_VARIABLE cond_empty;
} YabEventQueue_win32;


YabEventQueue * YabThreadCreateQueue(int qsize){
	YabEventQueue_win32 * p = (YabEventQueue_win32*)malloc(sizeof(YabEventQueue_win32));
	p->buffer = (void**)malloc(sizeof(void*)* qsize);
	p->capacity = qsize;
	p->size = 0;
	p->in = 0;
	p->out = 0;
  InitializeSRWLock(&p->mutex);
  InitializeConditionVariable (&p->cond_full);
  InitializeConditionVariable (&p->cond_empty);
	return (YabEventQueue *)p;
}

void YabThreadDestoryQueue(YabEventQueue * queue_t){
  YabEventQueue_win32 * queue = (YabEventQueue_win32*)queue_t;
  AcquireSRWLockExclusive(&queue->mutex);
  while (queue->size == queue->capacity) {
    SleepConditionVariableSRW(&(queue->cond_full), &(queue->mutex),INFINITE, 0);
  }
  free(queue->buffer);
  free(queue);
  ReleaseSRWLockExclusive(&queue->mutex);
}



void YabAddEventQueue(YabEventQueue * queue_t, void* evcode){
  YabEventQueue_win32 * queue = (YabEventQueue_win32*)queue_t;
  AcquireSRWLockExclusive(&(queue->mutex));
  while (queue->size == queue->capacity){
    SleepConditionVariableSRW(&(queue->cond_full), &(queue->mutex),INFINITE, 0);
  }
  queue->buffer[queue->in] = evcode;
  ++queue->size;
  ++queue->in;
  queue->in %= queue->capacity;
  ReleaseSRWLockExclusive(&(queue->mutex));
  WakeConditionVariable(&queue->cond_empty);
}


void* YabWaitEventQueue(YabEventQueue * queue_t){
  void* value;
  YabEventQueue_win32 * queue = (YabEventQueue_win32*)queue_t;
  AcquireSRWLockExclusive(&(queue->mutex));
  while (queue->size == 0){
    SleepConditionVariableSRW(&(queue->cond_empty), &(queue->mutex),INFINITE, 0);
  }

  value = queue->buffer[queue->out];
  --queue->size;
  ++queue->out;
  queue->out %= queue->capacity;
  ReleaseSRWLockExclusive(&(queue->mutex));
  WakeConditionVariable(&queue->cond_full);
  return value;
}

int YaGetQueueSize(YabEventQueue * queue_t){
  int size = 0;
  YabEventQueue_win32 * queue = (YabEventQueue_win32*)queue_t;
  AcquireSRWLockExclusive(&(queue->mutex));
  size = queue->size;
  ReleaseSRWLockExclusive(&(queue->mutex));
  return size;
}

typedef struct YabSem_win32
{
  HANDLE sem;
} YabSem_win32;

void YabSemPost( YabSem * mtx ){
    YabSem_win32 * pmtx;
    pmtx = (YabSem_win32 *)mtx;
    ReleaseSemaphore(pmtx->sem, 1, NULL);
}

void YabSemWait( YabSem * mtx ){
    YabSem_win32 * pmtx;
    pmtx = (YabSem_win32 *)mtx;
    WaitForSingleObject(pmtx->sem, 0L);
}

int YabSemTryWait( YabSem * mtx ){
    YabSem_win32 * pmtx;
    pmtx = (YabSem_win32 *)mtx;
    return WaitForSingleObject(pmtx->sem, 100L);
}

YabSem * YabThreadCreateSem(int val){
    YabSem_win32 * mtx = (YabSem_win32 *)malloc(sizeof(YabSem_win32));
    mtx->sem = CreateSemaphore( NULL, val, val, NULL);
    return (YabMutex *)mtx;
}

void YabThreadFreeSem( YabSem * mtx ){
    if( mtx != NULL ){
        YabSem_win32 * pmtx;
        pmtx = (YabSem_win32 *)mtx;
        CloseHandle(pmtx->sem);
        free(pmtx);
    }
}


typedef struct YabMutex_win32
{
	CRITICAL_SECTION mutex;
} YabMutex_win32;

void YabThreadLock(YabMutex * mtx){
	YabMutex_win32 * pmtx;
	pmtx = (YabMutex_win32 *)mtx;
  if (mtx == NULL) return;
	EnterCriticalSection(&pmtx->mutex);
}

void YabThreadUnLock(YabMutex * mtx){
	YabMutex_win32 * pmtx;
  if (mtx == NULL) return;
	pmtx = (YabMutex_win32 *)mtx;
	LeaveCriticalSection(&pmtx->mutex);
}

YabMutex * YabThreadCreateMutex(){
	YabMutex_win32 * mtx = (YabMutex_win32 *)malloc(sizeof(YabMutex_win32));
	InitializeCriticalSection(&mtx->mutex);
	return (YabMutex *)mtx;
}

void YabThreadFreeMutex( YabMutex * mtx ){

	if (mtx != NULL){
		DeleteCriticalSection(&((YabMutex_win32 *)mtx)->mutex);
		free(mtx);
	}
}

//////////////////////////////////////////////////////////////////////////////
#if 0
static int init = 0;

typedef BOOL ( * EnterSynchronizationBarrier_fct)( LPSYNCHRONIZATION_BARRIER lpBarrier, DWORD dwFlags);
EnterSynchronizationBarrier_fct enterSynchronizationBarrier = NULL;

typedef BOOL ( * InitializeSynchronizationBarrier_fct)(  LPSYNCHRONIZATION_BARRIER lpBarrier, LONG lTotalThreads, LONG lSpinCount);
InitializeSynchronizationBarrier_fct initializeSynchronizationBarrier = NULL;

static void DoDynamicInit() {
  HINSTANCE mon_module = LoadLibrary("kernel32.dll");
  if(mon_module != NULL)
  {
   enterSynchronizationBarrier = (EnterSynchronizationBarrier_fct)GetProcAddress(mon_module, "EnterSynchronizationBarrier");
   initializeSynchronizationBarrier = (InitializeSynchronizationBarrier_fct)GetProcAddress(mon_module, "InitializeSynchronizationBarrier");
  }
}
#endif

//////////////////////////////////////////////////////////////////////////////

typedef struct YabCond_win32
{
	CONDITION_VARIABLE cond;
} YabCond_win32;


void YabThreadCondWait(YabCond *ctx, YabMutex * mtx) {
    YabCond_win32 * pctx;
    YabMutex_win32 * pmtx;
    if (mtx==NULL) return;
    pctx = (YabCond_win32 *)ctx;
    pmtx = (YabMutex_win32 *)mtx;
    YabThreadLock(mtx);
    SleepConditionVariableCS (&pctx->cond, &pmtx->mutex, INFINITE);
    YabThreadUnLock(mtx);
}

void YabThreadCondSignal(YabCond *mtx) {
    YabCond_win32 * pmtx;
    if (mtx==NULL) return;
    pmtx = (YabCond_win32 *)mtx;
    WakeConditionVariable (&pmtx->cond);
}

YabCond * YabThreadCreateCond(){

	YabCond_win32 * mtx = (YabCond_win32 *)malloc(sizeof(YabCond_win32));
	InitializeConditionVariable(&mtx->cond);
	return (YabCond *)mtx;
}

void YabThreadFreeCond( YabCond *mtx ) {
	if (mtx != NULL){
		free(mtx);
	}
}

//////////////////////////////////////////////////////////////////////////////
