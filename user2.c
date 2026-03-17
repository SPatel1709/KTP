#include "ksocket.h"

char buffer[MSG_SIZE];
char* eof_marker="`";

int main(int argc, char* argv[])
{
    if(argc!=6)
    {
        fprintf(stderr,"Usage: %s <SRC_IP> <SRC_PORT> <DST_IP> <DST_PORT> <OUTPUT_FILE>\n",argv[0]);
        return EXIT_FAILURE;
    }

    const char *src_ip = argv[1];
    int src_port = atoi(argv[2]);
    const char *dst_ip = argv[3];
    int dst_port = atoi(argv[4]);
    const char *output_file = argv[5];

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

    FILE *fp = fopen(output_file, "wb");
    if (fp == NULL)
    {
        perror("fopen");
        k_close(ksockfd);
        return EXIT_FAILURE;
    }

    printf("[USER2] Waiting for messages, writing to: %s\n", output_file);

    while (1)
    {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);

        ssize_t recv_bytes = k_recvfrom(ksockfd,buffer,sizeof(buffer),(struct sockaddr *)&addr,&addr_len);

        if (recv_bytes < 0)
        {
            if (g_error == ENOMESSAGE)
            {
                sleep(1);
                continue;
            }
            fprintf(stderr, "k_recvfrom failed\n");
            break;
        }

        if (strcmp(buffer, eof_marker) == 0)
        {
            printf("[USER2] EOF received, exiting.\n");
            break;
        }

        size_t len = strlen(buffer);
        if (fwrite(buffer, 1, len, fp) != len)
        {
            perror("fwrite");
            break;
        }
    }

    fclose(fp);
    k_close(ksockfd);
    return 0;
}
