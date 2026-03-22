/*Mini Project 1 Submission
Group Details:
Member 1 Name: Shrey Patel
Member 1 Roll number: 23CS10051
Member 2 Name: Arham J Bhansali
Member 2 Roll number: 23CS30007*/


#include "ksocket.h"
error_t g_error;
pthread_mutex_t mutex_socket[NUM_SOCKETS];

window_t init_window(){
    window_t w;
    w.base = 0;
    w.last_ack = 0;
    w.size = WINDOW_SIZE;
    w.used = 0;
    w.nxt_seq_num = WINDOW_SIZE + 1;  // ← next seq after initial 1..10

    for(int wnd = 0; wnd < WINDOW_SIZE; ++wnd)
    {
        w.recv_ack[wnd] = false;
        w.timeout[wnd] = -1;
        w.msg_seq_num[wnd] = wnd + 1;  // 1..10
    }
    return w;
}

k_sockfd_t k_socket(int __domain, int __type, int protocol)
{
    assert(__type == SOCK_KTP && __domain == AF_INET && "Incorrect Sock type for KTP");

    ktp_socket_t *SM = k_shmat();

    if (SM == NULL)
        return -1;

    for (int i = 0; i < NUM_SOCKETS; ++i)
    {
        pthread_mutex_lock(&mutex_socket[i]);
        if (SM[i].is_free)
        {
            SM[i].is_free = false;
            SM[i].is_closed = false;
            SM[i].is_bound = false;
            SM[i].no_space = false;
            SM[i].pid = getpid();
            SM[i].sockfd = -1;
            memset(&SM[i].src_addr, 0, sizeof(SM[i].src_addr));
            memset(&SM[i].dest_addr, 0, sizeof(SM[i].dest_addr));
            // SM[i].fin_timeout=-1;
            for (int j = 0; j < BUFFSIZE; j++)
            {
                SM[i].send_buffer_empty[j] = true;
            }
            SM[i].swnd = init_window();
            SM[i].rwnd = init_window();
            SM[i].swnd.nxt_seq_num=1;
            pthread_mutex_unlock(&mutex_socket[i]);
            k_shmdt((void *)SM);
            return i;
        }

        pthread_mutex_unlock(&mutex_socket[i]);
    }
    g_error = ENOSPACE;
    k_shmdt((void *)SM);

    return -1;
}

int k_bind(k_sockfd_t __fd,char* __src_ip, int __src_port, char* __dest_ip, int __dest_port){
    ktp_socket_t* SM=k_shmat();
    pthread_mutex_lock(&mutex_socket[__fd]);
    SM[__fd].src_addr.sin_family=AF_INET;
    SM[__fd].src_addr.sin_port=htons(__src_port);
    SM[__fd].src_addr.sin_addr.s_addr=inet_addr(__src_ip);

    SM[__fd].dest_addr.sin_family=AF_INET;
    SM[__fd].dest_addr.sin_port=htons(__dest_port);
    SM[__fd].dest_addr.sin_addr.s_addr=inet_addr(__dest_ip);

    pthread_mutex_unlock(&mutex_socket[__fd]);
    k_shmdt((void*)SM);

    printf("Socket bound with ksockfd: %d src_port: %d dest_port: %d\n", __fd, __src_port,__dest_port);

    return 0;
}


