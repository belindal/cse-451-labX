#include <inc/lib.h>
#define C 10
#define R 14
#define NUM_TRIALS 10
#define debug 0
#define showProcessors 0

struct global_envids {
	int ids[C+2][C+2]; 
	int A[C][C];
	int OUT[R][C];
};

#define VA	((struct global_envids *) 0xA0000000)

struct linked_node {
	int val;
	struct linked_node* next;
};

void
printArray(int *array, int rows, int cols) {
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			cprintf("%d ", array[cols * i + j]);
		}
		cprintf("\n");
	}
}

void
copyArray(int *to, int *from, int rows, int cols) {
        for (int i = 0; i < rows; i++) {
                for (int j = 0; j < cols; j++) {
                        to[cols * i + j] = from[cols * i + j];
                }
        }
}

bool
checkequality(int *exp, int *real, int rows, int cols)
{
	bool isequal = true;
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			if (exp[cols * i + j] != real[cols * i + j]) {
				cprintf("Matrices are unequal at position: row %d column %d. Expected %d, but is actually %d\n", i, j, exp[cols * i + j], real[cols * i + j]);
				isequal = false;
			}
		}
	}
	return isequal;
}

void
printProgramOutput(bool test, bool printarray, int OUT_expected[R][C], long uptime) {
	cprintf("\n");
	if (test) {
		assert(checkequality((int *)OUT_expected, (int *)VA->OUT, R, C));
		sys_colorprint("Equality ", 1);
		sys_colorprint("test ", 2);
		sys_colorprint("passed ", 3);
		sys_colorprint(":)\n", 4);
		// cprintf("Equality test passed :)\n");
	}
	if (printarray) {
		sys_colorprint("INxA = \n", 5);
		printArray((int *)VA->OUT, R, C);
	}
	sys_colorprint("Done in ", 5);
	cprintf("%lu ", uptime);
	sys_colorprint("ms.\n", 5);
}

void
northProc(int col) {
	while (1) {
		// send 0 to south
		ipc_send(VA->ids[1][col], 0, 0, 0);
	}
}

void
eastProc(void) {
	while (1) {
		// receive but do nothing
		ipc_recv(NULL, 0, 0);
	}   
}

void
southProc(int col, int parent_id) {
	int val;
	envid_t envid;
	int numRecv = 0;
	while (numRecv < R) {
		// receive and record final value for output column
		val = ipc_recv(&envid, 0, 0);
		VA->OUT[numRecv][col-1] = val;
		numRecv++;
	}

	exit();
}

void
centerProc(int row, int col) {
	int val;
	envid_t envid;
	struct linked_node *west_top = 0;
	struct linked_node *west_bottom = 0;
	struct linked_node *north_top = 0;
	struct linked_node *north_bottom = 0;
	while (1) {
		// receive value from west
		val = ipc_recv(&envid, 0, 0);
		// check whether sending envid from west or north
		if (envid == VA->ids[row][col-1]) {
			// recieving from east
			// send east, directly
			ipc_send(VA->ids[row][col+1], val, 0, 0);
			// update linkedlist
			struct linked_node *newWestNode = (struct linked_node *) malloc(sizeof(struct linked_node));
			newWestNode->val = val;
			newWestNode->next = 0;
			if (west_bottom != 0) { west_bottom->next = newWestNode; }
			west_bottom = newWestNode;
			if (west_top == 0) { west_top = newWestNode; }
		} else if (envid == VA->ids[row-1][col]) {
			// recieving from north
			// update linkedlist
			struct linked_node *newNorthNode = (struct linked_node *) malloc(sizeof(struct linked_node));
			newNorthNode->val = val;
			newNorthNode->next = 0;
			if (north_bottom != 0) { north_bottom->next = newNorthNode; }
			north_bottom = newNorthNode;
			if (north_top == 0) { north_top = newNorthNode; }
		} else {
			cprintf("Recieving from invalid envid %d\n", envid);
			printArray((int *)VA->ids, C+2, C+2);
			panic("");
		}
		// have something in both queues (can send along)
		while (west_top != 0 && north_top != 0) {
			// will always be synchronized because we always pop north and west at the same time (in this loop)
			int cumsum = north_top->val + west_top->val * VA->A[row-1][col-1];
			if (debug)
				cprintf("(%d,%d) = N + W * A = %d + %d * %d = %d\n", row, col, north_top->val, west_top->val, VA->A[row-1][col-1], cumsum);
			// send south
			ipc_send(VA->ids[row+1][col], cumsum, 0, 0);
			// remove tops from queues
			west_top = west_top->next;
			north_top = north_top->next;
		}
	}
}

