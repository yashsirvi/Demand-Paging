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

key_t shm1_key = 1234;      // Key for shared memory for page tables
key_t shm2_key = 5678;      // Key for shared memory for free frames list
key_t mq1_key = 9101;      // Key for message queue
key_t mq2_key = 1121;      // Key for message queue
key_t mq3_key = 3141;      // Key for message queue

int k, m, f;

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

struct page_table *tables;

typedef struct {
    char type[30];
    char entry[50];
} Entry;

// Create signal handler for SIGINT (Resets the shared memory and message queues and terminates all processes)
void sigint_handler(int signum) {
    // printf("Master: Received SIGINT\n");
    // fflush(stdout);

    // Resetting shared memory
    int shm1_id = shmget(shm1_key, 0, 0666);
    int shm2_id = shmget(shm2_key, 0, 0666);
    char * shm1 = (char *) shmat(shm1_id, NULL, 0);
    char * shm2 = (char *) shmat(shm2_id, NULL, 0);
    memset(shm1, 0, 1);
    memset(shm2, 0, 1);
    shmdt(shm1);
    shmdt(shm2);
    shmctl(shm1_id, IPC_RMID, NULL);
    shmctl(shm2_id, IPC_RMID, NULL);

    // Resetting message queues
    int mq1_id = msgget(mq1_key, 0);
    int mq2_id = msgget(mq2_key, 0);
    int mq3_id = msgget(mq3_key, 0);
    // Clearing the message queues
    msgctl(mq1_id, IPC_RMID, NULL);
    msgctl(mq2_id, IPC_RMID, NULL);
    msgctl(mq3_id, IPC_RMID, NULL);

    // Terminating all processes
    kill(0, SIGKILL);
    exit(0);
}

void reorder_output_file() {
    //Opening results.txt file and reordering the sequences, so that page_fault_seq, inv_page_ref_seq and global_ref_seq are clustered together
    FILE *file = fopen("results.txt", "r");

    Entry *entries = NULL;
    int num_entries = 0;
    int capacity = 0;

    char line[200];
    while (fgets(line, 200, file)) {
        // Remove the newline character at the end of the line
        line[strcspn(line, "\n")] = '\0';

        if (num_entries >= capacity) {
            capacity += 100;
            entries = (Entry *)realloc(entries, capacity * sizeof(Entry));
        }

        char type[30], entry[50];
        sscanf(line, "%[^-]- (%[^)])", type, entry);

        strcpy(entries[num_entries].type, type);
        strcpy(entries[num_entries].entry, entry);
        num_entries++;
    }

    fclose(file);

    file = fopen("results.txt", "w");

    // Write page fault sequences
    fprintf(file, "Page fault sequences:\n");
    for (int i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].type, "Page fault sequence ") == 0) {
            fprintf(file, "(%s)\n", entries[i].entry);
        }
    }
    // Printing total number of page faults per process
    for (int i = 0; i < k; i++) {
        fprintf(file, "Total page faults for process %d: %d\n", i, tables[i].total_page_faults);
    }
    fprintf(file, "\n");

    // Write invalid page references
    fprintf(file, "Invalid page references:\n");
    for (int i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].type, "Invalid page reference ") == 0) {
            fprintf(file, "(%s)\n", entries[i].entry);
        }
    }
    // Printing total number of invalid page references per process
    for (int i = 0; i < k; i++) {
        fprintf(file, "Total invalid page references for process %d: %d\n", i, tables[i].invalid_page_references);
    }
    fprintf(file, "\n");

    // Write global reference sequences
    fprintf(file, "Global reference sequences:\n");
    for (int i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].type, "Global reference sequence ") == 0) {
            fprintf(file, "(%s)\n", entries[i].entry);
        }
    }
    fclose(file);
    free(entries);

    //Printing all the lines in the file
    file = fopen("results.txt", "r");
    char c;
    while ((c = fgetc(file)) != EOF) {
        printf("%c", c);
    }
    fclose(file);
}

