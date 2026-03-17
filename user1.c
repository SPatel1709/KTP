#include "ksocket.h"


char buffer[MSG_SIZE];
char* eof_marker="`";   // this is hardly used by anyone never seen




int main(int argc,char* argv[]){

    if(argc!=5)
    {
        fprintf(stderr,"Usage: %s <SRC_IP> <SRC_PORT> <DST_IP> <DST_PORT>\n",argv[0]);
        exit(EXIT_FAILURE);
    }

    


    return 0;
}