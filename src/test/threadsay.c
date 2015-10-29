#include "threadsay.h"
#include <sys/socket.h>
#include <sys/resource.h>
#include <assert.h>

static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

static conn *listen_conn = NULL;
conn **conns;

static CQ_ITEM *cqi_freelist;

static int last_thread = -1;
static int max_fds;
struct stats stats;
static thread_cg *thread_cgs;
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;
static volatile bool allow_new_conns = true;
static struct event maxconnsevent;
static pthread_mutex_t cqi_freelist_lock;
static struct event_base *main_base;
static LIBEVENT_DISPATCHER_THREAD dispatcher_thread;

pthread_mutex_t conn_lock = PTHREAD_MUTEX_INITIALIZER;

void STATS_LOCK() {
    pthread_mutex_lock(&stats_lock);
}

void STATS_UNLOCK() {
    pthread_mutex_unlock(&stats_lock);
}

int thread_init(int num_of_threads){
    int i;
    pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);
    pthread_mutex_init(&cqi_freelist_lock, NULL);
    cqi_freelist = NULL;

    thread_cgs = calloc(num_of_threads, sizeof(thread_cg));

    if(!thread_cgs){
        perror("can't alloc thread_cg");
        exit(1);
    }
    dispatcher_thread.base = main_base;
    dispatcher_thread.thread_id = pthread_self();

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

    pthread_mutex_lock(&init_lock);
    while (init_count < NUM_OF_THREADS) {
        pthread_cond_wait(&init_cond, &init_lock);
    }
    pthread_mutex_unlock(&init_lock);

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
    t_cg->new_conn_queue = malloc(sizeof(struct conn_queue));
    if (t_cg->new_conn_queue == NULL) {
        perror("Failed to allocate memory for connection queue");
        exit(EXIT_FAILURE);
    }
    cq_init(t_cg->new_conn_queue);
    //未完成，应该还是需要实现连接管理的
    
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
    CQ_ITEM *item;

    if (read(fd, buf, 1) != 1)
    {
        perror("thread_libevent_process read error");
        exit(1);
    }

    switch(buf[0]){
        case 'c':
            //从连接管理中，pop出来一个连接，将连接(conn_new)注册到event_handler来处理
            item = cq_pop(me->new_conn_queue);
            if (NULL != item) {
                conn *c = conn_new(item->sfd, item->init_state, item->event_flags,
                                   item->read_buffer_size, item->transport, me->base);
                if (c == NULL) {
                    if (IS_UDP(item->transport)) {
                        fprintf(stderr, "Can't listen for events on UDP socket\n");
                        exit(1);
                    } else {
                            fprintf(stderr, "Can't listen for events on fd %d\n",
                                item->sfd);
                        close(item->sfd);
                    }
                } else {
                    c->thread = me;
                }
                cqi_free(item);
            }
            break;
        case 'p':
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


void master_event_handler(const int fd, const short which, void *arg) {
    conn *c;
    c = (conn *)arg;

    assert(c != NULL);
    // c->which = which;

    if (fd != c->sfd)
    {
        perror("fd != sfd");
        conn_close(c);
        return;
    }
    drive_machine(c, NULL);
    return;
}

// 改成用bufferevent之后，接受的参数得改。。。
void event_handler(struct bufferevent *incoming, void *arg) {
    conn *c;
    c = (conn *)arg;

    assert(c != NULL);
    // c->which = which;
    int fd;
    fd = (int)bufferevent_getfd(incoming);

    if (fd != c->sfd)
    {
        perror("fd != sfd");
        conn_close(c);
        return;
    }
    drive_machine(c, incoming);
    return;

}

void buf_error_callback(struct bufferevent *incoming,
                        short what,
                        void *arg)
{
    int sfd = (int)bufferevent_getfd(incoming);
    printf("error %d and %d", sfd, what);
}

void buf_write_callback(struct bufferevent *incoming,
                        void *arg)
{	
    conn *c;
    c = (conn*) arg;
    assert(c != NULL);
    int fd;
    fd = (int)bufferevent_getfd(incoming);
    if (fd != c->sfd){
		perror("fd != sfd");
		conn_close(c);
		return;
	}
	drive_machine(c, incoming);
	return;
}
//void drive_machine(conn* c){
//改成bufferevent之后event_handler和drive_machine都不能幸免需要改参数
void drive_machine(conn* c, struct bufferevent * incoming){
    assert(c != NULL);
    // 真正的处理函数，根据不同的conn的状态来进行不同的处理。
    // 虽然我做这个demo只是把消息回显而已，但貌似这么多种状态还是得处理。
    bool stop = false;
    int sfd;

    socklen_t addrlen;
    struct sockaddr_storage addr;
    struct evbuffer *evreturn;
    char *req;

    while(!stop){
        switch(c->state){
            case conn_listening:
                //如果是listenting状态，则需要把连接accept
                addrlen = sizeof(addr);
                sfd = accept(c->sfd, (struct sockaddr *)&addr, &addrlen);
                if (sfd == -1)
                {
                    // accept错误
                    perror("sfd accept failed");
                    accept_new_conns(false); //用来决定主线程是否还需要accept连接，如果已经接近饱和了，就停住，等一个时间间隔再来试。
                    stop = true;
                }
                //分派conn任务
                dispatch_conn_new(sfd, conn_new_cmd, EV_READ | EV_PERSIST,
                                     DATA_BUFFER_SIZE, tcp_transport);
                stop = true;
                break;
            case conn_new_cmd:
                /* fall through */
            case conn_read:
                /*  now try reading from the socket */
                req = evbuffer_readline(incoming->input); 
                if (req == NULL){
                    //conn_set_state(c, conn_closing);
                    //goto set_conn_closing;
                    conn_set_state(c, conn_waiting);
                    break;    
                }
                if(c->req != NULL){
                    free(c->req);
                    c->req = NULL;
                }
                c->req = req;
                conn_set_state(c, conn_mwrite);
		//if (!update_event(c, EV_WRITE | EV_PERSIST)){
		//	printf("conn_read update event failed");
		//	goto set_conn_closing;
		//}
		//stop = true;
		//stop = true;
                break;
                //evreturn = evbuffer_new();
                //evbuffer_add_printf(evreturn, "You said %s\n", req);
               // bufferevent_write_buffer(incoming, evreturn);
                //evbuffer_free(evreturn);
                //free(req);

            set_conn_closing: 
                conn_set_state(c, conn_closing);
                break;
            case conn_mwrite:
                //所有的回复到在这个函数进行输出
                req = c->req;
                evreturn = evbuffer_new();
	//	bufferevent_setwatermark(incoming, EV_WRITE, 0, 0);
               int res1 =  evbuffer_add_printf(evreturn, "You said %s\n", req);
               int res2 =  bufferevent_write_buffer(incoming, evreturn);
//		int res3 = bufferevent_flush(incoming, EV_WRITE, BEV_FLUSH);
		int res4 = evbuffer_get_length(incoming->output);
                evbuffer_free(evreturn);
                free(req);
		evreturn = NULL;
		//int fd = (int)bufferevent_getfd(incoming);
		//char buf[20];
		//
		//sprintf(buf, "You said %s\n", req);
		//write(fd, buf, 11);
                c->req = NULL;
                conn_set_state(c,conn_waiting);
		stop = true;
                break;
            case conn_waiting:
                if (!update_event(c, EV_READ | EV_PERSIST)) {
                    fprintf(stderr, "Couldn't update event\n");
                    conn_set_state(c, conn_closing);
                    break;
                }
                conn_set_state(c, conn_read);
                stop = true;
                break;
            case conn_closing:
                conn_close(c);
                stop = true;
                break;
            }
                /* otherwise we have a real error, on which we close the connection */
        }

}

void dispatch_conn_new(int sfd, enum conn_states init_state, int event_flags,
                       int read_buffer_size, enum network_transport transport) {
    CQ_ITEM *item = cqi_new();
    char buf[1];
    if (item == NULL) {
        close(sfd);
        /* given that malloc failed this may also fail, but let's try */
        fprintf(stderr, "Failed to allocate memory for connection object\n");
        return ;
    }

    int tid = (last_thread + 1) % NUM_OF_THREADS;

    thread_cg *thread = thread_cgs + tid;

    last_thread = tid;

    item->sfd = sfd;
    item->init_state = init_state;
    item->event_flags = event_flags;
    item->read_buffer_size = read_buffer_size;
    item->transport = transport;

    //thread里面的new_conn_queue的属性原来这个才是每一个线程的连接管理池好么……
    cq_push(thread->new_conn_queue, item);

    // MEMCACHED_CONN_DISPATCH(sfd, thread->thread_id);
    buf[0] = 'c';
    if (write(thread->write_fd, buf, 1) != 1) {
        perror("Writing to thread notify pipe");
    }
}

static CQ_ITEM *cqi_new(void) {
    CQ_ITEM *item = NULL;
    pthread_mutex_lock(&cqi_freelist_lock);
    if (cqi_freelist) {
        item = cqi_freelist;
        cqi_freelist = item->next;
    }
    pthread_mutex_unlock(&cqi_freelist_lock);

    if (NULL == item) {
        int i;

        /* Allocate a bunch of items at once to reduce fragmentation */
        item = malloc(sizeof(CQ_ITEM) * ITEMS_PER_ALLOC);
        if (NULL == item) {
            STATS_LOCK();
            stats.malloc_fails++;
            STATS_UNLOCK();
            return NULL;
        }

        /*
         * Link together all the new items except the first one
         * (which we'll return to the caller) for placement on
         * the freelist.
         */
        for (i = 2; i < ITEMS_PER_ALLOC; i++)
            item[i - 1].next = &item[i];

        pthread_mutex_lock(&cqi_freelist_lock);
        item[ITEMS_PER_ALLOC - 1].next = cqi_freelist;
        cqi_freelist = &item[1];
        pthread_mutex_unlock(&cqi_freelist_lock);
    }

    return item;
}

static void cq_push(CQ *cq, CQ_ITEM *item) {
    item->next = NULL;

    pthread_mutex_lock(&cq->lock);
    if (NULL == cq->tail)
        cq->head = item;
    else
        cq->tail->next = item;
    cq->tail = item;
    pthread_mutex_unlock(&cq->lock);
}

static CQ_ITEM *cq_pop(CQ *cq) {
    CQ_ITEM *item;

    pthread_mutex_lock(&cq->lock);
    item = cq->head;
    if (NULL != item) {
        cq->head = item->next;
        if (NULL == cq->head)
            cq->tail = NULL;
    }
    pthread_mutex_unlock(&cq->lock);

    return item;
}

static void cq_init(CQ *cq) {
    pthread_mutex_init(&cq->lock, NULL);
    cq->head = NULL;
    cq->tail = NULL;
}

static void cqi_free(CQ_ITEM *item) {
    pthread_mutex_lock(&cqi_freelist_lock);
    item->next = cqi_freelist;
    cqi_freelist = item;
    pthread_mutex_unlock(&cqi_freelist_lock);
}

void accept_new_conns(const bool do_accept) {
    //这个函数貌似很重要呀，控制主线程现在还要不要accept连接了
    pthread_mutex_lock(&conn_lock);
    do_accept_new_conns(do_accept);
    pthread_mutex_unlock(&conn_lock);
}

void do_accept_new_conns(const bool do_accept) {
    conn *next;
    //update_event 表示将监听事件更改，0表示不监听？应该是吧。H

    for (next = listen_conn; next; next = next->next) {
        if (do_accept) {
            update_event(next, EV_READ | EV_PERSIST);
            if (listen(next->sfd, 32) != 0) {
                perror("listen");
            }
        }
        else {
            update_event(next, 0);
            if (listen(next->sfd, 0) != 0) {
                perror("listen");
            }
        }
    }

    if (do_accept) {
        STATS_LOCK();
        stats.accepting_conns = true;
        STATS_UNLOCK();
    } else {
        STATS_LOCK();
        stats.accepting_conns = false;
        stats.listen_disabled_num++;
        STATS_UNLOCK();
        allow_new_conns = false;
        //倒计时，重新调用accept_new_conns来将conns重新注册监听事件
        maxconns_handler(-42, 0, 0);
    }
}

static void maxconns_handler(const int fd, const short which, void *arg) {
    struct timeval t = {.tv_sec = 0, .tv_usec = 10000};

    if (fd == -42 || allow_new_conns == false) {
        /* reschedule in 10ms if we need to keep polling */
        evtimer_set(&maxconnsevent, maxconns_handler, 0);
        event_base_set(main_base, &maxconnsevent);
        evtimer_add(&maxconnsevent, &t);
    } else {
        evtimer_del(&maxconnsevent);
        accept_new_conns(true);
    }
}

static bool update_event(conn *c, const int new_flags) {
    assert(c != NULL);

    
    if (c->ev_flags == new_flags)
        return true;
    pthread_t tid = pthread_self();
    if (tid == dispatcher_thread.thread_id)
    {
        struct event_base *base = c->event.ev_base;
        if (event_del(&c->event) == -1) return false;    
        event_set(&c->event, c->sfd, new_flags, master_event_handler, (void *)c);
        event_base_set(base, &c->event);
        c->ev_flags = new_flags;
        if (event_add(&c->event, 0) == -1) return false;
        return true;
    }else{
        struct event_base *base = bufferevent_get_base(c->buf_ev);
        bufferevent_free(c->buf_ev);
        c->buf_ev = bufferevent_new(c->sfd, event_handler, buf_write_callback, buf_error_callback, (void * )c);
        bufferevent_base_set(base, c->buf_ev);
        
        c->ev_flags = new_flags;
        if (bufferevent_enable(c->buf_ev, new_flags) == -1) return false;
        return true;
    }    
    //这个也得改成bufferevent的形式
/*

    event_set(&c->event, c->sfd, new_flags, event_handler, (void *)c);
    event_base_set(base, &c->event);
    event_add(&c->event, 0);


    c->buf_ev = bufferevent_new(c->sfd, event_handler, event_handler, NULL, (void * )c);
    bufferevent_base_set(base, c->buf_ev);
    bufferevent_enable(c->buf_ev, new_flags);

    c->ev_flags = new_flags;
    if (event_add(&c->event, 0) == -1) return false;
    return true;
*/
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
    pthread_t tid = pthread_self();
    if (tid == dispatcher_thread.thread_id){
        event_del(&c->event);
    }else{
        bufferevent_free(c->buf_ev);
    }
    
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
        if ( !(new_c = (conn *)calloc(1, sizeof(conn))) )
        {
            perror("alloc conn fail");
            exit(1);
        }

        new_c->sfd = sfd;
        conns[sfd] = new_c;
    }

    new_c->state = init_state;
    //以上是连接的初始化，接下来是连接的事件注册
    
     pthread_t tid = pthread_self();
     if (tid == dispatcher_thread.thread_id)
     {
        event_set(&new_c->event, sfd, event_flags, master_event_handler, (void*)new_c);
        event_base_set(base, &new_c->event);
        event_add(&new_c->event, 0);
     }else{
        new_c->buf_ev = bufferevent_new(sfd, event_handler, event_handler, buf_error_callback, (void *)new_c);
        bufferevent_base_set(base, new_c->buf_ev);
        bufferevent_enable(new_c->buf_ev, event_flags);
     }
     
     
     /*
     想着想着还是决定改成用bufferevent。
     因为master线程与slave用的是同一个函数来做处理event_handler -> drive_machine，
     所以master线程的accept也需要改为用bufferevent，而且event_handler以及drive_machine也得改
     还有struct conn的event也得改呀，得改成bufferevent类型呢

     草草草！bufferevent在socket还没有连接上的时候是用不了了的，好么！完全不会有什么读写操作，只有error回调
     查了一个下午好么！！！
     */

     /*
    new_c->buf_ev = bufferevent_new(sfd, event_handler, event_handler, buf_error_callback, (void *)new_c);
    bufferevent_base_set(base, new_c->buf_ev);
    bufferevent_enable(new_c->buf_ev, event_flags);
    */
    
    return new_c;
}

