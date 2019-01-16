
#ifndef __BROKER_INVOKE__
#define __BROKER_INVOKE__

typedef enum sock_type {
    ZQ_DEAL = 0,
    ZQ_PULL,
    ZQ_PUSH,
    ZQ_PUB,
    ZQ_SUB
} sock_type_t;

char sock_type_str[][128] = {
    "zq_deal",
    "zq_pull",
    "zq_push",
    "zq_pub",
    "zq_sub",
};

typedef struct relay_arg {
    void *sock_from;
    void *sock_to;
    char relay_name[128];
} relay_arg_t;

typedef struct service_ctx {
    int id;
    char used;

    char svc_name[128];
    short svc_start_port;

    void *zmq_context;
    void *dealer;
    void *push;
    void *pull;
    void *sub;
    void *pub;

    struct event_base *evbase;
    struct event *ev_dealer_to_push;
    struct event *ev_pull_to_dealer;
    struct event *ev_sub_to_pub;
    relay_arg_t relay_dealer;
    relay_arg_t relay_pull;
    relay_arg_t relay_sub;
} service_ctx_t;

typedef struct main_ctx {
    /* node tree */
    GNode *root_node;

    /* regi thread */
    void *zmq_sub;
    char interhub_address[128];
    void *zmq_pub_to_interhub;

    /* router thread */
    void *zmq_pub;
    void *zmq_router;

    /* service thread */
    struct event_base *evbase;
    void *zmq_context;
    service_ctx_t svc_ctx[MAX_SVC_NUM];

    /* for config watch (in router thread) */
    // TODO!!! must close & free these FD
    int inotifyFd;
    int wd;
} main_ctx_t;

/* tree dot */
typedef struct node_data {
    int  node_type;
    char host[128];
    char name[128];
    short svc_start_port;
    short via_type;

    time_t hb_rcv_time;
    mq_sndrcv_t sndrcv;

    service_ctx_t *svc_ctx;
} node_data_t;

/* ------------------------- broker.c --------------------------- */
void    connect_svc_slot(service_ctx_t *svc_ctx);
service_ctx_t   *set_svc_slot(main_ctx_t *main_ctx, service_ctx_t *svc_ctx, int id, mq_regi_t *svc_regi);
void    svc_release(evutil_socket_t fd, short what, void *arg);
void    mq_relay_callback(evutil_socket_t fd, short what, void *arg);
void    mq_relay_callback_sub_to_pub(evutil_socket_t fd, short what, void *arg);
void    start_svc_polling(evutil_socket_t fd, short what, void *arg);
service_ctx_t   *create_new_dealer(main_ctx_t *main_ctx, mq_regi_t *svc_regi);
GNode   *NewNode(int req_type, mq_regi_t *svc_regi, service_ctx_t *new_svc);
GNode   *AddNode(GNode *parent, GNode *child, GNode *looser_brother);
void    RemoveNode(GNode *node);
void    chk_tree_callback(GNode *node, gpointer input_arg);
void    update_node_info(node_data_t *node_data, mq_regi_t *svc_regi);
void    chk_tree(main_ctx_t *main_ctx, mq_regi_t *svc_regi, int req_type, service_ctx_t *new_svc);
int     chk_svc_dealer(main_ctx_t *main_ctx, mq_regi_t *svc_regi);
int     regi_action(main_ctx_t *main_ctx, const char *action, const char *peer_address);
void    svcregi_callback(evutil_socket_t fd, short what, void *arg);
void    traverse_dot_make(GNode *root, FILE *fp);
void    maketree_callback(evutil_socket_t fd, short what, void *arg);
void    remove_node_callback(GNode *node, gpointer input_arg) ;
void    traverse_dot_remove(GNode *node, time_t *current);
void    removetree_callback(evutil_socket_t fd, short what, void *arg);
void    relay_req_to_interhub(main_ctx_t *main_ctx, char *svc_host);
void    relay_res_to_interhub(main_ctx_t *main_ctx);
void    drain_left_part_message(void *zmq_sock);
void    router_callback(evutil_socket_t fd, short what, void *arg);
void    publish_conf_from_file(main_ctx_t *main_ctx, char *filename, char *svc, char *conf);
void    check_inotify_event(main_ctx_t *main_ctx, struct inotify_event *ievent);
void    create_config_watch(struct event_base *evbase, main_ctx_t *main_ctx);
void    initialize(main_ctx_t *main_ctx);
void    main_callback(evutil_socket_t fd, short what, void *arg);
int     main(int argc, char **argv);

#endif
