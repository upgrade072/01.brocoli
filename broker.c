#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <event2/thread.h>
#include <event.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/dns.h>

#include <sys/inotify.h>

#include "lib_wrapzmq.h"
#include "zhelpers.h"
#include "mq.h"
#include "broker.h"

#define TMCHK_NORESP 5
#define CONFIG_DIR "%s/conf"
#define LOCALDOMAIN "localdomain"

extern char *__progname;

void connect_svc_slot(service_ctx_t *svc_ctx)
{
    /* dealer : drain all svc req from router */
    svc_ctx->dealer = zmq_socket(svc_ctx->zmq_context, ZMQ_DEALER);
    zero_linger(svc_ctx->dealer);
    zmq_setsockopt(svc_ctx->dealer, ZMQ_IDENTITY, svc_ctx->svc_name, strlen(svc_ctx->svc_name));
    zmq_connect(svc_ctx->dealer, LISTEN_ADDR_ROUTER);
    fprintf(stderr, "log] dealer(%s) connect to (%s)\n", svc_ctx->svc_name, LISTEN_ADDR_ROUTER);

    /* subscriber : subscript svc specific response from publisher */
    svc_ctx->sub = zmq_socket(svc_ctx->zmq_context, ZMQ_SUB);
    zero_linger(svc_ctx->sub);
    zmq_setsockopt(svc_ctx->sub, ZMQ_SUBSCRIBE, svc_ctx->svc_name, strlen(svc_ctx->svc_name)); // caution!!! ZMQ_SUBSCRIBE
    zmq_connect(svc_ctx->sub, LISTEN_ADDR_PUB);
    fprintf(stderr, "log] subscriber(%s) connect to (%s) sub id(%s:%d)\n", svc_ctx->svc_name, LISTEN_ADDR_PUB, svc_ctx->svc_name, strlen(svc_ctx->svc_name));

    char port_name[128] = {0,};
    /* publisher : relay subscribed response to specific process */
    svc_ctx->pub = zmq_socket(svc_ctx->zmq_context, ZMQ_PUB);
    zero_linger(svc_ctx->pub);
    sprintf(port_name, "tcp://*:%d", svc_ctx->svc_start_port);
    zmq_bind(svc_ctx->pub, port_name);
    sprintf(port_name, "ipc://%s_PUBL.ipc", svc_ctx->svc_name);
    zmq_bind(svc_ctx->pub, port_name);
    fprintf(stderr, "log] pub %s bind\n", port_name);

    /* push : requested service r.r. distribute */
    svc_ctx->push = zmq_socket(svc_ctx->zmq_context, ZMQ_PUSH);
    zero_linger(svc_ctx->push);
    sprintf(port_name, "tcp://*:%d", svc_ctx->svc_start_port + 1);
    zmq_bind(svc_ctx->push, port_name);
    sprintf(port_name, "ipc://%s_PUSH.ipc", svc_ctx->svc_name);
    zmq_bind(svc_ctx->push, port_name);
    fprintf(stderr, "log] push %s bind\n", port_name);

    /* pull : fail queueing svc request */
    svc_ctx->pull = zmq_socket(svc_ctx->zmq_context, ZMQ_PULL);
    zero_linger(svc_ctx->pull);
    sprintf(port_name, "tcp://*:%d", svc_ctx->svc_start_port + 2);
    zmq_bind(svc_ctx->pull, port_name);
    sprintf(port_name, "ipc://%s_PULL.ipc", svc_ctx->svc_name);
    zmq_bind(svc_ctx->pull, port_name);
    fprintf(stderr, "log] pull %s bind\n", port_name);
}

service_ctx_t *set_svc_slot(main_ctx_t *main_ctx, service_ctx_t *svc_ctx, int id, mq_regi_t *svc_regi)
{
    svc_ctx->evbase = main_ctx->evbase;
    svc_ctx->zmq_context = main_ctx->zmq_context; // CAUTION!!! not thread safe
    svc_ctx->used = true;
    svc_ctx->id = id;

    strcpy(svc_ctx->svc_name, svc_regi->tag.svc);
    svc_ctx->svc_start_port = svc_regi->port / 10 * 10; 

    return svc_ctx;
}

