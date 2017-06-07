/******************************************************************************
 *
 *  File Name........: mythread.c
 *
 *  Description......: Creates a mythread library
 *
 *  Created by skollip on 2/3/16
 *
 *  Revision History.: A
 *
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include "mythread.h"
#include <unistd.h>
#include <sys/utsname.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <signal.h>

int numtask;
_MyThread *taskrunning = nil;
_Tasklist readyQ;
ucontext_t schedcontext;

struct Tasklist_t	/* used internally */
{
	_MyThread *head;
	_MyThread *tail;
};

struct MyThread_t
{
	_MyThread	*next;
	_MyThread	*prev;
	_MyThread   *parent;
	_MyThread   *children[1000];
	_MyThread   *block;
	ucontext_t	context;
	int     massblock;
	int     childcount;
	int     terminate;
	int     id;
	char	*stk;
	int	    stksize;
};

void addtask(_Tasklist *l, _MyThread *t)
{
	if(l->tail){
		l->tail->next = t;
		t->prev = l->tail;
	}else{
		l->head = t;
		t->prev = nil;
	}
	l->tail = t;
	t->next = nil;
}

void deltask(_Tasklist *l, _MyThread *t)
{
	if(t->prev)
		t->prev->next = t->next;
	else
		l->head = t->next;
	if(t->next)
		t->next->prev = t->prev;
	else
		l->tail = t->prev;
}

MyThread unqueue(_Tasklist *l)
{
    _MyThread *t = l->head;
    deltask(l, l->head);
    return t;
}

int taskmanager(void)
{
    _MyThread *t = nil;
    
    while(1) {
        t = readyQ.head;
        
        if(!t) {
            //no runnable tasks
            return 0;
        }
        deltask(&readyQ, t);
        taskrunning = t;
        swapcontext(&schedcontext, &taskrunning->context);
        t = taskrunning;
        int i;
        if(t->terminate) {
            _MyThread *p;
            if(t->parent) {
                p = t->parent;
                
                if(p->block == t) {
                    addtask(&readyQ, p);
                }else if(p->massblock) {
                    for(i = 0; i < 1000; i++) {
                        if(p->children[i]) {
                            if(p->children[i] == t) {
                                p->children[i] = nil;
                                p->childcount--;
                            }
                        }
                    }
                    if(p->childcount == 0) {
                        addtask(&readyQ, p);
                    }
                }
            }
            for(i = 0; i < 1000; i++) {
                if(t->children[i]) {
                    t->children[i]->parent = nil;
                }
            }
            free(t->context.uc_stack.ss_sp);
            free(t);
            t = nil;
        }
        
    }
}

// Create a new thread.
MyThread MyThreadCreate(void(*start_funct)(void *), void *args)
{
    _MyThread *t = nil;
	
	/* allocate the task and stack together */
	t = malloc(sizeof *t);
	if(t == 0){
		printf("taskalloc malloc\n");
	}
	
	/* do a reasonable initialization */
	memset(&t->context, 0, sizeof t->context);
	
	/* must initialize with current context */
	if(getcontext(&t->context) < 0){
		printf("getcontext\n");
		abort();
	}
    
	/* call makecontext to do the real work. */
	/* leave a few words open on both ends */
	t->context.uc_stack.ss_sp = calloc(1, SIGSTKSZ);
	t->context.uc_stack.ss_size = SIGSTKSZ;
	
	makecontext(&t->context, (void(*)())start_funct, 1, args);
	
	t->block = nil;
	t->childcount = 0;
	t->massblock = 0;
	numtask++;
	t->id = numtask;
	if(taskrunning) {
	    t->parent = taskrunning;
	    taskrunning->children[numtask] = t;
	    taskrunning->childcount++;
	}
	addtask(&readyQ, t);
	
	return t;
}

// Terminate invoking thread
void MyThreadExit(void)
{
    taskrunning->terminate = 1;
    swapcontext(&taskrunning->context, &schedcontext);
}

// Yield invoking thread
void MyThreadYield(void)
{
    if(readyQ.head) {
        _MyThread *t = unqueue(&readyQ);
        addtask(&readyQ, taskrunning);
        taskrunning = t;
        swapcontext(&readyQ.tail->context, &taskrunning->context);
    }
}

// Join with a child thread
int MyThreadJoin(MyThread thread)
{
    if(thread) {
        _MyThread *p = taskrunning;
        _MyThread *t = thread;
        p->block = t;
        taskrunning = unqueue(&readyQ);
        swapcontext(&p->context, &taskrunning->context);
        return 0;
    }
    return -1;
}

// Join with all children
void MyThreadJoinAll(void)
{
    int i;
    int f = taskrunning->childcount;
    _MyThread *p = taskrunning;
    
    if(f) {
        p->massblock = 1;
        taskrunning = unqueue(&readyQ);
        swapcontext(&p->context, &taskrunning->context);
    }
}

void MyThreadInit(void(*start_funct)(void *), void *args)
{
    MyThreadCreate(start_funct, args);
    taskmanager();
}

