#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <getopt.h>

enum states {CREATED, READY, RUNNING, BLOCKED};
enum transitions {TRANS_TO_READY, TRANS_TO_RUN, TRANS_TO_BLOCK, TRANS_TO_PREEMPT};
enum schedulers {F, L, S, R, P};

struct event {
	struct process* proc;
	enum states oldState;
	enum states newState;
	enum transitions transition;
	int timestamp;
};

struct process {
	int pid;
	int AT;
	int TC;
	int CB;
	int IO;
	int cpuburst;
	int ioburst;
	int remaining;
	int static_prio;
	int dynamic_prio;
	int timeinprevstate;
	int state_ts;
	int iowaittime;
	int cpuwaittime;
	enum states state;
};

struct queue {
	struct node* head;
	struct node* tail;
	int size;
};

struct node {
	void* val;
	struct node* next;
};

struct prioQueue {
	struct queue** active;
	struct queue** expired;
};

static const char delims[] = " \t\n";
static int quantum = 10000;
static int ofs = 0;
static int* randvals;
static int randCount;
static int totalIO = 0;
static int numProcs = 0;
static int CURRENT_TIME;
static bool CALL_SCHEDULER = false;
static bool debug = false; // debug flag for printing each event
static struct process* CURRENT_RUNNING_PROCESS = NULL;
static enum schedulers sched;
static void* runQueue = NULL;

void run_simulation(struct queue* eventQueue, struct queue* blockedQueue, struct process* printout[]);
int myrandom(int burst);
int* createRandArray(char* filename);
struct queue* startQueue();
void enqueue(struct queue* q, void* val);
void* dequeue(struct queue* q);
void reverseQueue(struct queue* q);
struct process* dequeue_min_rem(struct queue* q);
struct prioQueue* startPrioRunQueue();
void swapQueues(struct prioQueue* runQueue);
struct process* createProcess(char line[], int pid);
struct process* get_next_process(void* runQueue, enum transitions transition);
struct process* get_next_process_fcfs(struct queue* runQueue);
struct process* get_next_process_lcfs(struct queue* runQueue);
struct process* get_next_process_sjf(struct queue* runQueue);
struct process* get_next_process_rr(struct queue* runQueue);
struct process* get_next_process_prio(struct prioQueue* runQueue);
void add_process(void* runQueue, struct process* proc);
void add_process_flsr(struct queue* runQueue, struct process* proc);
void add_process_p(struct prioQueue* runQueue, struct process* proc);
struct event* createEvent(struct process* proc, 
						enum states oldState, enum states newState, 
						enum transitions transition, int timestamp); 
struct queue* createEventQueue(char* filename);
struct event* get_event(struct queue* eventQueue);
int get_next_event_time(struct queue* eventQueue);
void put_event(struct queue* q, struct event* evt);
void printSched();
void printFinalStats(struct process* p[], int numProcs);
const char* printState(enum states s);
const char* printTransition(enum transitions t);


int main(int argc, char* argv[]) {

	char *fullOpt = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "s:")) != -1) {
		switch (opt) {
		case 's':
			fullOpt = optarg;
			break;
		case '?':
			if (optopt == 's') {
				printf("Pass arguments –s [FLS | R<num> | P<num> ].\n");
			} else {
				printf("Illegal option\n");
			}
			return 1;
		default:
			exit(1);
		}
	}

	switch(fullOpt[0]) {
		case 'F':
			sched = F;
			break;
		case 'L':
			sched = L;
			break;
		case 'S':
			sched = S;
			break;
		case 'R':
			sched = R;
			quantum = atoi(&fullOpt[1]);
			break;
		case 'P':
			sched = P;
			quantum = atoi(&fullOpt[1]);
			break;
	}

	// initialize event, blocked, run queues, and printout
	if(fullOpt[0] == 'P') {
		runQueue = (struct prioQueue*) startPrioRunQueue();
	} else {
		runQueue = (struct queue*) startQueue();
	}

	randvals = createRandArray(argv[3]);
	struct queue* eventQueue = createEventQueue(argv[2]);

	// Use blockedQueue to keep track of totalIO for ioutil calculation, since process may use
	// io at the same time (overlapping io times)
	struct queue* blockedQueue = startQueue();	

	numProcs = eventQueue->size;
	struct process* printout[numProcs + 5]; 

	run_simulation(eventQueue, blockedQueue, printout);
	printFinalStats(printout, numProcs);
}


