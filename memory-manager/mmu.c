#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>

struct list {
	struct node* head;
	struct node* tail;
	int size;
};

struct node {
	void* val;
	struct node* next;
};

struct vma_t {
	unsigned int start_vpage : 6;
	unsigned int end_vpage : 6;
	unsigned int write_protect : 1;
	unsigned int filemapped : 1;
};

struct pte_t {
	unsigned int present : 1;
	unsigned int write_protect : 1;
	unsigned int modified : 1;
	unsigned int referenced : 1;
	unsigned int pagedout : 1;
	unsigned int filemapped : 1;
	unsigned int frame : 7;
};

struct frame_t {
	unsigned int fid;
	unsigned int pid;
	unsigned int vpage;
};

struct pstat_t {
	unsigned long unmaps;
	unsigned long maps;
	unsigned long ins;
	unsigned long outs;
	unsigned long fins;
	unsigned long fouts;
	unsigned long zeros;
	unsigned long segv;
	unsigned long segprot;
};

struct process {
	unsigned int pid;
	struct pte_t* pagetable;
	struct list* vma_list;
	struct pstat_t* pstat;
	unsigned int* ages;
};

static const char delims[] = " \t\n";
static const int NUM_VPAGES = 64;
static int NUM_FRAMES;
static int procCount = 0;
static struct process* procArray = NULL;
static struct process CURRENT_PROCESS;
static char operation;
static unsigned int curr_vpage;
static struct frame_t* frametable = NULL;
static int freeFrame = 0; // count of free frames...max is NUM_FRAMES, then paging starts
static char PAGER;
static int frameInd = 0; // index into the frametable...incrememnted when you choose victim frame
static bool PAGER_ON = false;
static unsigned long instCount = 0;
static unsigned long ctxSwitches = 0;
static unsigned long cost = 0;
static int ofs = 0;
static int* randVals;
static int randCount;
static int requests; // number of paging requests (select_victim frame calls)
static bool PRINT_INSTR = false, PRINT_PTE = false, PRINT_FT = false, PRINT_SUM = false;
static struct node* CURR_CLOCK_HAND = NULL;
static struct list* clockList;
static bool START_CLOCK = true;

struct frame_t* get_frame();
struct frame_t* allocate_frame_from_free_list();
struct frame_t* select_victim_frame();
struct frame_t* select_victim_frame_fifo();
struct frame_t* select_victim_frame_second_chance();
struct frame_t* select_victim_frame_clock();
struct frame_t* select_victim_frame_random();
struct frame_t* select_victim_frame_nru();
struct frame_t* select_victim_frame_aging();
void createFrameTable();
struct pstat_t* createPstat();
struct pte_t* createPageTable();
void createProcArray(FILE* fp);
struct list* createList();
unsigned int* createAges();
void initCircularClock();
void add(struct list* l, void* v);
struct vma_t* find_vma_list();
void printFrameTable();
void printProcess();
void printList(struct list* l);
void printPageTable(struct pte_t* pagetable);
void printStats();
void printPageTables();
bool get_next_instruction(FILE* fp);
void createRandArray(char* filename);
int myrandom(int size);
void runSimulation(FILE* fp);


