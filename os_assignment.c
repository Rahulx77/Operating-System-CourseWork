/* =============================================================================
 * os_assignment.c
 *
 * Task 1: Process Management and Threading
 *
 * This program demonstrates, in four self-contained parts across multiple processes:
 * PROCESS CREATION - Explicitly uses fork() and wait() in main().
 * PART 1 - A race condition on a shared counter, and how a mutex fixes it.
 * PART 2 - A bounded-buffer Producer/Consumer system using semaphores
 * (classic inter-thread synchronisation / "monitor-style" access).
 * PART 3 - A Round-Robin CPU scheduler simulation (Gantt chart, waiting
 * time and turnaround time calculation).
 * PART 4 - A two-thread, two-lock deadlock scenario, prevented by
 * enforcing a global lock-acquisition ordering.
 *
 * Compile:  gcc -Wall -Wextra -pthread -o os_assignment os_assignment.c
 * Run:      ./os_assignment
 *
 * Author: (student name)
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

/* -----------------------------------------------------------------------
 * Small helper: a short, randomised sleep to simulate real work and to
 * force unpredictable thread interleaving (which is exactly what makes
 * race conditions and deadlocks possible in the first place).
 * --------------------------------------------------------------------- */
static void simulate_work(int max_ms) {
    int ms = (rand() % max_ms) + 1;
    usleep(ms * 1000);
}

/* =============================================================================
 * PART 1: RACE CONDITION  vs  MUTEX-PROTECTED COUNTER
 * ========================================================================== */

#define NUM_COUNTER_THREADS 3
#define INCREMENTS_PER_THREAD 100000

long g_shared_counter = 0;
pthread_mutex_t g_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
int g_use_mutex = 0;   /* toggled between the two demonstration runs */

void *counter_worker(void *arg) {
    long tid = (long)arg;
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        if (g_use_mutex) {
            /* Critical section is protected: only one thread may execute
             * the read-modify-write sequence on g_shared_counter at a time. */
            pthread_mutex_lock(&g_counter_mutex);
            g_shared_counter++;
            pthread_mutex_unlock(&g_counter_mutex);
        } else {
            /* UNSAFE: g_shared_counter++ is NOT atomic. It is really
             * load g_shared_counter -> local register
             * add 1 to the register
             * store register -> g_shared_counter
             * The three steps are written out explicitly below (instead of
             * relying on "g_shared_counter++") and sched_yield() is used to
             * deliberately widen the window between the read and the write.
             * This makes the interleaving reproducible even on a single CPU
             * core, so the lost-update race condition is visible on every
             * run rather than only occasionally:
             *
             * Thread X reads counter (e.g. 41)
             * Thread X is preempted before writing back
             * Thread Y reads the SAME value (41), adds 1, writes 42
             * Thread X resumes, adds 1 to its stale copy, writes 42
             * -> one increment is silently lost (counter should be 43)
             */
            long temp = g_shared_counter;
            sched_yield();
            temp = temp + 1;
            g_shared_counter = temp;
        }
    }
    printf("  [Counter-Thread %ld] finished %d increments\n", tid, INCREMENTS_PER_THREAD);
    return NULL;
}