void westProc(int row, int IN[R][C]) {
	for (int k = 0; k < R; k++) {
		if (debug)
			cprintf("SW setup code (%d,%d)\n", row,1);
		// I am WEST row i, responsible for IN column i
		// iterate through this column's rows
		ipc_send(VA->ids[row][1], IN[k][row - 1], 0, 0);
	}
	exit();
}

void
matmul_setup()
{
	int r;
	if ((r = sys_page_alloc(0, VA, PTE_P|PTE_W|PTE_U|PTE_SHARE)) < 0) {
		panic("page alloc failed!: %e", r);
	}

	struct global_envids info = { };
	memcpy(VA, &info, sizeof(info));

	// fork ALL intermdiate processes
	for (int i = C + 1; i >= 0; i--) {
		for(int j = C + 1; j >= 0; j--) {
			// disregard SOUTH and WEST, as well as corners
			if (j == 0 || i == C + 1 || (i == C+1 && j==0) || 
				(i == C+1 && j==C+1) || (i == 0 && j == C+1)) {
				continue;
			}
			int id;
			if ((id = fork()) < 0)
				panic("fork: %e", id);
			if (id == 0) {
				if (i == 0) {
					northProc(j);
				}
				else if (j == C+1) {
					eastProc();
				}
				else {
					centerProc(i,j);
				}
			}
			else {
				// parent record's child's id
				VA->ids[i][j] = id;
				if (debug)
					cprintf("NEC setup code (%d,%d)\n", i, j);
			}
		}
	}
	if (debug)
	    printArray((int *)VA->ids, C+2, C+2);

}

// kill all processes that had been setup (CENTER, NORTH, EAST)
void
matmul_kill()
{
	// delete ALL intermediate processes
	for (int i = C + 1; i >= 0; i--) {
		for(int j = C + 1; j >= 0; j--) {
			// disregard SOUTH and WEST (these have already exited), as well as corners
			if (j == 0 || i == C + 1 || (i == C+1 && j==0) || 
				(i == C+1 && j==C+1) || (i == 0 && j == C+1)) {
				VA->ids[i][j] = 0;
				continue;
			}
			sys_env_destroy(VA->ids[i][j]);
			VA->ids[i][j] = 0;
		}
	}
	if (debug)
	    printArray((int *)VA->ids, C+2, C+2);

}

// if test flag non-zero, test result against OUT_expected
long
user_run(int IN[R][C], int A[C][C], int OUT_expected[R][C], bool test, bool printarray)
{
	copyArray((int *)VA->A, (int *)A, C, C);
	int parent_id = sys_getenvid();
	
	struct sysinfo info;
	sys_sysinfo(&info);
	sys_colorprint("computing INxA...", 5);
	long startUptime = info.uptime / NANOSECONDS_PER_MILLISECOND;
	
	// fork South and West processes
	// parent record's child's id
	for (int j = C; j >= 1; j--) {
		int id;
		if ((id = fork()) < 0) {
			panic("fork: %e", id);
		} else if (id == 0) {
			southProc(j, parent_id);
		} else {
			VA->ids[C + 1][j] = id;
		}
	}
	for (int i = C; i >= 1; i--) {
		int id;
		if ((id = fork()) < 0) {
			panic("fork: %e", id);
		} else if (id == 0) {
			westProc(i, IN);
		} else {
				VA->ids[i][0] = id;
		}
	}
	if (showProcessors) {
		sys_colorprint("\n-- Processors --\n", 5);
		printArray((int *)VA->ids, C+2, C+2);
	}

	// wait for all South processes to complete
	for (int k = C; k >= 1; k--) {
		wait(VA->ids[C+1][k]);
	}
	
	sys_sysinfo(&info);
	long endUptime = info.uptime / NANOSECONDS_PER_MILLISECOND;
	
	printProgramOutput(test, printarray, OUT_expected, endUptime - startUptime);
	
	return endUptime - startUptime;
}

