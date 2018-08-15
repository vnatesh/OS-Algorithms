#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>

enum schedulers {I, J, S, C, F};

struct list {
	struct node* head;
	struct node* tail;
	int size;
};

struct node {
	void* val;
	struct node* next;
};

struct request {
	int io;
	int arrival_time;
	int track;
	int start_time;
	int end_time;
};

static const char delims[] = " \t\n";
static const int MAX_REQ = 10010;
static enum schedulers sched;
static int CURRENT_TIME = 0;
static int prev_track = 0;
static int requestCount = 0;
static int tot_movement = 0;
static int tot_turnaround = 0;
static int wait_time = 0;
static int tot_wait = 0;
static int max_waittime = -1;
static int direction = 1;
static int currTrack = 0;
static int qInd = 0;
static int backupInd = 0;
static bool ACTIVE = false;
static struct list* ioQueue[2] = {NULL, NULL};
static struct request* requestArray = NULL;
static struct request* CURR_REQ = NULL;

struct list* createList();
void add(struct list* l, void* v);
void* removeFirst(struct list* l);
struct request* delete(struct request * req);
void createRequestArray(char* filename);
void initTime();
struct request* get_next_request();
struct request* get_next_request_fifo();
struct request* get_next_request_sstf();
struct request* get_next_request_look();
struct request* get_next_request_clook();
struct request* get_next_request_flook();
struct request* get_max_track_io();
struct request* get_min_track_io();
void printRequestArray();
void printIoQueue();
void printOutput();
void runSimulation(char* filename);


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
				printf("Pass arguments –s].\n");
			} else {
				printf("Illegal option\n");
			}
			return 1;
		default:
			exit(1);
		}
	}

	switch(fullOpt[0]) {
		case 'i':
			sched = I;
			break;
		case 'j':
			sched = J;
			break;
		case 's':
			sched = S;
			break;
		case 'c':
			sched = C;
			break;
		case 'f':
			sched = F;
			qInd = 1;
			break;
	}

	runSimulation(argv[2]);
	printOutput();
	free(requestArray);
	free(ioQueue[0]);
	free(ioQueue[1]);
	return 0;
}


void runSimulation(char* filename) {

	createRequestArray(filename);
	ioQueue[0] = createList();
	ioQueue[1] = createList();
	initTime();
	int i = 0;
	int doneCount = 0;
	bool incrTime = true;

	while(doneCount!=requestCount) {
		// add new request to io queue if one arrived
		if(requestArray[i].arrival_time == CURRENT_TIME) {
			add(ioQueue[backupInd], (void*) &requestArray[i]);
			i++;
			incrTime = false;
		}

		// if a request is finished, update stats
		if(ACTIVE && (currTrack == CURR_REQ->track)) {

			CURR_REQ->end_time = CURRENT_TIME;
			tot_turnaround += CURR_REQ->end_time - CURR_REQ->arrival_time; 
			wait_time = CURR_REQ->start_time - CURR_REQ->arrival_time;
			tot_wait += wait_time;
			max_waittime = (wait_time > max_waittime ? wait_time : max_waittime);
			tot_movement += abs(CURR_REQ->track - prev_track);
			prev_track = CURR_REQ->track;
			doneCount++;
			ACTIVE = false;
			incrTime = false;
		}
		// move head by one track
		if(ACTIVE && (currTrack != CURR_REQ->track)) {

			if(prev_track < CURR_REQ->track) {
				currTrack++;
			} else {
				currTrack--;
			}

			incrTime = true;
		}

		// if no request is being served, get a new one from the ioqueue
		if((!ACTIVE) && (ioQueue[qInd]->size > 0 || ioQueue[backupInd]->size > 0 )) {

			CURR_REQ = get_next_request();
			CURR_REQ->start_time = CURRENT_TIME;
			ACTIVE = true;
			incrTime = false;
		}

		// increment time if nothing available in queue
		if((!ACTIVE) && (doneCount != requestCount) && (ioQueue[qInd]->size == 0)) {
			incrTime = true;
		}

		if(incrTime) {
			CURRENT_TIME++;
		}
	}
}


