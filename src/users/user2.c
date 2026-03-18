#include "ksocket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <src_ip> <src_port> <dest_ip> <dest_port> <output_file>\n",
            prog);
    exit(EXIT_FAILURE);
}

static void write_all(int fd, const char *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, n - done);
        if (w < 0) die("write");
        done += (size_t)w;
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

    const char *src_ip    = argv[1];
    int src_port          = atoi(argv[2]);
    const char *dest_ip   = argv[3];
    int dest_port         = atoi(argv[4]);
    const char *outfile   = argv[5];

    int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("open output file");

    k_sockfd_t kfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (kfd < 0) {
        close(fd);
        die("k_socket");
    }

    if (k_bind(kfd, (char *)src_ip, src_port, (char *)dest_ip, dest_port) < 0) {
        close(fd);
        die("k_bind");
    }

    char block[MSG_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    long long file_size = -1;

    /* receive metadata block */
    while (file_size < 0) {
        ssize_t r = k_recvfrom(kfd, block, MSG_SIZE,
                               (struct sockaddr *)&from_addr, &from_len);
        if (r < 0) {
            short_sleep();
            continue;
        }

        block[MSG_SIZE - 1] = '\0';
        file_size = atoll(block);
        if (file_size < 0) {
            close(fd);
            fprintf(stderr, "Invalid file size metadata received\n");
            return EXIT_FAILURE;
        }
    }

    long long remaining = file_size;

    while (remaining > 0) {
        ssize_t r = k_recvfrom(kfd, block, MSG_SIZE,
                               (struct sockaddr *)&from_addr, &from_len);
        if (r < 0) {
            short_sleep();
            continue;
        }

        size_t to_write = (remaining >= MSG_SIZE) ? MSG_SIZE : (size_t)remaining;
        write_all(fd, block, to_write);
        remaining -= (long long)to_write;
    }

    close(fd);

    if (k_close(kfd) < 0) die("k_close");

    printf("[USER2] Received file '%s' (%lld bytes)\n", outfile, file_size);
    return 0;
}