int main(int argc, char* argv[]) {

	int len = 0;
	int opt;
	opterr = 0;

	while ((opt = getopt (argc, argv, "a:o:f:")) != -1) {
		switch (opt) {
			case 'a':
				PAGER = optarg[0];
				break;

			case 'o':

				len = strlen(optarg);
				if(len > 4) {
					printf("Too many options\n");
					exit(1);
				}

				for(int i = 0; i < len; i++) {
					if(optarg[i] == 'O') {
						PRINT_INSTR = true;
					} else if(optarg[i] == 'P') {
						PRINT_PTE = true;
					} else if(optarg[i] == 'F') {
						PRINT_FT = true;
					} else if(optarg[i] == 'S') {
						PRINT_SUM = true;
					} else {
						printf("Illegal option\n");
						exit(1);
					}
				}

				break;

			case 'f':
				NUM_FRAMES = atoi(optarg);
				break;

			case '?':
				if (optopt == 'a' || optopt == 'o' || optopt == 'f') {
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				} else if(isprint(optopt)) {
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				} else {
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				}
				return 1;

			default:
			abort ();
		}
	}

	createFrameTable();
	FILE* fp;

	for(int i = optind; i < argc; i++) {

		if(i == argc - 2) {
			fp = fopen(argv[i], "r");
			if(!fp) {
				printf("Error: Could not open input file\n");
				exit(1);
			}
			createProcArray(fp);
		}

		if(i == argc - 1) {
			createRandArray(argv[i]);
		}
	}

	runSimulation(fp);
	
	if(PRINT_PTE) printPageTables();
	if(PRINT_FT) printFrameTable();
	if(PRINT_SUM) printStats();

	free(randVals);
	free(frametable);
	fclose(fp);

	return 0;
}


void runSimulation(FILE* fp) {

	while(get_next_instruction(fp)) {

		if(PRINT_INSTR) printf("%lu: ==> %c %d\n", instCount, operation, curr_vpage);

		if(operation == 'c') {
			CURRENT_PROCESS = procArray[curr_vpage];
			instCount++;
			ctxSwitches++;
			cost += 121;
			continue;
		}

		cost++;
		struct pte_t* pte = &(CURRENT_PROCESS.pagetable[curr_vpage]);
		struct vma_t* vma = NULL;

		if(!pte->present) {

			if(!(vma = find_vma_list())) {
				if(PRINT_INSTR) printf("  SEGV\n");
				CURRENT_PROCESS.pstat->segv++;
				instCount++;
				continue;
			}

			// get victim frame and reset its proc,vpage entry
			struct frame_t* newframe = get_frame();

			if(PAGER_ON) {
				if(PRINT_INSTR) printf("  UNMAP %d:%d\n", newframe->pid, newframe->vpage);

				struct process* oldProc = &(procArray[newframe->pid]);
				oldProc->pstat->unmaps++;
				struct pte_t* oldPTE = &(procArray[newframe->pid].pagetable[newframe->vpage]);
				oldPTE->present = 0;

				// if page was modified, we have page out to disk (swap device) or re-map to file
				if(oldPTE->modified) {
					if(oldPTE->filemapped) {
						if(PRINT_INSTR) printf("  FOUT\n");
						oldProc->pstat->fouts++;
					} else {
						if(PRINT_INSTR) printf("  OUT\n");
						oldProc->pstat->outs++;
						oldPTE->pagedout = 1;
					}
				}
			}

			pte->present = 1; // we are giving this page a frame in memory so present is set to 1
			pte->write_protect = vma->write_protect;
			pte->filemapped = vma->filemapped;
			pte->frame = newframe->fid; // assign victim frame to pte
			newframe->pid = CURRENT_PROCESS.pid; // set reverse mapping vals for newframe 
			newframe->vpage = curr_vpage;
			pte->modified = 0;

			if(pte->pagedout) {
				if(pte->filemapped) {
					if(PRINT_INSTR) printf("  FIN\n");
					CURRENT_PROCESS.pstat->fins++;
				} else {
					if(PRINT_INSTR) printf("  IN\n");
					CURRENT_PROCESS.pstat->ins++;
				}

			} else {
				if(pte->filemapped) {
					if(PRINT_INSTR) printf("  FIN\n");
					CURRENT_PROCESS.pstat->fins++;
				} else {
					if(PRINT_INSTR) printf("  ZERO\n");
					CURRENT_PROCESS.pstat->zeros++;
				}
			}

			if(PRINT_INSTR) printf("  MAP %d\n", pte->frame);
			CURRENT_PROCESS.pstat->maps++;
		}

		pte->referenced = 1; // set to 1 for any r/w operation
		// if op is a write but write_protect is set for pte, issue segprot. Otherise, set modified to 1 
		if(operation == 'w') {
			if(pte->write_protect) {
				printf("  SEGPROT\n");
				CURRENT_PROCESS.pstat->segprot++;
			} else {
				pte->modified = 1;
			}
		}

		instCount++;
	}
}