// virtual function C implementaiton for the scheduler
struct request* get_next_request() {
	switch(sched) {
		case I:
			return get_next_request_fifo();
			break;
		case J:
			return get_next_request_sstf();
			break;
		case S:
			return get_next_request_look();
			break;
		case C:
			return get_next_request_clook();
			break;
		case F:
			return get_next_request_flook();
			break;
		default:
			printf("Illegal scheduler type\n");
			exit(1);
	}
}


struct request* get_next_request_flook() {

	// if active queue is empty, switch it with the backup queue
	if(ioQueue[qInd]->size == 0) {
		backupInd = qInd;
		qInd = 1 - backupInd;
	}

	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;
	struct request* ret = NULL;
	int shortest = INT_MAX;
	int dist = 0;
	struct request* max = get_max_track_io();
	struct request* min = get_min_track_io();

	if(ioQueue[qInd]->size == 1) {
		ret = (struct request*) removeFirst(ioQueue[qInd]);
		if(ret->track > currTrack) {
			direction = 1;
		} else {
			direction = 0;
		}
		return ret;
	}

	if(direction == 1) {
		if(currTrack > max->track) {
			direction = 0;
		}
	}

	if(direction == 0) {
		if(currTrack < min->track) {
			direction = 1;
		}
	}

	if(direction == 1) {

		while(h != NULL) {

			r = (struct request*) h->val;
			dist = r->track - currTrack;

			if((dist >= 0) && (dist < shortest)) {
				shortest = dist;
				ret = r;
			} 

			h = h->next;
		}

	} else {

		while(h != NULL) {

			r = (struct request*) h->val;
			dist = currTrack - r->track;

			if((dist >= 0) && (dist < shortest)) {
				shortest = dist;
				ret = r;
			} 

			h = h->next;
		}	
	}

	return delete(ret);
}


struct request* get_next_request_clook() {

	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;
	struct request* ret = NULL;
	int shortest = INT_MAX;
	int dist = 0;
	struct request* max = get_max_track_io();
	struct request* min = get_min_track_io();

	if(currTrack > max->track) {
		return delete(min);
	}

	while(h != NULL) {

		r = (struct request*) h->val;
		dist = r->track - currTrack;

		if((dist >= 0) && (dist < shortest)) {
			shortest = dist;
			ret = r;
		} 

		h = h->next;
	}

	return delete(ret);
}


struct request* get_next_request_look() {

	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;
	struct request* ret = NULL;
	int shortest = INT_MAX;
	int dist = 0;
	struct request* max = get_max_track_io();
	struct request* min = get_min_track_io();

	if(ioQueue[qInd]->size == 1) {
		ret = (struct request*) removeFirst(ioQueue[qInd]);
		if(ret->track > currTrack) {
			direction = 1;
		} else {
			direction = 0;
		}
		return ret;
	}

	if(direction == 1) {
		if(currTrack > max->track) {
			direction = 0;
		}
	}

	if(direction == 0) {
		if(currTrack < min->track) {
			direction = 1;
		}
	}

	if(direction == 1) {

		while(h != NULL) {

			r = (struct request*) h->val;
			dist = r->track - currTrack;

			if((dist >= 0) && (dist < shortest)) {
				shortest = dist;
				ret = r;
			} 

			h = h->next;
		}

	} else {

		while(h != NULL) {

			r = (struct request*) h->val;
			dist = currTrack - r->track;

			if((dist >= 0) && (dist < shortest)) {
				shortest = dist;
				ret = r;
			} 

			h = h->next;
		}	
	}

	return delete(ret);
}

struct request* get_next_request_fifo() {

	return (struct request*) removeFirst(ioQueue[qInd]);
}

struct request* get_next_request_sstf() {

	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;
	struct request* ret = NULL;
	int shortest = INT_MAX;

	if(ioQueue[qInd]->size == 1) {
		return (struct request*) removeFirst(ioQueue[qInd]);
	}

	while(h != NULL) {

		r = (struct request*) h->val;

		if((abs(r->track - currTrack) < shortest) ) {
			shortest = abs(r->track - currTrack);
			ret = r;
		} 

		h = h->next;
	}

