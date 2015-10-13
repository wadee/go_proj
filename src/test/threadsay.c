#include <event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>

#define SERVER_PORT 8080
int debug = 0;

typedef struct{
    int read_fd;
    int write_fd;
    pthread_t thread_id;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
    struct event notify_event; 
} thread_cg;

typedef struct conn conn;
struct conn {
    //一个连接里面应该有什么属性呢
    //首先应该是文件描述符
    int sfd;
    //此连接目前的状态，因为我们需要根据不同的状态进行不同的处理
    int stats;
    //struct event event. libevent注册事件,放在连接体本身作为一个属性存起来
    struct event event;
    //连接管理，连接池嘛，所以可以是一个链表，之后需要涉及到链表的增删改查
    conn * next;
};

static conn *listen_conn = NULL;
conn **conns;

static thread_cg *thread_cgs;
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;

int thread_init(int num_of_threads){
    int i;
    pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);

    thread_cgs = calloc(num_of_threads, sizeof(thread_cg));

    if(!thread_cgs){
        perror("can't alloc thread_cg");
        exit(1);
    }

    for(i = 0; i < num_of_threads; i++){
        int fds[2];
        if(pipe(fds)){
            perror("can't create notify pipe");
            exit(1);
        }

        thread_cgs[i].read_fd = fds[0];
        thread_cgs[i].write_fd = fds[1];

        setup_thread(&thread_cgs[i]);
    }
    //未完成，需要等待现成完成setup,然后才退出函数
    for(i = 0; i < num_of_threads; i++){
        start_worker(worker_libevent, &thread_cgs[i]);
    }

}

static void start_worker(void *(*func)(void *), void *arg) {
    pthread_t       thread;
    pthread_attr_t  attr;
    int             ret;

    pthread_attr_init(&attr);

    if ((ret = pthread_create(&thread, &attr, func, arg)) != 0) {
        fprintf(stderr, "Can't create thread: %s\n",
                strerror(ret));
        exit(1);
    }
}
static void *worker_libevent(void *arg) {
    thread_cg *me = arg;
    pthread_mutex_lock(&init_lock);
    init_count++;
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);

    event_base_loop(me->base, 0);
    return NULL;
}

int setup_thread(thread_cg * t_cg){
    t_cg->base = event_init();
    if(! t_cg->base){
        fprintf(stderr, "can't alloc event base\n");
        exit(1);
    }
    event_set(&t_cg->notify_event, t_cg->read_fd, EV_READ | EV_PERSIST, thread_libevent_process, t_cg);
    event_base_set(t_cg->base, &t_cg->notify_event);

    if (event_add(&t_cg->notify_event, 0) == -1)
    {
        fprintf(stderr, "Can't monitor libevent notify pipe\n");
        exit(1);
    }
    //未完成，应该还是需要实现连接管理的
    pthread_mutex_lock(&init_lock);
    while (init_count < num_of_threads) {
        pthread_cond_wait(&init_cond, &init_lock);
    }
    pthread_mutex_unlock(&init_lock);
}

static int new_socket(struct addrinfo *ai) {
    int sfd;
    int flags;

    if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
        fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("setting O_NONBLOCK");
        close(sfd);
        return -1;
    }
    return sfd;
}

static void thread_libevent_process(int fd, short which, void *arg) {   
    thread_cg *me = arg;
    char buf[1];

    if (read(fd, buf, 1) != 1)
    {
        perror("thread_libevent_process read error");
        exit(1);
    }

    switch(buf[0]){
        case 'c':
            //从连接管理中，pop出来一个连接，将连接(conn_new)注册到event_handler来处理

            break;
        case 'p':
            //
            break;
    }

}

