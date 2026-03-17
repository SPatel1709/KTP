#include "ksocket.h"


char buffer[MSG_SIZE];
char* eof_marker="`";   // this is hardly used by anyone never seen




int main(int argc,char* argv[]){

    if(argc!=6)
    {
        fprintf(stderr,"Usage: %s <SRC_IP> <SRC_PORT> <DST_IP> <DST_PORT> <INPUT_FILE>\n",argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *src_ip = argv[1];
    int src_port = atoi(argv[2]);
    const char *dst_ip = argv[3];
    int dst_port = atoi(argv[4]);
    const char *input_file = argv[5];

    k_sockfd_t ksockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (ksockfd < 0)
    {
        fprintf(stderr, "k_socket failed\n");
        return EXIT_FAILURE;
    }

    if (k_bind(ksockfd, (char *)src_ip, src_port, (char *)dst_ip, dst_port) < 0)
    {
        fprintf(stderr, "k_bind failed\n");
        k_close(ksockfd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dst_port);
    dest_addr.sin_addr.s_addr = inet_addr(dst_ip);

    FILE *fp = fopen(input_file, "rb");
    if (fp == NULL)
    {
        perror("fopen");
        k_close(ksockfd);
        return EXIT_FAILURE;
    }

    printf("[USER1] Sending file: %s\n", input_file);

    while (1)
    {
        size_t read_bytes = fread(buffer, 1, MSG_SIZE - 1, fp);
        if (read_bytes == 0)
        {
            break;
        }

        buffer[read_bytes] = '\0';

        ssize_t sent = -1;
        while (sent < 0)
        {
            sent = k_sendto(ksockfd,buffer,read_bytes + 1,(const struct sockaddr *)&dest_addr,sizeof(dest_addr));

            if (sent < 0)
            {
                if (g_error == ENOSPACE)
                {
                    sleep(1);
                    continue;
                }
                fprintf(stderr, "k_sendto failed\n");
                fclose(fp);
                k_close(ksockfd);
                return EXIT_FAILURE;
            }
        }
    }

    if (ferror(fp))
    {
        perror("fread");
    }
    fclose(fp);

    k_sendto(ksockfd,eof_marker,strlen(eof_marker)+1,(const struct sockaddr *)&dest_addr,sizeof(dest_addr));
    k_close(ksockfd);

    return 0;
}