void svc_release(evutil_socket_t fd, short what, void *arg)
{
    service_ctx_t *svc_ctx = (service_ctx_t *)arg;

    fprintf(stderr, "log] %s called!! release svc [[[%s]]]\n", __func__, svc_ctx->svc_name);

    zmq_close(svc_ctx->dealer);
    zmq_close(svc_ctx->sub);
    zmq_close(svc_ctx->push);
    zmq_close(svc_ctx->pull);
    zmq_close(svc_ctx->pub);

    event_free(svc_ctx->ev_dealer_to_push);
    event_free(svc_ctx->ev_pull_to_dealer);
    event_free(svc_ctx->ev_sub_to_pub);

    memset(svc_ctx, 0x00, sizeof(service_ctx_t));
}

void mq_relay_callback(evutil_socket_t fd, short what, void *arg)
{
    relay_arg_t *relay = (relay_arg_t *)arg;

    int size = 0;
    int recv_byte = 0;
    int more = 0;
    size_t more_size = sizeof(more);
    char buffer[MAX_BUFF_SIZE] = {0,}; // TODO!!! modify size

    while ((recv_byte = zmq_recv(relay->sock_from, buffer, sizeof(buffer), ZMQ_DONTWAIT)) >= 0) {
        //fprintf(stderr, "dbg] (((%s))) relay [%d:%s]\n", relay->relay_name, recv_byte, buffer);
        zmq_getsockopt(relay->sock_from, ZMQ_RCVMORE, &more, &more_size);
        zmq_send(relay->sock_to, buffer, recv_byte, ZMQ_DONTWAIT| (more ? ZMQ_SNDMORE : 0));
    }
}

void mq_relay_callback_sub_to_pub(evutil_socket_t fd, short what, void *arg)
{
    relay_arg_t *relay = (relay_arg_t *)arg;

    char svc[1024] = {0,};

    /* will recv
     * - svc [<-throw this]
     * - svc_pid | MQ_CONF
     * - message(s)
     */
    while (zmq_recv(relay->sock_from, svc, sizeof(svc), ZMQ_DONTWAIT) >= 0) { // remove svc tag
#if 0
        relay_snd_more(relay->sock_from, relay->sock_to, NULL);
#else
        if (relay_snd_more(relay->sock_from, relay->sock_to, NULL) < 0) {
            drain_left_part_message(relay->sock_from);
            return;
        }
#endif
    }
}

void start_svc_polling(evutil_socket_t fd, short what, void *arg)
{
    service_ctx_t *svc_ctx = (service_ctx_t *)arg;

    /* connect sock */
    connect_svc_slot(svc_ctx);

    /* start service relay */
    svc_ctx->relay_dealer = { .sock_from = svc_ctx->dealer, .sock_to = svc_ctx->push };
    sprintf(svc_ctx->relay_dealer.relay_name, "%s", " dealer to push ");
    svc_ctx->relay_pull = { .sock_from = svc_ctx->pull, .sock_to = svc_ctx->dealer };
    sprintf(svc_ctx->relay_pull.relay_name, "%s", " pull to dealer ");
    svc_ctx->relay_sub = { .sock_from = svc_ctx->sub, .sock_to = svc_ctx->pub };
    sprintf(svc_ctx->relay_sub.relay_name, "%s", " sub to pub");

    fprintf(stderr, "log] start svc [[[%s]]] polling\n", svc_ctx->svc_name);

    svc_ctx->ev_dealer_to_push = add_fd_read_callback(svc_ctx->evbase, svc_ctx->dealer,
            mq_relay_callback, &svc_ctx->relay_dealer);
    svc_ctx->ev_pull_to_dealer = add_fd_read_callback(svc_ctx->evbase, svc_ctx->pull,
            mq_relay_callback, &svc_ctx->relay_pull);
    svc_ctx->ev_sub_to_pub = add_fd_read_callback(svc_ctx->evbase, svc_ctx->sub,
            mq_relay_callback_sub_to_pub, &svc_ctx->relay_sub);
}

service_ctx_t *create_new_dealer(main_ctx_t *main_ctx, mq_regi_t *svc_regi)
{
    service_ctx_t *new_svc_slot = NULL;

    for (int i = 0; i < MAX_SVC_NUM; i++) {
        service_ctx_t *svc_ctx = &main_ctx->svc_ctx[i];
        if (svc_ctx->used != true) {
            /* TODO!!! and CAUTION!!! regi_thrd ==>> svc_dealer_thrd */
            // ??? may be move to event call 
            new_svc_slot = set_svc_slot(main_ctx, svc_ctx, i, svc_regi);
            event_base_once(svc_ctx->evbase, -1, EV_TIMEOUT, start_svc_polling, svc_ctx, 0);
            break;
        }
    }

    if (new_svc_slot == NULL) 
        fprintf(stderr, "err] all service slot is full\n");

    return new_svc_slot;
}