/*
	scheduler agnostic simulation
*/
void run_simulation(struct queue* eventQueue, struct queue* blockedQueue, struct process* printout[]) {
	
	int io_start;
	struct process* tmp = NULL;
	struct event* evt;

	while((evt =  get_event(eventQueue))) {

		struct process* proc = evt->proc;
		CURRENT_TIME = evt->timestamp;
		//state_ts is the timestamp that we entered into this current state (evt->oldstate)
		evt->proc->timeinprevstate = CURRENT_TIME - proc->state_ts;
		switch(evt->transition) {

			case TRANS_TO_READY: {
				// must come from blocking or preemption

				if(evt->proc->remaining <= 0) {
					evt->proc->state_ts = CURRENT_TIME;
					printout[evt->proc->pid] = evt->proc;
					CURRENT_RUNNING_PROCESS = NULL;
					CALL_SCHEDULER = true;
					if(debug) printf("%d %d %d: Done\n", CURRENT_TIME, evt->proc->pid, evt->proc->timeinprevstate);
					break;
				}

				if(debug) printf("%d %d %d: %s -> %s\n", CURRENT_TIME, evt->proc->pid, evt->proc->timeinprevstate,
											printState(evt->oldState), printState(evt->newState));
				if(evt->proc->state == CREATED) {
					evt->proc->remaining = evt->proc->remaining - evt->proc->ioburst;
				} else if(evt->proc->state == BLOCKED) {

					evt->proc->dynamic_prio = evt->proc->static_prio - 1;
					evt->proc->iowaittime += evt->proc->timeinprevstate;
					tmp = dequeue(blockedQueue);

					if(blockedQueue->size == 0) {
						totalIO += CURRENT_TIME - io_start;
					}
				}
				// add evt->proc to runqueue
				evt->proc->state = READY;
				evt->proc->state_ts = CURRENT_TIME;
				add_process((void*) runQueue, evt->proc);
				CALL_SCHEDULER = true;
				break;
			}

			case TRANS_TO_RUN: {
				if(debug) printf("%d %d %d: %s -> %s cb=%d rem=%d prio=%d \n", CURRENT_TIME, evt->proc->pid, 
										evt->proc->timeinprevstate,
										printState(evt->oldState), printState(evt->newState),
										evt->proc->cpuburst, evt->proc->remaining, evt->proc->dynamic_prio);
				
				evt->proc->cpuwaittime += evt->proc->timeinprevstate;
				evt->proc->state_ts = CURRENT_TIME;
				struct event* e;

				// create event for either preemption or blocking and put event into eventqueue
				if(evt->proc->cpuburst <= quantum) {
					e = createEvent(evt->proc, RUNNING, BLOCKED, TRANS_TO_BLOCK, CURRENT_TIME + evt->proc->cpuburst);
				} else {
					evt->proc->cpuburst -= quantum;
					evt->proc->remaining -= quantum;
					e = createEvent(evt->proc, RUNNING, READY, TRANS_TO_PREEMPT, CURRENT_TIME + quantum);
				}
				put_event(eventQueue, e);			
				break;
			}

			case TRANS_TO_BLOCK: {

				evt->proc->remaining = evt->proc->remaining - evt->proc->cpuburst;
				evt->proc->cpuburst = 0;

				if(evt->proc->remaining <= 0) {
					evt->proc->state_ts = CURRENT_TIME;
					printout[evt->proc->pid] = evt->proc;
					if(debug) printf("%d %d %d: Done\n", CURRENT_TIME, evt->proc->pid, evt->proc->timeinprevstate);
					CURRENT_RUNNING_PROCESS = NULL;
					CALL_SCHEDULER = true;
					break;
				}

				evt->proc->state = BLOCKED;
				evt->proc->state_ts = CURRENT_TIME; 
				evt->proc->ioburst = myrandom(evt->proc->IO);

				if(blockedQueue->size == 0) {
					io_start = CURRENT_TIME;
				}

				enqueue(blockedQueue, (void*) evt->proc);
				if(debug) printf("%d %d %d: %s -> %s  ib=%d rem=%d \n", CURRENT_TIME, evt->proc->pid, 
										evt->proc->timeinprevstate, printState(evt->oldState), 
										printState(evt->newState), evt->proc->ioburst,
										evt->proc->remaining);

				// create event for when process becomes ready again and put event into eventQueue
				struct event* e = createEvent(evt->proc, BLOCKED, READY, 
											TRANS_TO_READY, 
											CURRENT_TIME + evt->proc->ioburst);
				put_event(eventQueue, e);
				CALL_SCHEDULER = true;
				CURRENT_RUNNING_PROCESS = NULL;
				break;
			}

			case TRANS_TO_PREEMPT: {
				// add evt->proc to runqueue, no event is generated
				if(debug) printf("%d %d %d: %s -> %s cb=%d rem=%d prio=%d \n", CURRENT_TIME, evt->proc->pid, 
										evt->proc->timeinprevstate,
										printState(evt->oldState), printState(evt->newState),
										evt->proc->cpuburst, evt->proc->remaining, evt->proc->dynamic_prio);
				evt->proc->state = READY;
				evt->proc->state_ts = CURRENT_TIME; 
				evt->proc->dynamic_prio--;
				add_process((void*) runQueue, evt->proc);
				CALL_SCHEDULER = true;
				CURRENT_RUNNING_PROCESS = NULL;
				break;
			}
		}

		if(CALL_SCHEDULER) {
			// if next event is supposed to happen now, exit the scheduler and queue in the next event			
			if(get_next_event_time(eventQueue) == CURRENT_TIME) {
				continue;
			} 

			CALL_SCHEDULER = false;
			if(CURRENT_RUNNING_PROCESS == NULL) {
				CURRENT_RUNNING_PROCESS = get_next_process((void*) runQueue, evt->transition);
				if(CURRENT_RUNNING_PROCESS == NULL) {
					continue;
				}

				struct event* e = createEvent(CURRENT_RUNNING_PROCESS, READY, 
											  RUNNING, TRANS_TO_RUN, CURRENT_TIME);
				put_event(eventQueue, e);
			}
		}
		free(evt);
	}
	free(randvals);
}


