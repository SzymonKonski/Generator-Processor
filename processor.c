#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>
#include <stdbool.h>

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                perror(source),kill(0,SIGKILL),\
                exit(EXIT_FAILURE))

#define CHAR2 11

typedef struct timespec timespec_t;

volatile sig_atomic_t last_signal = 0;

int sethandler( void (*f)(int), int sigNo) {
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1==sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sig_handler(int sig) {
    last_signal = sig;
}

void usage(char* pname){
	fprintf(stderr, "USAGE: %s\n",pname);
    fprintf(stderr, "1 <= t <= 10\n");
    fprintf(stderr, "0 <= p <= 100\n");
    fprintf(stderr, "queue2 name\n");
	exit(EXIT_FAILURE);
}

void read_arguments(int argc, char** argv, int* t, int* p) {
    if(argc != 4) usage(argv[0]);
    *p = atoi(argv[2]);
    *t = atoi(argv[1]);
    if(*p < 0 || *p > 100) usage(argv[0]);
    if(*t < 1 || *t > 10) usage(argv[0]);
}

void send_msg_q2(char* msg_q2, mqd_t queue2) {
    int pid_size =  sizeof(pid_t);
    int msg_size2 = sizeof(pid_t) + CHAR2*sizeof(char);
    *((pid_t *)msg_q2) = getpid();

    for(int i=1; i<4; i++)
        msg_q2[pid_size+i] = '0';

    msg_q2[msg_size2-1] = '\0';

    printf("Changed message: %d%s\n",*((pid_t*)msg_q2), msg_q2+pid_size);
    if(last_signal == SIGINT) return;
    if(mq_send(queue2,msg_q2,msg_size2,0))
    {
        if(errno == EINTR && last_signal == SIGINT)
            return;
        else
            ERR("mq_send Q2");
    }
}

void wait_for_msg(char* msg_q2, char* msg_prev, mqd_t queue2, int t) {
    timespec_t tm2;
    int pid_size =  sizeof(pid_t);
    int msg_size2 = sizeof(pid_t) + CHAR2*sizeof(char);

    do{
        clock_gettime(CLOCK_REALTIME, &tm2);
        tm2.tv_sec += t;
        printf("Previous message: %d%s\n",*((pid_t*)msg_prev), msg_prev+pid_size);
        if(last_signal == SIGINT) break;
    }while(mq_timedreceive(queue2,msg_q2,msg_size2,NULL, &tm2) < msg_size2 && last_signal != SIGINT);

    msg_q2[msg_size2-1] = '\0';
    tm2.tv_sec = 0;
}

void read_from_queue2(mqd_t queue2, int t, int p) {
    int pid_size =  sizeof(pid_t);
    int msg_size2 = sizeof(pid_t) + CHAR2*sizeof(char);
    char *msg_q2 = malloc(msg_size2);
    char *msg_prev = malloc(msg_size2);
    if(!msg_q2) ERR("malloc");
    if(!msg_prev) ERR("malloc");
    bool first_time = true;
    timespec_t tm1; 

    while(last_signal != SIGINT) {
        if(first_time == true) {
            if(mq_receive(queue2,msg_q2,msg_size2,NULL)<msg_size2) {
                if (errno == EINTR && last_signal == SIGINT) break;
                else  ERR("mq_receive Q2");
            }
            first_time = false;
        }
        else {
            clock_gettime(CLOCK_REALTIME, &tm1);
            tm1.tv_sec += 1;
            if(mq_timedreceive(queue2,msg_q2,msg_size2,NULL, &tm1) < msg_size2) {
                if(errno == ETIMEDOUT) {
                    wait_for_msg(msg_q2, msg_prev, queue2, t);
                    if(last_signal == SIGINT) break;
                }
                else if (errno == EINTR && last_signal == SIGINT) break;
                else ERR("mq_timedreceive Q2");
            }
            tm1.tv_sec = 0;     
        }
        msg_q2[msg_size2-1] = '\0';
        memcpy(msg_prev, msg_q2, msg_size2);
        sleep(t);
        if(last_signal == SIGINT) break;
        printf("Message: %d%s\n",*((pid_t*)msg_q2), msg_q2+pid_size);

        if(rand()%100 < p && last_signal != SIGINT)
            send_msg_q2(msg_q2, queue2);
    }
    free(msg_q2);
    free(msg_prev);
}

void open_queue2(mqd_t* queue2, char* q2) {
    if((*queue2=TEMP_FAILURE_RETRY(mq_open(q2, O_RDWR, 0600, NULL)))==(mqd_t)-1) {
        if(errno == ENOENT) {
            printf("queue Q2 does not exists\n");
            exit(EXIT_FAILURE);
        }
        else
            ERR("mq_open Q2");
    }
}

int main(int argc, char** argv) {

    srand(getpid());
    int t,p;
    read_arguments(argc, argv, &t, &p);
    char* q2 = argv[3];

    mqd_t queue2;
    if(sethandler(sig_handler,SIGINT)) ERR("Seting SIGINT");
    open_queue2(&queue2, q2);
    read_from_queue2(queue2, t, p);
    
    printf("Closed processor\n");
    return EXIT_SUCCESS;
}