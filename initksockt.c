#include "ksocket.h"

fd_set master;
#define TIMEOUT 100000

void log_error(char* msg)
{
    fprintf(stderr,"%s\n",msg);
    exit(EXIT_FAILURE);
}

/* Memory Managers here*/
void init_Memory(int num_sockets)
{
    if (num_sockets <= 0)    log_error("Socket number invalid");
    key_t token = ftok(FTOK_FILE, 'M');


    for(int i=0;i<num_sockets;++i)
    {
        if(pthread_mutex_init(&mutex_socket[i],NULL)!=0){
            log_error("init_memory: Failed mutex_init");
        }
    }
    
    
    int shmid = shmget(token, num_sockets * sizeof(ktp_socket_t), IPC_CREAT|0666);
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

    for(int i=0;i<NUM_SOCKETS;++i)
    {
        if(pthread_mutex_destroy(&mutex_socket[i])!=0)
        {
            fprintf(stderr,"Failed to destroy mutex\n");
        }
    }

    if (signo == SIGSEGV){
        log_error("Segmentation fault");
    }

    exit(EXIT_SUCCESS);
}

// void close_socket(k_sockfd_t sockfd,ktp_socket_t* slot){
    
//     slot->is_free=true;
//     FD_CLR(slot->sockfd,&master);
//     close(slot->sockfd);
//     printf("Closed KTP socket %d\n",sockfd);
// }


void close_socket(k_sockfd_t sockfd, ktp_socket_t* slot)
{
    if (slot->is_bound && slot->sockfd >= 0) {
        FD_CLR(slot->sockfd, &master);
        close(slot->sockfd);
    }

    slot->is_free = true;
    slot->is_closed = false;
    slot->is_bound = false;
    slot->no_space = false;
    slot->pid = 0;
    slot->sockfd = -1;

    memset(&slot->src_addr, 0, sizeof(slot->src_addr));
    memset(&slot->dest_addr, 0, sizeof(slot->dest_addr));

    for (int i = 0; i < BUFFSIZE; ++i) {
        slot->send_buffer_empty[i] = true;
        memset(slot->send_buffer[i], 0, sizeof(slot->send_buffer[i]));
        memset(slot->recv_buffer[i], 0, sizeof(slot->recv_buffer[i]));
    }

    slot->swnd=init_window();
    slot->rwnd=init_window();
    slot->fin_timeout = -1;

    printf("Closed KTP socket %d\n", sockfd);
}

/* timeout checker*/
bool check_timeout(ktp_socket_t* slot)
{
    return (slot->swnd.timeout[slot->swnd.base] != -1 && time(NULL) >= slot->swnd.timeout[slot->swnd.base]);
}

/* send msg utils*/
char* get_msg_type(packet_type_t msg_type)
{
    if(msg_type==DATA)
    return "DATA";

    else if(msg_type==SYN)
    return "SYN";

    else if(msg_type==ACK)
    return "ACK";

    else if(msg_type==FIN)
    return "FIN";

    else if(msg_type==FIN_ACK)
    return "FACK";

    else
     return NULL;
    
}

ssize_t send_pkt(int sockfd,struct sockaddr_in* dest_addr,packet_type_t msg_type,uint16_t seq,uint8_t rwnd,char* msg){
    char buffer[PKT_SIZE];

    memset(buffer, 0, sizeof(buffer));
    // char type[MSG_TYPE]=get_msg_type(msg_type);

    uint8_t k_rwnd=rwnd;
    uint16_t k_seq=htons(seq);

    memcpy(buffer,get_msg_type(msg_type),MSG_TYPE);
    memcpy(buffer+MSG_TYPE,&k_seq,sizeof(uint16_t));
    memcpy(buffer+MSG_TYPE+sizeof(uint16_t),&k_rwnd,sizeof(uint8_t));
    
    if(msg!=NULL) // we can memcpy from a null msg
    memcpy(buffer+MSG_TYPE+sizeof(uint16_t)+sizeof(uint8_t),msg,MSG_SIZE);

    return sendto(sockfd,buffer,PKT_SIZE,0,(struct sockaddr*)dest_addr,sizeof(struct sockaddr_in));
}



/* Receive msg utils*/