/* 
	virtual function C implementation for adding processes to each scheduler via switch/case
*/
void add_process(void* runQueue, struct process* proc) {

	if(sched != P) {
		return add_process_flsr((struct queue*) runQueue, proc);
	} else {
		return add_process_p((struct prioQueue*) runQueue, proc);
	}
}

void add_process_flsr(struct queue* runQueue, struct process* proc) {
	
	if(sched != P) {
		if(proc->dynamic_prio == -1) {
			proc->dynamic_prio = proc->static_prio - 1;
		}
		enqueue(runQueue, (void*) proc);
	}
}

void add_process_p(struct prioQueue* runQueue, struct process* proc) {
	
	if(proc->dynamic_prio == -1) {
		proc->dynamic_prio = proc->static_prio - 1;
		enqueue(runQueue->expired[proc->dynamic_prio], (void*) proc);
		// add to expired queue at index proc->dynamic_prio
	} else {
		enqueue(runQueue->active[proc->dynamic_prio], (void*) proc);			
		// add to active queue at index proc->dynamic_prio
	}

	swapQueues(runQueue);
}


/* 
	virtual function C implementation for getting the next process for each scheduler via switch/case
*/
struct process* get_next_process(void* runQueue, enum transitions transition) {
	switch(sched) {
		case F:
			return get_next_process_fcfs((struct queue*) runQueue);
			break;
		case L:
			return get_next_process_lcfs((struct queue*) runQueue);
			break;
		case S:
			return get_next_process_sjf((struct queue*) runQueue);
			break;
		case R:
			return get_next_process_rr((struct queue*) runQueue);
			break;
		case P:
			return get_next_process_prio((struct prioQueue*) runQueue);
			break;
		default:
			printf("Illegal scheduler type\n");
			exit(1);
	}
}
 
