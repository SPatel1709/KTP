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
    
    return shmid;
}


void* R_job(){

}

void* S_job(){

}


int main(){

    //initialising threads
    pthread_t R,S;
    pthread_create(R,NULL,&R_job,NULL);
    pthread_create(S,NULL,&S_job,NULL);

    //initialising shared memory
    int SM_id=init_SM(NUM_SOCKETS);

    return 0;
}