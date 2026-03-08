#include "ksocket.h"


int k_socket(int __domain,int __type,int protocol){
    assert(__type==SOCK_KTP && "Incorrect Sock type for KTP");

    int sockfd;
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



int k_bind(int __fd,const struct sockaddr* __src_addr,socklen_t __src_len,const struct sockaddr* __dest_addr,socklen_t __dest_len){

    int bind_result=bind(__fd,__src_addr,__src_len);

    if(bind_result>0)
    {
        /*update the things in the SM*/
    }
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