GNode *NewNode(int req_type, mq_regi_t *svc_regi, service_ctx_t *new_svc)
{
    node_data_t *data = (node_data_t *)malloc(sizeof(node_data_t));
    memset(data, 0x00, sizeof(node_data_t));

    data->node_type = req_type;

    sprintf(data->host, "%s", svc_regi->svc_host);
    sprintf(data->name, "%s", req_type == NT_SVC ? svc_regi->tag.svc : svc_regi->tag.name);

    data->svc_start_port = (req_type == NT_SVC ? svc_regi->port * 10 / 10 : 0);
    data->via_type = svc_regi->conn_type;

    data->svc_ctx = (req_type == NT_SVC ? new_svc : NULL);

    time(&data->hb_rcv_time);

    return g_node_new(data);
}

GNode *AddNode(GNode *parent, GNode *child, GNode *looser_brother)
{
    return g_node_insert_before(parent, looser_brother, child);
}

void RemoveNode(GNode *node)
{
    node_data_t *data = (node_data_t *)node->data;
    if (data->node_type == NT_SVC && data->svc_ctx != NULL) {
        service_ctx_t *svc_ctx = data->svc_ctx;
        event_base_once(svc_ctx->evbase, -1, EV_TIMEOUT, svc_release, svc_ctx, 0);
    }
    g_node_destroy(node);
}

void chk_tree_callback(GNode *node, gpointer input_arg)
{
    node_data_t *data = (node_data_t *)node->data;
    chk_tree_arg_t *arg = (chk_tree_arg_t *)input_arg;;

    mq_regi_t *svc_regi = arg->svc_regi;

    char *compare_name = NULL;
    if (arg->type == NT_SVC)
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
    memcpy(&node_data->sndrcv, &svc_regi->sndrcv, sizeof(mq_sndrcv_t));
}

/* if find success, update time else created it, update time */
void chk_tree(main_ctx_t *main_ctx, mq_regi_t *svc_regi, int req_type, service_ctx_t *new_svc)
{
    GNode *root_node = main_ctx->root_node;
    assert(root_node != NULL);

    if (req_type != NT_SVC && req_type != NT_PROC)
        return;

    chk_tree_arg_t find_svc = { .svc_regi = svc_regi, .type = NT_SVC };
    chk_tree_arg_t find_proc = { .svc_regi = svc_regi, .type = NT_PROC };

    /* find service */
    g_node_children_foreach(root_node, G_TRAVERSE_ALL, chk_tree_callback, &find_svc);

    /* find proc */
    if (req_type == NT_PROC && find_svc.same_node_exist && find_svc.find_node != NULL)
        g_node_children_foreach(find_svc.find_node, G_TRAVERSE_ALL, chk_tree_callback, &find_proc);

    /* get response */
    GNode *base_node = NULL;
    GNode *find_node = NULL;
    chk_tree_arg_t *result = NULL;

    if (req_type == NT_SVC) {
        base_node = root_node;
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
        GNode *new_node = NewNode(req_type, svc_regi, new_svc);
        AddNode(base_node, new_node, 
                (result->insert_before_this && result->find_node != NULL) ? find_node : NULL);
    }
}

int chk_svc_dealer(main_ctx_t *main_ctx, mq_regi_t *svc_regi)
{
    // TODO : some hash or smart algorithm
    short target_port = svc_regi->port / 10 * 10; // for easy monitor
    int alreadyServiced = false;
    for (int i = 0; i < MAX_SVC_NUM; i++) {
        service_ctx_t *svc_ctx = &main_ctx->svc_ctx[i];
        if (svc_ctx->used != true)
            continue;

        if (target_port == svc_ctx->svc_start_port) {
            if (strcmp(svc_regi->tag.svc, svc_ctx->svc_name)) {
                fprintf(stderr, "err] (%s:%s) request port (%d). but it already serviced as (%s)\n",
                        svc_regi->tag.svc, svc_regi->tag.name, target_port,
                        svc_ctx->svc_name);
                return (1);
            } else {
                alreadyServiced = true;
                break;
            }
        }
    }

    if (alreadyServiced == true) {
        // add process node
        chk_tree(main_ctx, svc_regi, NT_SVC, NULL);
        chk_tree(main_ctx, svc_regi, NT_PROC, NULL);
    } else {
        service_ctx_t *new_svc = NULL;
        // add service node
        if ((new_svc = create_new_dealer(main_ctx, svc_regi)) != NULL) {
            chk_tree(main_ctx, svc_regi, NT_SVC, new_svc);
        }
    }

    return (1);
}
 
