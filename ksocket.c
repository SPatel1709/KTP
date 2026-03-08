#include "ksocket.h"


window_t init_window(){
    window_t w;
    w.base=1;
    w.last_acknowledged=0;
    w.next_sequence_number=1;

    for(int wnd=0;wnd<WINDOW_SIZE;++wnd)
    {
        w.received_ack[wnd]=false;
        w.timeout[wnd]=-1;
        w.message_sequence_numbers[i]=i+1;
    }
    return w;
}

ksockfd_t k_socket(int __domain,int __type,int protocol){
    assert(__type==SOCK_KTP && __domain==AF_INET && "Incorrect Sock type for KTP");

    int sockfd;
    int k_sockfd;
    
    ktp_socket_t* SM=k_shmat();

    if(SM==NULL) return -1;
    
    for(int i=0;i<NUM_SOCKETS;++i)
    {
        pthread_mutex_lock(&mutex_socket[i]);
        if(SM[i].is_free)
        {
            sockfd=socket(__domain,SOCK_DGRAM,protocol);
            if(sockfd<0)
            {
                k_sockfd=-1;
            }
            else{
                k_sockfd = i;
                SM[i].sockfd = sockfd;
                SM[i].is_free = false;
                SM[i].is_closed=false;
                SM[i].pid = getpid();
                memset(&SM[i].src_addr, 0, sizeof(SM[i].src_addr));
                memset(&SM[i].dest_addr, 0, sizeof(SM[i].dest_addr));
                for (int j = 0; j < BUFFSIZE; j++)
                {
                    SM[i].send_buffer_empty[j] = true;
                }
                SM[i].swnd = init_window();
                SM[i].rwnd = init_window();
            }
            pthread_mutex_unlock(&mutex_socket[i]);
            k_shmdt((void*)SM);
            return k_sockfd;
        }

        pthread_mutex_unlock(&mutex_socket[i]);
    }
    g_error=ENOSPACE;
    sockfd=-1;

    k_shmdt((void*)SM);
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
    k_shmdt((void*)SM);

    return bind_result;
}


ssize_t k_sendto(int __fd,const void *__buf,size_t __n,int __flags,const struct sockaddr *_dest_addr,socklen_t __addr_len){
    ktp_socket_t* SM=k_shmat();
    pthread_mutex_lock(&mutex_socket[__fd]);
    struct sockaddr_in temp = *((struct sockaddr_in*)_dest_addr);
    
    if(temp.sin_addr.s_addr!=SM[__fd].dest_addr.sin_addr.s_addr || temp.sin_port!=SM[__fd].dest_addr.sin_port){
        g_error=ENOTBOUND;
        pthread_mutex_unlock(&mutex_socket[__fd]);
        return (ssize_t)-1;
    }

    if(1/*Check if the send buffer is full or not IF FULL THEN THIS*/)
    {
        g_error=ENOSPACE;
        pthread_mutex_unlock(&mutex_socket[__fd]);
        return (ssize_t)-1;
    }

    ssize_t size=sendto(__fd,__buf,__n,__flags,_dest_addr,__addr_len);
    return size;
}


/* Need to see this how this works*/
ssize_t k_recvfrom(int __fd,void *__restrict__ __buf,size_t __n,int __flags,struct sockaddr *__restrict__ __addr,socklen_t *__restrict__ __addr_len)
{



    
}


int k_close(ksockfd_t __fd)
{
    /*Clean the shared memory first*/

    ktp_socket_t *SM=k_shmat();
    if(SM==(void*)-1) return -1;

    int ret_val = -1;
    pthread_mutex_lock(&mutex_socket[__fd]);
    
    
    if(!SM[__fd].is_free)
    {
        SM[__fd].is_free=true;
        SM[__fd].is_closed=true;
        ret_val=close(SM[__fd].sockfd);
    }

    pthread_mutex_unlock(&mutex_socket[__fd]);
    k_shmdt((void*)SM);

    return ret_val;
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