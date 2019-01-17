
#ifndef __MQ_INVOKE__
#define __MQ_INVOKE__

#define TM_TICK         1000000 // every 1 sec
#define TM_INTERVAL     10000   // every 10 ms check
#define TM_POLLING      1000    // every 1 ms check

#define MAX_BUFF_SIZE   1024 * 12
#define MAX_LOG_SIZE    1024

#define MQ_REGI_PORT    9000
#define MQ_ROUTE_PORT   9001
#define MQ_ROUTE_ADDR   "tcp://0.0.0.0:9001"
#define MQ_HUB_PORT     9002
/* for broker */
#define LISTEN_ADDR_SUB     "tcp://*:9000"  // related to MQ_REGI_PORT
#if 0
#define LISTEN_ADDR_PUB     "ipc://BROKER_PUBL.ipc"
#define LISTEN_ADDR_ROUTER  "ipc://BROKER_ROUTER.ipc"
#else
/* not work because they need single context to communication */
// --> modify done good work
#define LISTEN_ADDR_PUB     "inproc://BROKER_PUBL.inproc"
#define LISTEN_ADDR_ROUTER  "inproc://BROKER_ROUTER.inproc"
#endif

#define MQ_REG "svc_reg"
#define MQ_REQ "svc_req"
#define MQ_RES "svc_res"
#define MQ_REQ_INTERHUB "svc_req_interhub"
#define MQ_RES_INTERHUB "svc_res_interhub"
#define MQ_LOG "svc_log"
#define MQ_CONF "svc_conf"

#define INTERHUB_NAME "inter_hub"

#define MAX_SVC_NUM     128

#define SHAPE_BROKER "style=\"rounded,filled,radial\" fillcolor=\"white:powderblue\" gradientangle=135"
#define SHAPE_NOSEND_NORECV "style=\"rounded,filled,radial\" fillcolor=\"white:lightgrey\" gradientangle=135"
#define SHAPE_SENDEXIST_NORECV "style=\"rounded,filled\" fillcolor=\"red:orange\" gradientangle=90"
#define SHAPE_SENDEXIST_SMALLRECV "style=\"rounded,filled\" fillcolor=\"red:yellow\" gradientangle=90"
#define SHAPE_NORMAL "style=rounded"

typedef enum node_type {
    NT_INTERHUB = 0,
    NT_BROKER, 
    NT_SVC,
    NT_PROC
} node_type_t;

typedef enum conn_type {
    CONN_IPC = 0,
    CONN_TCP
} conn_type_t;

/* TODO!!! this tag is per-msg budden reduce size as possible */
#define MQ_MAX_HOSTNAME 24
#define MQ_MAX_SVCNAME 24
#define MQ_MAX_NAME 48
typedef struct mq_tag {
    bool via_interhub;
    char broker[MQ_MAX_NAME];
    char svc[MQ_MAX_SVCNAME];           /* svc name */
    char name[MQ_MAX_NAME];             /* proc name */
} mq_tag_t;

typedef struct mq_sndrcv {
    int send_cnt;
    int recv_cnt;
    int send_byte;
    int recv_byte;
} mq_sndrcv_t;

typedef struct mq_regi {
    char broker_host[MQ_MAX_HOSTNAME];
    char broker_name[MQ_MAX_NAME];
    char svc_host[MQ_MAX_HOSTNAME];         /* proc exist host */

    mq_tag_t tag;
    mq_sndrcv_t sndrcv;

    short port;
    short conn_type;

    time_t current;
    time_t before;
} mq_regi_t;

typedef struct chk_tree_arg {
    mq_regi_t *svc_regi;
    int type;

    int find;
    int same_node_exist; 
    int insert_before_this; 
    GNode *find_node;
} chk_tree_arg_t;

typedef struct client_sock {
    short my_svc_port;

    int via_ipc;
    char tcp_address[256];

    mq_sndrcv_t sndrcv;

    void *zmq_context;
    void *zmq_pub;
    void *zmq_sub;
    void *zmq_pull;
    void *zmq_push;

    void *zmq_conf;     // subscribe config change
} client_sock_t;

#endif /* endof __MQ_INVOKE__ */
