#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <event2/thread.h>
#include <event.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/dns.h>

#include <glib-2.0/gmodule.h>

#include "lib_wrapzmq.h"
#include "zhelpers.h"
#include "mq.h"
#include "hub.h"

extern char *__progname;

void set_broker_slot(main_ctx_t *main_ctx, broker_ctx_t *broker_ctx, int id, mq_regi_t *svc_regi)
{
    broker_ctx->used = true;
    broker_ctx->id = id;
    strcpy(broker_ctx->broker_host, svc_regi->broker_host);
    strcpy(broker_ctx->broker_name, svc_regi->broker_name);
    sprintf(broker_ctx->broker_id, "%s_%s", svc_regi->broker_host, svc_regi->broker_name);
    fprintf(stderr, "log] broker id is (id:%d name:%s), len (%d)\n", 
            broker_ctx->id, broker_ctx->broker_id, strlen(broker_ctx->broker_id));
}

// TODO may be it change to bsearch algorithm !!!
void chk_small_tree(broker_ctx_t *broker_ctx, mq_regi_t *svc_regi)
{
    node_data_small_t broker_svc = {0,};
    sprintf(broker_svc.broker_id, "%s_%s", svc_regi->broker_host, svc_regi->broker_name);
    sprintf(broker_svc.svc_exist, "%s", svc_regi->tag.svc);

    /* if any broker exist, create broker & service */
    unsigned int broker_num = g_node_n_children(broker_ctx->root_node);
    if (broker_num == 0) {
        GNode *new_broker = NewNodeSmall(&broker_svc, NT_BROKER);
        g_node_insert(broker_ctx->root_node, -1, new_broker);
        GNode *new_svc = NewNodeSmall(&broker_svc, NT_SVC);
        g_node_insert(new_broker, -1, new_svc);
        return;
    }
    /* find broker, if find update time */
    GNode *find_broker = NULL;
    for (int i = 0; i < broker_num; i++) {
        GNode *nth_broker = g_node_nth_child(broker_ctx->root_node, i);
        node_data_small_t *data = (node_data_small_t *)nth_broker->data;
        if (!strcmp(data->broker_id, broker_svc.broker_id)) {
            time(&data->hb_rcv_time);
            find_broker = nth_broker;
            break;
        }
    }
    /* if broker not exist, create broker */
    if (find_broker == NULL) {
        GNode *new_broker = NewNodeSmall(&broker_svc, NT_BROKER);
        g_node_insert(broker_ctx->root_node, -1, new_broker);
        find_broker = new_broker;
    }

    /* if broker-related-service not exist, create one */
    unsigned int svc_exist_num = g_node_n_children(find_broker);
    if (svc_exist_num == 0) {
        GNode *new_svc = NewNodeSmall(&broker_svc, NT_SVC);
        g_node_insert(find_broker, -1, new_svc);
        return;
    }
    /* find service, if find update time */
    GNode *find_svc = NULL;
    for (int i = 0; i < svc_exist_num; i++) {
        GNode *nth_svc = g_node_nth_child(find_broker, i);
        node_data_small_t *data = (node_data_small_t *)nth_svc->data;
        if (!strcmp(data->svc_exist, broker_svc.svc_exist)) {
            time(&data->hb_rcv_time);
            find_svc = nth_svc;
            break;
        }
    }
    /* if svc not exist, create svc */
    if (find_svc == NULL) {
        GNode *new_svc = NewNodeSmall(&broker_svc, NT_SVC);
        g_node_insert(find_broker, -1, new_svc);
        return;
    }
}