void get_message(char buf[], char *type, uint16_t *seq, uint8_t *rwnd, char *msg)
{
    memcpy(type, buf, MSG_TYPE);
    type[MSG_TYPE]='\0'; // Null-terminate the type string
    
    uint16_t temp;
    memcpy(&temp, buf + MSG_TYPE, sizeof(uint16_t));
    *seq = ntohs(temp);
    
    //rwnd is just one byte as window is just of 10 size
    *rwnd = buf[MSG_TYPE + sizeof(uint16_t)];
    memcpy(msg,buf+HEADER_SIZE,MSG_SIZE);
}

void handle_ack(ktp_socket_t *slot, uint16_t seq, uint16_t rwnd)
{
    int acked = 0;

    for (int counter = 0; counter < slot->swnd.used; ++counter) {
        int i = (slot->swnd.base + counter) % WINDOW_SIZE;

        if (slot->swnd.msg_seq_num[i] == seq) {
            acked = (i - slot->swnd.base + WINDOW_SIZE) % WINDOW_SIZE + 1;
            break;
        }
    }

    if (acked > 0) {
        for (int ct = 0, k = slot->swnd.base; ct < acked; ++ct, k = (k + 1) % WINDOW_SIZE) {
            slot->swnd.timeout[k] = -1;
            slot->send_buffer_empty[k] = true;
            memset(slot->send_buffer[k], 0, MSG_SIZE);

            slot->swnd.msg_seq_num[k] = slot->swnd.nxt_seq_num % MAX_SEQ + 1;
            slot->swnd.nxt_seq_num = slot->swnd.msg_seq_num[k];
        }

        slot->swnd.base = (slot->swnd.base + acked) % WINDOW_SIZE;
        slot->swnd.used -= acked;
    }

    // duplicate ACK case: just update advertised remote rwnd
    slot->swnd.size = rwnd;
}

void handle_data(ktp_socket_t *slot, int slot_idx, uint16_t seq, char *msg)
{
    int found_idx = -1;
    bool duplicate = false;

    for (int cnt = 0; cnt < WINDOW_SIZE; ++cnt) {
        int i = (slot->rwnd.base + cnt) % WINDOW_SIZE;
        if (slot->rwnd.msg_seq_num[i] == seq) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        // Outside current rwnd -> ignore
        return;
    }

    if (slot->rwnd.recv_ack[found_idx]) {
        // Duplicate packet already buffered
        duplicate = true;
    } else {
        slot->rwnd.recv_ack[found_idx] = true;
        memcpy(slot->recv_buffer[found_idx], msg, MSG_SIZE);
        slot->rwnd.size--;   // free space decreases

        if (slot->rwnd.size == 0) {
            slot->no_space = true;
        }
    }

    // ACK only if this packet extends contiguous in-order prefix from rwnd.base
    int last_contig = -1;
    for (int cnt = 0; cnt < WINDOW_SIZE; ++cnt) {
        int i = (slot->rwnd.base + cnt) % WINDOW_SIZE;
        if (!slot->rwnd.recv_ack[i]) break;
        last_contig = i;
    }

    if (last_contig == -1) {
        // No new in-order progress, so no ACK for out-of-order packet
        if (duplicate) {
            // optional: resend current ACK for a duplicate old packet
            send_pkt(slot->sockfd, &(slot->dest_addr), ACK,
                     slot->rwnd.last_ack, slot->rwnd.size, NULL);
        }
        return;
    }

    uint16_t new_last_ack = slot->rwnd.msg_seq_num[last_contig];
    if (new_last_ack != slot->rwnd.last_ack || duplicate) {
        slot->rwnd.last_ack = new_last_ack;
        printf("[THREAD R] (SENT): <ACK %d, RWND %d> ksocket %d\n",
               slot->rwnd.last_ack, slot->rwnd.size, slot_idx);

        if (send_pkt(slot->sockfd, &(slot->dest_addr), ACK,
                     slot->rwnd.last_ack, slot->rwnd.size, NULL) < 0) {
            fprintf(stderr, "(ERROR) [handle_data]: send_ack\n");
        }
    }
}