ssize_t k_sendto(int __fd,const void *__buf,size_t __n,const struct sockaddr *_dest_addr,socklen_t __addr_len){
    ktp_socket_t* SM=k_shmat();

    if(SM==NULL) return -1;
    pthread_mutex_lock(&mutex_socket[__fd]);
    struct sockaddr_in temp = *((struct sockaddr_in*)_dest_addr);
    
    if(temp.sin_addr.s_addr!=SM[__fd].dest_addr.sin_addr.s_addr || temp.sin_port!=SM[__fd].dest_addr.sin_port){
        g_error=ENOTBOUND;
        pthread_mutex_unlock(&mutex_socket[__fd]);
        k_shmdt((void*)SM);
        return (ssize_t)-1;
    }

    if (SM[__fd].swnd.used == BUFFSIZE)
    {
        g_error = ENOSPACE;
        pthread_mutex_unlock(&mutex_socket[__fd]);
        k_shmdt((void*)SM);
        return -1;
    }

    /*now here check if there is any space left in the send buffer*/
    for (int j = SM[__fd].swnd.base, ctr = 0; ctr < BUFFSIZE; j = (j + 1) % BUFFSIZE, ctr++){
        if (SM[__fd].send_buffer_empty[j])
        {
            int cpy = (__n < MSG_SIZE) ? __n : MSG_SIZE;
            memset(SM[__fd].send_buffer[j], 0, MSG_SIZE);
            memcpy(SM[__fd].send_buffer[j], __buf, cpy);

            SM[__fd].send_buffer_empty[j] = false;
            SM[__fd].swnd.timeout[j] = -1;   // pending, not sent yet
            // SM[__fd].swnd.used++;

            pthread_mutex_unlock(&mutex_socket[__fd]);
            k_shmdt((void*)SM);
            return cpy;
        }
    }
    g_error=ENOSPACE;
    pthread_mutex_unlock(&mutex_socket[__fd]);
    k_shmdt((void*)SM);
    return (ssize_t)-1;
}


/*Different from above as if it was not obvious ;)*/
ssize_t k_recvfrom(int __fd, void *__restrict__ __buf, size_t __n,struct sockaddr *__restrict__ __addr,socklen_t *__restrict__ __addr_len)
{
    ktp_socket_t* SM = k_shmat();
    if (SM == NULL) return -1;

    pthread_mutex_lock(&mutex_socket[__fd]);

    int slot = SM[__fd].rwnd.base;

    if(!SM[__fd].rwnd.recv_ack[slot])
    {
        g_error = ENOMESSAGE;
        pthread_mutex_unlock(&mutex_socket[__fd]);
        k_shmdt((void*)SM);
        return -1;
    }

    int cpy = (__n < MSG_SIZE) ? __n : MSG_SIZE;
    memcpy(__buf, SM[__fd].recv_buffer[slot], cpy);

    SM[__fd].rwnd.recv_ack[slot] = false;
    memset(SM[__fd].recv_buffer[slot], 0, MSG_SIZE);

    int old_base = SM[__fd].rwnd.base;
    SM[__fd].rwnd.msg_seq_num[old_base] = SM[__fd].rwnd.nxt_seq_num;
    SM[__fd].rwnd.nxt_seq_num = SM[__fd].rwnd.nxt_seq_num % MAX_SEQ + 1;

    SM[__fd].rwnd.base = (old_base + 1) % WINDOW_SIZE;
    SM[__fd].rwnd.size++;   // one more free slot

    if(__addr != NULL && __addr_len != NULL)
    {
        memcpy(__addr, &SM[__fd].dest_addr, sizeof(struct sockaddr_in));
        *__addr_len = sizeof(struct sockaddr_in);
    }

    pthread_mutex_unlock(&mutex_socket[__fd]);
    k_shmdt((void*)SM);
    return cpy;
}


int k_close(k_sockfd_t __fd)
{
    /*Clean the shared memory first*/

    ktp_socket_t *SM=k_shmat();
    if(SM==NULL) return -1;

    pthread_mutex_lock(&mutex_socket[__fd]);
    if(!SM[__fd].is_free)
    {
        SM[__fd].is_closed=true;
    }
    pthread_mutex_unlock(&mutex_socket[__fd]);
    k_shmdt((void*)SM);

    return 0;
}



int k_shmget(){
    key_t token=ftok(FTOK_FILE,'M');
    return shmget(token,0,0666);
}

ktp_socket_t* k_shmat(){
    int shmid=k_shmget();

    if(shmid==-1)
    {
        return NULL;
    }

    ktp_socket_t *SM=(ktp_socket_t* )shmat(shmid,NULL,0);

    if(SM==NULL) return NULL;

    return SM;
}

int k_shmdt(const void* SM)
{
    return shmdt(SM);
}

bool drop_message(double P)
{
    // srand(time(NULL));
    double r=(float)rand()/(float)RAND_MAX;
    // printf("Random number generated: %f\n", r);
    // printf("%d\n",(int)(r<P));
    return r<P;
}
