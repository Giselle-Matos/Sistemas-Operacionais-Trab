#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <time.h>

#define MSG_KEY 0x5678

typedef struct {
    int var1;
    int var2;
    pthread_mutex_t var1_mutex;
    pthread_mutex_t var2_mutex;
} shared_data;

typedef struct {
    long msg_type;
    int child_num;
} message;

void random_sleep() {
    int ms = (rand() % 1800) + 200;
    usleep(ms * 1000);
}

void *thread_func(void *arg) {
    shared_data *shdata = (shared_data *)arg;
    pthread_t tid = pthread_self();
    for (int i = 0; i < 30; ++i) {
        pthread_mutex_lock(&shdata->var1_mutex);
        pthread_mutex_lock(&shdata->var2_mutex);

        if (shdata->var1 == 0 && shdata->var2 == 120) {
            pthread_mutex_unlock(&shdata->var2_mutex);
            pthread_mutex_unlock(&shdata->var1_mutex);
            return NULL;
        }

        int local_var = shdata->var1;
        local_var--;
        random_sleep();
        shdata->var1 = local_var;

        shdata->var2++;
        
        printf("Thread TID: %ld, Iteration: %d, Var1: %d, Var2: %d\n", (long)tid, i+1, shdata->var1, shdata->var2);

        pthread_mutex_unlock(&shdata->var2_mutex);
        pthread_mutex_unlock(&shdata->var1_mutex);

        random_sleep();
    }
    return NULL;
}

void child_process(shared_data *shdata, int child_num) {
    for (int i = 0; i < 30; ++i) {
        pthread_mutex_lock(&shdata->var1_mutex);
        pthread_mutex_lock(&shdata->var2_mutex);

        if (shdata->var1 == 0 && shdata->var2 == 120) {
            pthread_mutex_unlock(&shdata->var2_mutex);
            pthread_mutex_unlock(&shdata->var1_mutex);
            return; 
        }

        int local_var = shdata->var1;
        local_var--;
        random_sleep();
        shdata->var1 = local_var;

        shdata->var2++;
        
        printf("Child PID: %d, Iteration: %d, Var1: %d, Var2: %d\n", getpid(), i+1, shdata->var1, shdata->var2);

        pthread_mutex_unlock(&shdata->var2_mutex);
        pthread_mutex_unlock(&shdata->var1_mutex);

        random_sleep();
    }

    int msgid = msgget(MSG_KEY, 0666 | IPC_CREAT);
    message msg;
    msg.msg_type = 1;
    msg.child_num = child_num;
    msgsnd(msgid, &msg, sizeof(message) - sizeof(long), 0);
}

void child_process_with_threads(shared_data *shdata, int child_num) {
    pthread_t threads[2];

    for (int i = 0; i < 2; ++i) {

        pthread_create(&threads[i], NULL, thread_func, shdata);
    }

    for (int i = 0; i < 2; ++i) {
        pthread_join(threads[i], NULL);

    }

    int msgid = msgget(MSG_KEY, 0666 | IPC_CREAT);
    message msg;
    msg.msg_type = 1;
    msg.child_num = child_num;
    msgsnd(msgid, &msg, sizeof(message) - sizeof(long), 0);
}

int main() {
    srand(time(NULL));

    key_t shm_key = ftok("main.c", 'R');
    if (shm_key == -1) {
        perror("ftok");
        exit(1);
    }

    int shmid =  shmget(shm_key, sizeof(shared_data), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }

    shared_data *shdata = (shared_data *)shmat(shmid, NULL, 0);
    if (shdata == (void *)-1) {

        perror("shmat");
        exit(1);
    }

    shdata->var1 = 120;
    shdata->var2 = 0;

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shdata->var1_mutex, &mattr);
    pthread_mutex_init(&shdata->var2_mutex, &mattr);

    pthread_mutexattr_destroy(&mattr);

    pid_t pids[4];

    for (int i = 0; i < 4; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) {
            if (i < 2) {
                child_process(shdata, i + 1);
            } else {
                child_process_with_threads(shdata, i + 1);
            }
            exit(0);
            
        }
    }

    int msgid = msgget(MSG_KEY, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("msgget");
        exit(1);
    }

    message msg;

    for (int i = 0; i < 4; ++i) {
        msgrcv(msgid, &msg, sizeof(message) - sizeof(long), 1, 0);
        printf("Parent received completion message from child %d\n", msg.child_num);
    }

    printf("Final values: Var1: %d, Var2: %d\n", shdata->var1, shdata->var2);

    pthread_mutex_destroy(&shdata->var1_mutex);
    pthread_mutex_destroy(&shdata->var2_mutex);

    shmdt(shdata);
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}
