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

struct message_1 {
    long type;
    int pid;
};

struct message_2 {
    long type;
    int pid;
};

int main(int argc, char * argv[]) {
    // Arguments are MQ1, MQ2 keys and number of processes
    if (argc != 4) {
        printf("Usage: %s <mq1_key> <mq2_key> <k>\n", argv[0]);
        return 1;
    }

    key_t mq1_key = atoi(argv[1]);
    key_t mq2_key = atoi(argv[2]);
    int k = atoi(argv[3]);

    // Getting both the message queues
    int mq1_id = msgget(mq1_key, 0666);
    int mq2_id = msgget(mq2_key, 0666);

    // Getting the semaphore (which master will wait on)
    key_t sem_key = ftok("master.c", 1);
    int sem_id = semget(sem_key, 1, 0666);

    // Getting the k semaphores (which each process will wait on)
    key_t sem2_key = ftok("master.c", 2);
    int sem2_id = semget(sem2_key, k, 0666);

    struct message_1 msg1;
    struct message_2 msg2;

    while (k > 0) {
        usleep(10000);
        // Waiting for message on MQ1 (Ready Queue)
        memset(&msg1, 0, sizeof(msg1));
        msgrcv(mq1_id, &msg1, sizeof(msg1) - sizeof(long), 0, 0);

        // Scheduling the process
        // printf("Scheduler: Scheduled process %d\n", msg1.pid);
        // fflush(stdout);

        int cur_pid = msg1.pid;
        // Signal the process
        struct sembuf sem_op;
        sem_op.sem_num = cur_pid;
        sem_op.sem_op = 1;
        sem_op.sem_flg = 0;
        semop(sem2_id, &sem_op, 1);

        // printf("Scheduler: Signalled process %d\n", cur_pid);
        // fflush(stdout);

        // Waiting for message on MQ2 (from MMU)
        memset(&msg2, 0, sizeof(msg2));
        msgrcv(mq2_id, &msg2, sizeof(msg2) - sizeof(long), 0, 0);

        // Checking type of message
        if (msg2.type == 1) {
            // Page Fault Handled, enqueue the process again
            // printf("Scheduler: Process %d had a page fault (HANDLED)\n", msg2.pid);
            // fflush(stdout);
        } else if (msg2.type == 2) {
            // Process Terminated
            // printf("Scheduler: Process %d TERMINATED\n", msg2.pid);
            // fflush(stdout);

            k--;
        }
    }

    // Sending signal to Master
    // printf("Scheduler: All processes terminated\n");
    // fflush(stdout);

    struct sembuf sem_op;
    sem_op.sem_num = 0;
    sem_op.sem_op = 1;
    sem_op.sem_flg = 0;

    semop(sem_id, &sem_op, 1);

    return 0;
}