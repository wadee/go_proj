#include <event.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>


#define SERVER_PORT 8081
#define EX_OSERR 71
#define IS_UDP(x) (x == udp_transport)
#define ITEM_UPDATE_INTERVAL 60
int debug = 0;

#define ITEMS_PER_ALLOC 64
#define DATA_BUFFER_SIZE 2048
#define NUM_OF_THREADS 4
#define MAX_CONNS 10

typedef struct {
    pthread_t thread_id;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
} LIBEVENT_DISPATCHER_THREAD;
/* An item in the connection queue. */
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

typedef struct conn_queue_item CQ_ITEM;
struct conn_queue_item {
    int               sfd;
    enum conn_states  init_state;
    int               event_flags;
    int               read_buffer_size;
    enum network_transport     transport;
    CQ_ITEM          *next;
};

typedef struct conn_queue CQ;
struct conn_queue {
    CQ_ITEM *head;
    CQ_ITEM *tail;
    pthread_mutex_t lock;
};



struct stats {
    pthread_mutex_t mutex;
    unsigned int  curr_items;
    unsigned int  total_items;
    uint64_t      curr_bytes;
    unsigned int  curr_conns;
    unsigned int  total_conns;
    uint64_t      rejected_conns;
    uint64_t      malloc_fails;
    unsigned int  reserved_fds;
    unsigned int  conn_structs;
    uint64_t      get_cmds;
    uint64_t      set_cmds;
    uint64_t      touch_cmds;
    uint64_t      get_hits;
    uint64_t      get_misses;
    uint64_t      touch_hits;
    uint64_t      touch_misses;
    uint64_t      evictions;
    uint64_t      reclaimed;
    time_t        started;          /* when the process was started */
    bool          accepting_conns;  /* whether we are currently accepting */
    uint64_t      listen_disabled_num;
    unsigned int  hash_power_level; /* Better hope it's not over 9000 */
    uint64_t      hash_bytes;       /* size used for hash tables */
    bool          hash_is_expanding; /* If the hash table is being expanded */
    uint64_t      expired_unfetched; /* items reclaimed but never touched */
    uint64_t      evicted_unfetched; /* items evicted but never touched */
    bool          slab_reassign_running; /* slab reassign in progress */
    uint64_t      slabs_moved;       /* times slabs were moved around */
    uint64_t      lru_crawler_starts; /* Number of item crawlers kicked off */
    bool          lru_crawler_running; /* crawl in progress */
    uint64_t      lru_maintainer_juggles; /* number of LRU bg pokes */
};

typedef struct{
    int read_fd;
    int write_fd;
    pthread_t thread_id;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
    struct event notify_event; 
    short  ev_flags; //H
    struct conn_queue *new_conn_queue;
} thread_cg;

typedef struct conn conn;
struct conn {
    //一个连接里面应该有什么属性呢
    //首先应该是文件描述符
    int sfd;
    //此连接目前的状态，因为我们需要根据不同的状态进行不同的处理
    int state;
    //struct event event. libevent注册事件,放在连接体本身作为一个属性存起来
    struct event event;
    //收到的数据
    char *req;
    short  ev_flags;
    struct bufferevent *buf_ev;

    thread_cg *thread;
    //libevent 的回调函数的第二个参数，表示触发的事件
    short  which;
    //连接管理，连接池嘛，所以可以是一个链表，之后需要涉及到链表的增删改查
    conn * next;
};


static void *worker_libevent(void *arg);
int setup_thread(thread_cg * t_cg);
static void start_worker(void *(*func)(void *), void *arg);
int thread_init(int num_of_threads);
static int new_socket(struct addrinfo *ai);
static void thread_libevent_process(int fd, short which, void *arg);
static int server_socket(const char *interface,
                         int port,
                         enum network_transport transport,
                         FILE *portnumber_file);
void event_handler(struct bufferevent *incoming, void *arg);

void master_event_handler(const int fd, const short which, void *arg);

void drive_machine(conn* c, struct bufferevent * incoming);
void dispatch_conn_new(int sfd, enum conn_states init_state, int event_flags,
                       int read_buffer_size, enum network_transport transport);
static void cq_init(CQ *cq);
static CQ_ITEM *cqi_new(void);
static void cq_push(CQ *cq, CQ_ITEM *item);
static CQ_ITEM *cq_pop(CQ *cq);
static void cqi_free(CQ_ITEM *item);
void accept_new_conns(const bool do_accept);
void do_accept_new_conns(const bool do_accept);
static void maxconns_handler(const int fd, const short which, void *arg);
static bool update_event(conn *c, const int new_flags);
static void conn_close(conn *c);
static void conn_set_state(conn *c, enum conn_states state);
conn *conn_new(const int sfd, enum conn_states init_state,
                const int event_flags,
                const int read_buffer_size, enum network_transport transport,
                struct event_base *base);
static void conn_init(void);
static void stats_init(void);