// Will print resulting array at the end
void computeAverageRuntime(int IN[R][C], int A[C][C], int OUT_expected[R][C], int numRuns, bool test) {
	long average = user_run(IN, A, OUT_expected, test, false);
	int count;
	for (count = 1; count < numRuns; count++) {
		long uptime = user_run(IN, A, OUT_expected, test, false);
		average = (average * count + uptime)/(count+1);
		
	}
	cprintf("\n");
	sys_colorprint("average run time: ", 5);
	cprintf("%lu\n", average);
	sys_colorprint("total runs: ", 5);
	cprintf("%d\n", count);
	sys_colorprint("INxA = \n", 5);
	printArray((int *)VA->OUT, R, C);
}

void
umain(int argc, char **argv)
{
	cprintf("Welcome to Matrix Multiplication!\n");

	// Set up multiplication processes (North, East, Center nodes) 
	matmul_setup();

	// Specify matrices to multiply
/*
	// test case 1 (C = 3, R = 4)
	int IN[R][C] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}};
	int A[C][C] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};	
	int OUT_expected[R][C] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}};
*/
/*	// test case 2 (C = 3, R = 2)
	int IN[R][C] = {{7, 12, 11}, {3, -1, -5}};
	int A[C][C] = {{1, 2, 5}, {-4, 8, 0}, {0, 0, 2}};
	int OUT_expected[R][C] = {{-41, 110, 57}, {7, -2, 5}};
*/
/*
	// test case 3 (larger base array. C = 4, R = 6)
	int IN[R][C] = {
		{700, 1200, 110, 3400}, 
		{300, -100, -500, 3400}, 
		{700, 1200, 110, 3400}, 
		{300, -100, -500, 3400}, 
		{700, 1200, 110, 3400}, 
		{300, -100, -500, 3400}};
	int A[C][C] = {
		{2, 0, 0, 0}, 
		{0, 1, 0, 0}, 
		{0, 0, 1, 0}, 
		{0, 0, 0, 1}};
	int OUT_expected[R][C] = {
		{1400, 1200, 110, 3400}, 
		{600, -100, -500, 3400}, 
		{1400, 1200, 110, 3400}, 
		{600, -100, -500, 3400}, 
		{1400, 1200, 110, 3400}, 
		{600, -100, -500, 3400}};
*/
///*
	// test case 4 (C = 10, R = 14)
	int IN[R][C] = { {2,0,0,0,0,0,0,0,0,0}, {4,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {6,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0} };
	int A[C][C] = {
			{1,1,1,1,1,1,1,1,1,1},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0},
			{0,0,0,0,0,0,0,0,0,0}
		};
	int OUT_expected[R][C] = { {2,2,2,2,2,2,2,2,2,2}, {4,4,4,4,4,4,4,4,4,4}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {6,6,6,6,6,6,6,6,6,6}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1} };
//*/
	sys_colorprint("\nA: \n", 1);
	printArray((int*)A, C, C);
	
	sys_colorprint("\nIN: \n", 2);
	printArray((int*)IN, R, C);

	sys_colorprint("\nBeginning Trial Runs \n", 3);
	computeAverageRuntime(IN, A, OUT_expected, NUM_TRIALS, true);

	// finished running program, time to destroy all children...
        cprintf("\nMatrix Multiplication Done.\n");
	matmul_kill();

	exit();
}
