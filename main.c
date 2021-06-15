#include "dv.h"
#define MAX_NODE 1000
#define MAX_NAME 16
struct router
{
    char name[MAX_NAME];
};
struct neighbor
{
    struct router r;
    int port;
    double cost;
    long last_update;
    int node_down;
};
struct config
{
    int frequency;
    int max_valid_time;
    double unreachable;
    struct router self;
};
struct route_table_item
{
    double distance;
    struct router dest_node;
    struct router neighbor;
    int reachable;
};
struct message
{
    double distance;
    struct router dest_node;
    struct router src_node;
};
int port;
int seq = 0;
struct neighbor neighbors[MAX_NODE];
int neighbor_count = 0;
struct route_table_item route_table[MAX_NODE];
int table_size = 0;
struct config config;
int flag = 1;
int crash = 0;
FILE *logfp;
void log_init(const char *name)
{
    char logfile[32];
    sprintf(logfile, "router_%s.log", name);
    logfp = Fopen(logfile, "a+");
    Fputs("--- log start ---\n", logfp);
}
void do_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(logfp, fmt, args);
    vprintf(fmt, args);
    va_end(args);
}
int read_config(void)
{
    int conf_count = 0;
    FILE *fp;
    char str[1024];
    char conf_name[128];
    char value[128];
    fp = Fopen("router.conf", "r");
    while (Fgets(str, 1024, fp))
    {
        sscanf(str, "%[^=]=%s", conf_name, value);
        if (!strcmp(conf_name, "frequency"))
        {
            config.frequency = atoi(value);
            conf_count += 1;
        }
        else if (!strcmp(conf_name, "unreachable"))
        {
            config.unreachable = atof(value);
            conf_count += 1;
        }
        else if (!strcmp(conf_name, "max_valid_time"))
        {
            config.max_valid_time = atoi(value);
            conf_count += 1;
        }
    }
    Fclose(fp);
    if (conf_count != 3)
    {
        return 0;
    }
    do_log("config read ok. frequency=%d unreachable=%f max_valid_time=%d\n", config.frequency, config.unreachable, config.max_valid_time);
    return 1;
}
int router_equal(struct router r1, struct router r2)
{
    return strcmp(r1.name, r2.name) == 0;
}
int read_nodes(char *filename)
{
    FILE *fp = Fopen(filename, "r");
    int ret;
    while ((ret = fscanf(fp, "%s %lf%d", neighbors[neighbor_count].r.name, &neighbors[neighbor_count].cost, &neighbors[neighbor_count].port)) != EOF)
    {
        if (ret != 3)
        {
            return 0;
        }
        neighbor_count++;
    }
    Fclose(fp);
    do_log("neighbor nodes read ok total %d neighbors\n", neighbor_count);
    return 1;
}
long get_timestamp_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int find_node_from_table(struct router r)
{
    for (int i = 0; i < table_size; i++)
    {
        if (router_equal(route_table[i].dest_node, r))
        {
            return i;
        }
    }
    return -1;
}
int find_neighbor_node(struct router r)
{
    for (int i = 0; i < neighbor_count; i++)
    {
        if (router_equal(neighbors[i].r, r))
        {
            return i;
        }
    }
    return -1;
}
void *build_message_from_table(int *psize)
{
    int c = 0;
    static struct message mesgs[MAX_NODE];
    memset(mesgs, 0, sizeof(mesgs));
    do_log("## Sent. Source Node = %s; Sequence Number = %d\n", config.self.name, seq++);
    for (int i = 0; i < table_size; i++)
    {
        if (!route_table[i].reachable)
        {
            continue;
        }
        mesgs[c].dest_node = route_table[i].dest_node;
        mesgs[c].distance = route_table[i].distance;
        mesgs[c].src_node = config.self;
        do_log("DestNode = %s; Distance = %f; SrcNode=%s\n", mesgs[c].dest_node.name, mesgs[c].distance, mesgs[c].src_node);
        c++;
    }
    *psize = c * sizeof(struct message);
    return mesgs;
}
void update_table_from_message(struct message *mesgs, int n)
{
    int table_index, neighbor_index;
    // do_log("## Received. Source Node = %s; Sequence Number = seq\n", mesgs[0].src_node);
    neighbor_index = find_neighbor_node(mesgs[0].src_node);
    if (neighbor_index == -1)
    {
        fprintf(stderr, "got unknown neighbor node %s, throw it\n", mesgs[0].src_node.name);
        return;
    }
    neighbors[neighbor_index].last_update = get_timestamp_ms();
    neighbors[neighbor_index].node_down = 0;
    for (int i = 0; i < n; i++)
    {
        // do_log("DestNode = %s; Distance = %f; SrcNode=%s\n", mesgs[i].dest_node.name, mesgs[i].distance, mesgs[i].src_node);
        // exclude self
        if (router_equal(mesgs[i].dest_node, config.self))
        {
            continue;
        }
        if ((table_index = find_node_from_table(mesgs[i].dest_node)) == -1)
        {
            struct route_table_item item;
            memset(&item, 0, sizeof(item));
            item.distance = mesgs[i].distance + neighbors[neighbor_index].cost;
            item.dest_node = mesgs[i].dest_node;
            item.neighbor = mesgs[i].src_node;
            item.reachable = 1;
            // insert
            route_table[table_size++] = item;
            do_log("insert %s to table distance=%f neighbor=%s\n", item.dest_node, item.distance, item.neighbor.name);
        }
        else
        {
            double dist = 0.0;
            dist = mesgs[i].distance + neighbors[neighbor_index].cost;
            if (route_table[table_index].distance > dist)
            {
                route_table[table_index].distance = dist;
                route_table[table_index].neighbor = mesgs[i].src_node;
                route_table[table_index].reachable = 1;
                do_log("update %s distance=%f neighbor=%s\n", route_table[table_index].dest_node,
                       route_table[table_index].distance, route_table[table_index].neighbor.name);
            }
            else if (router_equal(mesgs[i].src_node, route_table[table_index].neighbor) && route_table[table_index].distance != dist)
            {
                route_table[table_index].distance = dist;
                do_log("update %s distance=%f neighbor=%s\n", route_table[table_index].dest_node,
                       route_table[table_index].distance, route_table[table_index].neighbor.name);
                // unreachable
                if (dist > config.unreachable || fabs(dist - config.unreachable) < 0.0000001)
                {
                    route_table[table_index].reachable = 0;
                    do_log("mark %s unreachable\n", route_table[table_index].dest_node);
                }
            }
        }
    }
}