int regi_action(main_ctx_t *main_ctx, const char *action, const char *peer_address)
{
    void *zmq_sub = main_ctx->zmq_sub;
    void *zmq_pub_to_interhub = main_ctx->zmq_pub_to_interhub;

    if (!strcmp(action, MQ_REG)) {
        mq_regi_t svc_regi = {0,};

        if (zmq_recv(zmq_sub, &svc_regi, sizeof(svc_regi), ZMQ_DONTWAIT) < 0)
            return -1;

        /* add from address to regi msg */
        if (svc_regi.conn_type != CONN_IPC) 
            sprintf(svc_regi.svc_host, "%s", peer_address);
        else
            sprintf(svc_regi.svc_host, LOCALDOMAIN);

        /* relay receive regi to interhub, if interhub exist */
        if (zmq_pub_to_interhub != NULL) {
            sprintf(svc_regi.broker_name, "%s_%d", __progname, getpid());
            if (zmq_send(zmq_pub_to_interhub, MQ_REG, strlen(MQ_REG), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0)
                zmq_send(zmq_pub_to_interhub, &svc_regi, sizeof(mq_regi_t), ZMQ_DONTWAIT);
        }

        /* manage node */
        return chk_svc_dealer(main_ctx, &svc_regi);
    } 
    /* else if ... */

    return 0;
}

void svcregi_callback(evutil_socket_t fd, short what, void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;
    void *zmq_sub = main_ctx->zmq_sub;

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

void traverse_dot_make(GNode *root, FILE *fp)
{
    unsigned int service_num = g_node_n_children(root);
    for (int i = 0; i < service_num; i++) {
        GNode *service_node = g_node_nth_child(root, i);
        node_data_t *svc_data = (node_data_t *)service_node->data;
        fprintf(fp, "Broker -> %s;\n", svc_data->name);
        fprintf(fp, "subgraph cluster_%d {\n", i);
        fprintf(fp, "\tfontsize=10;\n");
        fprintf(fp, "\tlabel=\"[ %s ]\";\n", svc_data->name);

        fprintf(fp, "\t%s [shape=record  style=rounded label=\"SERVICE: %s|%19s|{%s_PUBL.ipc\\n%s_PUSH.ipc\\n%s_PULL.ipc|port %d\\nport %d\\nport %d}\"];\n",
                svc_data->name, svc_data->name, 
                ctime(&svc_data->hb_rcv_time),
                svc_data->name,
                svc_data->name,
                svc_data->name,
                svc_data->svc_start_port,
                svc_data->svc_start_port + 1,
                svc_data->svc_start_port + 2);
        unsigned int proc_num = g_node_n_children(service_node);
        for (int j = 0; j < proc_num; j++) {
            GNode *proc_node = g_node_nth_child(service_node, j);
            node_data_t *proc_data = (node_data_t *)proc_node->data;

            char host_cnvt[128] = {0,};
            for (int k = 0; k < 128; k++) 
                proc_data->host[k] == '.' ? host_cnvt[k] = '_' : host_cnvt[k] = proc_data->host[k];

            fprintf(fp, "\tsubgraph cluster_%s {\n", host_cnvt);
            if (!strcmp(host_cnvt, LOCALDOMAIN)) {
                fprintf(fp, "\t\tstyle=filled;\n");
                fprintf(fp, "\t\tcolor=white;\n");
            }
            fprintf(fp, "\t\tfontsize=10;\n");
            fprintf(fp, "\t\tlabel = \"%s\";\n", proc_data->host);
            fprintf(fp, "\t\tlabelloc = b;\n"); // bottom

            fprintf(fp, "\t\t%s [shape=record %s label=\"{%s|%s}|%19s|send(cnt %d/byte %d)\\nrecv(cnt %d/byte %d)\"];\n",
                    proc_data->name, 
                    (proc_data->sndrcv.send_cnt == 0 && proc_data->sndrcv.recv_cnt == 0) ? SHAPE_NOSEND_NORECV :
                    (proc_data->sndrcv.send_cnt > 0 && proc_data->sndrcv.recv_cnt == 0) ? SHAPE_SENDEXIST_NORECV :
                    (proc_data->sndrcv.send_cnt > 0 && ((proc_data->sndrcv.recv_cnt * 100 / proc_data->sndrcv.send_cnt) < 70)) ? SHAPE_SENDEXIST_SMALLRECV : SHAPE_NORMAL, 
                    proc_data->via_type == CONN_IPC ? "ipc" : "tcp",
                    proc_data->name,
                    ctime(&proc_data->hb_rcv_time),
                    proc_data->sndrcv.send_cnt,
                    proc_data->sndrcv.send_byte,
                    proc_data->sndrcv.recv_cnt,
                    proc_data->sndrcv.recv_byte);

            if (proc_data->via_type == CONN_IPC) {
                fprintf(fp, "%s -> %s;\n", svc_data->name, proc_data->name);
                fprintf(fp, "\t}\n");
            } else {
                fprintf(fp, "\t}\n");
                fprintf(fp, "%s -> %s;\n", svc_data->name, proc_data->name);
            }
        }

        fprintf(fp, "}\n");
    }
}

void maketree_callback(evutil_socket_t fd, short what, void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;
    GNode *root = main_ctx->root_node;

    FILE *fp = fopen("./svc_status.dat", "w");
    fprintf(fp, "digraph svc_status {\n");
    fprintf(fp, "overlap=false;\n");
    fprintf(fp, "graph [ fontname=\"NanumGothicCoding\"];\n");
    fprintf(fp, "node [ fontsize=10 fontname=\"NanumGothicCoding\"];\n");
    fprintf(fp, "edge [ fontname=\"NanumGothicCoding\"];\n");
    fprintf(fp, "Broker [shape=doublecircle];\n");

    traverse_dot_make(root, fp);

    fprintf(fp, "}\n");
    fclose(fp);
    char make_png_cmd[] = "dot -Grankdir=LR -Tsvg -Nfontname=NanumGothicCoding ./svc_status.dat -o ./svc_status.svg >/dev/null 2>&1 | cp ./svc_status.svg /var/www/html/svc_status >/dev/null 2>&1";
    system(make_png_cmd);
}

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

static void *svc_regi_thrd(void *arg)
{

    main_ctx_t *main_ctx = (main_ctx_t *)arg;

    /* own evbase */
    struct event_base *evbase = event_base_new();

    /* zmq context */
    void *zmq_context = zmq_init(1);

    /* subscriber services regi */
    main_ctx->zmq_sub = zmq_socket(zmq_context, ZMQ_SUB);
    zero_linger(main_ctx->zmq_sub);
    zmq_setsockopt (main_ctx->zmq_sub, ZMQ_SUBSCRIBE, MQ_REG, strlen(MQ_REG));
    zmq_bind(main_ctx->zmq_sub, LISTEN_ADDR_SUB);

    /* publish received regi to interhub */
    if (main_ctx->interhub_address[0]) {
        fprintf(stderr, "log] try connect to interhub [%s]...", main_ctx->interhub_address);
        main_ctx->zmq_pub_to_interhub = zmq_socket(zmq_context, ZMQ_PUB);
        zero_linger(main_ctx->zmq_pub_to_interhub);
        zmq_connect(main_ctx->zmq_pub_to_interhub, main_ctx->interhub_address);
        fprintf(stderr, "well done\n");
    } else {
        fprintf(stderr, "log] interhub not setted, run single mode\n");
        main_ctx->zmq_pub_to_interhub = NULL;
    }

    /* regi subscribe action
     * 1) create service node & manage it
     * 2) relay to inter hub */
    add_fd_read_callback(evbase, main_ctx->zmq_sub, svcregi_callback, main_ctx);

    struct timeval tm_maketree = {0, TM_TICK};
    struct event *ev_maketree;
    ev_maketree = event_new (evbase, -1, EV_PERSIST, maketree_callback, main_ctx);
    event_add(ev_maketree, &tm_maketree);

    struct timeval tm_removetree = {0, TM_TICK};
    struct event *ev_removetree;
    ev_removetree = event_new (evbase, -1, EV_PERSIST, removetree_callback, main_ctx);
    event_add(ev_removetree, &tm_removetree);

    event_base_loop(evbase, EVLOOP_NO_EXIT_ON_EMPTY);
    
    // never reach here
    event_base_free (evbase);

    fprintf(stderr, "err] regi thrd exited\n");
}

void relay_req_to_interhub(main_ctx_t *main_ctx, char *svc_host)
{
    void *zmq_router = main_ctx->zmq_router;

    mq_tag_t from_tag = {0,};
    if (zmq_recv(zmq_router, &from_tag, sizeof(mq_tag_t), ZMQ_DONTWAIT) < 0 || 
            from_tag.via_interhub == true)
        return; // CAUTION!!! prevent node loop

    from_tag.via_interhub = true;

    /* simple log test */
    char log_buffer[MAX_LOG_SIZE] = {0,};
    sprintf(log_buffer, "BROKER SAY] hey~! from [%s] to [%s] un-routable, send it to you with label [%s]", 
            from_tag.svc, svc_host, INTERHUB_NAME);
    if (zmq_send(zmq_router, INTERHUB_NAME, strlen(INTERHUB_NAME), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0 &&
        zmq_send(zmq_router, MQ_LOG, strlen(MQ_LOG), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0) 
        zmq_send(zmq_router, log_buffer, strlen(log_buffer), ZMQ_DONTWAIT);

    /* send to interhub */
    if (zmq_send(zmq_router, INTERHUB_NAME, strlen(INTERHUB_NAME), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0 &&
            zmq_send(zmq_router, MQ_REQ, strlen(MQ_REQ), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0 &&
            zmq_send(zmq_router, svc_host, strlen(svc_host), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0 &&
            zmq_send(zmq_router, &from_tag, sizeof(mq_tag_t), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0) {
        relay_snd_more(zmq_router, zmq_router, NULL);
    }
}

void relay_res_to_interhub(main_ctx_t *main_ctx)
{
    fprintf(stderr, "dbg] %s called \n", __func__);

    void *zmq_router = main_ctx->zmq_router;

    if (zmq_send(zmq_router, INTERHUB_NAME, strlen(INTERHUB_NAME), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0 &&
            zmq_send(zmq_router, MQ_RES_INTERHUB, strlen(MQ_RES_INTERHUB), ZMQ_DONTWAIT|ZMQ_SNDMORE) > 0) {
        int res = relay_snd_more(zmq_router, zmq_router, NULL);
        //fprintf(stderr, "dbg!!!] relay done res(%d)\n", res);
    }
}

void drain_left_part_message(void *zmq_sock)
{
    int more = 0;
    size_t more_size = sizeof(more);
    char buffer[MAX_BUFF_SIZE] = {0,};
    while (zmq_getsockopt(zmq_sock, ZMQ_RCVMORE, &more, &more_size) == 0) {

        if (!more)
            break;
        else {
            zmq_recv(zmq_sock, buffer, MAX_BUFF_SIZE, ZMQ_DONTWAIT);
        }
    }
}

void router_callback(evutil_socket_t fd, short what, void *arg)
{
    main_ctx_t *main_ctx = (main_ctx_t *)arg;
    void *zmq_router = main_ctx->zmq_router;

    char from_msg[128] = {0,};
    char msg_type[128] = {0,};
    int from_len = 0, type_len = 0;

    while ((from_len = zmq_recv(zmq_router, from_msg, sizeof(from_msg), ZMQ_DONTWAIT)) >= 0 &&
           (type_len = zmq_recv(zmq_router, msg_type, sizeof(msg_type), ZMQ_DONTWAIT)) >= 0) {
        from_msg[from_len] = '\0';
        msg_type[type_len] = '\0';

        //fprintf(stderr, "dbg] router recv from(%s) type(%s)\n", from_msg, msg_type);

        if (!strcmp(msg_type, MQ_RES_INTERHUB)) {
            relay_res_to_interhub(main_ctx);
        } else {
            void *dest = NULL;
            if (!strcmp(msg_type, MQ_REQ_INTERHUB)) 
                dest = main_ctx->zmq_router;
            else if (!strcmp(msg_type, MQ_RES)) 
                dest = main_ctx->zmq_pub;
            else if (!strcmp(msg_type, MQ_REQ))
                dest = main_ctx->zmq_router;
            else if (!strcmp(msg_type, MQ_LOG)) {
                char log_buff[MAX_LOG_SIZE] = {0,};
                /* dont relay */
                if (zmq_recv(zmq_router, log_buff, MAX_LOG_SIZE, ZMQ_DONTWAIT) >= 0) 
                    fprintf(stderr, "relayed log] - %s -\n", log_buff);
                continue;
            } else {
                /* dont relay */
                fprintf(stderr, "err] unknown msg_type [%s] received\n", msg_type);
                drain_left_part_message(zmq_router);
                continue;
            }

            char svc_host[128] = {0,};
            if (relay_snd_more(zmq_router, dest, svc_host) < 0 && errno == EHOSTUNREACH) {
                if (main_ctx->zmq_pub_to_interhub != NULL) {
                    //fprintf(stderr, "dbg!!!] interhub exist send to it!\n");
                    relay_req_to_interhub(main_ctx, svc_host);
                } else {
                    //fprintf(stderr, "dbg!!!] interhub not exist drain remain part\n");
                    drain_left_part_message(zmq_router);
                }
            }
        }
    }
}

void publish_conf_from_file(main_ctx_t *main_ctx, char *filename, char *svc, char *conf)
{
    char dir_file[1024] = {0,};
    int len = sprintf(dir_file, CONFIG_DIR, getenv("HOME"));
    sprintf(dir_file + len, "/%s", filename);

    FILE *fp = fopen(dir_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "log] cant fp to file [%s]\n", dir_file);
        return;
    }

    if (zmq_send(main_ctx->zmq_pub, svc, strlen(svc), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
        zmq_send(main_ctx->zmq_pub, MQ_CONF, strlen(MQ_CONF), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0 &&
        zmq_send(main_ctx->zmq_pub, conf, strlen(conf), ZMQ_DONTWAIT|ZMQ_SNDMORE) >= 0) {
        char buff[MAX_BUFF_SIZE] = {0,};
        while(fgets(buff, 1024, fp)) {
            if (zmq_send(main_ctx->zmq_pub, buff, strlen(buff), ZMQ_DONTWAIT|ZMQ_SNDMORE) < 0) {
                break;
            }
        }
        zmq_send(main_ctx->zmq_pub, NULL, 0, ZMQ_DONTWAIT); // send null byte, we done
    }
    fclose(fp);

    return;
}

void check_inotify_event(main_ctx_t *main_ctx, struct inotify_event *ievent)
{
    char received_fname[1024] = {0,};

    if (ievent->len > 0) {
        fprintf(stderr, "dbg] we recv inotify about [%s]\n", ievent->name);
        strcpy(received_fname, ievent->name);
    } else {
        return;
    }

    char *ext = strchr(received_fname, '.');
    if (ext && strcmp(ext + 1, "conf")) {
        fprintf(stderr, "err] extension is not conf\n");
        return;
    }

    char svc_name[1024] = {0,};
    char conf_name[1024] = {0,};
    char *ptr = NULL;

    *ext = '\0';
    if ((ptr = strrchr(received_fname, '_')) != NULL) {
        sprintf(conf_name, "%s", ptr + 1);
        *ptr = '\0';
        sprintf(svc_name, "%s", received_fname);

        fprintf(stderr, "log] filename mean service[%s] config_name[%s]\n", svc_name, conf_name);
        publish_conf_from_file(main_ctx, ievent->name, svc_name, conf_name);
        fprintf(stderr, "dbg] job returned!\n");
        return;
    }

    fprintf(stderr, "err] filename not match service_conf.conf\n");
    return;
}

static void config_watch_callback(struct bufferevent* bev, void* args)
{
    main_ctx_t *main_ctx = (main_ctx_t *)args;

    char buf[MAX_BUFF_SIZE] = {0,};
    size_t numRead = bufferevent_read(bev, buf, MAX_BUFF_SIZE);
    char *ptr;
    for (ptr = buf; ptr < buf + numRead; ) {

        struct inotify_event *event = (struct inotify_event*)ptr;
        check_inotify_event(main_ctx, event);

        ptr += sizeof(struct inotify_event) + event->len;
    }
}

void create_config_watch(struct event_base *evbase, main_ctx_t *main_ctx)
{
    if ((main_ctx->inotifyFd = inotify_init()) == -1) {
        fprintf(stderr, "err] inotify watch init fail, check libc6 library\n");
        return;
    }

    char config_dir[1024] = {0,};
    sprintf(config_dir, CONFIG_DIR, getenv("HOME"));

    /* we check only close write */
    if ((main_ctx->wd = inotify_add_watch(main_ctx->inotifyFd, config_dir, IN_CLOSE_WRITE | IN_MOVED_TO)) == -1) {
        fprintf(stderr, "err] config dir [%s] not exist\n", config_dir);
        return;
    }

    // TODO!!! check non-blocking mode set ?????
    // TODO!!! REMOVE FD on program exit !!!!!!!
    struct bufferevent *ev_watch_config = bufferevent_socket_new(evbase, main_ctx->inotifyFd, /*BEV_OPT_CLOSE_ON_FREE*/0);
    bufferevent_setcb(ev_watch_config, config_watch_callback, NULL, NULL, main_ctx);
    bufferevent_enable(ev_watch_config, EV_READ);

    return;
}

static void *svc_router_thrd(void *arg)
{

    main_ctx_t *main_ctx = (main_ctx_t *)arg;

    /* own evbase */
    struct event_base *evbase = event_base_new();

    void *zmq_context = zmq_init(4); // TODO modify

    /* create publisher */
    main_ctx->zmq_pub = zmq_socket(zmq_context, ZMQ_PUB);
    zero_linger(main_ctx->zmq_pub);
    zmq_bind(main_ctx->zmq_pub, LISTEN_ADDR_PUB);

    /* creater router */
    main_ctx->zmq_router = zmq_socket(zmq_context, ZMQ_ROUTER);
    zero_linger(main_ctx->zmq_router);

    int route_opt = 1;
    size_t route_opt_size = sizeof(route_opt);
    zmq_setsockopt(main_ctx->zmq_router, ZMQ_ROUTER_MANDATORY, &route_opt, route_opt_size);
    /* multibind - for local dealer */
    zmq_bind(main_ctx->zmq_router, LISTEN_ADDR_ROUTER);
    /* multibind - for tcp interhub */
    zmq_bind(main_ctx->zmq_router, MQ_ROUTE_ADDR);

    add_fd_read_callback(evbase, main_ctx->zmq_router, router_callback, main_ctx);

    /* inotify watch test */
    create_config_watch(evbase, main_ctx);

    event_base_loop(evbase, EVLOOP_NO_EXIT_ON_EMPTY);

    // never reach here
    event_base_free (evbase);

    fprintf(stderr, "err] router thrd exited!\n");
}

static void *svc_dealer_thrd(void *arg)
{

    main_ctx_t *main_ctx = (main_ctx_t *)arg;

    /* own evbase */
    main_ctx->evbase = event_base_new();
    main_ctx->zmq_context = zmq_init(4); // TODO modify

    event_base_loop(main_ctx->evbase, EVLOOP_NO_EXIT_ON_EMPTY);

    // never reach here
    event_base_free (main_ctx->evbase);

    fprintf(stderr, "err] svc thrd exited!\n");
}

void initialize(main_ctx_t *main_ctx)
{

    mq_regi_t self_regi = {0,};

    char hostname[128] = {0, };
    gethostname(hostname, sizeof(hostname));
    sprintf(self_regi.svc_host, "%s", hostname);
    sprintf(self_regi.tag.name, "Broker");

    main_ctx->root_node = NewNode(NT_BROKER, &self_regi, NULL);
}

static int gprof_exit;
void main_callback(evutil_socket_t fd, short what, void *arg)
{
#if 0
    fprintf(stderr, "main] tick [%d]\n", gprof_exit++);
    if (gprof_exit >= 100) exit(0);
#endif
}

int main(int argc, char **argv)
{
    /* libevent, multi-thread safe code (always locked) */
    evthread_use_pthreads();

    main_ctx_t main_ctx = {0,};

    if (argc != 1 && argc != 2) {
        fprintf(stderr, "usage: %s [interhub tcp_address]\n", __progname);
        exit(0);
    } else {
        initialize(&main_ctx);
        if (argc == 2)
            sprintf(main_ctx.interhub_address, "tcp://%s:%d", argv[1], MQ_HUB_PORT);
    }

    // TODO!!! thread initial
    pthread_t service_worker = {0,};
    pthread_create(&service_worker, NULL, svc_dealer_thrd, &main_ctx);

    pthread_t router_worker = {0,};
    pthread_create(&router_worker, NULL, svc_router_thrd, &main_ctx);

    pthread_t regi_worker = {0,};
    pthread_create(&regi_worker, NULL, svc_regi_thrd, &main_ctx);

    /* own evbase */
    struct event_base *evbase = event_base_new();

    struct timeval tm_tick = {0, TM_TICK};
    struct event *ev_main;
    ev_main = event_new (evbase, -1, EV_PERSIST, main_callback, NULL);
    event_add(ev_main, &tm_tick);

    event_base_loop(evbase, EVLOOP_NO_EXIT_ON_EMPTY);

    fprintf(stderr, "err] main exited!\n");

    return 0;
}