struct process* get_next_process_fcfs(struct queue* runQueue) {

	struct process* p = (struct process*) dequeue(runQueue);
	if(p == NULL) {
		return NULL;
	}
 
	p->state = RUNNING;
	p->cpuburst = myrandom(p->CB); 
	if(p->remaining < p->cpuburst) {
		p->cpuburst = p->remaining;
	}

	return p;
}

struct process* get_next_process_lcfs(struct queue* runQueue) {

	reverseQueue(runQueue);
	struct process* p = (struct process*) dequeue(runQueue);
	if(p == NULL) {
		return NULL;
	}

	p->state = RUNNING;
	p->cpuburst = myrandom(p->CB); 
	if(p->remaining < p->cpuburst) {
		p->cpuburst = p->remaining;
	}

	reverseQueue(runQueue);
	return p;
}

struct process* get_next_process_sjf(struct queue* runQueue) {
	
	struct process* p = dequeue_min_rem(runQueue);

	if(p == NULL) {
		return NULL;
	}

	p->state = RUNNING;
	p->cpuburst = myrandom(p->CB); 

	if(p->remaining < p->cpuburst) {
		p->cpuburst = p->remaining;
	}

	return p;
}

struct process* get_next_process_rr(struct queue* runQueue) {

	struct process* p = (struct process*) dequeue(runQueue);
	if(p == NULL) {
		return NULL;
	}

	if(p->cpuburst == 0) {
		p->cpuburst = myrandom(p->CB); 
	}

	p->state = RUNNING;
	if(p->remaining < p->cpuburst) {
		p->cpuburst = p->remaining;
	}

	return p;
}

// get the next highest priority proc in the activeQueue
struct process* get_next_process_prio(struct prioQueue* runQueue) {

	swapQueues(runQueue);
	struct process* p = NULL;

	for(int i = 3; i > -1; i--) {
		if(runQueue->active[i]->size > 0 ) {
			p = (struct process*) dequeue(runQueue->active[i]);
			break;
		}
	}

	if(p == NULL) {
		return NULL;
	}

	if(p->cpuburst == 0) {
		p->cpuburst = myrandom(p->CB); 
	}

	p->state = RUNNING;
	if(p->remaining < p->cpuburst) {
		p->cpuburst = p->remaining;
	}

	return p;
}


/*
	Process and event initialization functions
*/

struct event* createEvent(struct process* proc, enum states oldState, enum states newState, 
						enum transitions transition, int timestamp) {
	struct event* evt = (struct event*) malloc(sizeof(struct event));
	evt->proc = proc;
	evt->oldState = oldState;
	evt->newState = newState;
	evt->transition = transition;
	evt->timestamp = timestamp;
	return evt;
}

struct process* createProcess(char line[], int pid) {

	struct process* proc = (struct process*) malloc(sizeof(struct process));
	proc->pid = pid;
	proc->AT = atoi(strtok(line, delims));
	proc->TC = atoi(strtok(NULL, delims));
	proc->CB = atoi(strtok(NULL, delims));
	proc->IO = atoi(strtok(NULL, delims));
	proc->cpuburst = 0;
	proc->ioburst = 0;
	proc->remaining = proc->TC;
	proc->static_prio = myrandom(4);
	proc->dynamic_prio = proc->static_prio - 1;
	proc->timeinprevstate = 0;
	proc->state_ts = proc->AT;
	proc->state = CREATED;
	proc->iowaittime = 0;
	proc->cpuwaittime = 0;
	return proc;
}

struct queue* createEventQueue(char* filename) {

	FILE* fp = fopen(filename,"r");
	if(!fp) {
		printf("Error: Could not open file\n");
		exit(1);
	}

	char line[100];
	int i = 0;

	struct queue* eventQueue = startQueue();

	while(fgets(line, 100, fp)) {

		struct process* proc = createProcess(line, i);
		struct event* evt = createEvent(proc, CREATED, READY, TRANS_TO_READY, proc->AT);
		enqueue(eventQueue, (void*) evt);
		i++;
	}