void run_part1_race_condition_demo(void) {
    pthread_t threads[NUM_COUNTER_THREADS];

    printf("\n================ PART 1: Race Condition Demonstration ================\n");

    /* --- Run WITHOUT mutex protection --- */
    g_shared_counter = 0;
    g_use_mutex = 0;
    printf("\n-- Run A: %d threads incrementing WITHOUT a mutex --\n", NUM_COUNTER_THREADS);
    for (long i = 0; i < NUM_COUNTER_THREADS; i++)
        pthread_create(&threads[i], NULL, counter_worker, (void *)i);
    for (int i = 0; i < NUM_COUNTER_THREADS; i++)
        pthread_join(threads[i], NULL);

    long expected = (long)NUM_COUNTER_THREADS * INCREMENTS_PER_THREAD;
    printf("  Expected final value : %ld\n", expected);
    printf("  Actual final value   : %ld  %s\n", g_shared_counter,
           (g_shared_counter != expected) ? "<-- LOST UPDATES (race condition!)" : "(no corruption this run - races are timing dependent)");

    /* --- Run WITH mutex protection --- */
    g_shared_counter = 0;
    g_use_mutex = 1;
    printf("\n-- Run B: %d threads incrementing WITH a mutex --\n", NUM_COUNTER_THREADS);
    for (long i = 0; i < NUM_COUNTER_THREADS; i++)
        pthread_create(&threads[i], NULL, counter_worker, (void *)i);
    for (int i = 0; i < NUM_COUNTER_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf("  Expected final value : %ld\n", expected);
    printf("  Actual final value   : %ld  %s\n", g_shared_counter,
           (g_shared_counter == expected) ? "<-- CORRECT (mutex prevented the race)" : "<-- UNEXPECTED");
}

/* =============================================================================
 * PART 2: PRODUCER / CONSUMER (bounded buffer) using SEMAPHORES + MUTEX
 * ========================================================================== */

#define BUFFER_SIZE 5
#define ITEMS_TO_PRODUCE 10

int g_buffer[BUFFER_SIZE];
int g_buf_in = 0, g_buf_out = 0;

sem_t g_empty_slots;   /* counts free slots in the buffer   */
sem_t g_full_slots;    /* counts filled slots in the buffer */
pthread_mutex_t g_buffer_mutex = PTHREAD_MUTEX_INITIALIZER; /* protects indices */

void *producer_thread(void *arg) {
    long id = (long)arg;
    for (int i = 1; i <= ITEMS_TO_PRODUCE; i++) {
        int item = (int)(id * 1000 + i);
        simulate_work(50);

        sem_wait(&g_empty_slots);      /* wait for a free slot              */
        pthread_mutex_lock(&g_buffer_mutex);

        g_buffer[g_buf_in] = item;
        printf("  [Producer %ld] produced item %d -> slot %d\n", id, item, g_buf_in);
        g_buf_in = (g_buf_in + 1) % BUFFER_SIZE;

        pthread_mutex_unlock(&g_buffer_mutex);
        sem_post(&g_full_slots);       /* signal a new full slot            */
    }
    return NULL;
}

void *consumer_thread(void *arg) {
    long id = (long)arg;
    for (int i = 1; i <= ITEMS_TO_PRODUCE; i++) {
        sem_wait(&g_full_slots);       /* wait for an available item        */
        pthread_mutex_lock(&g_buffer_mutex);

        int item = g_buffer[g_buf_out];
        printf("  [Consumer %ld] consumed item %d <- slot %d\n", id, item, g_buf_out);
        g_buf_out = (g_buf_out + 1) % BUFFER_SIZE;

        pthread_mutex_unlock(&g_buffer_mutex);
        sem_post(&g_empty_slots);      /* signal a newly-freed slot         */

        simulate_work(50);
    }
    return NULL;
}

void run_part2_producer_consumer_demo(void) {
    pthread_t producer, consumer;

    printf("\n============= PART 2: Producer-Consumer (Bounded Buffer) =============\n");
    printf("  Buffer size = %d, items to produce/consume = %d\n\n", BUFFER_SIZE, ITEMS_TO_PRODUCE);

    sem_init(&g_empty_slots, 0, BUFFER_SIZE); /* all slots start empty  */
    sem_init(&g_full_slots, 0, 0);            /* no items at the start  */

    pthread_create(&producer, NULL, producer_thread, (void *)1);
    pthread_create(&consumer, NULL, consumer_thread, (void *)1);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    sem_destroy(&g_empty_slots);
    sem_destroy(&g_full_slots);

    printf("  All items produced and consumed successfully. No data was lost or overwritten.\n");
}

/* =============================================================================
 * PART 3: ROUND-ROBIN CPU SCHEDULER SIMULATION
 * ========================================================================== */

typedef struct {
    int pid;
    int arrival_time;
    int burst_time;
    int remaining_time;
    int completion_time;
    int waiting_time;
    int turnaround_time;
} Process;

void run_part3_round_robin_demo(void) {
    #define NUM_PROCESSES 5
    #define QUANTUM 4

    Process procs[NUM_PROCESSES] = {
        {1, 0, 10, 10, 0, 0, 0},
        {2, 1, 5,  5,  0, 0, 0},
        {3, 2, 8,  8,  0, 0, 0},
        {4, 3, 3,  3,  0, 0, 0},
        {5, 4, 6,  6,  0, 0, 0},
    };

    printf("\n================ PART 3: Round-Robin Scheduler Simulation ============\n");
    printf("  Quantum = %d ms\n\n", QUANTUM);
    printf("  %-6s %-10s %-8s\n", "PID", "Arrival", "Burst");
    for (int i = 0; i < NUM_PROCESSES; i++)
        printf("  %-6d %-10d %-8d\n", procs[i].pid, procs[i].arrival_time, procs[i].burst_time);

    /* Simple ready-queue simulation: queue holds process indices. */
    int queue[1000], qh = 0, qt = 0; /* head / tail */
    int in_queue[NUM_PROCESSES];
    memset(in_queue, 0, sizeof(in_queue));

    int time_now = 0;
    int completed = 0;
    int next_arrival_idx = 0; /* processes are already sorted by arrival_time */

    printf("\n  Gantt chart:\n  |");

    /* Enqueue any processes that have arrived at time 0 */
    while (next_arrival_idx < NUM_PROCESSES && procs[next_arrival_idx].arrival_time <= time_now) {
        queue[qt++] = next_arrival_idx;
        in_queue[next_arrival_idx] = 1;
        next_arrival_idx++;
    }

    while (completed < NUM_PROCESSES) {
        if (qh == qt) {
            /* Ready queue empty but processes still remain -> CPU idles
             * until the next process arrives. */
            time_now = procs[next_arrival_idx].arrival_time;
            queue[qt++] = next_arrival_idx;
            in_queue[next_arrival_idx] = 1;
            next_arrival_idx++;
        }

        int idx = queue[qh++];
        Process *p = &procs[idx];

        int slice = (p->remaining_time < QUANTUM) ? p->remaining_time : QUANTUM;
        printf(" P%d(%d-%d) |", p->pid, time_now, time_now + slice);

        time_now += slice;
        p->remaining_time -= slice;

        /* Any new processes that arrived during this slice join the queue
         * BEFORE the current process is re-queued (standard round-robin rule). */
        while (next_arrival_idx < NUM_PROCESSES && procs[next_arrival_idx].arrival_time <= time_now) {
            queue[qt++] = next_arrival_idx;
            in_queue[next_arrival_idx] = 1;
            next_arrival_idx++;
        }

        if (p->remaining_time > 0) {
            queue[qt++] = idx; /* back of the queue */
        } else {
            p->completion_time = time_now;
            p->turnaround_time = p->completion_time - p->arrival_time;
            p->waiting_time = p->turnaround_time - p->burst_time;
            completed++;
        }
    }
    printf("\n");

    double total_wait = 0, total_turnaround = 0;
    printf("\n  %-6s %-10s %-14s %-16s\n", "PID", "Burst", "Waiting Time", "Turnaround Time");
    for (int i = 0; i < NUM_PROCESSES; i++) {
        printf("  %-6d %-10d %-14d %-16d\n", procs[i].pid, procs[i].burst_time,
               procs[i].waiting_time, procs[i].turnaround_time);
        total_wait += procs[i].waiting_time;
        total_turnaround += procs[i].turnaround_time;
    }
    printf("\n  Average waiting time    : %.2f ms\n", total_wait / NUM_PROCESSES);
    printf("  Average turnaround time : %.2f ms\n", total_turnaround / NUM_PROCESSES);
}

/* =============================================================================
 * PART 4: DEADLOCK DEMONSTRATION AND PREVENTION (lock ordering)
 * ========================================================================== */

pthread_mutex_t g_lock_A = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_lock_B = PTHREAD_MUTEX_INITIALIZER;

/* SAFE version: both threads acquire the locks in the SAME global order
 * (A before B). This removes the "circular wait" condition, which is one
 * of the four necessary conditions for deadlock (Coffman conditions), so
 * deadlock becomes impossible for this pair of locks regardless of
 * thread scheduling/timing. */
void *safe_thread_1(void *arg) {
    (void)arg;
    printf("  [Thread 1] trying to lock A...\n");
    pthread_mutex_lock(&g_lock_A);
    printf("  [Thread 1] locked A. Doing some work...\n");
    simulate_work(100);

    printf("  [Thread 1] trying to lock B...\n");
    pthread_mutex_lock(&g_lock_B);
    printf("  [Thread 1] locked B. Critical section running...\n");
    simulate_work(50);

    pthread_mutex_unlock(&g_lock_B);
    printf("  [Thread 1] unlocked B\n");
    pthread_mutex_unlock(&g_lock_A);
    printf("  [Thread 1] unlocked A. Done.\n");
    return NULL;
}

void *safe_thread_2(void *arg) {
    (void)arg;
    printf("  [Thread 2] trying to lock A...\n");
    pthread_mutex_lock(&g_lock_A);   /* same order as thread 1: A then B */
    printf("  [Thread 2] locked A. Doing some work...\n");
    simulate_work(100);

    printf("  [Thread 2] trying to lock B...\n");
    pthread_mutex_lock(&g_lock_B);
    printf("  [Thread 2] locked B. Critical section running...\n");
    simulate_work(50);

    pthread_mutex_unlock(&g_lock_B);
    printf("  [Thread 2] unlocked B\n");
    pthread_mutex_unlock(&g_lock_A);
    printf("  [Thread 2] unlocked A. Done.\n");
    return NULL;
}

void run_part4_deadlock_prevention_demo(void) {
    pthread_t t1, t2;
    printf("\n=========== PART 4: Deadlock Prevention (Lock Ordering) ===============\n");
    printf("  Two threads each need locks A and B. Both acquire them in the SAME\n");
    printf("  order (A, then B) so a circular wait can never form.\n\n");

    pthread_create(&t1, NULL, safe_thread_1, NULL);
    pthread_create(&t2, NULL, safe_thread_2, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("\n  Both threads completed without deadlock.\n");
}

/* =============================================================================
 * MAIN (Includes Explicit Process Creation via fork)
 * ========================================================================== */

int main(void) {
    srand((unsigned int)time(NULL));

    printf("###########################################################\n");
    printf("#  Operating Systems Assignment - Task 1                  #\n");
    printf("#  Process Management, Threading & Synchronisation Demo   #\n");
    printf("###########################################################\n");

    printf("\n[PARENT PROCESS] (PID: %d) preparing to demonstrate real Process Creation.\n", getpid());
    
    // Explicit OS-level Process Creation
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        return 1;
    } 
    else if (pid == 0) {
        // --- Executed exclusively by the newly created CHILD Process ---
        printf("\n-----------------------------------------------------------\n");
        printf("[CHILD PROCESS] Spawned successfully (PID: %d, Parent PID: %d).\n", getpid(), getppid());
        printf("[CHILD PROCESS] Executing Threading, Mutex and Semaphore tasks.\n");
        printf("-----------------------------------------------------------\n");

        run_part1_race_condition_demo();
        run_part2_producer_consumer_demo();

        printf("\n-----------------------------------------------------------\n");
        printf("[CHILD PROCESS] Threading tasks finished. Exiting process safely.\n");
        printf("-----------------------------------------------------------\n");
        exit(0); 
    } 
    else {
        // --- Executed exclusively by the PARENT Process ---
        printf("[PARENT PROCESS] Created a child process with PID: %d.\n", pid);
        printf("[PARENT PROCESS] Waiting for child process to finish its workload first...\n");
        
        // Synchronizing process lifecycle (waits until Child terminates)
        wait(NULL); 
        
        printf("\n[PARENT PROCESS] Child process returned control. Continuing execution...\n");
        
        run_part3_round_robin_demo();
        run_part4_deadlock_prevention_demo();

        printf("\nAll demonstrations completed successfully across all processes.\n");
    }

    return 0;
}