void send_thread(void *p)
{
    int sockfd;
    int bufsize;
    const void *buf;
    struct timespec slptm;
    struct sockaddr_in servaddr;
    slptm.tv_sec = config.frequency / 1000;
    slptm.tv_nsec = config.frequency % 1000 * 1000000L;
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // Inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (;;)
    {
        if (crash)
        {
            goto end;
        }

        if (flag)
        {
            buf = build_message_from_table(&bufsize);
            for (int i = 0; i < neighbor_count; i++)
            {
                servaddr.sin_port = htons(neighbors[i].port);
                Sendto(sockfd, buf, bufsize, 0, (SA *)&servaddr, sizeof(servaddr));
            }
        }
        nanosleep(&slptm, NULL);
    }
end:
    Close(sockfd);
}
void recv_thread(int *pservport)
{
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    int n;
    socklen_t len;
    char mesg[MAXLINE];
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    servaddr.sin_port = htons(*pservport);
    Bind(sockfd, (SA *)&servaddr, sizeof(servaddr));
    for (;;)
    {
        if (crash)
        {
            goto end;
        }
        len = sizeof(cliaddr);
        n = Recvfrom(sockfd, mesg, MAXLINE, 0, (SA *)&cliaddr, &len);
        // do_log("recvfrom: %d\n", cliaddr.sin_port);
        if (flag)
        {
            if (n % sizeof(struct message) != 0)
            {
                printf("got corrupted message with length %d, throw it\n", n);
                continue;
            }
            update_table_from_message((struct message *)mesg, n / sizeof(struct message));
        }
    }
end:
    Close(sockfd);
}

void mark_unreachable_thread(void *p)
{
    struct timeval tv;
    long time_stamp;
    // 100ms
    tv.tv_sec = 0;
    tv.tv_usec = 1000 * 100;
    int err;
    for (;;)
    {
        if (crash)
        {
            goto end;
        }
        time_stamp = get_timestamp_ms();
        for (int i = 0; i < neighbor_count; i++)
        {
            if (!neighbors[i].last_update || neighbors[i].node_down)
            {
                continue;
            }

            if (time_stamp - neighbors[i].last_update > config.max_valid_time)
            {
                neighbors[i].node_down = 1;
                do_log("neighbor %s down\n", neighbors[i].r.name);
                for (int j = 0; j < table_size; j++)
                {
                    if (router_equal(route_table[j].neighbor, neighbors[i].r))
                    {
                        route_table[j].distance = config.unreachable + 1.0;
                        do_log("mark %s unreachable\n", route_table[j].dest_node);
                    }
                }
            }
        }
        select(0, NULL, NULL, NULL, &tv);
    }
end:;
}
int main(int argc, char *argv[])
{
    char cmdbuf[1024];
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s id port file\n", argv[0]);
        return 1;
    }
    strcpy(config.self.name, argv[1]);
    log_init(config.self.name);
    port = atoi(argv[2]);
program_start:
    if (!read_config())
    {
        fprintf(stderr, "read_config() falied\n", argv[0]);
        return 2;
    }
    if (!read_nodes(argv[3]))
    {
        fprintf(stderr, "read_nodes() falied\n", argv[0]);
        return 2;
    }
    // add self to table
    route_table[table_size].dest_node = config.self;
    route_table[table_size].distance = 0.0;
    route_table[table_size].neighbor = config.self;
    route_table[table_size].reachable = 1;
    table_size++;
    // start
    pthread_t send_threadid, recv_threadid, mark_threadid;
    Pthread_create(&send_threadid, NULL, (Pthreadfunc)send_thread, NULL);
    Pthread_create(&recv_threadid, NULL, (Pthreadfunc)recv_thread, &port);
    Pthread_create(&mark_threadid, NULL, (Pthreadfunc)mark_unreachable_thread, NULL);
    while (1)
    {
        putchar('>');
        fgets(cmdbuf, sizeof(cmdbuf), stdin);
        if (!strncmp(cmdbuf, "down", 4))
        {
            flag = 0;
            do_log("node down!\n");
        }
        else if (!strncmp(cmdbuf, "up", 2))
        {
            flag = 1;
            do_log("node up!\n");
        }
        else if (!strncmp(cmdbuf, "crash", 5))
        {
            crash = 1;
            Pthread_join(send_threadid, NULL);
            Pthread_join(recv_threadid, NULL);
            Pthread_join(mark_threadid, NULL);
            // clear all data
            seq = 0;
            memset(neighbors, 0, sizeof(neighbors));
            neighbor_count = 0;
            memset(route_table, 0, sizeof(route_table));
            table_size = 0;
            crash = 0;
            flag = 1;
            do_log("node crash, all of the data lost\n");
        }
        else if (!strncmp(cmdbuf, "boot", 4))
        {
            do_log("node boot\n");
            goto program_start;
        }
        else if (!strncmp(cmdbuf, "exit", 4))
        {
            return 0;
        }
    }
    return 0;
}
