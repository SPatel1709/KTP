#include "ksocket.h"


int k_socket(int __domain,int __type,int protocol){
    assert(__type==SOCK_KTP && __domain==AF_INET && "Incorrect Sock type for KTP");

    int sockfd;


    ktp_socket_t* SM=k_shmat();

    if(SM==NULL) return -1;
    
    for(int i=0;i<NUM_SOCKETS;++i)
    {
        
    }

    if(     1     /*Checks if the SM is empty or not*/)
    {   
        /*Initialising the UDP socket from here*/
        sockfd=socket(__domain,SOCK_DGRAM,protocol);
    }
    else{
        g_error=ENOSPACE;
        sockfd=-1;
    }
    return sockfd;
}



int k_bind(int __fd,char* __src_ip, int __src_port, char* __dest_ip, int __dest_port){

    ktp_socket_t* SM=k_shmat();
    pthread_mutex_lock(&mutex_socket[__fd]);
    SM[__fd].src_addr.sin_family=AF_INET;
    SM[__fd].src_addr.sin_port=htons(__src_port);
    SM[__fd].src_addr.sin_addr.s_addr=inet_addr(__src_ip);

    SM[__fd].dest_addr.sin_family=AF_INET;
    SM[__fd].dest_addr.sin_port=htons(__dest_port);
    SM[__fd].dest_addr.sin_addr.s_addr=inet_addr(__dest_ip);

    pthread_mutex_unlock(&mutex_socket[__fd]);
    int bind_result=bind(__fd,(struct sockaddr*)&SM[__fd].src_addr, sizeof(SM[__fd].src_addr));

    return bind_result;
}


ssize_t k_sendto(int __fd,const void *__buf,size_t __n,int __flags,const struct sockaddr *__addr,socklen_t __addr_len){
    
    if(1/*Check if the dest IP/port matches or not, IT SHOULD NOT MATCH*/){
        g_error=ENOTBOUND;
        return (ssize_t)-1;
    }

    if(1/*Check if the send buffer is full or not IF FULL THEN THIS*/)
    {
        g_error=ENOSPACE;
        return (ssize_t)-1;
    }

    ssize_t size=sendto(__fd,__buf,__n,__flags,__addr,__addr_len);
    return size;
}


/* Need to see this how this works*/
ssize_t k_recvfrom(int __fd,void *__restrict__ __buf,size_t __n,int __flags,struct sockaddr *__restrict__ __addr,socklen_t *__restrict__ __addr_len)
{



    
}


int k_close(__fd)
{
    /*Clean the shared memory first*/

    close(__fd);
}



int k_shmget(){
    key_t token=ftok(FTOK_FILE,'M');
    return shmget(token,0,0);
}

ktp_socket_t* k_shmat(){
    int shmid=k_shmget();

    if(shmid==-1)
    {
        return NULL;
    }

    ktp_socket_t *SM=(ktp_socket_t* )shmat(shmid,NULL,0);

    if(SM==(void*)-1) return NULL;

    return SM;
}

int k_shmdt(const void* SM)
{
    return shmdt(SM);
}