	return delete(ret);
}

struct request* get_max_track_io() {

	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;
	int max = 0;
	struct request* ret = NULL;

	while(h != NULL) {
		r = (struct request*) h->val;
		if(r->track > max) {	
			max = r->track;
			ret = r;

		} 
		h = h->next;
	}
	return ret;
}

struct request* get_min_track_io() {

	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;
	int min = INT_MAX;
	struct request* ret = NULL;

	while(h != NULL) {
		r = (struct request*) h->val;
		if(r->track < min) {	
			min = r->track;
			ret = r;
		} 
		h = h->next;
	}

	return ret;
}

void initTime() {
	struct request r = requestArray[0];
	CURRENT_TIME = r.arrival_time;
}


void createRequestArray(char* filename) {

	FILE* fp = fopen(filename,"r");

	if(!fp) {
		printf("Error: Could not open file\n");
		exit(1);
	}

	requestArray = (struct request*) malloc(sizeof(struct request) * MAX_REQ);

	char line[100];
	fgets(line, 100, fp); fgets(line, 100, fp); 
	int i = 0;

	while(fgets(line, 100, fp)) {

		requestArray[i].io = i;
		requestArray[i].arrival_time = atoi(strtok(line, delims));
		requestArray[i].track = atoi(strtok(NULL, delims));
		requestArray[i].start_time = -1;
		requestArray[i].end_time = -1;
		i++;
	}

	requestCount = i;
}


/*
	singly-linked list functions
*/

struct list* createList() {

	struct list* l = (struct list*) malloc(sizeof(struct list));
	l->tail = NULL;
	l->head = NULL;
	l->size = 0;
	return l;
}

void add(struct list* l, void* v) {

	struct node* n = (struct node*) malloc(sizeof(struct node));
	n->val = v;
	n->next = NULL;

	if(l->size == 0) {
		l->head = n;
	} else {
		l->tail->next = n;
	}

	l->tail = n;
	l->size++;
}

void* removeFirst(struct list* l) {
	if(l->size == 0) {
		return NULL;
	}
	void* val = l->head->val;
	free(l->head);
	l->head = l->head->next;
	l->size--;
	if(l->size == 0) {
		l->tail = NULL;
	}
	return val;
}

struct request* delete(struct request* req) {

	if(ioQueue[qInd]->size == 0) {
		return NULL;
	}

	struct node* h = ioQueue[qInd]->head;
	struct request* r = (struct request*) h->val;

	if(r->track == req->track) {

		return (struct request*) removeFirst(ioQueue[qInd]);
	}

	struct node* prev = h;
	h = h->next;

	while(h != NULL) {
		r = (struct request*) h->val;
		if(r->track == req->track) {
			prev->next = h->next;
			free(h);
			ioQueue[qInd]->size--;
			break;
		}
		prev = h;
		h = h->next;
	}

	if(prev->next == NULL) {
		ioQueue[qInd]->tail = prev;
	}

	return r;
}


/*
	Printout functions
*/

void printOutput() {

	struct request r;
	for(int i = 0; i < requestCount; i++) {
		r = requestArray[i];
		printf("%5d: %5d %5d %5d\n", i, r.arrival_time, r.start_time, r.end_time);
	}

	double avg_turnaround = ((double) tot_turnaround) / ((double) requestCount);
	double avg_waittime = ((double) tot_wait) / ((double) requestCount);
	printf("SUM: %d %d %.2lf %.2lf %d\n", CURRENT_TIME, tot_movement, avg_turnaround, avg_waittime, max_waittime);
}

void printRequestArray() {

	struct request r;
	for(int i = 0; i < requestCount; i++) {
		r = requestArray[i];
		printf("io# : %d  start : %d  track: %d\n", i, r.arrival_time, r.track);
	}
}

void printIoQueue() {
	struct node* h = ioQueue[qInd]->head;
	struct request* r = NULL;


	while(h != NULL) {

		r = (struct request*) h->val;
		printf("%d:%d ", r->io, r->track);

		h = h->next;
	}
	printf("\n");
}
