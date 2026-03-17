#include "ksocket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <src_ip> <src_port> <dest_ip> <dest_port> <input_file>\n",
            prog);
    exit(EXIT_FAILURE);
}

static void build_dest_addr(struct sockaddr_in *addr, const char *ip, int port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr->sin_addr) != 1) {
        fprintf(stderr, "Invalid destination IP: %s\n", ip);
        exit(EXIT_FAILURE);
    }
}

static void short_sleep(void)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 20 * 1000 * 1000; // 20 ms
    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[])
{
    if (argc != 6) usage(argv[0]);

    const char *src_ip   = argv[1];
    int src_port         = atoi(argv[2]);
    const char *dest_ip  = argv[3];
    int dest_port        = atoi(argv[4]);
    const char *infile   = argv[5];

    int fd = open(infile, O_RDONLY);
    if (fd < 0) die("open input file");

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        die("fstat");
    }

    long long file_size = (long long)st.st_size;

    k_sockfd_t kfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (kfd < 0) {
        close(fd);
        die("k_socket");
    }

    if (k_bind(kfd, (char *)src_ip, src_port, (char *)dest_ip, dest_port) < 0) {
        close(fd);
        die("k_bind");
    }

    struct sockaddr_in dest_addr;
    build_dest_addr(&dest_addr, dest_ip, dest_port);

    char block[MSG_SIZE];

    /* first message = file size */
    memset(block, 0, sizeof(block));
    snprintf(block, sizeof(block), "%lld", file_size);

    while (k_sendto(kfd, block, MSG_SIZE,
                    (const struct sockaddr *)&dest_addr,
                    sizeof(dest_addr)) < 0) {
        short_sleep();  //needed because we have defined POSIX version different that does not have usleep
    }

    while (1) {
        memset(block, 0, sizeof(block));

        ssize_t nread = read(fd, block, MSG_SIZE);
        if (nread < 0) {
            close(fd);
            die("read");
        }
        if (nread == 0) break;

        while (k_sendto(kfd, block, MSG_SIZE,
                        (const struct sockaddr *)&dest_addr,
                        sizeof(dest_addr)) < 0) {
            short_sleep();
        }
    }

    close(fd);

    if (k_close(kfd) < 0) die("k_close");

    printf("[USER1] Sent file '%s' (%lld bytes)\n", infile, file_size);
    return 0;
}