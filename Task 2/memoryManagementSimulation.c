/* =============================================================================
 *
 * Task 2: Memory Management Simulation (Updated with Address Translation)
 *
 * This version accurately bridges the gap between raw hardware memory addresses 
 * and page tracking. It accepts a stream of 16-bit virtual memory addresses,
 * partitions them using a configurable Page Size, and extracts the page number
 * before passing them to the FIFO and LRU algorithms.
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_ADDRESSES 15
#define UNASSIGNED -1

typedef struct {
    int total_requests;
    int page_faults;
    int page_hits;
    double hit_ratio;
    double miss_ratio;
} SimMetrics;

// Simple print layout to view the current contents of physical memory RAM
void print_frames(int frames[], int num_frames) {
    printf("[");
    for (int i = 0; i < num_frames; i++) {
        if (frames[i] == UNASSIGNED) {
            printf("  -  ");
        } else {
            printf(" P:%-2d ", frames[i]);
        }
        if (i < num_frames - 1) printf("|");
    }
    printf("]");
}

/* =============================================================================
 * FIRST-IN, FIRST-OUT (FIFO) WITH ADDRESS TRANSLATION
 * ========================================================================== */
void run_fifo_simulation(int virtual_addresses[], int num_addrs, int page_size, int num_frames, SimMetrics *metrics) {
    int frames[num_frames];
    for (int i = 0; i < num_frames; i++) frames[i] = UNASSIGNED;

    int current_fifo_index = 0;
    metrics->total_requests = num_addrs;
    metrics->page_faults = 0;
    metrics->page_hits = 0;

    printf("\n--- Running FIFO Simulation (Page Size: %d Bytes, Frames: %d) ---\n", page_size, num_frames);
    printf("%-10s | %-8s | %-8s | %-16s | %-12s\n", "Virt Addr", "Page No", "Offset", "Frame State", "Status");
    printf("----------------------------------------------------------------------\n");

    for (int i = 0; i < num_addrs; i++) {
        int v_addr = virtual_addresses[i];
        
        // Dynamic Translation Mathematics
        int incoming_page = v_addr / page_size;
        int offset = v_addr % page_size;
        
        bool is_hit = false;

        for (int j = 0; j < num_frames; j++) {
            if (frames[j] == incoming_page) {
                is_hit = true;
                break;
            }
        }

        if (is_hit) {
            metrics->page_hits++;
            printf(" 0x%-7X | %-8d | %-8d | ", v_addr, incoming_page, offset);
            print_frames(frames, num_frames);
            printf(" | HIT\n");
        } else {
            metrics->page_faults++;
            frames[current_fifo_index] = incoming_page;
            current_fifo_index = (current_fifo_index + 1) % num_frames;

            printf(" 0x%-7X | %-8d | %-8d | ", v_addr, incoming_page, offset);
            print_frames(frames, num_frames);
            printf(" | MISS (Fault)\n");
        }
    }

    metrics->hit_ratio = (double)metrics->page_hits / num_addrs * 100.0;
    metrics->miss_ratio = (double)metrics->page_faults / num_addrs * 100.0;
}

/* =============================================================================
 * LEAST RECENTLY USED (LRU) WITH ADDRESS TRANSLATION
 * ========================================================================== */
