# Process Management, Threading and Synchronization

## Overview

This project demonstrates core Operating System concepts using the C programming language and POSIX libraries on Linux. The implementation combines process creation, multithreading, synchronization mechanisms, CPU scheduling simulation, and deadlock prevention into a single application.

## Features

- Process creation using `fork()`
- Process synchronization using `wait()`
- Race condition demonstration
- Mutex-based synchronization
- Producer-Consumer implementation using semaphores
- Round Robin CPU scheduling simulation
- Deadlock prevention using lock ordering
- Multithreading using POSIX Threads (Pthreads)

## Requirements

- Ubuntu/Linux
- GCC Compiler
- POSIX Threads Library
- POSIX Semaphore Library

## Compilation

Open a terminal in the project directory and compile the program using:

```bash
gcc -Wall -Wextra -pthread -o task1 Pasted\ code\(4\).c
```

If the source file has been renamed, use:

```bash
gcc -Wall -Wextra -pthread -o task1 task1.c
```

## Execution

Run the executable using:

```bash
./task1
```

## Program Demonstration

The program automatically performs the following demonstrations:

### Part 1 – Race Condition
- Creates multiple threads that increment a shared counter.
- Demonstrates incorrect results without a mutex.
- Demonstrates correct synchronization using a mutex.

### Part 2 – Producer-Consumer Problem
- Uses a bounded buffer.
- Synchronizes producer and consumer threads with semaphores and a mutex.
- Prevents data corruption and buffer overflow.

### Part 3 – Round Robin CPU Scheduling
- Simulates five processes.
- Uses a fixed time quantum.
- Displays:
  - Gantt Chart
  - Waiting Time
  - Turnaround Time
  - Average Waiting Time
  - Average Turnaround Time

### Part 4 – Deadlock Prevention
- Demonstrates two threads accessing shared resources.
- Prevents deadlock by enforcing a consistent lock acquisition order.

## Expected Output

The program displays:

- Parent and Child process creation
- Thread execution
- Race condition results
- Producer and Consumer operations
- Round Robin scheduling results
- Deadlock prevention demonstration
- Successful program completion

## Libraries Used

- stdio.h
- stdlib.h
- string.h
- unistd.h
- pthread.h
- semaphore.h
- sched.h
- time.h
- sys/types.h
- sys/wait.h

## Notes

- The program is designed for Linux systems.
- Compile using the `-pthread` option to enable POSIX thread support.
- The child process executes the threading and synchronization demonstrations, while the parent process performs the scheduling and deadlock demonstrations after waiting for the child to finish.

## Exit

The program terminates automatically after completing all demonstrations successfully.