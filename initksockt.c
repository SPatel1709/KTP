#include <stdio.h>
#include <pthread.h>
#include <sys/shm.h>
#include "ksocket.h"




// garbage collector

int init_SM(int num_sockets)
{
    if(num_sockets<=0)
    {
        fprintf(stderr,"Socket number invalid\n");
    }

    key_t token=ftok(FTOK_FILE,'M');
    
    int shmid=shmget(token,num_sockets*sizeof(ktp_socket_t),IPC_EXCL);

    if(shmid<0)
    {
        fprintf(stderr,"initksocket: Failed shmget\n");
        exit(1);
    }

    ktp_socket_t *SM=(ktp_socket_t*)shmat(shmid,NULL,0);

    if(SM==(void*)-1)
    {
        fprintf(stderr,"initksocket: Failed shmat\n");
    }

    for(int i=0;i<num_sockets;++i)
    {
        SM[i].is_free=true;
    }

    printf("%d Sockets initialised\n",num_sockets);
    shmdt((void*)SM);
}


void cleanup(int signo){
    int shmid = k_shmget();

    if (shmid != -1){
        shmctl(shmid, IPC_RMID, 0);
        printf("SHM %d removed\n", shmid);
    }

    if (signo == SIGSEGV){
        printf("Segmentation fault\n");
    }
    exit(0);
}

void* thread_Garbage(){
    ktp_socket_t *SM = k_shmat();

    while (1){
        sleep(T);
        for (int i = 0; i < NUM_SOCKETS; i++){
            pthread_mutex_lock(&mutex_socket[i]);
            if (!SM[i].is_free && !SM[i].is_closed){
                if (kill(SM[i].pid, 0) == -1){
                    printf("G: Process %d terminated\n", SM[i].pid);
                    SM[i].is_closed = true;
                }
            }
            pthread_mutex_unlock(&mutex_socket[i]);
        }
    }
}


void* thread_R(){

}

void* thread_S(){

}


int main(){

    srand(time(NULL));

    
    //initialising shared memory
    init_SM(NUM_SOCKETS);
    
    signal(SIGINT,cleanup);
    signal(SIGSEGV,cleanup);

    
    //initialising threads
    pthread_t R,S,Garb;
    pthread_create(&R,NULL,&thread_R,NULL);
    pthread_create(&S,NULL,&thread_S,NULL);
    pthread_create(&Garb,NULL,&thread_Garbage,NULL);


    pthread_exit(NULL);    
    return 0;
}