struct frame_t* get_frame() {

	struct frame_t* frame = allocate_frame_from_free_list();
	
	// if no more free frames, call paging algorithm
	if(frame == NULL) {
		PAGER_ON = true;
		requests++;
		frame = select_victim_frame();
	}

	return frame;
}

// if freeInd is > 63, can't allocate so return NULL.
struct frame_t* allocate_frame_from_free_list() {

	if(freeFrame < NUM_FRAMES) {
		return &frametable[freeFrame++];
	}
	return NULL;
}


/* 	
	virtual function C implementation for selecting paging algorithm to evict victim frame 
*/
struct frame_t* select_victim_frame() {
	switch(PAGER) {
		case 'f':
			return select_victim_frame_fifo();
			break;
		case 's':
			return select_victim_frame_second_chance();
			break;
		case 'c':
			return select_victim_frame_clock();
			break;
		case 'r':
			return select_victim_frame_random();
			break;
		case 'n':
			return select_victim_frame_nru();
			break;
		case 'a':
			return select_victim_frame_aging();
			break;
		default:
			printf("Illegal pager type\n");
			exit(1);
	}
}


struct frame_t* select_victim_frame_fifo() {

	if(frameInd == NUM_FRAMES) {
		frameInd = 0;
	}

	return &frametable[frameInd++];
}


struct frame_t* select_victim_frame_second_chance() {

	if(frameInd == NUM_FRAMES) {
		frameInd = 0;
	}

	struct frame_t* frame = &frametable[frameInd];
	struct pte_t* pte = &(procArray[frame->pid].pagetable[frame->vpage]);

	// reset R bit, advance frame index
	if(pte->referenced && pte->present) {
		 pte->referenced = 0;
		 frameInd++;
		 return select_victim_frame_second_chance();
	}

	return &frametable[frameInd++];
}


struct frame_t* select_victim_frame_clock() {

	if(START_CLOCK) {
		initCircularClock();
		START_CLOCK = false;
	}

	struct frame_t* evictedFrame = NULL;
	struct frame_t* frame = (struct frame_t*) CURR_CLOCK_HAND->val;
	struct pte_t* pte = &(procArray[frame->pid].pagetable[frame->vpage]);

	if(pte->referenced && pte->present) {
		pte->referenced = 0;
		CURR_CLOCK_HAND = CURR_CLOCK_HAND->next;
		return select_victim_frame_clock();
	}

	evictedFrame = (struct frame_t*) CURR_CLOCK_HAND->val;
	CURR_CLOCK_HAND = CURR_CLOCK_HAND->next;
	return evictedFrame;
}


struct frame_t* select_victim_frame_random() {

	return &frametable[myrandom(NUM_FRAMES)];
}


struct frame_t* select_victim_frame_nru() {

	int c0 = 0, c1 = 0, c2 = 0, c3 = 0;
	struct frame_t* array[4][NUM_FRAMES];
	struct frame_t* frame;
	struct pte_t* pte;

	for(int i = 0; i < NUM_FRAMES; i++) {
	
		frame = &frametable[i];
		pte = &(procArray[frame->pid].pagetable[frame->vpage]);

		if(pte->present) { 
			if(!(pte->referenced) && !(pte->modified)) {
				array[0][c0++] = frame;
			} else if(!(pte->referenced) && pte->modified) {
				array[1][c1++] = frame;
			} else if(pte->referenced && !(pte->modified)) {
				array[2][c2++] = frame;
			} else if(pte->referenced && pte->modified) {
				array[3][c3++] = frame;
			}
		}

		// reset referenced bit every 10th request
		if((requests % 10) == 0) {
			pte->referenced = 0; 
		}
	}

	// select a random frame from lowest non-empty class
	for(int i = 0; i < 4; i++) {
		if(c0) {
			return array[0][myrandom(c0)];
		} else if(c1) {
			return array[1][myrandom(c1)];
		} else if(c2) {
			return array[2][myrandom(c2)];
		} else if(c3) {
			return array[3][myrandom(c3)];
		}
	}

