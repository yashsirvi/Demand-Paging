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

struct message_3 {
    long type;              // 1 for request, 2 for response
    int pid;
    int page;
    int frame;
};

int main(int argc, char * argv[]) {
    // Arguments are process_id, MQ1, MQ3 keys, k and comma separated ref string
    if (argc != 6) {
        printf("Usage: %s <process_id> <mq1_key> <mq3_key> <k> <ref_string>\n", argv[0]);
        return 1;
    }

    int process_id = atoi(argv[1]);
    key_t mq1_key = atoi(argv[2]);
    key_t mq3_key = atoi(argv[3]);
    int k = atoi(argv[4]);
    char * ref_string = argv[5];

    //Print the ref_string
    // printf("Process %d: Reference string: %s\n", process_id, ref_string);
    // fflush(stdout);

    // Getting the message queues
    int mq1_id = msgget(mq1_key, 0666);
    int mq3_id = msgget(mq3_key, 0666);

    // Getting the k semaphores (which each process will wait on)
    key_t sem2_key = ftok("master.c", 2);
    int sem2_id = semget(sem2_key, k, 0666);

    struct message_1 msg1;
    struct message_3 msg3;
    
    //Converting ref_string to array of integers (Each integer is separated by comma)
    int ref_string_len = strlen(ref_string);
    int * ref_string_int_array = (int *) malloc(ref_string_len * sizeof(int));
    int ref_string_int_array_len = 0;
    char * token = strtok(ref_string, ",");
    while (token != NULL) {
        ref_string_int_array[ref_string_int_array_len++] = atoi(token);
        token = strtok(NULL, ",");
    }

    int ref_string_index = 0;

    while (1) {
        // Sending message on MQ1 (Ready Queue)
        // printf("Process %d: Requesting to scheduler\n", process_id);
        // fflush(stdout);

        memset(&msg1, 0, sizeof(msg1));
        msg1.type = 1;
        msg1.pid = process_id;
        msgsnd(mq1_id, &msg1, sizeof(msg1.pid), 0);

        // Waiting on semaphore
        struct sembuf sem_op;
        sem_op.sem_num = process_id;
        sem_op.sem_op = -1;
        sem_op.sem_flg = 0;
        semop(sem2_id, &sem_op, 1);

        // printf("Process %d: Got signal from scheduler\n", process_id);
        // fflush(stdout);

        // Going through reference string an sending messages on MQ3
        while (ref_string_index != ref_string_int_array_len) {
            // printf("Process %d: Requesting page %d\n", process_id, ref_string_int_array[ref_string_index]);
            // fflush(stdout);

            memset(&msg3, 0, sizeof(msg3));
            msg3.type = 1;
            msg3.pid = process_id;
            msg3.page = ref_string_int_array[ref_string_index];
            msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

            // Waiting for response
            memset(&msg3, 0, sizeof(msg3));
            msgrcv(mq3_id, &msg3, sizeof(msg3) - sizeof(long), (long) 2, 0);

            // printf("Process %d: Got frame %d for page %d\n", process_id, msg3.frame, msg3.page);
            // fflush(stdout);

            if (msg3.frame == -1) {
                // printf("Process %d: Page %d not in memory\n", process_id, msg3.page);
                // fflush(stdout);
                break;
            } else if (msg3.frame == -2) {
                // printf("Process %d: Page %d is invalid reference\n", process_id, msg3.page);
                // fflush(stdout);
                exit(0);
            } else {
                ref_string_index++;
            }
        }

        if (ref_string_index == ref_string_int_array_len) {
            break;
        }
    }

    // Sending message on MQ3 to terminate process
    memset(&msg3, 0, sizeof(msg3));
    msg3.type = 1;
    msg3.pid = process_id;
    msg3.page = -9;
    msgsnd(mq3_id, &msg3, sizeof(msg3) - sizeof(long), 0);

    exit(0);
}