void relay_to_broker(broker_ctx_t *broker_ctx, const char *msg_type)
{
    void *zmq_sub = broker_ctx->zmq_sub;
    void *zmq_dealer = broker_ctx->zmq_dealer;

    // receive my_topic(id)-> svc-> from-> message
    char tgsvc_name[128] = {0,};
    mq_tag_t from_tag = {0,};

     if (zmq_recv(zmq_sub, tgsvc_name, sizeof(tgsvc_name), ZMQ_DONTWAIT) >= 0 &&
        zmq_recv(zmq_sub, &from_tag, sizeof(mq_tag_t), ZMQ_DONTWAIT) >= 0) {
        //fprintf(stderr, "dbg] tgsvc %s, from tag %s\n", tgsvc_name, from_tag.name);
        if (zmq_send(zmq_dealer, msg_type, strlen(msg_type), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
            zmq_send(zmq_dealer, tgsvc_name, strlen(tgsvc_name), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
            zmq_send(zmq_dealer, &from_tag, sizeof(mq_tag_t), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0) {
            //fprintf(stderr, "dbg] we send %s %s %s\n", msg_type, tgsvc_name, from_tag.name);
            relay_snd_more(zmq_sub, zmq_dealer, NULL);
        }
    }
}

void broker_sub_callback(evutil_socket_t fd, short what, void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;
    void *zmq_sub = broker_ctx->zmq_sub;

    int len = 0;
    char action[1024] = {0,};

    /* subscribe receive only TOPIC that subscribed */
    while ((len = zmq_recv(zmq_sub, action, sizeof(action), ZMQ_DONTWAIT)) >= 0) {
        if (!strcmp(action, MQ_REG)) {
            mq_regi_t svc_regi = {0,};
            if (zmq_recv(zmq_sub, &svc_regi, sizeof(mq_regi_t), ZMQ_DONTWAIT) >= 0) {
                chk_small_tree(broker_ctx, &svc_regi);
            }
        } else {
            char msg_type[128] = {0,};
            //fprintf(stderr, "dbg] subscribe topic [%s]\n", action);
            if (zmq_recv(zmq_sub, msg_type, sizeof(msg_type), ZMQ_DONTWAIT) >= 0) {
                //fprintf(stderr, "dbg] for broker thread msg [%s]\n", msg_type);
                if (!strcmp(msg_type, MQ_REQ_INTERHUB))
                    //relay_to_broker(broker_ctx, MQ_REQ_INTERHUB);
                    relay_to_broker(broker_ctx, MQ_REQ); // CAUTION!!! change to normal req
                else if (!strcmp(msg_type, MQ_RES_INTERHUB))
                    relay_to_broker(broker_ctx, MQ_RES); // CAUTION!!! change to normal resp
            }
        }
    }
}

void create_broker_ctx(void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;

    /* pub : response will be send by this */
    broker_ctx->zmq_pub = zmq_socket(broker_ctx->zmq_context, ZMQ_PUB);
    zero_linger(broker_ctx->zmq_pub);
    zmq_connect(broker_ctx->zmq_pub, LISTEN_ADDR_XSUB);

    /* sub : receive svc request by this */
    broker_ctx->zmq_sub = zmq_socket(broker_ctx->zmq_context, ZMQ_SUB);
    zero_linger(broker_ctx->zmq_sub);
    // TODO!!! SUBSCRIBE TOPIC!!!! that svc_regi, my name, and ... 
    zmq_setsockopt(broker_ctx->zmq_sub, ZMQ_SUBSCRIBE, MQ_REG, strlen(MQ_REG));
    zmq_setsockopt(broker_ctx->zmq_sub, ZMQ_SUBSCRIBE, broker_ctx->broker_id, strlen(broker_ctx->broker_id));
    fprintf(stderr, "log] now this thread will subscribe [%s] [%s]\n", MQ_REG, broker_ctx->broker_id);
    zmq_connect(broker_ctx->zmq_sub, LISTEN_ADDR_XPUB);

    /* dealer : drain all svc req from router */
    broker_ctx->zmq_dealer = zmq_socket(broker_ctx->zmq_context, ZMQ_DEALER);
    zero_linger(broker_ctx->zmq_dealer);
    zmq_setsockopt(broker_ctx->zmq_dealer, ZMQ_IDENTITY, INTERHUB_NAME, strlen(INTERHUB_NAME));
    char addr_str[256] = {0,};
    sprintf(addr_str, "tcp://%s:%d", broker_ctx->broker_host, MQ_ROUTE_PORT);
    zmq_connect(broker_ctx->zmq_dealer, addr_str);
    fprintf(stderr, "log] dealer for [%s:%s] connect to (%s)\n", broker_ctx->broker_host, broker_ctx->broker_name, addr_str);
}

#define TMCHK_BROKER_SVC 3
void remove_smalltree_callback(evutil_socket_t fd, short what, void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;
    time_t current;
    time(&current);

    unsigned int broker_num = g_node_n_children(broker_ctx->root_node);
    for (int iii = 0; iii < broker_num; iii++) {
        GNode *nth_broker = g_node_nth_child(broker_ctx->root_node, iii);
        node_data_small_t *data = (node_data_small_t *)nth_broker->data;
        if (current - data->hb_rcv_time > TMCHK_BROKER_SVC) {
            fprintf(stderr, "log] Small Node [broker %s] destroyed, in broker[%s]\n", 
                    data->broker_id, broker_ctx->broker_name);
            g_node_destroy(nth_broker);
            // CAUTION!!! nth_broker will loose reference, must return
            return;
        }

        unsigned int svc_num = g_node_n_children(nth_broker);
        for (int jjj = 0; jjj < svc_num; jjj++) {
            GNode *nth_svc = g_node_nth_child(nth_broker, jjj);
            node_data_small_t *data = (node_data_small_t *)nth_svc->data;
            if (current - data->hb_rcv_time > TMCHK_BROKER_SVC) {
                fprintf(stderr, "log] Small Node [broker %s | svc %s] destroyed, in broker[%s]\n", 
                        data->broker_id, data->svc_exist, broker_ctx->broker_name);
                g_node_destroy(nth_svc);
                // CAUTION!!! nth_svc will loose reference, must return
                return;
            }
        }
    }
}

char *find_svc_exist_tree(broker_ctx_t *broker_ctx, char *svc_name) 
{
    unsigned int broker_num = g_node_n_children(broker_ctx->root_node);

    for (int iii = 0; iii < broker_num; iii++) {

        GNode *nth_broker = g_node_nth_child(broker_ctx->root_node, iii);
        node_data_small_t *broker_data = (node_data_small_t *)nth_broker->data;
        if (!strcmp(broker_data->broker_id, broker_ctx->broker_id))
            continue; // this node is my broker data, skip

        unsigned int svc_num = g_node_n_children(nth_broker);
        for (int jjj = 0; jjj < svc_num; jjj++) {
            GNode *nth_svc = g_node_nth_child(nth_broker, jjj);
            node_data_small_t *svc_data = (node_data_small_t *)nth_svc->data;
            if (!strcmp(svc_name, svc_data->svc_exist)) {
                fprintf(stderr, "INTERHUB SAY] don't worry we find svc [%s] in broker [%s]\n",
                        svc_name, broker_data->broker_id);
                return broker_data->broker_id;
            }
        }
    }

    fprintf(stderr, "err] (%s) cant find svc %s in anywhere\n", __func__, svc_name);

    return NULL;
}

void find_svc_and_relay(broker_ctx_t *broker_ctx, void *sock_from)
{
    void *zmq_pub = broker_ctx->zmq_pub;

    int recv_byte = 0;
    char tgsvc_name[128] = {0,};
    char *broker_id = NULL;
    mq_tag_t from_tag = {0,};

    if (((recv_byte = zmq_recv(sock_from, tgsvc_name, sizeof(tgsvc_name), ZMQ_DONTWAIT)) >= 0) &&
        ((broker_id = find_svc_exist_tree(broker_ctx, tgsvc_name)) != NULL) &&
        ((recv_byte = zmq_recv(sock_from, &from_tag, sizeof(mq_tag_t), ZMQ_DONTWAIT)) >= 0)) {
        sprintf(from_tag.broker, "%s", broker_ctx->broker_id);
        if (zmq_send(zmq_pub, broker_id, strlen(broker_id), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
            zmq_send(zmq_pub, MQ_REQ_INTERHUB, strlen(MQ_REQ_INTERHUB), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
            zmq_send(zmq_pub, tgsvc_name, strlen(tgsvc_name), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
            zmq_send(zmq_pub, &from_tag, sizeof(mq_tag_t), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0) {
            relay_snd_more(sock_from, zmq_pub, NULL);
        }
    }
}

void find_broker_and_relay(broker_ctx_t *broker_ctx, void *sock_from)
{
    void *zmq_pub = broker_ctx->zmq_pub;

    int recv_byte = 0;
    char broker_id[128] = {0,};

    if ((recv_byte = zmq_recv(sock_from, broker_id, sizeof(broker_id), ZMQ_DONTWAIT)) >= 0) {
    //fprintf(stderr, "dbg] (%s) broker id is (%s)\n", __func__, broker_id);
        if (zmq_send(zmq_pub, broker_id, strlen(broker_id), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
            zmq_send(zmq_pub, MQ_RES_INTERHUB, strlen(MQ_REQ_INTERHUB), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0) {
            relay_snd_more(sock_from, zmq_pub, NULL);
        }
    }
}

void interhub_callback(evutil_socket_t fd, short what, void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;
    void *zmq_dealer = broker_ctx->zmq_dealer;

    char msg_type[128] = {0,};

    while (zmq_recv(zmq_dealer, msg_type, sizeof(msg_type), ZMQ_DONTWAIT) >= 0) {
        if (!strcmp(msg_type, MQ_REQ)) {
            find_svc_and_relay(broker_ctx, zmq_dealer);
        } else if (!strcmp(msg_type, MQ_RES_INTERHUB)) {
            find_broker_and_relay(broker_ctx, zmq_dealer);
        } else if (!strcmp(msg_type, MQ_LOG)) {
            char log_buffer[MAX_LOG_SIZE] = {0,};
            if (zmq_recv(zmq_dealer, log_buffer, MAX_LOG_SIZE, ZMQ_DONTWAIT) >= 0)
                fprintf(stderr, "%s\n", log_buffer);
        }
    }
}
#define LSYNC_SRC_DIR_CMD "mkdir --p %s/conf/%s"
#define LSYNCD_RUN_CMD "lsyncd -insist -delay 1 -log scarce -pidfile lsyncd_%s.pid -rsync %s/conf/%s %s://$HOME/conf"
#define LSYNCD_PID_CMD "cat %s/lsyncd_%s.pid"
#define LSYNCD_PID_DEL "rm -f %s/lsyncd_%s.pid"
#define LSYNCD_KILL_CMD "kill -9 %d"

int get_lsyncd_pid(char *ipaddr)
{
    FILE *fp = NULL;
    char cmd[1024] = {0,};
    char buff[1024] = {0,};

    sprintf(cmd, LSYNCD_PID_CMD, get_current_dir_name(), ipaddr);

    if ((fp = popen(cmd, "r")) == NULL)
        return (-1);

    if (fgets(buff, 1024, fp) == NULL)
        return (-1);
    else
        pclose(fp);

    sprintf(cmd, LSYNCD_PID_DEL, get_current_dir_name(), ipaddr);
    system(cmd);

    return atoi(buff);
}

void sync_broker_config(broker_ctx_t *broker_ctx)
{
    char lsyncd_cmd[1024] = {0,};
    sprintf(lsyncd_cmd, LSYNC_SRC_DIR_CMD,
            get_current_dir_name(),
            broker_ctx->broker_host);
    system(lsyncd_cmd);


    sprintf(lsyncd_cmd, LSYNCD_RUN_CMD,
            broker_ctx->broker_host,
            get_current_dir_name(),
            broker_ctx->broker_host,
            broker_ctx->broker_host);
    system(lsyncd_cmd);

    if ((broker_ctx->lsyncd_pid = get_lsyncd_pid(broker_ctx->broker_host)) < 0) {
        fprintf(stderr, "err] fail to run lsyncd for config sync\n");
    } else {
        fprintf(stderr, "log] lsyncd for [%s] successfuly start\n", broker_ctx->broker_host);
    }
}

void unsync_broker_config(broker_ctx_t *broker_ctx)
{
    char lsyncd_kill[1024] = {0,};

    if (broker_ctx->lsyncd_pid > 0) {
        sprintf(lsyncd_kill, LSYNCD_KILL_CMD, broker_ctx->lsyncd_pid);
        system(lsyncd_kill);
        fprintf(stderr, "log] lsyncd for [ip:%s pid:%d] successfuly killed\n", broker_ctx->broker_host, broker_ctx->lsyncd_pid);
    }
}

static void *broker_thread(void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;

    fprintf(stderr, "log] broker thread for [%s] started!\n", broker_ctx->broker_host);

    /* config sync */
    sync_broker_config(broker_ctx);

    /* initialize */
    broker_ctx->evbase_thrd = event_base_new();
    broker_ctx->zmq_context = zmq_init(1);
    node_data_small_t root_node = {0, };
    broker_ctx->root_node = NewNodeSmall(&root_node, NT_INTERHUB);

    struct timeval tm_removetree = {0, TM_TICK};
    struct event *ev_removetree;
    ev_removetree = event_new (broker_ctx->evbase_thrd, -1, EV_PERSIST, remove_smalltree_callback, broker_ctx);
    event_add(ev_removetree, &tm_removetree);

    create_broker_ctx(broker_ctx);
    add_fd_read_callback(broker_ctx->evbase_thrd, broker_ctx->zmq_sub, broker_sub_callback, broker_ctx);
    add_fd_read_callback(broker_ctx->evbase_thrd, broker_ctx->zmq_dealer, interhub_callback, broker_ctx);

    // TODO .. some more
    event_base_loop(broker_ctx->evbase_thrd, EVLOOP_NO_EXIT_ON_EMPTY);

    // all evbase job done clear
    event_base_free (broker_ctx->evbase_thrd);

    /* destory zmq context */
    zmq_ctx_term(broker_ctx->zmq_context);

    /* stop config sync */
    unsync_broker_config(broker_ctx);

    // clear broker ctx
    fprintf(stderr, "log] broker thread for [%s] terminated!\n", broker_ctx->broker_host);
    memset(broker_ctx, 0x00, sizeof(broker_ctx_t));
}

void create_thread_action(evutil_socket_t fd, short what, void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;
    fprintf(stderr, "log] event : create broker thread for [%s:%s] ", 
            broker_ctx->broker_host, broker_ctx->broker_name);
    if (pthread_create(&broker_ctx->thread_id, NULL, broker_thread, broker_ctx) == 0)
        fprintf(stderr, "well created thread id [%u]\n", broker_ctx->thread_id);
    else
        fprintf(stderr, "create failed!!!\n");
}

void start_broker_polling(main_ctx_t *main_ctx, broker_ctx_t *broker_ctx)
{
    event_base_once(main_ctx->evbase_main, -1, EV_TIMEOUT, create_thread_action, broker_ctx, NULL);
}

broker_ctx_t *create_new_broker(main_ctx_t *main_ctx, mq_regi_t *svc_regi)
{
    broker_ctx_t *new_broker_slot = NULL;
    
    for(int i = 0; i < MAX_BROKER_NUM; i++) {
        broker_ctx_t *broker_ctx = &main_ctx->broker_ctx[i];
        if (broker_ctx->used != true) {
            set_broker_slot(main_ctx, broker_ctx, i, svc_regi);
            new_broker_slot = broker_ctx;
            start_broker_polling(main_ctx, new_broker_slot); // TODO it has return value
            break;
        }
    }

    if (new_broker_slot == NULL)
        fprintf(stderr, "err] all broker slot is full\n");

    return new_broker_slot;
}

void chk_tree_callback(GNode *node, gpointer input_arg)
{
    node_data_t *data = (node_data_t *)node->data;
    chk_tree_arg_t *arg = (chk_tree_arg_t *)input_arg;;

    mq_regi_t *svc_regi = arg->svc_regi;

    char *compare_name = NULL;
    if (arg->type == NT_BROKER)
        compare_name = svc_regi->broker_name;
    else if (arg->type == NT_SVC)
        compare_name = svc_regi->tag.svc;
    else if (arg->type == NT_PROC)
        compare_name = svc_regi->tag.name;
    else
        return;

    if (arg->find == true) return;
    if (arg->find != true) {

        int res = strcmp(data->name, compare_name);
        if (res < 0) {
            return;
        } else if (res == 0)  {
            arg->find = true;
            arg->find_node = node;
            arg->same_node_exist = true;
        } else {
            arg->find = true;
            arg->find_node = node;
            arg->insert_before_this = true;
        }
    }
}

void update_node_info(node_data_t *node_data, mq_regi_t *svc_regi)
{
    time_t *update_hb_time = &node_data->hb_rcv_time;
    time(update_hb_time);
    if (node_data->node_type == NT_PROC)
        memcpy(&node_data->sndrcv, &svc_regi->sndrcv, sizeof(mq_sndrcv_t));
}

GNode *NewNode(int req_type, mq_regi_t *svc_regi, broker_ctx_t *new_broker)
{
    node_data_t *data = (node_data_t *)malloc(sizeof(node_data_t));
    memset(data, 0x00, sizeof(node_data_t));

    data->node_type = req_type;

    sprintf(data->host, "%s", (req_type == NT_BROKER || req_type == NT_SVC) ? 
            svc_regi->broker_host : svc_regi->svc_host);
    sprintf(data->name, "%s", req_type == NT_INTERHUB ? INTERHUB_NAME : 
            req_type == NT_BROKER ? svc_regi->broker_name : 
            req_type == NT_SVC ? svc_regi->tag.svc : svc_regi->tag.name);

    data->broker_ctx = (req_type == NT_BROKER ? new_broker : NULL);

    time(&data->hb_rcv_time);

    return g_node_new(data);
}

GNode *NewNodeSmall(node_data_small_t *small_node, int node_type)
{
    fprintf(stderr, "log] New Small Node (for %s) created\n", 
            node_type == NT_INTERHUB ? "root" : node_type == NT_BROKER ? small_node->broker_id : small_node->svc_exist);
    node_data_small_t *data = (node_data_small_t *)malloc(sizeof(node_data_small_t));
    memcpy(data, small_node, sizeof(node_data_small_t));
    data->node_type = node_type;
    time(&data->hb_rcv_time);

    return g_node_new(data);
}

GNode *AddNode(GNode *parent, GNode *child, GNode *looser_brother)
{
    return g_node_insert_before(parent, looser_brother, child);
}

void broker_release(evutil_socket_t fd, short what, void *arg)
{
    broker_ctx_t *broker_ctx = (broker_ctx_t *)arg;

    fprintf(stderr, "log] %s called!!! release broker [[[id:%d name:%s]]] ... ", __func__,
            broker_ctx->id, broker_ctx->broker_name);
    
    /* destroy small node */
    g_node_destroy(broker_ctx->root_node);

    /* close all open sock */
    zmq_close(broker_ctx->zmq_dealer);
    zmq_close(broker_ctx->zmq_pub);
    zmq_close(broker_ctx->zmq_sub);

    /* destory zmq context */
    zmq_ctx_term(broker_ctx->zmq_context);

    /* escape evbase loop */
    event_base_loopbreak(broker_ctx->evbase_thrd);

    fprintf(stderr, "well done\n");
}

void RemoveNode(GNode *node)
{
    node_data_t *data = (node_data_t *)node->data;
    if (data->node_type == NT_BROKER && data->broker_ctx != NULL) {
        broker_ctx_t *broker_ctx = data->broker_ctx;
        event_base_once(broker_ctx->evbase_thrd, -1, EV_TIMEOUT, broker_release, broker_ctx, 0);
    }
    g_node_destroy(node);
}

/* if find success, update time else created it, update time */
void chk_tree(main_ctx_t *main_ctx, mq_regi_t *svc_regi, int req_type, broker_ctx_t *new_broker)
{
    GNode *root_node = main_ctx->root_node;
    assert(root_node != NULL);

    if (req_type != NT_BROKER && req_type != NT_SVC && req_type !=NT_PROC)
        return;

    chk_tree_arg_t find_broker = { .svc_regi = svc_regi, .type = NT_BROKER };
    chk_tree_arg_t find_svc = { .svc_regi = svc_regi, .type = NT_SVC };
    chk_tree_arg_t find_proc = { .svc_regi = svc_regi, .type = NT_PROC };

    /* find broker */
    g_node_children_foreach(root_node, G_TRAVERSE_ALL, chk_tree_callback, &find_broker);

    /* find service */
    if ((req_type == NT_SVC || req_type == NT_PROC) && find_broker.same_node_exist && find_broker.find_node != NULL)
        g_node_children_foreach(find_broker.find_node, G_TRAVERSE_ALL, chk_tree_callback, &find_svc);

    /* find proc */
    if (req_type == NT_PROC && find_svc.same_node_exist && find_svc.find_node != NULL)
        g_node_children_foreach(find_svc.find_node, G_TRAVERSE_ALL, chk_tree_callback, &find_proc);

    /* get response */
    GNode *base_node = NULL;
    GNode *find_node = NULL;
    chk_tree_arg_t *result = NULL;

    if (req_type == NT_BROKER) {
        base_node = root_node;
        result = &find_broker;
        find_node = result->find_node;
    } else if (req_type == NT_SVC) {
        base_node = find_broker.find_node;
        result = &find_svc;
        find_node = result->find_node;
    } else if (req_type == NT_PROC) {
        base_node = find_svc.find_node;
        result = &find_proc;
        find_node = result->find_node;
    }

    /* same node exist, update hb rcv time */
    if (result->same_node_exist && result->find_node != NULL) {
        node_data_t *node_data = (node_data_t *)find_node->data;
        update_node_info(node_data, svc_regi);
    } else {
    /* same node not exist, insert by name order */
        GNode *new_node = NewNode(req_type, svc_regi, new_broker);
        AddNode(base_node, new_node,
                (result->insert_before_this && result->find_node != NULL) ? find_node : NULL);
    }
}

int chk_svc_broker(main_ctx_t *main_ctx, mq_regi_t *svc_regi)
{
    char *broker_name = svc_regi->broker_name;
    char *broker_host = svc_regi->broker_host;

    int alreadyServiced = false;

    for (int i = 0; i < MAX_BROKER_NUM; i++) {
        broker_ctx_t *broker_ctx = &main_ctx->broker_ctx[i];
        if (broker_ctx->used != true)
            continue;

        if (broker_ctx->used == true && !strcmp(broker_host, broker_ctx->broker_host)) {
            if (strcmp(broker_name, broker_ctx->broker_name)) {
                fprintf(stderr, "err] recv regi-req from %s but already serviced, can't accept\n", broker_host);
                return 0;
            } else {
                alreadyServiced = true;
            }
        }
    }

    if (alreadyServiced == true) {
        /* do something
         * - check broker
         *   check service
         *   check proc
         *   !!! relay regi to xsub & xpub */
        chk_tree(main_ctx, svc_regi, NT_BROKER, NULL);
        chk_tree(main_ctx, svc_regi, NT_SVC, NULL);
        chk_tree(main_ctx, svc_regi, NT_PROC, NULL);
    } else {
        /* do something
         * create broker context
         * trigger main evbase to create thread with broker context */
        broker_ctx_t *new_broker = NULL;
        if ((new_broker = create_new_broker(main_ctx, svc_regi)) != NULL) {
            chk_tree(main_ctx, svc_regi, NT_BROKER, new_broker);
        }
    }

    return (1);
}

int regi_action(main_ctx_t *main_ctx, const char *action, const char *peer_address)
{
    void *zmq_sub = main_ctx->zmq_sub_from_broker;
    void *zmq_xsub = main_ctx->zmq_pub_to_xsub;

    if (!strcmp(action, MQ_REG)) {
        mq_regi_t svc_regi = {0,};

        if (zmq_recv(zmq_sub, &svc_regi, sizeof(svc_regi), ZMQ_DONTWAIT) < 0)
            return -1;

        /* add from address to regi msg */
        sprintf(svc_regi.broker_host, "%s", peer_address);

        /* pub regi message to dealer thread */
        if (zmq_send(zmq_xsub, MQ_REG, strlen(MQ_REG), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0) {
            zmq_send(zmq_xsub, &svc_regi, sizeof(svc_regi), ZMQ_DONTWAIT);
        }

        /* manage node */
        return chk_svc_broker(main_ctx, &svc_regi);
    }
    /* else if ... */

    return 0;
}

void svcregi_callback(evutil_socket_t fd, short what, void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;
    void *zmq_sub = main_ctx->zmq_sub_from_broker;

    /* we want peer address */
    zmq_msg_t msg;
    zmq_msg_init (&msg);

    while (zmq_msg_recv (&msg, zmq_sub, ZMQ_DONTWAIT) >= 0) {
        const char *peer_address = zmq_msg_gets(&msg, "Peer-Address");
        char *action = (char *)zmq_msg_data (&msg);
        //fprintf(stderr, "dbg] we recv [%s] from (%s)\n", action, peer_address);
        regi_action(main_ctx, action, peer_address);
    }
    zmq_msg_close (&msg); // caution!! related all value will lose reference
}

void draw_node_callback(GNode *node, gpointer input_arg)
{
    FILE *fp = (FILE *)input_arg;

    GNode *parent = node->parent;
    node_data_t *parent_data = (node_data_t *)parent->data;
    GNode *child = node;
    node_data_t *child_data = (node_data_t *)node->data;

    char parent_name[256] = {0,};
    char child_name[256] {0,};

    if (parent_data->node_type == NT_SVC)
        sprintf(parent_name, "%s_%s", parent_data->name, parent_data->host);
    else
        sprintf(parent_name, "%s", parent_data->name);

    if (child_data->node_type == NT_SVC)
        sprintf(child_name, "%s_%s", child_data->name, child_data->host);
    else
        sprintf(child_name, "%s", child_data->name);

    for (int k = 0; k < 256; k++)
        parent_name[k] == '.' ? parent_name[k] = '_' : parent_name[k] = parent_name[k];
    for (int k = 0; k < 256; k++)
        child_name[k] == '.' ? child_name[k] = '_' : child_name[k] = child_name[k];

    switch (child_data->node_type) {
        case NT_BROKER:
            fprintf(fp, "\t%s [shape=record %s label=\"[broker] %s|%s|%s\" href=\"http://%s/svc_status/index.html\"];\n",
                    child_name, 
                    SHAPE_BROKER,
                    child_data->name,
                    child_data->host,
                    ctime(&child_data->hb_rcv_time),
                    child_data->host);
            break;
        case NT_SVC:
            fprintf(fp, "\t%s [shape=record  style=rounded label=\"[svc] %s|%s|%s\"];\n",
                    child_name, child_data->name,
                    child_data->host,
                    ctime(&child_data->hb_rcv_time));
            break;
        case NT_PROC:
            fprintf(fp, "\t%s [shape=record %s label=\"[proc] %s|%s|%s|send(cnt %d/byte %d)\nrecv(cnt %d/byte %d)\"];\n",
                    child_name,
                    (child_data->sndrcv.send_cnt == 0 && child_data->sndrcv.recv_cnt == 0) ? SHAPE_NOSEND_NORECV :
                    (child_data->sndrcv.send_cnt > 0 && child_data->sndrcv.recv_cnt == 0) ? SHAPE_SENDEXIST_NORECV :
                    (child_data->sndrcv.send_cnt > 0 && ((child_data->sndrcv.recv_cnt * 100 / child_data->sndrcv.send_cnt) < 70)) ? SHAPE_SENDEXIST_SMALLRECV : SHAPE_NORMAL,
                    child_data->name,
                    child_data->host,
                    ctime(&child_data->hb_rcv_time),
                    child_data->sndrcv.send_cnt,
                    child_data->sndrcv.send_byte,
                    child_data->sndrcv.recv_cnt,
                    child_data->sndrcv.recv_byte);
            break;
        default:
            return;
    }

    fprintf(fp, "\t%s -> %s;\n",
            parent == NULL ? "root" : parent_name,
            child_name);

    traverse_dot_make(node, fp);
}

void traverse_dot_make(GNode *node, FILE *fp)
{
    if (G_NODE_IS_LEAF(node))
        return;
    else
        g_node_children_foreach(node, G_TRAVERSE_ALL, draw_node_callback, fp);
}

void maketree_callback(evutil_socket_t fd, short what, void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;
    GNode *root = main_ctx->root_node;

    FILE *fp = fopen("./broker_status.dat", "w");
    fprintf(fp, "digraph broker_status {\n");
    fprintf(fp, "overlap=scale;\n");
    fprintf(fp, "mindist=.2;\n");
    fprintf(fp, "node [ fontsize=10];\n");
    fprintf(fp, "%s [shape=doublecircle href=\"http://%s:1234\"];\n", INTERHUB_NAME, 
            main_ctx->argv_address != NULL ? main_ctx->argv_address : "0.0.0.0");

    traverse_dot_make(root, fp);

    fprintf(fp, "}");
    fclose(fp);

    char make_png_cmd[] = "circo -Grankdir=LR -Tsvg -Nfontname=NanumGothicCoding ./broker_status.dat -o ./broker_status.svg | cp ./broker_status.svg /var/www/html/broker_status";
    system(make_png_cmd);
}

#define TMCHK_NORESP 5
void remove_node_callback(GNode *node, gpointer input_arg)
{
    time_t *current = (time_t *)input_arg;

    node_data_t *node_data = (node_data_t *)node->data;

    if (*current - node_data->hb_rcv_time > TMCHK_NORESP) {
        RemoveNode(node);
    } else {
        traverse_dot_remove(node, current);
    }
}

void traverse_dot_remove(GNode *node, time_t *current)
{
    if (G_NODE_IS_LEAF(node))
        return;
    else
        g_node_children_foreach(node, G_TRAVERSE_ALL, remove_node_callback, current);
}

void removetree_callback(evutil_socket_t fd, short what, void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;
    GNode *root = main_ctx->root_node;
    time_t current = {0,};
    time(&current);

    traverse_dot_remove(root, &current);
}

static void *broker_regi_thrd(void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;

    /* own evbase */
    struct event_base *evbase = event_base_new();
    
    /* zmq context */
    void *zmq_context = zmq_init(1);

    /* subscriber services regi */
    main_ctx->zmq_sub_from_broker = zmq_socket(zmq_context, ZMQ_SUB);
    zero_linger(main_ctx->zmq_sub_from_broker);
    zmq_setsockopt (main_ctx->zmq_sub_from_broker, ZMQ_SUBSCRIBE, MQ_REG, strlen(MQ_REG));
    zmq_bind(main_ctx->zmq_sub_from_broker, main_ctx->interhub_address);

    /* relay received regi to dealer thread */
    main_ctx->zmq_pub_to_xsub = zmq_socket(zmq_context, ZMQ_PUB);
    zero_linger(main_ctx->zmq_pub_to_xsub);
    zmq_connect(main_ctx->zmq_pub_to_xsub, LISTEN_ADDR_XSUB);

    /* broker regi subscribe action 
     * - create dealer thread per broker 
     * - relay regi message to dealer thread */
    add_fd_read_callback(evbase, main_ctx->zmq_sub_from_broker, svcregi_callback, main_ctx);

    struct timeval tm_maketree = {0, TM_TICK};
    struct event *ev_maketree;
    ev_maketree = event_new (evbase, -1, EV_PERSIST, maketree_callback, main_ctx);
    event_add(ev_maketree, &tm_maketree);

    struct timeval tm_removetree = {0, TM_TICK};
    struct event *ev_removetree;
    ev_removetree = event_new (evbase, -1, EV_PERSIST, removetree_callback, main_ctx);
    event_add(ev_removetree, &tm_removetree);

    fprintf(stderr, "log] create regi thread welldone, will listen via [%s]\n", main_ctx->interhub_address);

    event_base_loop(evbase, EVLOOP_NO_EXIT_ON_EMPTY);

    // never reach here
    event_base_free (evbase);
}

static void *xsub_and_xpub_thrd(void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;

    void *zmq_context = zmq_init(4); // TODO modify

    /* create xsub */
    main_ctx->zmq_xsub = zmq_socket(zmq_context, ZMQ_XSUB);
    zero_linger(main_ctx->zmq_xsub);
    zmq_bind(main_ctx->zmq_xsub, LISTEN_ADDR_XSUB);

    /* create xpub */
    main_ctx->zmq_xpub = zmq_socket(zmq_context, ZMQ_XPUB);
    zero_linger(main_ctx->zmq_xpub);
    zmq_bind(main_ctx->zmq_xpub, LISTEN_ADDR_XPUB);

    /* while proxy */
    // TODO!!! there exist [zmq_proxy_steerable(..., capture, control)]
    zmq_proxy(main_ctx->zmq_xsub, main_ctx->zmq_xpub, NULL);

    fprintf(stderr, "err] never reach here (%s)\n", __func__);
}

void initialize(main_ctx_t *main_ctx)
{
    mq_regi_t self_regi = {0,};

    main_ctx->root_node = NewNode(NT_INTERHUB, &self_regi, NULL);
}

static int gprof_exit;
void main_callback(evutil_socket_t fd, short what, void *arg)
{   
    //fprintf(stderr, "main] tick [%d]\n", gprof_exit++);
#if 0
    if (gprof_exit >= 100) exit(0);
#endif
}

int main(int argc, char **argv)
{
    /* libevent, multi-thread safe code (always locked) */
    evthread_use_pthreads();

    main_ctx_t main_ctx = {0,};

#if 0
    if (argc != 1 && argc != 2) {
        fprintf(stderr, "usage: %s [\"my_tcp_address\"]\n", __progname);
        exit(0);
    } else {
        if (argc == 2) {
            main_ctx.argv_address = argv[1];
            sprintf(main_ctx.interhub_address, "tcp://%s:%d", argv[1], MQ_HUB_PORT);
        } else {
            main_ctx.argv_address = NULL;
            sprintf(main_ctx.interhub_address, "tcp://0.0.0.0:%d", MQ_HUB_PORT);
        }
        initialize(&main_ctx);
    }
#else
    if (argc != 2) {
        fprintf(stderr, "usage: %s [\"vip_address\"]\n", __progname);
        exit(0);
    } else {
        main_ctx.argv_address = argv[1];
        sprintf(main_ctx.interhub_address, "tcp://0.0.0.0:%d", MQ_HUB_PORT);
        initialize(&main_ctx);
    }
#endif

    /* CAUTIION!! don't change thread create order */
    pthread_t xsub_xpub_worker = {0,};
    pthread_create(&xsub_xpub_worker, NULL, xsub_and_xpub_thrd, &main_ctx);

    pthread_t regi_worker = {0,};
    pthread_create(&regi_worker, NULL, broker_regi_thrd, &main_ctx);

    /* own evbase */
    main_ctx.evbase_main = event_base_new();

    struct timeval tm_tick = {0, TM_TICK};
    struct event *ev_main;
    ev_main = event_new (main_ctx.evbase_main, -1, EV_PERSIST, main_callback, NULL);
    event_add(ev_main, &tm_tick);

    event_base_loop(main_ctx.evbase_main, EVLOOP_NO_EXIT_ON_EMPTY);

    return 0;
}