void run_lru_simulation(int virtual_addresses[], int num_addrs, int page_size, int num_frames, SimMetrics *metrics) {
    int frames[num_frames];
    int last_used_time[num_frames];
    
    for (int i = 0; i < num_frames; i++) {
        frames[i] = UNASSIGNED;
        last_used_time[i] = 0;
    }

    metrics->total_requests = num_addrs;
    metrics->page_faults = 0;
    metrics->page_hits = 0;

    printf("\n--- Running LRU Simulation (Page Size: %d Bytes, Frames: %d) ---\n", page_size, num_frames);
    printf("%-10s | %-8s | %-8s | %-16s | %-12s\n", "Virt Addr", "Page No", "Offset", "Frame State", "Status");
    printf("----------------------------------------------------------------------\n");

    for (int time = 0; time < num_addrs; time++) {
        int v_addr = virtual_addresses[time];
        
        // Dynamic Translation Mathematics
        int incoming_page = v_addr / page_size;
        int offset = v_addr % page_size;
        
        bool is_hit = false;
        int hit_index = -1;

        for (int j = 0; j < num_frames; j++) {
            if (frames[j] == incoming_page) {
                is_hit = true;
                hit_index = j;
                break;
            }
        }

        if (is_hit) {
            metrics->page_hits++;
            last_used_time[hit_index] = time;

            printf(" 0x%-7X | %-8d | %-8d | ", v_addr, incoming_page, offset);
            print_frames(frames, num_frames);
            printf(" | HIT\n");
        } else {
            metrics->page_faults++;
            
            int target_frame_idx = -1;
            for (int j = 0; j < num_frames; j++) {
                if (frames[j] == UNASSIGNED) {
                    target_frame_idx = j;
                    break;
                }
            }

            if (target_frame_idx == -1) {
                int oldest_time = last_used_time[0];
                target_frame_idx = 0;
                for (int j = 1; j < num_frames; j++) {
                    if (last_used_time[j] < oldest_time) {
                        oldest_time = last_used_time[j];
                        target_frame_idx = j;
                    }
                }
            }

            frames[target_frame_idx] = incoming_page;
            last_used_time[target_frame_idx] = time;

            printf(" 0x%-7X | %-8d | %-8d | ", v_addr, incoming_page, offset);
            print_frames(frames, num_frames);
            printf(" | MISS (Fault)\n");
        }
    }

    metrics->hit_ratio = (double)metrics->page_hits / num_addrs * 100.0;
    metrics->miss_ratio = (double)metrics->page_faults / num_addrs * 100.0;
}

/* =============================================================================
 * TEST ENGINE
 * ========================================================================== */
int main(void) {
    // Array of raw 16-bit virtual memory address updates representing real instruction calls
    int virtual_addresses[MAX_ADDRESSES] = {
        0x0004, 0x0102, 0x00A0, 0x0400, 0x0108, 
        0x0204, 0x0008, 0x0410, 0x0208, 0x000C, 
        0x0800, 0x0412, 0x010C, 0x0804, 0x0010
    };

    // Configuration Settings 
    int page_size = 256; // Configurable layout cap (e.g., 256 bytes per page frame)
    int num_frames = 3;  // Physical RAM capability

    SimMetrics fifo_metrics;
    SimMetrics lru_metrics;

    printf("======================================================================\n");
    printf("      Memory Simulator - Address Translation & Paging Engine          \n");
    printf("======================================================================\n");
    printf("Configured System Parameters:\n");
    printf(" -> Page Size      : %d Bytes\n", page_size);
    printf(" -> RAM Size       : %d Physical Frames (%d Bytes total)\n", num_frames, num_frames * page_size);
    
    run_fifo_simulation(virtual_addresses, MAX_ADDRESSES, page_size, num_frames, &fifo_metrics);
    run_lru_simulation(virtual_addresses, MAX_ADDRESSES, page_size, num_frames, &lru_metrics);

    // Performance Summary Breakdown
    printf("\n===========================================================\n");
    printf("                ALGORITHM PERFORMANCE SUMMARY               \n");
    printf("===========================================================\n");
    printf("%-20s | %-10s | %-10s\n", "Metric", "FIFO", "LRU");
    printf("-----------------------------------------------------------\n");
    printf("%-20s | %-10d | %-10d\n", "Total Requests", fifo_metrics.total_requests, lru_metrics.total_requests);
    printf("%-20s | %-10d | %-10d\n", "Page Faults (Misses)", fifo_metrics.page_faults, lru_metrics.page_faults);
    printf("%-20s | %-10d | %-10d\n", "Page Hits", fifo_metrics.page_hits, lru_metrics.page_hits);
    printf("%-20s | %-9.2f%% | %-9.2f%%\n", "Hit Ratio", fifo_metrics.hit_ratio, lru_metrics.hit_ratio);
    printf("%-20s | %-9.2f%% | %-9.2f%%\n", "Miss Ratio", fifo_metrics.miss_ratio, lru_metrics.miss_ratio);
    printf("===========================================================\n");

    return 0;
}