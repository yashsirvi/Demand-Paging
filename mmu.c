#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <signal.h>

struct page_table_entry {
    int page;
    int frame;
    int valid_bit;
    int lru_counter;
};

struct page_table {
    struct page_table_entry *entries;
    int size;
    int total_page_faults;
    int invalid_page_references;
};

struct message_2 {
    long type;
    int pid;
};

struct message_3 {
    long type;              // 1 for request, 2 for response
    int pid;
    int page;
    int frame;
};

struct page_fault {
    int pid;
    int page;
};

struct inv_page_ref {
    int pid;
    int page;
};

struct global_ref {
    int timestamp;
    int pid;
    int page;
};

//Handler for SIGINT
void sigint_handler(int signum) {
    exit(0);
}

int main(int argc, char * argv[]) {
    // Receives MQ2, MQ3, SHM1 and SHM2 ids, k and m as arguments to the MMU
    if (argc != 7) {
        printf("Usage: %s <mq2_id> <mq3_id> <shm1_id> <shm2_id> <k> <m>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sigint_handler);

    int timestamp_count = 0;

    int mq2_id = atoi(argv[1]);
    int mq3_id = atoi(argv[2]);
    int shm1_id = atoi(argv[3]);
    int shm2_id = atoi(argv[4]);
    int k = atoi(argv[5]);
    int m = atoi(argv[6]);
    
    // Getting the shared memory for page tables and free frames list
    struct page_table *tables = (struct page_table *) shmat(shm1_id, NULL, 0);
    struct page_table_entry *entries_base = (struct page_table_entry *) (tables + k);
    for (int i = 0; i < k; i++) {
        tables[i].entries = entries_base + i * m;
    }
    int * free_frames = (int *) shmat(shm2_id, NULL, 0);        //Ended by -1

    struct message_2 msg2;
    struct message_3 msg3;

    //Open a file "results.txt"
    FILE * results_file = fopen("results.txt", "w");

    // printf("MMU: Initialized\n");
    // fflush(stdout);

    while (1) {
        // printf("MMU: Waiting for page request\n");
        // fflush(stdout);

        // Waiting for a page request on from MQ3
        memset(&msg3, 0, sizeof(msg3));
        msgrcv(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 1, 0);

        int pid = msg3.pid;
        int page = msg3.page;

        // printf("MMU: Received request from Process %d for page %d\n", pid, page);
        // fflush(stdout);

        // If page == -9, then the process has terminated
        if (page == -9) {
            // printf("MMU: Process %d terminated\n", pid);
            // fflush(stdout);
            
            // Freeing the frames of the process
            for (int i = 0; i < tables[pid].size; i++) {
                if (tables[pid].entries[i].valid_bit == 1) {
                    free_frames[tables[pid].entries[i].frame] = 0;
                    tables[pid].entries[i].valid_bit = 0;
                    tables[pid].entries[i].frame = -1;
                }
            }

            // Sending Type 2 message to Scheduler
            memset(&msg2, 0, sizeof(msg2));
            msg2.type = 2;
            msg2.pid = pid;
            msgsnd(mq2_id, &msg2, sizeof(msg2) - sizeof(long), 0);

            continue;
        } else {
            // Writing to results.txt
            fprintf(results_file, "Global reference sequence - (%d,%d,%d)\n", timestamp_count, pid, page);
            fflush(results_file);

            // Checking if page ref is invalid
            if (page >= tables[pid].size) {
                printf("TRYING TO ACCESS INVALID PAGE REFERENCE\n");
                fflush(stdout);

                // Writing to results.txt
                fprintf(results_file, "Invalid page reference - (%d,%d)\n", pid, page);
                fflush(results_file);

                // Incrementing invalid page references
                tables[pid].invalid_page_references++;

                // Responding to the process with frame = -2
                memset(&msg3, 0, sizeof(msg3));
                msg3.type = 2;
                msg3.pid = pid;
                msg3.page = page;
                msg3.frame = -2;
                msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

                // Freeing the frames of the process
                for (int i = 0; i < tables[pid].size; i++) {
                    if (tables[pid].entries[i].valid_bit == 1) {
                        free_frames[tables[pid].entries[i].frame] = 0;
                        tables[pid].entries[i].valid_bit = 0;
                        tables[pid].entries[i].frame = -1;
                    }
                }

                // Sending scheduler a Type 2 message
                memset(&msg2, 0, sizeof(msg2));
                msg2.type = 2;
                msg2.pid = pid;
                msgsnd(mq2_id, &msg2, sizeof(msg2) - sizeof(long), 0);

                // Incrementing timestamp
                timestamp_count++;

                continue;
            }

            // Checking if page is in memory
            if (tables[pid].entries[page].valid_bit == 1) {
                // Page is in memory
                // printf("MMU: Page %d of Process %d is in memory\n", page, pid);
                // fflush(stdout);

                // Incrementing LRU counters of all pages
                for (int i = 0; i < tables[pid].size; i++) {
                    if (tables[pid].entries[i].valid_bit == 1) {
                        tables[pid].entries[i].lru_counter++;
                    }
                }

                // Resetting LRU counter of the accessed page
                tables[pid].entries[page].lru_counter = 0;

                // Returning the frame number
                memset(&msg3, 0, sizeof(msg3));
                msg3.type = 2;
                msg3.pid = pid;
                msg3.page = page;
                msg3.frame = tables[pid].entries[page].frame;
                msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

                // Incrementing timestamp
                timestamp_count++;

                continue;
            } else {
                // Page is not in memory
                // printf("MMU: Page %d of Process %d is not in memory\n", page, pid);
                // fflush(stdout);

                //Writing to results.txt
                fprintf(results_file, "Page fault sequence - (%d,%d)\n", pid, page);
                fflush(results_file);

                // Incrementing page faults
                tables[pid].total_page_faults++;

                // Checking if there is a free frame
                int cur_frame = 0;
                while (free_frames[cur_frame] == 1) {
                    cur_frame++;
                }

                // If free_frames[cur_frame] != -1, then it is a free frame
                if (free_frames[cur_frame] ==  0) {
                    // Free frame found
                    // printf("MMU: Page %d of Process %d is loaded in frame %d\n", page, pid, cur_frame);
                    // fflush(stdout);

                    // Adding to the page table
                    tables[pid].entries[page].frame = cur_frame;
                    tables[pid].entries[page].valid_bit = 1;

                    // Setting the frame as occupied
                    free_frames[cur_frame] = 1;

                    // Resetting LRU counter of the accessed page
                    tables[pid].entries[page].lru_counter = 0;

                    // Returning -1 as frame number
                    memset(&msg3, 0, sizeof(msg3));
                    msg3.type = 2;
                    msg3.pid = pid;
                    msg3.page = page;
                    msg3.frame = -1;
                    msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

                    // Sending a Type 1 message to the Scheduler
                    memset(&msg2, 0, sizeof(msg2));
                    msg2.type = 1;
                    msg2.pid = pid;
                    msgsnd(mq2_id, &msg2, sizeof(msg2) - sizeof(long), 0);

                    // Incrementing timestamp
                    timestamp_count++;

                    continue;
                } else {
                    // No free frame found

                    // Finding the page with the maximum LRU counter
                    int max_lru = -1;
                    int max_lru_page = -1;
                    for (int i = 0; i < tables[pid].size; i++) {
                        if (tables[pid].entries[i].valid_bit == 1) {
                            if (tables[pid].entries[i].lru_counter > max_lru) {
                                max_lru = tables[pid].entries[i].lru_counter;
                                max_lru_page = i;
                            }
                        }
                    }

                    // If the process has no allocated frames, then return -1
                    if (max_lru_page == -1) {
                        // No allocated frames
                        // printf("No allocated frames for Process %d\n", pid);
                        // fflush(stdout);

                        memset(&msg3, 0, sizeof(msg3));
                        msg3.type = 2;
                        msg3.pid = pid;
                        msg3.page = page;
                        msg3.frame = -1;
                        msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

                        // Sending a Type 1 message to the Scheduler
                        memset(&msg2, 0, sizeof(msg2));
                        msg2.type = 1;
                        msg2.pid = pid;
                        msgsnd(mq2_id, &msg2, sizeof(msg2) - sizeof(long), 0);

                        // Incrementing timestamp
                        timestamp_count++;

                        continue;
                    }

                    // printf("MMU: Page %d of Process %d is loaded in frame %d\n", page, pid, tables[pid].entries[max_lru_page].frame);
                    // fflush(stdout);

                    // Replacing the page with the maximum LRU counter
                    cur_frame = tables[pid].entries[max_lru_page].frame;
                    tables[pid].entries[max_lru_page].frame = -1;
                    tables[pid].entries[max_lru_page].valid_bit = 0;
                    tables[pid].entries[page].frame = cur_frame;
                    tables[pid].entries[page].valid_bit = 1;

                    // Resetting LRU counter of the accessed page
                    tables[pid].entries[page].lru_counter = 0;

                    // Returning -1 as frame number
                    memset(&msg3, 0, sizeof(msg3));
                    msg3.type = 2;
                    msg3.pid = pid;
                    msg3.page = page;
                    msg3.frame = -1;
                    msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

                    // Sending a Type 1 message to the Scheduler
                    memset(&msg2, 0, sizeof(msg2));
                    msg2.type = 1;
                    msg2.pid = pid;
                    msgsnd(mq2_id, &msg2, sizeof(msg2) - sizeof(long), 0);

                    // Incrementing timestamp
                    timestamp_count++;

                    continue;
                }
            }
        }
    }
}