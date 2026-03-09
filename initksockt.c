
#include "ksocket.h"

fd_set master;
#define TIMEOUT 100000

void log_error(char* msg)
{
    fprintf(stderr,"%s\n",msg);
    exit(EXIT_FAILURE);
}


/* Memory Managers here*/
void init_SM(int num_sockets)
{
    if (num_sockets <= 0)    log_error("Socket number invalid");
    key_t token = ftok(FTOK_FILE, 'M');


    int shmid = shmget(token, num_sockets * sizeof(ktp_socket_t), IPC_EXCL);
    if (shmid < 0)          log_error("initksocket: Failed shmget");


    ktp_socket_t *SM = (ktp_socket_t *)shmat(shmid, NULL, 0);
    if (SM == (void *)-1)   log_error("initksocket: Failed shmat");


    for (int i = 0; i < num_sockets; ++i)
    {
        SM[i].is_free = true;
    }

    printf("%d Sockets initialised\n", num_sockets);
    shmdt((void *)SM);
}

void cleanup(int signo){
    int shmid = k_shmget();

    if (shmid != -1){
        shmctl(shmid, IPC_RMID, 0);
        printf("SHM %d removed\n", shmid);
    }

    if (signo == SIGSEGV){
        log_error("Segmentation fault");
    }
    exit(EXIT_SUCCESS);
}


/* Data manipulations*/

void get_message();

ssize_t send_data();

ssize_t send_ack();






/* Thread logic here */

void* thread_Garbage(){
    ktp_socket_t *SM = k_shmat();

    while (1){
        sleep(T);
        for (int i = 0; i < NUM_SOCKETS; i++){
            pthread_mutex_lock(&mutex_socket[i]);
            if (!SM[i].is_free && !SM[i].is_closed){
                if (kill(SM[i].pid, 0) == -1){
                    printf("[THREAD G]: Process %d terminated\n", SM[i].pid);
                    SM[i].is_closed = true;
                }
            }
            pthread_mutex_unlock(&mutex_socket[i]);
        }
    }
}


/*
    socket and bind
*/


int socket_bind(ktp_socket_t* slot,fd_set *master,int *maxfd)
{
    int k_sockfd=socket(AF_INET,SOCK_DGRAM,0);

    if(k_sockfd<0)
    {
        fprintf(stderr,"(ERROR) [socket_bind]: Socket\n");
        return -1;
    }

    if(bind(k_sockfd,(struct sockaddr *)&slot->src_addr,sizeof(slot->src_addr))<0)
    {
        fprintf(stderr,"(ERROR) [socket_bind]: bind\n");
        close(k_sockfd);
        return -1;
    }

    slot->sockfd=k_sockfd;
    FD_SET(slot->sockfd,master);
    if(slot->sockfd > *maxfd)
    {
        *maxfd=slot->sockfd;
    }

    slot->is_bound=true;

    return 0;
}


void handle_buffer(ktp_socket_t* slot,k_sockfd_t recv_socket,ssize_t recv_bytes,char* buffer,struct sockaddr_in send_addr){
    if(recv_bytes<0)
    {
        fprintf(stderr,"(ERROR) [handle_buffer]: recv_bytes\n");
        return;
    }

    /* If the connection gets closed */
    if(recv_bytes==0)
    {
        phtread_mutex_lock(&mutex_socket[recv_socket]);
        if(!(slot->is_free) && slot->is_bound)
        {
            printf("[THREAD R]: Connection Closed by other end\n");
            slot->is_closed=true;
        }
        pthread_mutex_unlock(&mutex_socket[recv_socket]);
        return;
    }

    /* If the message is actual message */
    pthread_mutex_lock(&mutex_socket[recv_socket]);
    
    if(!slot->is_free && slot->is_bound)
    {
        if(drop_message(p))
        {
            printf("[DROPPED]: ksocket %d, Type: %s, Seq: %d\n",recv_socket,);
            pthread_mutex_unlock(&mutex_socket[recv_socket]);
            return;
        }

        if(strcmp(type,"DATA")==0)
        {
            printf("[THREAD R]: Data Received Ksocket: %d Seq: %d\n",recv_socket,seq);

            slot->no_space=false;

            bool is_duplicate=true;

            for(int i=slot->rwnd.base,cnt=0;cnt<slot->rwnd.size;i=(i+1)%WINDOW_SIZE,++cnt)
            {
                if(slot->rwnd.msg_seq_num[i]==seq)
                {
                    if(!slot->rwnd.recv_ack[i])
                    {
                        slot->rwnd.recv_ack[i]=true;
                        memcpy(slot->recv_buffer[i],msg,MSG_SIZE);
                        
                        is_duplicate=false;

                        int last_ack=-1;
                        for (int ct = 0; ct < slot->rwnd.size; ct++) {
                            int k = (slot->rwnd.base + ct)%WINDOW_SIZE;

                            if (!slot->rwnd.recv_ack[k])
                                break;

                            last_ack = k;
                        }

                        if(last_ack==-1)
                        {
                            slot->rwnd.last_acknowledged
                        }
                }
            }

        }


    }
    







}


void* thread_R(){

     int max_fd=0;
     fd_set read_set;
     struct timeval timeout;

     FD_ZERO(&master);

     ktp_socket_t* SM=k_shmat();
      
     char buffer[PKT_SIZE];

     while(1)
     {
        read_set=master;
        timeout.tv_sec=0;
        timeout.tv_usec=TIMEOUT;

        select(max_fd+1,&read_set,NULL,NULL,&timeout);


        int recv_socket=-1;
        int recv_bytes=-1;
        struct sockaddr_in send_addr;
        socklen_t addr_len=sizeof(send_addr);

        for(int i=0;i<NUM_SOCKETS;++i)
        {
            pthread_mutex_lock(&mutex_socket[i]);

            if(!SM[i].is_free && SM[i].is_bound && FD_ISSET(SM[i].sockfd,&read_set))
            {
                recv_socket=SM[i].sockfd;
                recv_bytes=recvfrom(SM[i].sockfd,buffer,PKT_SIZE,0,(struct sockaddr *)&send_addr,&addr_len);
            }

            pthread_mutex_unlock(&mutex_socket[i]);

            if(recv_socket!=-1)
            {
                handle_buffer(&SM[i],recv_socket,recv_bytes,buffer,send_addr);
                break;
            }
        }

        // No socket was bounded if it was not free
        if(recv_socket==-1)
        {
            for(int i=0;i<NUM_SOCKETS;++i)
            {
                pthread_mutex_lock(&mutex_socket[i]);

                if(!SM[i].is_free)
                {
                    /* Checking if the socket is not free and is not bounded*/
                    if(!SM[i].is_bound)
                    {
                        socket_bind(&SM[i],&master,&max_fd);
                        printf("[THREAD R]: Bound KTP socket %d to UDP Socket %d\n",i,SM[i].sockfd);
                    }


                    else if(SM[i].no_space && SM[i].rwnd.size>0)
                    {
                        int ack_bytes=send_ack();

                        if(ack_bytes<0)
                        {
                            fprintf(stderr,"(ERROR) [Thread R]: Send_ack\n");
                        }
                    }
                }
                pthread_mutex_unlock(&mutex_socket[i]);
            }
        }
     }
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