	return NULL;
}


struct frame_t* select_victim_frame_aging() {

	struct pte_t* pte = NULL;
	unsigned int* ageVec = NULL;
	unsigned int vpage;
	for(int i = 0; i < NUM_FRAMES; i++) {
		pte = &(procArray[frametable[i].pid].pagetable[frametable[i].vpage]);
		ageVec = procArray[frametable[i].pid].ages;
		vpage = frametable[i].vpage; 

		if(pte->referenced) {
			ageVec[vpage] = 0x80000000 | (ageVec[vpage] >> 1);
			pte->referenced = 0;

		} else {
			ageVec[vpage] = ageVec[vpage] >> 1;
		}
	}

	struct frame_t* minFrame = NULL;
	unsigned int minAge = UINT_MAX;
	// get min age frame
	for(int i = 0; i < NUM_FRAMES; i++) {
		if(procArray[frametable[i].pid].ages[frametable[i].vpage] < minAge) {
			minAge = procArray[frametable[i].pid].ages[frametable[i].vpage];
			minFrame = &frametable[i];
		}
	}

	struct process* proc = &(procArray[minFrame->pid]);
	proc->ages[minFrame->vpage] = 0;
	struct pte_t* oldPTE = &(proc->pagetable[minFrame->vpage]);
	oldPTE->referenced = 0;

	return minFrame;
}


void createFrameTable() {

	frametable = (struct frame_t*) malloc(sizeof(struct frame_t) * NUM_FRAMES);

	for(int i = 0; i < NUM_FRAMES; i++) {
		frametable[i].fid = i;
		frametable[i].pid = UINT_MAX;
		frametable[i].vpage = UINT_MAX;
	}
}


bool get_next_instruction(FILE* fp) {
	
	char line[100];
	if(!fgets(line, 100, fp)) {
		return false;
	}

	if(line[0] == '#') {
		return false;
	}

	char* op = strtok(line, delims);
	operation = op[0];
	curr_vpage = atoi(strtok(NULL, delims));
	return true;
}


void createProcArray(FILE* fp) {

	char line[100];
	fgets(line, 100, fp); fgets(line, 100, fp); fgets(line, 100, fp); fgets(line, 100, fp);
	procCount = atoi(strtok(line, delims));
	procArray = (struct process*) malloc(sizeof(struct process) * procCount);
	int vmaCount = 0;

	for(int i = 0; i < procCount; i++) {

		fgets(line, 20, fp); fgets(line, 20, fp); fgets(line, 20, fp);
		vmaCount = atoi(strtok(line, delims));
		procArray[i].pid = i; 
		procArray[i].pagetable = createPageTable();
		procArray[i].vma_list = createList();
		procArray[i].pstat = createPstat();
		procArray[i].ages = createAges();

		// add vma's to process vma_list
		for(int j = 0; j < vmaCount; j++) {

			fgets(line, 20, fp);
			struct vma_t* vma = (struct vma_t*) malloc(sizeof(struct vma_t));
			vma->start_vpage = atoi(strtok(line, delims));
			vma->end_vpage = atoi(strtok(NULL, delims));
			vma->write_protect = atoi(strtok(NULL, delims));
			vma->filemapped = atoi(strtok(NULL, delims));
			add(procArray[i].vma_list, (void*) vma);
		}
	}

	fgets(line, 100, fp);
}


struct pte_t* createPageTable() {
	
	struct pte_t* pagetable = (struct pte_t*) malloc(sizeof(struct pte_t) * NUM_VPAGES);
	// initialize each pte to 0 before instruction sim starts
	for(int i = 0; i < NUM_VPAGES; i++) {
		pagetable[i].present = 0;
		pagetable[i].write_protect = 0;
		pagetable[i].modified = 0;
		pagetable[i].referenced = 0;
		pagetable[i].pagedout = 0;
		pagetable[i].filemapped = 0;
		pagetable[i].frame = 0;
	}

	return pagetable;
}

struct pstat_t* createPstat() {

