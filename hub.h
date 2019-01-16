
#ifndef __HUB_INVOKE__
#define __HUB_INBOKE__

#define MAX_BROKER_NUM  12

#define LISTEN_ADDR_XSUB    "ipc://interhub_xsub.ipc"
#define LISTEN_ADDR_XPUB    "ipc://interhub_xpub.ipc"

typedef struct broker_ctx {
    int id;
    char used;
    char broker_id[MQ_MAX_NAME];
    char broker_name[MQ_MAX_NAME];
    char broker_host[MQ_MAX_HOSTNAME];
    pthread_t thread_id;

    struct event_base *evbase_thrd;

    void *zmq_context;
    void *zmq_dealer;
    void *zmq_pub;
    void *zmq_sub;

    /* node tree for broker-service */
    GNode *root_node;

    /* config sync proc LSYNCD pid */
    pid_t lsyncd_pid;
} broker_ctx_t;

typedef struct main_ctx {
    /* node tree for broker-status */
    GNode *root_node;

    /* main loop */
    struct event_base *evbase_main;

    /* xsub & xpub thrd */
    void *zmq_xsub;
    void *zmq_xpub;

    /* broker regi thrd */
    char *argv_address;
    char interhub_address[128];
    void *zmq_sub_from_broker;
    void *zmq_pub_to_xsub;

    /* broker context */
    broker_ctx_t broker_ctx[MAX_BROKER_NUM];
} main_ctx_t;

typedef struct node_data {
    int  node_type;
    char host[128];
    char name[128];

    time_t hb_rcv_time;
    mq_sndrcv_t sndrcv;

    broker_ctx_t *broker_ctx;
} node_data_t;

typedef struct node_data_small {
    int node_type;
    char broker_id[128];
    char svc_exist[128];

    time_t hb_rcv_time;
} node_data_small_t;

/* ------------------------- hub.c --------------------------- */
void    set_broker_slot(main_ctx_t *main_ctx, broker_ctx_t *broker_ctx, int id, mq_regi_t *svc_regi);
void    chk_small_tree(broker_ctx_t *broker_ctx, mq_regi_t *svc_regi);
void    relay_to_broker(broker_ctx_t *broker_ctx, const char *msg_type);
void    broker_sub_callback(evutil_socket_t fd, short what, void *arg);
void    create_broker_ctx(void *arg);
void    remove_smalltree_callback(evutil_socket_t fd, short what, void *arg);
char    *find_svc_exist_tree(broker_ctx_t *broker_ctx, char *svc_name) ;
void    find_svc_and_relay(broker_ctx_t *broker_ctx, void *sock_from);
void    find_broker_and_relay(broker_ctx_t *broker_ctx, void *sock_from);
void    interhub_callback(evutil_socket_t fd, short what, void *arg);
void    create_thread_action(evutil_socket_t fd, short what, void *arg);
void    start_broker_polling(main_ctx_t *main_ctx, broker_ctx_t *broker_ctx);
broker_ctx_t    *create_new_broker(main_ctx_t *main_ctx, mq_regi_t *svc_regi);
void    chk_tree_callback(GNode *node, gpointer input_arg);
void    update_node_info(node_data_t *node_data, mq_regi_t *svc_regi);
GNode   *NewNode(int req_type, mq_regi_t *svc_regi, broker_ctx_t *new_broker);
GNode   *NewNodeSmall(node_data_small_t *small_node, int node_type);
GNode   *AddNode(GNode *parent, GNode *child, GNode *looser_brother);
void    broker_release(evutil_socket_t fd, short what, void *arg);
void    RemoveNode(GNode *node);
void    chk_tree(main_ctx_t *main_ctx, mq_regi_t *svc_regi, int req_type, broker_ctx_t *new_broker);
int     chk_svc_broker(main_ctx_t *main_ctx, mq_regi_t *svc_regi);
int     regi_action(main_ctx_t *main_ctx, const char *action, const char *peer_address);
void    svcregi_callback(evutil_socket_t fd, short what, void *arg);
void    draw_node_callback(GNode *node, gpointer input_arg);
void    traverse_dot_make(GNode *node, FILE *fp);
void    maketree_callback(evutil_socket_t fd, short what, void *arg);
void    remove_node_callback(GNode *node, gpointer input_arg);
void    traverse_dot_remove(GNode *node, time_t *current);
void    removetree_callback(evutil_socket_t fd, short what, void *arg);
void    initialize(main_ctx_t *main_ctx);
void    main_callback(evutil_socket_t fd, short what, void *arg);
int     main(int argc, char **argv);

#endif
