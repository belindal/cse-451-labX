# LAB X WRITEUP

## Colored Console
I implemented a colored console, AKA the second challenge problem in [Lab 1](https://courses.cs.washington.edu/courses/cse451/18au/labs/lab1.html#formatted-printing-to-the-console).
I implemented ANSI escape sequences such that the user can embed an escape code in their string, in the format of ```ESC[XXm``` or ```ESC[XX;XXm``` (the `XX` portions are numberic codes) and the resulting string printed to the console will have color and/or highlight.
Here is a mapping of how each color maps to its respective foreground or background numberic code:

FG Code | BG Code | Color
--------|---------|-------
   30   |    40   | black
   31   |    41   | blue
   32   |    42   | green
   33   |    43   | cyan
   34   |    44   | red
   35   |    45   | magenta
   36   |    46   | brown
   37   |    47   | gray

For example, ```cprintf("ESC[31mhello");``` results in printing out a blue "hello," whereas ```ESC[31;42m``` results in printing out a blue "hello," on a green background.

### Why Did I Choose This?
It makes the the console prettier, I guess.

### Challenges
While it was relatively easy to implement colored printing in the kernel, by far the biggest challenging was implementing colored printing in user space. For starters, the `cprintf` in user space follows a completely different code-path than `cprintf` in kernel space, especially as it calls `sys_cputs`, which prints using `cprintf("%s", <user string>)`, rather than just directly printing the user string. To compensate for this, I implemented a system call `sys_colorprint(const char *s, int fgcolor, int bgcolor)` which takes in a user-specified string `s`, a foreground color code `fgcolor` (30-37), and a background color code `bgcolor` (40-47), and prints the user-specified string with the specified color combination.

### See The Code In Action
From kernel space, I colored-print the "Welcome to the JOS kernel monitor!" and "Type 'help' for a list of commands." strings in `kern/monitor.c` using ANSI escape sequence. This can be seen from running `make qemu`.

From user space, my implementation of CSP Matrix Multiplication (see below) frequently uses `sys_colorprint`. To see this in action, run `make run-matrixMultiply`.

## CSP Matrix Multiplication
We (Tali and I) implemented a concurrent matrix multiplication algorithm as outlined in the [Communicating Sequential Processes](https://www.cs.cmu.edu/~crary/819-f09/Hoare78.pdf) paper, AKA the final challenge problem in [Lab 4](https://courses.cs.washington.edu/courses/cse451/18au/labs/lab4.html#inter-process-communication-ipc).
This algorithm can efficiently multiply a Rx3 input matrix `IN` with a 3x3 square matrix `A` by taking advantage of messages being passed amongst a large number of concurrent processes.

We have extended this algorithm to be able to multiply an arbitrary-sized input matrix `IN` (dimensions RxC) with an arbitrary-sized square matrix `A` (dimensions CxC), with the only restriction on C being that it is less than 30.
(We only have 1024 processes total, and 2 processes are being used as the parent running the program and user/idle. The number of processes that the algorithm must use is a function of C: `# Processes = C^2 + 4C <= 1022` implies `C <= 30`.)

All code changes from this problem is localized in matrixMultiply.c

### How To Run Our Code
By default, we have set our code to run 10 trials of multiplying a 14-by-10 `IN` matrix
with a 10-by-10 `A` matrix. To change this (and/or specify your own matrices), change the
macros at the top of `matrixMultiply.c`:
```
#define C 10
#define R 14
#define NUM_TRIALS 10
```

In the `umain` function, we have provided sample test matrices. Uncomment/recomment
tests as necessary, or specify your own `IN` and `A` matrices.

When done, from the `lab` directory, run
```make run-matrixMultiply CPUS=<some number of CPUs>```.

### Why Is This Interesting/Important?
This algorithm is representative of what's possible with IPC.
It minimizes data reads and we can stream in infinitely long matrices (R is unbounded).

Also, this concurrent matrix multiplication is highly efficient with multiple CPUs.
We tested the algorithm on various-sized matrices and timed it running
on one or more CPUs. We noticed significant speedup when we ran on more CPUs.
For example, when multiplying a 14-by-10 matrix with a 10-by-10 matrix (test case
4), we found:

\# CPUs | Ave. Runtime (across 10 runs)
------- | -----------------------------
1       | 4279 ms
2       | 2344 ms
3       | 854 ms
4       | 921 ms

### Challenges
1. Who does process creation/forking?: How can we create processes such that
all processes know the IDs of who they'll be sending to? (Note `ipc_send` needs 
to know the id of the environment it's sending to, and that parents know the ID of
the children they've forked.) This is especially problematic for the center processes,
as they'll be sending to TWO processes: to their East and South.
    - Considered starting WEST and having it cascade fork processors to the East.
    This will take care of the East-ward sending, but how about South-ward sending?
    - **SOLN**: Fork all processes from the same parent - store all these env id's
    in a global array.
2. In what order does the parent create processes?: Once created, some processes
will try to send to other processes right away, regardless of whether the parent
process has created that other process.
    - **SOLN**: Since messages are only sent in 2 directions, East-ward and South-ward,
    initializing East->West and South->North will take care of this.
3. Wasn't enough to have the global array of env id's be global--Forked children
COW'd this global array when they tried to read from it, when it wasn't
necessarily complete, and only contained the env id's of processes to its East
and South. Since messages are only ever sent East-ward and South-ward, we thought
this would just be enough, but Center processes were problematic. Since Center
processes could recieve from 2 sources (North and West), they needed to check
where they were recieving from--aka checking against env id's to their North and
West (which isn't in their copy of the global env id array).
    - **SOLN**: We had to put the global env id array on a shared page, so changes
    the parent made to it would be seen by all children.
4. Center nodes can recieve from 2 locations (North and West). While we can use
the global env id array to decipher who sent the last message we recieved, we cannot
specify "only recieve from West." Troublesome as we may recieve, for example,
North several times in a row before recieving from the West (or vice versa).
In this case, we have to keep saving up the "North" values we've recieved until
the next "West" value comes, then sum with the first "North" value we recieved.
    - **SOLN**: We keep the seen values from each dimension in separate queues,
    and pop them off once we recieve a value from the other dimension. This required
    implementing a queue in our program.