static void conn_init(void) {
    /* We're unlikely to see an FD much higher than maxconns. */
    int next_fd = dup(1);
    int headroom = 10;      /* account for extra unexpected open FDs */
    struct rlimit rl;

    max_fds = MAX_CONNS + headroom + next_fd;

    /* But if possible, get the actual highest FD we can possibly ever see. */
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        max_fds = rl.rlim_max;
    } else {
        fprintf(stderr, "Failed to query maximum file descriptor; "
                "falling back to maxconns\n");
    }

    close(next_fd);

    if ((conns = calloc(max_fds, sizeof(conn *))) == NULL) {
        fprintf(stderr, "Failed to allocate connection structures\n");
        /* This is unrecoverable so bail out early. */
        exit(1);
    }
}

static void stats_init(void) {
    stats.curr_items = stats.total_items = stats.curr_conns = stats.total_conns = stats.conn_structs = 0;
    stats.get_cmds = stats.set_cmds = stats.get_hits = stats.get_misses = stats.evictions = stats.reclaimed = 0;
    stats.touch_cmds = stats.touch_misses = stats.touch_hits = stats.rejected_conns = 0;
    stats.malloc_fails = 0;
    stats.curr_bytes = stats.listen_disabled_num = 0;
    stats.hash_power_level = stats.hash_bytes = stats.hash_is_expanding = 0;
    stats.expired_unfetched = stats.evicted_unfetched = 0;
    stats.slabs_moved = 0;
    stats.lru_maintainer_juggles = 0;
    stats.accepting_conns = true; /* assuming we start in this state. */
    stats.slab_reassign_running = false;
    stats.lru_crawler_running = false;
    stats.lru_crawler_starts = 0;

    /* make the time we started always be 2 seconds before we really
       did, so time(0) - time.started is never zero.  if so, things
       like 'settings.oldest_live' which act as booleans as well as
       values are now false in boolean context... */
    // process_started = time(0) - ITEM_UPDATE_INTERVAL - 2;    // stats_prefix_init();
}

int main(int argc, char** argv){    
    /*
        主线程主体流程：
        conn_init;
        thread_init;
        还得有一个
        stat_init
        stats的结构是用来记录当前的状态的，stats是一个静态变量
    
        server_socket 请求，分发
        注册事件loop
    */
    int retval;
    main_base = event_init();
    FILE *portnumber_file = NULL;
    portnumber_file = fopen("/tmp/portnumber.file", "a");

    stats_init();
    conn_init();
    thread_init(NUM_OF_THREADS);

    server_socket("127.0.0.1", SERVER_PORT, tcp_transport, portnumber_file);

    if (event_base_loop(main_base, 0) != 0) {
	printf("event_base_loop error");
        retval = EXIT_FAILURE;
    }
    return retval;

    exit(0);

}
