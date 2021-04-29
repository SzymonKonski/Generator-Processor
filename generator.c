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

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
            perror(source),kill(0,SIGKILL),\
            exit(EXIT_FAILURE))

#define CHAR1 5
#define CHAR2 11

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
	fprintf(stderr, "USAGE: %s\n", pname);
    fprintf(stderr, "1 <= t <= 10\n");
    fprintf(stderr, "0 <= p <= 100\n");
    fprintf(stderr, "queue1 name\n");
    fprintf(stderr, "queue2 name\n");
    fprintf(stderr, "1 <= n <= 10\n");
	exit(EXIT_FAILURE);
}

void read_arguments(int argc, char** argv, int* t, int* p, int *n) {
    if(argc != 6 && argc != 5) usage(argv[0]);
    *t = atoi(argv[1]);
    *p = atoi(argv[2]);
    if(*t < 1 || *t > 10) usage(argv[0]);
    if(*p < 0 || *p > 100) usage(argv[0]);

    if(argc == 6) {
        *n = atoi(argv[5]);
        if(*n < 0 || *n > 10) usage(argv[0]);
    }
}

void write_to_queue1(int n, mqd_t queue1) {
    int pid_size =  sizeof(pid_t);
    int msg_size1 = sizeof(pid_t) + CHAR1*sizeof(char);
    char* msg = malloc(msg_size1);
    if(!msg) ERR("malloc");

    *((pid_t *)msg) = getpid();
    msg[pid_size] = '/';
    msg[msg_size1-1] = '\0';
    
    for(int i = 0; i<n; i++) {

        for(int j=1; j<4; j++)
            msg[pid_size + j] = rand()%('z' - 'a' + 1) + 'a';

        if(last_signal == SIGINT) break;        
        if(mq_send(queue1, msg, msg_size1, 1)) {
            if(errno == EINTR && last_signal == SIGINT)
                break;
            else
                ERR("mq_send Q1");
        }
        printf("Message in Q1 %d%s\n", *((pid_t*)msg), msg+pid_size);
    }
    free(msg);
}

void send_msg_q2(mqd_t queue2, char* msg_q1, char* msg_q2) {
    int pid_size =  sizeof(pid_t);
    int msg_size2 = sizeof(pid_t) + CHAR2*sizeof(char);
    *((pid_t *)msg_q2) = getpid();
    msg_q2[pid_size] = '/';
    msg_q2[pid_size+4] = '/';
    msg_q2[msg_size2-1] = '\0';

    for(int i=1; i<4; i++)
        msg_q2[pid_size + i] = msg_q1[pid_size + i];

    for(int i=1; i< 6; i++)
        msg_q2[pid_size + 4 + i] = rand()%('z' - 'a' + 1) + 'a';
    
    printf("Changed message: %d%s\n", *((pid_t*)msg_q2), msg_q2+sizeof(pid_t));

    if(last_signal == SIGINT) return;
    if(mq_send(queue2,msg_q2,msg_size2,1)) {  

        if(errno == EINTR && last_signal == SIGINT) 
            return;
        else
            ERR("mq_send Q2");
    }
}

void read_from_queue1(int t, int p, mqd_t queue1, mqd_t queue2) {
    int msg_size1 = sizeof(pid_t) + CHAR1*sizeof(char);
    int msg_size2 = sizeof(pid_t) + CHAR2*sizeof(char);
    char* msg_q1 = malloc(msg_size1);
    char* msg_q2 = malloc(msg_size2);
    if(!msg_q1) ERR("malloc");
    if(!msg_q2) ERR("malloc");

    while(last_signal != SIGINT) {
        if(mq_receive(queue1,msg_q1,msg_size1,NULL) < msg_size1) {
            if(errno == EINTR && last_signal == SIGINT)
                break;
            else 
                ERR("mq_receive Q1");
        }
        msg_q1[msg_size1-1] = '\0';
        sleep(t);
        if(last_signal == SIGINT) break;
        printf("Read message: %d%s\n", *((pid_t*)msg_q1), msg_q1+sizeof(pid_t));

        if(rand()%100 < p && last_signal != SIGINT)
            send_msg_q2(queue2, msg_q1, msg_q2);

        if(last_signal == SIGINT) break;
        if(mq_send(queue1,msg_q1,msg_size1,0))
        {
            if(errno == EINTR && last_signal == SIGINT)
                break;
            else
                ERR("mq_send Q1");
        }
    }

    free(msg_q2);
    free(msg_q1);
}

void create_queues(mqd_t* queue1, mqd_t* queue2, char* q1, char* q2) {
        struct mq_attr attr1, attr2;
        int msg_size1 = sizeof(pid_t) + CHAR1*sizeof(char);
        int msg_size2 = sizeof(pid_t) + CHAR2*sizeof(char);
        attr1.mq_maxmsg = 10;
        attr1.mq_msgsize = msg_size1;
        attr2.mq_maxmsg = 10;
        attr2.mq_msgsize = msg_size2;
        
        if((*queue1=TEMP_FAILURE_RETRY(mq_open(q1, O_RDWR | O_CREAT, 0600, &attr1)))==(mqd_t)-1) ERR("mq_open Q1");
        if((*queue2=TEMP_FAILURE_RETRY(mq_open(q2, O_WRONLY | O_CREAT, 0600, &attr2)))==(mqd_t)-1) ERR("mq_open Q2");
}

void open_queues(mqd_t* queue1, mqd_t* queue2, char* q1, char* q2) {

    if((*queue1=TEMP_FAILURE_RETRY(mq_open(q1, O_RDWR, 0600, NULL)))==(mqd_t)-1) {
        if(errno == ENOENT) {
            printf("queue Q1 does not exists\n");
            exit(EXIT_FAILURE);
        }
        else
            ERR("mq_open Q1");
    }

    if((*queue2=TEMP_FAILURE_RETRY(mq_open(q2, O_WRONLY, 0600, NULL)))==(mqd_t)-1) {
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
    int t,p,n=0;
    mqd_t queue1, queue2;
    read_arguments(argc, argv, &t, &p, &n);
    char* q1 = argv[3];
    char* q2 = argv[4];
    if(sethandler(sig_handler,SIGINT)) ERR("Seting SIGINT");
    
    if(n <= 0) 
        open_queues(&queue1, &queue2, q1, q2);
       
    else {
        create_queues(&queue1, &queue2, q1, q2);
        write_to_queue1(n, queue1);
    }

    read_from_queue1(t, p, queue1, queue2);
    if(TEMP_FAILURE_RETRY(mq_close(queue1))<0) ERR("mq_close Q1");
    if(TEMP_FAILURE_RETRY(mq_close(queue2))<0) ERR("mq_close Q2");

    if(n > 0){
        if(TEMP_FAILURE_RETRY(mq_unlink(q1))) 
            if(errno != ENOENT) ERR("mq unlink");
        
        if(TEMP_FAILURE_RETRY(mq_unlink(q2))) 
            if(errno != ENOENT) ERR("mq unlink");
    }
    
    printf("Closed generator\n");
    return EXIT_SUCCESS;
}