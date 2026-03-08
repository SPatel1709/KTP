#ifndef H_KSOCKET
#define H_KSOCKET

#define SOCK_KTP 9999

typedef enum error_t{
    ENOSPACE,
    ENOMESSAGE,
    ENOTBOUND,
    NOERROR
} error_t;

error_t g_error=NOERROR;


int k_socket(int __domain,int __type,int protocol);

/*assuming that ip mixing may occur making my protocol future proof*/
int k_bind(int __fd,const struct sockaddr* __src_addr,socklen_t __src_len,const struct sockaddr* __dest_addr,socklen_t __dest_len);
int k_close(int __fd);

/* 
here __restrict is like unique pointer of cpp that solely that
pointer can access the memory and 
thus the compiler can optimise things aggressively
*/
ssize_t k_sendto(int __fd,const void *__buf,size_t __n,int __flags,const struct sockaddr *__addr,socklen_t __addr_len);
ssize_t k_recvfrom(int __fd,void *__restrict__ __buf,size_t __n,int __flags,struct sockaddr *__restrict__ __addr,socklen_t *__restrict__ __addr_len);

#endif
