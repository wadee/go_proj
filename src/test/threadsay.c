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
#define IS_UDP(x) (x == udp_transport)
int debug = 0;

enum network_transport {
    local_transport, /* Unix sockets*/
    tcp_transport,
    udp_transport
};

enum conn_states {
    conn_listening,  /**< the socket which listens for connections */
    conn_new_cmd,    /**< Prepare connection for next command */
    conn_waiting,    /**< waiting for a readable socket */
    conn_read,       /**< reading in a command line */
    conn_parse_cmd,  /**< try to parse a command from the input buffer */
    conn_write,      /**< writing out a simple response */
    conn_nread,      /**< reading in a fixed number of bytes */
    conn_swallow,    /**< swallowing unnecessary bytes w/o storing */
    conn_closing,    /**< closing this connection */
    conn_mwrite,     /**< writing out many items sequentially */
    conn_closed,     /**< connection is closed */
    conn_max_state   /**< Max state value (used for assertion) */
};

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
    //libevent 的回调函数的第二个参数，表示触发的事件
    short  which;
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

    /*
        该函数主要作用：
        1.监听端口，注册监听端口事件.
        2.一旦有端口有数据，建立连接，将连接放到连接池中，conn_new为每一个连接注册时间监听
        3.通知线程有连接需要处理
    */

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

    hints.ai_socktype = SOCK_STREAM;

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
        
        error = setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
        if (error != 0)
            perror("setsockopt");

        error = setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
        if (error != 0)
            perror("setsockopt");

        error = setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
        if (error != 0)
            perror("setsockopt");

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
            if (!IS_UDP(transport) && listen(sfd, 1024) == -1) {
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

void event_handler(const int fd, const short which, void *arg) {
    conn *c;
    c = (conn *)arg;

    assert(c != NULL);
    c->which = which;

    if (fd != c->sfd)
    {
        perror("fd != sfd");
        conn_close(c);
        return;
    }


}

static void conn_close(conn *c) {
    /*
        清理工作：
            1.libevent事件删除
            2.c中的sfd的关闭
            3.conn的链表删除
            4.c的释放。(有可能不释放，如果是唯一一个的话，猜测的)
    */
    assert(c != NULL);
    /* delete the event, the socket and the conn */
    event_del(&c->event);

    fprintf(stderr, "<%d connection closed.\n", c->sfd);
    // conn_cleanup(c);
    // MEMCACHED_CONN_RELEASE(c->sfd);
    conn_set_state(c, conn_closed);
    close(c->sfd);
    return;
}


static void conn_set_state(conn *c, enum conn_states state) {
    assert(c != NULL);
    assert(state >= conn_listening && state < conn_max_state);

    if (state != c->state) {
        fprintf(stderr, "error\n");

        if (state == conn_write || state == conn_mwrite) {
            // MEMCACHED_PROCESS_COMMAND_END(c->sfd, c->wbuf, c->wbytes);
            //i don't know what should be done.
        }
        c->state = state;
    }
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
    
        server_socket 请求，分发
        注册事件loop    
    */
    FILE *portnumber_file = NULL;
    portnumber_file = fopen("/tmp/portnumber.file", "a");

    // conn_init();
    thread_init();

    server_socket(SERVER_PORT, tcp_transport, portnumber_file);

    event_base_loop(main_base, 0);

    exit(0);

}