void handle_buffer(ktp_socket_t* slot,k_sockfd_t slot_idx,ssize_t recv_bytes,char buffer[],struct sockaddr_in send_addr){
    if(recv_bytes<0)
    {
        fprintf(stderr,"(ERROR) [handle_buffer]: recv_bytes\n");
        return;
    }

    /* If the connection gets closed */
    // if(recv_bytes==0)
    // {
    //     if(!(slot->is_free) && slot->is_bound)
    //     {
    //         printf("[THREAD R]: Connection Closed by other end\n");
    //         slot->is_closed=true;
    //     }
    //     return;
    // }

    
    if(!slot->is_free && slot->is_bound)
    {

        if (send_addr.sin_addr.s_addr != slot->dest_addr.sin_addr.s_addr || send_addr.sin_port != slot->dest_addr.sin_port) {
            return;
        }

        char type[MSG_TYPE+1],msg[MSG_SIZE];
        u_int16_t seq;
        u_int8_t rwnd;
        get_message(buffer,type,&seq,&rwnd,msg);


        // if(drop_message(p))
        // {
        //     printf("[THEAD R] (DROPPED): ksocket %d, Type: %s, Seq: %d\n", slot_idx, type, seq);
        //     return;
        // }

        // if(strcmp(type,"DATA")==0)      handle_data(slot, slot_idx, seq, msg);
        
        // else if(strcmp(type,"ACK")==0)  handle_ack(slot, seq, rwnd);

        // else if (strcmp(type, "FIN") == 0)
        // {
        //     printf("[THREAD R] (SENT FIN): ksocket %d\n", slot_idx);

        //     // need to send FIN_ACK for FIN not FIN puttar thoda dhyan rakha karo.
        //     if(send_pkt(slot->sockfd,&(slot->dest_addr),FIN_ACK,slot->rwnd.last_ack,slot->rwnd.size,NULL/* No data needed */) < 0)
        //         fprintf(stderr, "(ERROR) [handle_buffer]: send_fin_ack\n");
        //     close_socket(slot_idx,slot);
        // }

        // else if (strcmp(type, "FACK") == 0)
        // {
        //     printf("[THREAD R] (FACK RECV): ksocket %d\n", slot_idx);
        //     close_socket(slot_idx,slot);
        // }

        // else
        // {
        //     fprintf(stderr, "(ERROR) [handle_buffer] : INVALID MSG TYPE\n");
        // }

        if (strcmp(type, "DATA") == 0)
        {
            if (drop_message(p))
            {
                printf("[THEAD R] (DROPPED): ksocket %d, Type: %s, Seq: %d\n", slot_idx, type, seq);
                return;
            }
            handle_data(slot, slot_idx, seq, msg);
        }
        else if (strcmp(type, "ACK") == 0)
        {
            if (drop_message(p))
            {
                printf("[THEAD R] (DROPPED): ksocket %d, Type: %s, Seq: %d\n", slot_idx, type, seq);
                return;
            }
            handle_ack(slot, seq, rwnd);
        }
        else if (strcmp(type, "FIN") == 0)
        {
            printf("[THREAD R] (SENT FIN): ksocket %d\n", slot_idx);

            if (send_pkt(slot->sockfd, &(slot->dest_addr), FIN_ACK,
                         slot->rwnd.last_ack, slot->rwnd.size, NULL) < 0)
                fprintf(stderr, "(ERROR) [handle_buffer]: send_fin_ack\n");

            close_socket(slot_idx, slot);
        }
        else if (strcmp(type, "FACK") == 0)
        {
            printf("[THREAD R] (FACK RECV): ksocket %d\n", slot_idx);
            close_socket(slot_idx, slot);
        }
        else
        {
            fprintf(stderr, "(ERROR) [handle_buffer] : INVALID MSG TYPE\n");
        }
    }
}


/* socket and bind */
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



void* thread_R(void* args){

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
        ssize_t recv_bytes=-1;
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

            if(recv_socket!=-1)
            {
                handle_buffer(&SM[i],i,recv_bytes,buffer,send_addr);
                pthread_mutex_unlock(&mutex_socket[i]);
                break;
            }
            pthread_mutex_unlock(&mutex_socket[i]);
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
                        if (socket_bind(&SM[i], &master, &max_fd) == 0) {
                            printf("[THREAD R]: Bound KTP socket %d to UDP Socket %d\n", i, SM[i].sockfd);
                        }
                    }


                    else if(SM[i].no_space && SM[i].rwnd.size>0)
                    {
                        int ack_bytes=send_pkt(SM[i].sockfd,&SM[i].dest_addr,ACK,SM[i].rwnd.last_ack,SM[i].rwnd.size,NULL);

                        if(ack_bytes<0)
                        {
                            fprintf(stderr,"(ERROR) [Thread R]: Send_ack\n");
                        }
                        else{
                            SM[i].no_space=false;
                        }
                    }
                }
                pthread_mutex_unlock(&mutex_socket[i]);
            }
        }
     }
}