	fclose(fp);
	return eventQueue;
}


/*
	Eventqueue get/put functions
*/

struct event* get_event(struct queue* eventQueue) {
	struct event* evt = (struct event*) dequeue(eventQueue);
	return evt;
}

// inserts event into event queue already sorted by timestamp. Breaks timestamp ties by queueing in events in the order they were generated
void put_event(struct queue* q, struct event* evt) {

	struct node* e = (struct node*) malloc(sizeof(struct node));
	e->val = (void*) evt;
	e->next = NULL;
	if(q->size == 0) {
		q->head = e;
		q->tail = e;
		q->size++;
		return;
	} 

	struct node* h = q->head;
	struct event* v = (struct event*) h->val;

	if(evt->timestamp < v->timestamp) {
		e->next = q->head;
		q->head = e;
		q->size++;
	} else {
		struct event* n;
		while(h->next != NULL) {
			n = (struct event*) h->next->val;
			if(evt->timestamp < n->timestamp ) {
				e->next = h->next;
				h->next = e;
				q->size++;
				return;
			} else {
				h = h->next;
			}
		}
		h->next = e;
		q->tail = e;
		q->size++;
	}
}

// return timestamp of the current head of the eventQueue
int get_next_event_time(struct queue* eventQueue) {
	if(eventQueue->size == 0) {
		return -1;
	}

	struct event* e = (struct event*) eventQueue->head->val;
	return e->timestamp;
}


/*
	Queueing functions
	Queues implemented as singly-linked lists
*/

struct queue* startQueue() {

	struct queue* q = (struct queue*) malloc(sizeof(struct queue));
	q->tail = NULL;
	q->head = NULL;
	q->size = 0;
	return q;
}

void enqueue(struct queue* q, void* val) {

	struct node* n = (struct node*) malloc(sizeof(struct node));
	n->val = val;
	n->next = NULL;
	if(q->size == 0) {
		q->head = n;
	} else {
		q->tail->next = n;
	}
	q->tail = n;
	q->size++;
}

void* dequeue(struct queue* q) {
	if(q->size == 0) {
		return NULL;
	}
	void* val = q->head->val;
	free(q->head);
	q->head = q->head->next;
	q->size--;
	if(q->size == 0) {
		q->tail = NULL;
	}
	return val;
}

// dequeue the job with shortest remaining time (SJF)
struct process* dequeue_min_rem(struct queue* q) {

	if(q->size == 0) {
		return NULL;
	}

	int shortest = 100000000;
	struct process* val = NULL;
	struct node* min_rem = NULL;
	struct node* h = q->head;

	while(h != NULL) {
		val = (struct process*) h->val;
 		if(val->remaining < shortest) {
 			shortest = val->remaining;
 			min_rem = h;
 		} 

 		h = h->next;
	}

	struct node* n = q->head;
	struct process* headProc = n->val;
	struct process* ret = (struct process*) min_rem->val;

	if(headProc->pid == ret->pid) {
		q->head = q->head->next;
		q->size--;
		if(q->size == 0) {
			q->tail = NULL;
		}
		return ret; 
	}

	struct process* p = NULL;
	struct node* prev = NULL;
	while(n != NULL) {
		p = (struct process*) n->val;
		if(p->remaining == ret->remaining) {
			prev->next = n->next;
			q->size--;
			break;
		}
		prev = n;
		n = n->next;
	}

	struct node* t = q->tail;
	struct process* tailProc = t->val;

	if(tailProc->pid == ret->pid) {
		q->tail = prev;
	}

	return ret;
}

void reverseQueue(struct queue* q) {

	if(q->size == 0){
		return;
	}

	q->tail = q->head;
	struct node* prev = NULL;
	struct node* curr = q->head;
	struct node* next = NULL;

	while(curr != NULL) {
		next  = curr->next;  
        curr->next = prev;   
        prev = curr;
        curr = next;
	}

	q->head = prev;
}

struct prioQueue* startPrioRunQueue() {

	struct prioQueue* pQueue = (struct prioQueue*) malloc(sizeof(struct queue*) * 2);
	pQueue->active = (struct queue**) malloc(sizeof(struct queue*) * 4);
	pQueue->expired = (struct queue**) malloc(sizeof(struct queue*) * 4);