int main(int argc, char * argv[]) {
    if (argc != 4) {
        printf("Usage: %s <k> <m> <f>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sigint_handler);

    k = atoi(argv[1]);      // Number of processes
    m = atoi(argv[2]);      // Number of pages
    f = atoi(argv[3]);      // Number of frames

    // Creating a semaphore for Master to wait for scheduler on
    key_t sem1_key = ftok("master.c", 1);
    int sem1_id = semget(sem1_key, 1, IPC_CREAT | 0666);
    semctl(sem1_id, 0, SETVAL, 0);

    // Creating k semaphores for each process to wait for Scheduler on
    key_t sem2_key = ftok("master.c", 2);
    int sem2_id = semget(sem2_key, k, IPC_CREAT | 0666);
    for (int i = 0; i < k; i++) {
        semctl(sem2_id, i, SETVAL, 0);
    }

    // Create shared memory for page tables (1 for each process, having maximum m pages)
    int shm1_id = shmget(shm1_key, k * sizeof(struct page_table) + k * m * sizeof(struct page_table_entry), IPC_CREAT | 0666);

    tables = (struct page_table *) shmat(shm1_id, NULL, 0);
    memset(tables, 0, k * sizeof(struct page_table) + k * m * sizeof(struct page_table_entry));

    struct page_table_entry *entries_base = (struct page_table_entry *) (tables + k);

    // Initialize page tables
    for (int i = 0; i < k; i++) {
        tables[i].entries = entries_base + i * m;
        tables[i].size = m;
        tables[i].total_page_faults = 0;
        tables[i].invalid_page_references = 0;
        for (int j = 0; j < m; j++) {
            tables[i].entries[j].page = j;
            tables[i].entries[j].frame = -1;
            tables[i].entries[j].valid_bit = 0;
            tables[i].entries[j].lru_counter = 0;
        }
    }

    // Create shared memory for free frames list
    int shm2_id = shmget(shm2_key, (f + 1) * sizeof(int), IPC_CREAT | 0666);

    int *free_frames = (int *) shmat(shm2_id, NULL, 0);
    memset(free_frames, 0, (f + 1) * sizeof(int));

    // Initialize free frames list (All frames are free initially)
    for (int i = 0; i < f; i++) {
        free_frames[i] = 0;         // 0 means free
    }
    free_frames[f] = -1;            // -1 means end of list

    // Creating a message queue (which is the ready queue)
    int mq1_id = msgget(mq1_key, IPC_CREAT | 0666);

    // Creating a message queue (which is for communication between scheduler and MMU)
    int mq2_id = msgget(mq2_key, IPC_CREAT | 0666);

    // Creating a message queue (which is for communication between processes and MMU)
    int mq3_id = msgget(mq3_key, IPC_CREAT | 0666);

    //Clearing the message queues (By receiving all messages)
    struct {
        long type;
        char data[100];
    } msg;
    while (msgrcv(mq1_id, &msg, sizeof(msg.data), 0, IPC_NOWAIT) != -1);
    while (msgrcv(mq2_id, &msg, sizeof(msg.data), 0, IPC_NOWAIT) != -1);
    while (msgrcv(mq3_id, &msg, sizeof(msg.data), 0, IPC_NOWAIT) != -1);

    // printf("mq1_id: %d\n", mq1_id);
    // printf("mq2_id: %d\n", mq2_id);
    // printf("mq3_id: %d\n", mq3_id);
    // fflush(stdout);

    int ** reference_strings = (int **) malloc(k * sizeof(int *));
    int * ref_str_lens = (int *) malloc(k * sizeof(int));

    // Initializing the page tables of each process and creating reference strings
    for (int i = 0; i < k; i++) {
        // Choosing a random number of pages for the process
        int m_i = rand() % m + 1;
        // Updating the size of the page table of the process
        tables[i].size = m_i;

        // Choosing length of reference string for the process
        int ref_str_len = rand() % (8 * m_i + 1) + 2 * m_i;
        ref_str_lens[i] = ref_str_len;

        // Creating reference string for the process (Will be passed as an argument to the process, each page number separated by a comma)
        reference_strings[i] = (int *) malloc(ref_str_len * sizeof(int));
        for (int j = 0; j < ref_str_len; j++) {
            reference_strings[i][j] = rand() % m_i;
        }
    }

    // With a low probability, some illegal memory accesses are generated for some processes
    for (int i = 0; i < k; i++) {
        if (rand() % 10 == 0) {
            //The generated page must be illegal
            int m_i = tables[i].size;
            reference_strings[i][rand() % ref_str_lens[i]] = m_i + rand() % (m - m_i);
        }
    }

    // Going through all process and allocating frames proportionally to the number of pages they have
    int total_page_count = 0;
    for (int i = 0; i < k; i++) {
        total_page_count += tables[i].size;
    }
    int cur_free_frame = 0;
    for (int i = 0; i < k; i++) {
        int frames_allocated = (int) (f * tables[i].size / total_page_count);
        // printf("Process %d: Total pages: %d, Frames allocated: %d\n", i, tables[i].size, frames_allocated);
        // fflush(stdout);
        // Going through free frames list and allocating frames to the process
        for (int j = 0; j < frames_allocated; j++) {
            tables[i].entries[j].frame = cur_free_frame;
            tables[i].entries[j].valid_bit = 1;
            free_frames[cur_free_frame] = 1;
            cur_free_frame++;
        }
    }

    // sleep(5);

    // printf("Master: Initialized\n");
    // fflush(stdout);

    // Creating the scheduler process
    int scheduler_pid = fork();
    if (scheduler_pid == 0) {
        // Passing MQ1, MQ2 keys and total number of processes as arguments to the scheduler
        char mq1_key_str[10];
        char mq2_key_str[10];
        char k_str[10];
        sprintf(mq1_key_str, "%d", mq1_key);
        sprintf(mq2_key_str, "%d", mq2_key);
        sprintf(k_str, "%d", k);
        char *args[] = {"./Scheduler", mq1_key_str, mq2_key_str, k_str, NULL};
        execvp(args[0], args);
    }

    // Creating the MMU process
    int mmu_pid = fork();
    if (mmu_pid == 0) {
        // Running on xterm
        // Passing MQ2, MQ3, SHM1 and SHM2 ids, k and m as arguments to the MMU
        char mq2_id_str[10];
        char mq3_id_str[10];
        char shm1_id_str[10];
        char shm2_id_str[10];
        char k_str[10];
        char m_str[10];
        sprintf(mq2_id_str, "%d", mq2_id);
        sprintf(mq3_id_str, "%d", mq3_id);
        sprintf(shm1_id_str, "%d", shm1_id);
        sprintf(shm2_id_str, "%d", shm2_id);
        sprintf(k_str, "%d", k);
        sprintf(m_str, "%d", m);
        char *args[] = {"xterm", "-hold", "-e", "./MMU", mq2_id_str, mq3_id_str, shm1_id_str, shm2_id_str, k_str, m_str, NULL};
        execvp(args[0], args);
    }

    // Creating the k processes
    for (int i = 0; i < k; i++) {
        int pid = fork();
        if (pid == 0) {
            // Passing the process id and MQ1, MQ3 keys as arguments to the process, and k
            char pid_str[10];
            char mq1_key_str[10];
            char mq3_key_str[10];
            char k_str[10];
            // Creating a comma separated string of the reference string.
            char ref_str[5000];
            sprintf(pid_str, "%d", i);
            sprintf(mq1_key_str, "%d", mq1_key);
            sprintf(mq3_key_str, "%d", mq3_key);
            sprintf(k_str, "%d", k);
            sprintf(ref_str, "%d", reference_strings[i][0]);
            for (int j = 1; j < ref_str_lens[i]; j++) {
                sprintf(ref_str, "%s,%d", ref_str, reference_strings[i][j]);
            }
            char *args[] = {"./Process", pid_str, mq1_key_str, mq3_key_str, k_str, ref_str, NULL};
            execvp(args[0], args);
        }

        usleep(250000);
    }

    // Waiting for signal from scheduler to terminate
    struct sembuf sem_op;
    sem_op.sem_num = 0;
    sem_op.sem_op = -1;
    sem_op.sem_flg = 0;
    semop(sem1_id, &sem_op, 1);

    // Killing scheduler and MMU
    // kill(scheduler_pid, SIGKILL);
    kill(mmu_pid, SIGINT);

    reorder_output_file();

    // Detaching and removing shared memory
    shmdt(tables);
    shmctl(shm1_id, IPC_RMID, NULL);
    shmdt(free_frames);
    shmctl(shm2_id, IPC_RMID, NULL);
    
    // Removing message queues
    msgctl(mq1_id, IPC_RMID, NULL);
    msgctl(mq2_id, IPC_RMID, NULL);
    msgctl(mq3_id, IPC_RMID, NULL);

    // Removing semaphores
    semctl(sem1_id, 0, IPC_RMID);
    semctl(sem2_id, 0, IPC_RMID);

    return 0;
}