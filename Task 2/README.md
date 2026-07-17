# Memory Management Simulation

## Overview

This project implements a virtual memory management simulator in C that demonstrates address translation and page replacement techniques. The simulator accepts a sequence of 16-bit virtual memory addresses, translates them into page numbers and offsets, and evaluates the performance of two page replacement algorithms: First-In First-Out (FIFO) and Least Recently Used (LRU).

## Features

- Simulates virtual memory address translation
- Configurable page size
- Configurable number of physical memory frames
- FIFO page replacement algorithm
- LRU page replacement algorithm
- Displays page hits and page faults
- Calculates hit ratio and miss ratio
- Prints the state of physical memory after each memory reference

## Requirements

- Ubuntu/Linux
- GCC Compiler

## Compilation

Open a terminal in the project directory and compile the program using:

```bash
gcc -Wall -Wextra -o memory_sim memory_sim.c
```

If your source file has a different name, replace `memory_sim.c` with the correct filename.

## Execution

Run the program using:

```bash
./memory_sim
```

## System Configuration

The simulator is configured with the following default parameters:

- Virtual address size: 16-bit
- Number of memory references: 15
- Page size: 256 Bytes
- Physical memory: 3 Frames

These values can be modified in the source code to test different memory configurations.

## Program Output

The simulator displays:

- Virtual memory address
- Calculated page number
- Offset within the page
- Current physical frame contents
- Page hit or page fault status

After both simulations, it prints a performance summary including:

- Total memory requests
- Page faults
- Page hits
- Hit ratio
- Miss ratio

## Algorithms Implemented

### FIFO (First-In First-Out)

- Replaces the page that has been in memory the longest.
- Uses a circular pointer to determine the next page for replacement.

### LRU (Least Recently Used)

- Replaces the page that has not been accessed for the longest time.
- Tracks the most recent access time for each page to make replacement decisions.

## Example Output

The program displays:

- FIFO simulation results
- LRU simulation results
- Frame contents after each memory access
- Final performance comparison table

## Notes

- The simulator is designed for educational purposes to demonstrate virtual memory management concepts.
- Address translation is performed by dividing each virtual address into a page number and an offset based on the configured page size.
- The program uses a predefined sequence of virtual memory addresses, which can be modified to test different access patterns.

## Exit

The program terminates automatically after displaying the performance summary.