	for(int i = 0; i < 4; i++) {
		pQueue->active[i] = startQueue();
		pQueue->expired[i] = startQueue();
	}

	return pQueue;
}

// swap active and expired queues when active queue is empty
void swapQueues(struct prioQueue* runQueue) {

	for(int i = 0; i < 4; i++) {
		if(runQueue->active[i]->size > 0) {
			return;
		}
	}

	struct queue** tmp = runQueue->expired;
	runQueue->expired = runQueue->active;
	runQueue->active = tmp;
}


/*
	Random value generator functions
*/

int myrandom(int burst) {

	if(ofs == randCount) {
		ofs = 0;
	}

	return 1 + (randvals[ofs++] % burst); 
}

int* createRandArray(char* filename) {

	FILE* fp = fopen(filename,"r");
	if(!fp) {
		printf("Error: Could not open file\n");
		exit(1);
	}

	char line[15];
	int i = 0;
	fgets(line, 15, fp);

	randCount = atoi(line);
	int* randArray = (int*) malloc(sizeof(int) * randCount); 
	
	while(fgets(line, 15, fp)) {
		randArray[i] = atoi(line);
		i++;
	}

	fclose(fp);
	return randArray;
}


/*
	Printout functions
*/

void printSched() {
   switch (sched) {
      case F: 
      	printf("FCFS\n");
      	break;
      case L: 
      	printf("LCFS\n");
      	break;
      case S: 
      	printf("SJF\n");
      	break;
      case R: 
      	printf("RR %d\n", quantum);
      	break;
      case P: 
      	printf("PRIO %d\n", quantum);
      	break;
   }
}

const char* printState(enum states s) {
   switch (s) {
      case CREATED: return "CREATED";
      case READY: return "READY";
      case RUNNING: return "RUNNG";
      case BLOCKED: return "BLOCK";
      default: 
      	printf("Illegal state\n");
      	exit(1);
   }
}

const char* printTransition(enum transitions t) {
   switch (t) {
      case TRANS_TO_READY: return "TRANS_TO_READY";
      case TRANS_TO_RUN: return "TRANS_TO_RUN";
      case TRANS_TO_BLOCK: return "TRANS_TO_BLOCK";
      case TRANS_TO_PREEMPT: return "TRANS_TO_PREEMPT";
      default: 
        printf("Illegal transition\n");
      	exit(1);
   }
}

void printFinalStats(struct process* p[], int numProcs) {

	printSched();
	for(int i = 0; i < numProcs; i++) {
		printf("%04d: %4d %4d %4d %4d %1d | %5d %5d %5d %5d\n",
			       p[i]->pid,
			       p[i]->AT, p[i]->TC, p[i]->CB, p[i]->IO, p[i]->static_prio,
			       p[i]->state_ts, // last time stamp
			       p[i]->state_ts - p[i]->AT,
			       p[i]->iowaittime,
			       p[i]->cpuwaittime);
	}

	int lastFT = 0;
	int totalCPU = 0;
	int totalTT = 0;
	int totalCW = 0;

	for(int i = 0; i < numProcs; i++) {
		if(p[i]->state_ts > lastFT) {
			lastFT = p[i]->state_ts;
		}
		totalCPU += p[i]->TC;
		totalTT += p[i]->state_ts - p[i]->AT;
		totalCW += p[i]->cpuwaittime; 
	}

	double cpu_util = (((double) totalCPU) / ((double) lastFT)) * 100;
	double io_util = (((double) totalIO) / ((double) lastFT)) * 100;
	double avg_turnaround = ((double) totalTT) / ((double) numProcs);
	double avg_waittime = ((double) totalCW) / ((double) numProcs);
	double throughput = (((double) numProcs) / ((double) lastFT)) * 100;

	printf("SUM: %d %.2lf %.2lf %.2lf %.2lf %.3lf\n",
	       lastFT,
	       cpu_util,
	       io_util,
	       avg_turnaround,
	       avg_waittime, 
	       throughput);
}
