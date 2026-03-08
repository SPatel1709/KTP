#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/signal.h>
#include <sys/shm.h>

#ifndef H_KSOCKET
#define H_KSOCKET

#define SOCK_KTP 9999
#define T 5
#define p 0.3
#define MESSAGE_SIZE 512 // size of the message in bytes
#define BUFFSIZE 10 // size of buffer in terms of number of messages
#define WINDOW_SIZE 10 // same as the buffer size
#define NUM_SOCKETS 10

#define FTOK_FILE "ksocket.c"

typedef int ksockfd_t;

typedef enum error_t{
    ENOSPACE,
    ENOMESSAGE,
    ENOTBOUND,
    NOERROR
} error_t;

error_t g_error=NOERROR;

/*Here is the definition of the structure for sliding window implementation*/
typedef struct{
    int base;
    u_int16_t size;
    u_int16_t next_sequence_number;
    u_int16_t last_acknowledged;
    int message_sequence_numbers[WINDOW_SIZE]; // send but not acked
    bool received_ack[WINDOW_SIZE]; // this is useful for the receiver
    time_t timeout[WINDOW_SIZE]; // this is useful for the sender
}window_t;

typedef struct {
    bool is_free;// information for free
    bool is_closed;
    pid_t pid;
    int sockfd; //actual socket fd
    struct sockaddr_in src_addr;
    struct sockaddr_in dest_addr;
    bool send_buffer_empty[BUFFSIZE];
    char send_buffer[BUFFSIZE][MESSAGE_SIZE]; // buffer for storing the messages to be sent
    char recv_buffer[BUFFSIZE][MESSAGE_SIZE]; // buffer for storing the messages received
    window_t swnd; //sender window
    window_t rwnd; //receiver window
}ktp_socket_t;

/* Initialising sockets at compile time */
pthread_mutex_t mutex_socket[NUM_SOCKETS] = {
    [0 ... NUM_SOCKETS-1] = PTHREAD_MUTEX_INITIALIZER
};

ksockfd_t k_socket(int __domain,int __type,int protocol);

/*assuming that ip mixing may occur making my protocol future proof*/
int k_bind(ksockfd_t __fd,char* __src_ip, int __src_port, char* __dest_ip, int __dest_port);
int k_close(ksockfd_t __fd);

/* 
here __restrict is like unique pointer of cpp that solely that
pointer can access the memory and 
thus the compiler can optimise things aggressively, very nice
*/
ssize_t k_sendto(ksockfd_t __fd,const void *__buf,size_t __n,int __flags,const struct sockaddr *_dest_addr,socklen_t __addr_len);
ssize_t k_recvfrom(ksockfd_t __fd,void *__restrict__ __buf,size_t __n,int __flags,struct sockaddr *__restrict__ __addr,socklen_t *__restrict__ __addr_len);

/* get the shared memory id */
int k_shmget();
ktp_socket_t* k_shmat();
int k_shmdt(const void* __shmaddr);

window_t k_window();


#endif