// void* thread_S(void* args){
//     ktp_socket_t* SM=k_shmat();
//     while(1){
//         sleep(T/2);
//         for(int i=0;i<NUM_SOCKETS;i++){//check for any socket that is not free and is bound
//             pthread_mutex_lock(&mutex_socket[i]);
//             if(!SM[i].is_free && SM[i].is_bound && !SM[i].is_closed){
//                 int timeout=check_timeout(&SM[i]);
//                 if(timeout){
//                     // now travwrse the window and resend all the messages that are not acked
//                     for(int j=SM[i].swnd.base,cnt=0;cnt<SM[i].swnd.size;j=(j+1)%WINDOW_SIZE,cnt++){
//                         //send all the messages, no need to check for ack
//                         if(SM[i].swnd.timeout[j]!=-1){
//                             printf("[THREAD S]: Timeout for Ksocket %d Seq: %d\n",i,SM[i].swnd.msg_seq_num[j]);
//                             int send_bytes=send_pkt(SM[i].sockfd, &SM[i].dest_addr, DATA, 
//                             SM[i].swnd.msg_seq_num[j],0/*not sending rwnd in data msg*/, SM[i].send_buffer[j]);//implemented now
//                         }
//                         SM[i].swnd.timeout[j]=time(NULL)+T;//next timeout after T secs
//                     }
//                 }
//             }
//             else if(!SM[i].is_free && SM[i].is_bound && SM[i].is_closed){
//                 // for sending the FIN packet
//                 // this thread sends FIN, and thread R will send FIN-ACK
//                 if(SM[i].fin_timeout==-1){
//                     // first fin send
//                     printf("[THREAD S]: Sending FIN for Ksocket %d\n",i);
//                     int send_bytes=send_pkt(SM[i].sockfd, &SM[i].dest_addr, FIN, 
//                             SM[i].swnd.last_ack,SM[i].rwnd.size, NULL);//implemented now, null as no message

//                     SM[i].fin_timeout=time(NULL); //this is not actually the timeout, variable name peace
//                 }
//                 else{
//                     time_t curr=time(NULL);
//                     if(curr-SM[i].fin_timeout>=T){
//                         // resend fin
//                         printf("[THREAD S]: Timeout for FIN packet for Ksocket %d\n",i);
//                         int send_bytes=send_pkt(SM[i].sockfd, &SM[i].dest_addr, FIN, 
//                             SM[i].swnd.last_ack,SM[i].rwnd.size, NULL); //function implemented done
//                         SM[i].fin_timeout=curr;
//                     }
//                 }
//             }
//             pthread_mutex_unlock(&mutex_socket[i]);

//         }
//         /*It then checks the current swnd for each of the KTP sockets and determines whether there is a
//             pending message from the sender-side message buffer that can be sent. If so, it sends that
//             message through the UDP sendto() call for the corresponding UDP socket and updates the
//             send timestamp.*/
//         // i forgot to implement this, implementing this below
//         for(int i=0;i<NUM_SOCKETS;i++){
//             pthread_mutex_lock(&mutex_socket[i]);
//             if(!SM[i].is_free && SM[i].is_bound && !SM[i].is_closed){
//                 for(int j=SM[i].swnd.base,cnt=0;cnt<SM[i].swnd.size;j=(j+1)%WINDOW_SIZE,cnt++){
//                     if(SM[i].swnd.timeout[j]==-1)
//                     {
//                         // if the message has not been sent before, send it and set the timeout
//                         printf("[THREAD S]: Sending message for Ksocket %d Seq: %d\n",i,SM[i].swnd.msg_seq_num[j]);
//                         int send_bytes=send_pkt(SM[i].sockfd, &SM[i].dest_addr, DATA, 
//                             SM[i].swnd.msg_seq_num[j],0/* not sending rwnd in data*/, SM[i].send_buffer[j]);
//                         SM[i].swnd.timeout[j]=time(NULL)+T;//
//                     }
//                 }
//             }
//             pthread_mutex_unlock(&mutex_socket[i]); 
//         }

//     }
// }

static int current_swnd_span(const ktp_socket_t *slot)
{
    return (slot->swnd.used < slot->swnd.size) ? slot->swnd.used : slot->swnd.size;
}

static int all_sent_and_acked(const ktp_socket_t *slot)
{
    for (int i = 0; i < BUFFSIZE; ++i) {
        if (!slot->send_buffer_empty[i]) return 0;
    }
    return 1;
}