static int server_socket(const char *interface,
                         int port,
                         enum network_transport transport,
                         FILE *portnumber_file){

    int sfd;
    struct linger ling = {0, 0};
    struct addrinfo *ai;
    struct addrinfo *next;
    struct addrinfo hints = { .ai_flags = AI_PASSIVE,
                              .ai_family = AF_UNSPEC };
    char port_buf[NI_MAXSERV];
    int error;
    int success = 0;
    int flags =1;

    hints.ai_socktype = IS_UDP(transport) ? SOCK_DGRAM : SOCK_STREAM;

    if (port == -1) {
        port = 0;
    }
    snprintf(port_buf, sizeof(port_buf), "%d", port);
    error= getaddrinfo(interface, port_buf, &hints, &ai);
    if (error != 0) {
        if (error != EAI_SYSTEM)
          fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
        else
          perror("getaddrinfo()");
        return 1;
    }

    for (next= ai; next; next= next->ai_next) {
        conn *listen_conn_add;
        if ((sfd = new_socket(next)) == -1) {
            /* getaddrinfo can return "junk" addresses,
             * we make sure at least one works before erroring.
             */
            if (errno == EMFILE) {
                /* ...unless we're out of fds */
                perror("server_socket");
                exit(EX_OSERR);
            }
            continue;
        }

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
        if (IS_UDP(transport)) {
            maximize_sndbuf(sfd);
        } else {
            error = setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
            if (error != 0)
                perror("setsockopt");

            error = setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
            if (error != 0)
                perror("setsockopt");

            error = setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
            if (error != 0)
                perror("setsockopt");
        }

        if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
            if (errno != EADDRINUSE) {
                perror("bind()");
                close(sfd);
                freeaddrinfo(ai);
                return 1;
            }
            close(sfd);
            continue;
        } else {
            success++;
            if (!IS_UDP(transport) && listen(sfd, settings.backlog) == -1) {
                perror("listen()");
                close(sfd);
                freeaddrinfo(ai);
                return 1;
            }
            if (portnumber_file != NULL &&
                (next->ai_addr->sa_family == AF_INET ||
                 next->ai_addr->sa_family == AF_INET6)) {
                union {
                    struct sockaddr_in in;
                    struct sockaddr_in6 in6;
                } my_sockaddr;
                socklen_t len = sizeof(my_sockaddr);
                if (getsockname(sfd, (struct sockaddr*)&my_sockaddr, &len)==0) {
                    if (next->ai_addr->sa_family == AF_INET) {
                        fprintf(portnumber_file, "%s INET: %u\n",
                                IS_UDP(transport) ? "UDP" : "TCP",
                                ntohs(my_sockaddr.in.sin_port));
                    } else {
                        fprintf(portnumber_file, "%s INET6: %u\n",
                                IS_UDP(transport) ? "UDP" : "TCP",
                                ntohs(my_sockaddr.in6.sin6_port));
                    }
                }
            }
        }


        if (!(listen_conn_add = conn_new(sfd, conn_listening,
                                         EV_READ | EV_PERSIST, 1,
                                         transport, main_base))) {
            fprintf(stderr, "failed to create listening connection\n");
            exit(EXIT_FAILURE);
        }
        listen_conn_add->next = listen_conn;
        listen_conn = listen_conn_add;
    }

    freeaddrinfo(ai);

    /* Return zero iff we detected no errors in starting up connections */
    return success == 0;


}

conn *conn_new(const int sfd, enum conn_states init_state,
                const int event_flags,
                const int read_buffer_size, enum network_transport transport,
                struct event_base *base) {
    /*此函数需要做的事情
        1.新建一个conn结构，加入到connQ
        2.为此链接（文件描述符），注册一个event_handler
        3.event_handler进行状态的处理，不同状态不同的处理方式，master,slave共用此方法。
    */
    conn * new_c;
    new_c = conns[sfd];
    if (NULL == new_c)
    {
        if ( !(c = (conn *)alloc(1, sizeof(conn))) )
        {
            perror("alloc conn fail");
            exit(1);
        }

        new_c->sfd = sfd;
        conns[sfd] = new_c;
    }

    new_c->stats = init_state;
    //以上是连接的初始化，接下来是连接的事件注册
    event_set(&new_c->event, sfd, event_flags, event_handler, (void*)new_c);
    event_base_set(base, &new_c->event);

    return new_c;

}

int main(int argc, char** argv){

    /*
        主线程主体流程：
        conn_init;
        thread_init;
    
        accept 请求，分发
        注册事件loop    
    */
    conn_init();
    thread_init();

    



}