	struct pstat_t* pstat = (struct pstat_t*) malloc(sizeof(struct pstat_t));
	pstat->unmaps = 0;
	pstat->maps = 0;
	pstat->ins = 0;
	pstat->outs = 0;
	pstat->fins = 0;
	pstat->fouts = 0;
	pstat->zeros = 0;
	pstat->segv = 0;
	pstat->segprot = 0;

	return pstat;
}

unsigned int* createAges() {

	unsigned int* ages = (unsigned int*) malloc(sizeof(unsigned int) * NUM_VPAGES);

	for(int i = 0; i < NUM_VPAGES; i++) {
		ages[i] = 0;
	}

	return ages;
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

// confirm the vpage from the instruction is a valid page in the vma of CURRENT_PROCESS
struct vma_t* find_vma_list() {

	struct node* h = CURRENT_PROCESS.vma_list->head;
	struct vma_t* vma = NULL;

	while(h != NULL) {
		vma = (struct vma_t*) h->val;
		if((vma->start_vpage <= curr_vpage) && (curr_vpage <= vma->end_vpage)) {
			return vma;
		} else {
			h = h->next;
		}
	}

	return NULL;
}

// create circular list from frametable
void initCircularClock() {

	struct list* l = createList();

	for(int i = 0; i < NUM_FRAMES ; i++) {
		add(l, (void*) &frametable[i]);
	}

	CURR_CLOCK_HAND = l->head;
	// circularizes list
	l->tail->next = l->head;
	clockList = l;
}


/*
	Random value generator functions
*/

int myrandom(int size) {

	if(ofs == randCount) {
		ofs = 0;
	}

	return (randVals[ofs++] % size); 
}

void createRandArray(char* filename) {

	FILE* fp = fopen(filename,"r");
	if(!fp) {
		printf("Error: Could not open rfile\n");
		exit(1);
	}

	char line[15];
	int i = 0;
	fgets(line, 15, fp);
	randCount = atoi(line);
	randVals = (int*) malloc(sizeof(int) * randCount); 
	
	while(fgets(line, 15, fp)) {
		randVals[i] = atoi(line);
		i++;
	}

	fclose(fp);
}


/*
	Printout functions
*/

void printFrameTable() {
	printf("FT: ");
	for(int i = 0; i < NUM_FRAMES; i++) {
		if(frametable[i].pid == UINT_MAX && frametable[i].vpage == UINT_MAX) {
			printf("* ");
		} else {
			printf("%d:%d ", frametable[i].pid, frametable[i].vpage);
		}
	}
	printf("\n");
}

void printPageTables() {

	for(int i = 0; i < procCount; i++) {
		struct pte_t* pagetable = procArray[i].pagetable;
		printf("PT[%d]: ", i);
		for(int i = 0; i < NUM_VPAGES; i++) {
	
			if(!pagetable[i].present) {
				if(pagetable[i].pagedout) {
					printf("# ");
				} else {
					printf("* ");
				}		
			} else {
				char r = (pagetable[i].referenced ? 'R' : '-');
				char m = (pagetable[i].modified ? 'M' : '-');
				char s = (pagetable[i].pagedout ? 'S' : '-');
				printf("%d:%c%c%c ", i, r, m, s);
			}
		}
		printf("\n");	
	}
}

void printStats() {

	for(int i = 0; i < procCount; i++) {
		struct process* proc = &procArray[i];
		struct pstat_t* pstat = proc->pstat;
		printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
		proc->pid, pstat->unmaps, pstat->maps, pstat->ins, pstat->outs, pstat->fins, 
		pstat->fouts, pstat->zeros, pstat->segv, pstat->segprot);
		cost += ((pstat->unmaps + pstat->maps) * 400) + ((pstat->ins + pstat->outs) * 3000) +
		((pstat->fins + pstat->fouts) * 2500) + (pstat->zeros * 150) + (pstat->segv * 240) + (pstat->segprot * 300);
	}

	printf("TOTALCOST %lu %lu %lu\n", ctxSwitches, instCount, cost);
}