void* thread_S(void* args)
{
    ktp_socket_t* SM = k_shmat();
    if (SM == NULL) return NULL;

    while (1) {
        sleep(T / 2);

        for (int i = 0; i < NUM_SOCKETS; ++i) {
            pthread_mutex_lock(&mutex_socket[i]);

            if (!SM[i].is_free && SM[i].is_bound) {
                int span = current_swnd_span(&SM[i]);
                time_t now = time(NULL);

                /* Step 1: retransmit timed-out sent packets in current swnd */
                int timed_out = 0;
                for (int j = SM[i].swnd.base, cnt = 0; cnt < span; j = (j + 1) % WINDOW_SIZE, ++cnt) {
                    if (!SM[i].send_buffer_empty[j] &&
                        SM[i].swnd.timeout[j] != -1 &&
                        now >= SM[i].swnd.timeout[j]) {
                        timed_out = 1;
                        break;
                    }
                }

                if (timed_out) {
                    for (int j = SM[i].swnd.base, cnt = 0; cnt < span; j = (j + 1) % WINDOW_SIZE, ++cnt) {
                        if (!SM[i].send_buffer_empty[j] && SM[i].swnd.timeout[j] != -1) {
                            printf("[THREAD S]: Timeout for Ksocket %d Seq: %d\n",
                                   i, SM[i].swnd.msg_seq_num[j]);

                            if (send_pkt(SM[i].sockfd, &SM[i].dest_addr, DATA,
                                         SM[i].swnd.msg_seq_num[j], 0,
                                         SM[i].send_buffer[j]) >= 0) {
                                SM[i].swnd.timeout[j] = now + T;
                            }
                        }
                    }
                }

                /* Step 2: send pending unsent packets in current swnd */
                for (int j = SM[i].swnd.base, cnt = 0; cnt < span; j = (j + 1) % WINDOW_SIZE, ++cnt) {
                    if (!SM[i].send_buffer_empty[j] && SM[i].swnd.timeout[j] == -1) {
                        printf("[THREAD S]: Sending message for Ksocket %d Seq: %d\n",
                               i, SM[i].swnd.msg_seq_num[j]);

                        if (send_pkt(SM[i].sockfd, &SM[i].dest_addr, DATA,
                                     SM[i].swnd.msg_seq_num[j], 0,
                                     SM[i].send_buffer[j]) >= 0) {
                            SM[i].swnd.timeout[j] = now + T;
                        }
                    }
                }

                /* Step 3: only after all data is drained, start FIN handshake */
                if (SM[i].is_closed && all_sent_and_acked(&SM[i])) {
                    if (SM[i].fin_timeout == -1) {
                        printf("[THREAD S]: Sending FIN for Ksocket %d\n", i);

                        if (send_pkt(SM[i].sockfd, &SM[i].dest_addr, FIN,
                                     SM[i].rwnd.last_ack, SM[i].rwnd.size, NULL) >= 0) {
                            SM[i].fin_timeout = now + T;
                        }
                    }
                    else if (now >= SM[i].fin_timeout) {
                        printf("[THREAD S]: Timeout for FIN packet for Ksocket %d\n", i);

                        if (send_pkt(SM[i].sockfd, &SM[i].dest_addr, FIN,
                                     SM[i].rwnd.last_ack, SM[i].rwnd.size, NULL) >= 0) {
                            SM[i].fin_timeout = now + T;
                        }
                    }
                }
            }

            pthread_mutex_unlock(&mutex_socket[i]);
        }
    }

    k_shmdt((void*)SM);
    return NULL;
}


/* Thread logic here */

void* thread_Garbage(void* args){
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
    k_shmdt(SM);
    return NULL;
}



int main(){

    srand(time(NULL));

    g_error=NOERROR;
    //initialising shared memory
    init_Memory(NUM_SOCKETS);
    
    signal(SIGINT,cleanup);
    signal(SIGSEGV,cleanup);
    
    //initialising threads
    pthread_t R,S,Garb;
    pthread_create(&R,NULL,&thread_R,NULL);
    pthread_create(&S,NULL,&thread_S,NULL);
    pthread_create(&Garb,NULL,&thread_Garbage,NULL);


    pthread_join(R, NULL);
    pthread_join(S, NULL);
    pthread_join(Garb, NULL);
    return 0;
}