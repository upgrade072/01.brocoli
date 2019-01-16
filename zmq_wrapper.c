
#include "lib_wrapzmq.h"

extern char *__progname;

void usage(int argc, char **argv, client_sock_t *cli_sock)
{
    if (argc == 2) {
        cli_sock->via_ipc = true;
        cli_sock->my_svc_port = atoi(argv[1]);
        fprintf(stderr, "%s svc_port[%d] connect via ipc\n", __progname, cli_sock->my_svc_port);
    } else if (argc == 3) {
        cli_sock->via_ipc = false;
        cli_sock->my_svc_port = atoi(argv[1]);
        sprintf(cli_sock->tcp_address, "%s", argv[2]);
        fprintf(stderr, "%s svc_port[%d] connect via tcp to %s\n", __progname, cli_sock->my_svc_port, cli_sock->tcp_address);
    } else {
        fprintf(stderr, "usage) %s my_svc_port [broker_ip]\n", __progname);
        exit(0);
    }
}

void crt_res_tag(char *my_svc_name, mq_regi_t *svc_regi, client_sock_t *cli_sock)
{
    int pid = getpid();

    //gethostname(svc_regi->tag.host, sizeof(svc_regi->tag.host));
    sprintf(svc_regi->tag.svc, "%s", my_svc_name);
    sprintf(svc_regi->tag.name, "%s_%d", __progname, pid);
    svc_regi->port = cli_sock->my_svc_port * 10 / 10;
    svc_regi->conn_type = (cli_sock->via_ipc == true ? CONN_IPC : CONN_TCP);

    svc_regi->before = 0;
    time(&svc_regi->current);
}

void make_addr_str(char *buf, int sock_type, mq_regi_t *svc_regi, client_sock_t *cli_sock)
{
    if (cli_sock->via_ipc) {
        switch (sock_type) {
            case ZMQ_SUB:  sprintf(buf, "ipc://%s_PUBL.ipc", svc_regi->tag.svc); break;
            case ZMQ_PULL: sprintf(buf, "ipc://%s_PUSH.ipc", svc_regi->tag.svc); break;
            case ZMQ_PUSH: sprintf(buf, "ipc://%s_PULL.ipc", svc_regi->tag.svc); break;
            default:
               fprintf(stderr, "something wrong!\n");
               exit(0);
        }
    } else {
        switch (sock_type) {
            case ZMQ_SUB:  sprintf(buf, "tcp://%s:%d", cli_sock->tcp_address, svc_regi->port); break;
            case ZMQ_PULL: sprintf(buf, "tcp://%s:%d", cli_sock->tcp_address, svc_regi->port+1); break;
            case ZMQ_PUSH: sprintf(buf, "tcp://%s:%d", cli_sock->tcp_address, svc_regi->port+2); break;
            default:
               fprintf(stderr, "something wrong!\n");
               exit(0);
        }
    }
}

void crt_cli_sock(client_sock_t *cli_sock, mq_regi_t *svc_regi, char *sub_addr)
{
    char addr_buff[1024] = {0,};

    cli_sock->zmq_context = zmq_init(1);
    cli_sock->zmq_pub = zmq_socket(cli_sock->zmq_context, ZMQ_PUB);
    cli_sock->zmq_sub = zmq_socket(cli_sock->zmq_context, ZMQ_SUB);
    cli_sock->zmq_pull = zmq_socket(cli_sock->zmq_context, ZMQ_PULL);
    cli_sock->zmq_push = zmq_socket(cli_sock->zmq_context, ZMQ_PUSH);
    cli_sock->zmq_conf = zmq_socket(cli_sock->zmq_context, ZMQ_SUB);

    zmq_setsockopt (cli_sock->zmq_sub, ZMQ_SUBSCRIBE, svc_regi->tag.name, strlen(svc_regi->tag.name));
    zmq_setsockopt (cli_sock->zmq_conf, ZMQ_SUBSCRIBE, MQ_CONF, strlen(MQ_CONF));

    zero_linger(cli_sock->zmq_pub);
    zero_linger(cli_sock->zmq_sub);
    zero_linger(cli_sock->zmq_pull);
    zero_linger(cli_sock->zmq_push);
    zero_linger(cli_sock->zmq_conf);

    sprintf(addr_buff, "tcp://%s:%d", sub_addr, MQ_REGI_PORT);
    zmq_connect(cli_sock->zmq_pub, addr_buff);
    make_addr_str(addr_buff, ZMQ_SUB, svc_regi, cli_sock);
    zmq_connect(cli_sock->zmq_sub, addr_buff); 
    make_addr_str(addr_buff, ZMQ_PULL, svc_regi, cli_sock);
    zmq_connect(cli_sock->zmq_pull, addr_buff);
    make_addr_str(addr_buff, ZMQ_PUSH, svc_regi, cli_sock);
    zmq_connect(cli_sock->zmq_push, addr_buff);
    make_addr_str(addr_buff, ZMQ_SUB, svc_regi, cli_sock);
    zmq_connect(cli_sock->zmq_conf, addr_buff); 

}

typedef enum snd_or_rcv {
    EN_SND = 0,
    EN_RCV,
    EN_NONE
} snd_or_rcv_t;

#define CHECK_N_RETURN(sock, byte, snd_or_rcv) \
if (byte <= 0) return (-1); \
if (snd_or_rcv == EN_SND) { sock->sndrcv.send_byte += byte; } \
else if (snd_or_rcv == EN_RCV) { sock->sndrcv.recv_byte += byte; }

int mq_regi(client_sock_t *cli_sock, mq_regi_t *svc_regi)
{
    int byte = 0;

    time(&svc_regi->current);

    if ((svc_regi->current - svc_regi->before) >= 1) 
        svc_regi->before = svc_regi->current;
    else
        return (0);

    // check traffic
    memcpy(&svc_regi->sndrcv, &cli_sock->sndrcv, sizeof(mq_sndrcv_t));
    memset(&cli_sock->sndrcv, 0x00, sizeof(mq_sndrcv_t));

    // send REG code
    byte = zmq_send(cli_sock->zmq_pub, MQ_REG, strlen(MQ_REG), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send message body
    byte = zmq_send(cli_sock->zmq_pub, svc_regi, sizeof(mq_regi_t), ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);
}

int mq_send_req(client_sock_t *cli_sock, mq_regi_t *svc_regi, const char *tgsvc_name, const void *buf, size_t len)
{
    int byte = 0;

    // send REQ code
    byte = zmq_send(cli_sock->zmq_push, MQ_REQ, strlen(MQ_REQ), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send TGT SVC name
    byte = zmq_send(cli_sock->zmq_push, tgsvc_name, strlen(tgsvc_name), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send from tag
    byte = zmq_send(cli_sock->zmq_push, &svc_regi->tag, sizeof(mq_tag_t), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send message body
    byte = zmq_send(cli_sock->zmq_push, buf, len, ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_SND);

    cli_sock->sndrcv.send_cnt++;
    return 0;
}

int mq_send_log(client_sock_t *cli_sock, mq_regi_t *svc_regi, char *format, ...)
{
    int byte = 0;

    // send LOG code
    byte = zmq_send(cli_sock->zmq_push, MQ_LOG, strlen(MQ_LOG), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    int len = 0;
    char buffer[MAX_LOG_SIZE] = {0,};
    len = sprintf(buffer, "%s|%s] ", svc_regi->tag.svc, svc_regi->tag.name);

    va_list args;
    va_start(args, format);
    len = vsnprintf(buffer + len, MAX_LOG_SIZE - len, format, args);

    // send message body
    byte = zmq_send(cli_sock->zmq_push, buffer, strlen(buffer), ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    return 0;
}

int mq_send_res(client_sock_t *cli_sock, mq_tag_t *dest_tag, const void *buf, size_t len)
{
    int byte = 0;

    // send RES code
    if (dest_tag->via_interhub == true)
        byte = zmq_send(cli_sock->zmq_push, MQ_RES_INTERHUB, strlen(MQ_RES_INTERHUB), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    else
        byte = zmq_send(cli_sock->zmq_push, MQ_RES, strlen(MQ_RES), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // if RES via interhub add BROKER ID
    if (dest_tag->via_interhub == true)
        byte = zmq_send(cli_sock->zmq_push, dest_tag->broker, strlen(dest_tag->broker), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send TGT SVC
    byte = zmq_send(cli_sock->zmq_push, dest_tag->svc, strlen(dest_tag->svc), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send TGT NAME
    byte = zmq_send(cli_sock->zmq_push, dest_tag->name, strlen(dest_tag->name), ZMQ_DONTWAIT|ZMQ_SNDMORE);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // send message body
    byte = zmq_send(cli_sock->zmq_push, buf, len, ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_SND);

    cli_sock->sndrcv.send_cnt++;
    return 0;
}

#define CHECK_READ_MORE_EXIST(sock, more, moresize)         \
    zmq_getsockopt(sock, ZMQ_RCVMORE, &more, &more_size);   \
    if (!more) return (-1);

#define MAKE_EOF(buf, byte)                                \
    {                                                       \
        char *ptr = (char *)buf;                    \
        ptr[byte > 0 ? byte : 0] = '\0';                    \
    }

int mq_recv_req(client_sock_t *cli_sock, mq_tag_t *tag, void *buf, size_t len)
{
    int byte = 0, more = 0;
    size_t more_size = sizeof(more);

    // read from tag
    byte = zmq_recv(cli_sock->zmq_pull, tag, sizeof(mq_tag_t), ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // read message body
    CHECK_READ_MORE_EXIST(cli_sock->zmq_pull, more, moresize);
    byte = zmq_recv(cli_sock->zmq_pull, buf, len, ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_RCV);

    cli_sock->sndrcv.recv_cnt++;
    return 0;
}

int mq_recv_res(client_sock_t *cli_sock, void *buf, size_t len)
{
    int byte = 0, more = 0;
    size_t more_size = sizeof(more);
    char name[128] = {0,};

    // remove name tag
    byte = zmq_recv(cli_sock->zmq_sub, name, sizeof(name), ZMQ_DONTWAIT);
    CHECK_N_RETURN(cli_sock, byte, EN_NONE);

    // receive data body
    byte = zmq_recv(cli_sock->zmq_sub, buf, len, ZMQ_DONTWAIT);
    MAKE_EOF(buf, byte);
    CHECK_N_RETURN(cli_sock, byte, EN_RCV);

    cli_sock->sndrcv.recv_cnt++;
    return 0;
}

int mq_recv_conf_name(client_sock_t *cli_sock, void *buf, size_t len)
{
    int byte = 0, more = 0;
    size_t more_size = sizeof(more);
    char name[128] = {0,};

    // remove MQ_CONF tag
    byte = zmq_recv(cli_sock->zmq_conf, name, sizeof(name), ZMQ_DONTWAIT);
    CHECK_READ_MORE_EXIST(cli_sock->zmq_conf, more, moresize);

    /* return conf name */
    byte = zmq_recv(cli_sock->zmq_conf, buf, len, ZMQ_DONTWAIT);
    MAKE_EOF(buf, byte);
    CHECK_READ_MORE_EXIST(cli_sock->zmq_conf, more, moresize);

    return 0;
}

int mq_recv_conf_body(client_sock_t *cli_sock, void *buf, size_t len)
{
    int byte = 0, more = 0;
    size_t more_size = sizeof(more);

    /* return conf body */
    byte = zmq_recv(cli_sock->zmq_conf, buf, len, ZMQ_DONTWAIT);
    MAKE_EOF(buf, byte);
    CHECK_READ_MORE_EXIST(cli_sock->zmq_conf, more, moresize);

    return 0;
}

void zero_linger (void *socket)
{
    int linger = 0;
    int rc = zmq_setsockopt (socket, ZMQ_LINGER, &linger, sizeof(linger));
    assert (rc == 0 || errno == ETERM);

    int rcv_hwm = 40000; // TODO !!! value adjust
    rc = zmq_setsockopt (socket, ZMQ_RCVHWM, &rcv_hwm, sizeof(rcv_hwm));
    assert (rc == 0 || errno == ETERM);

    int snd_hwm = 40000;
    rc = zmq_setsockopt (socket, ZMQ_SNDHWM, &snd_hwm, sizeof(snd_hwm));
    assert (rc == 0 || errno == ETERM);

    int rcv_buf = 1024 * 512;
    rc = zmq_setsockopt (socket, ZMQ_RCVBUF, &rcv_buf, sizeof(rcv_buf));
    assert (rc == 0 || errno == ETERM);

    int snd_buf = 1024 * 512;
    rc = zmq_setsockopt (socket, ZMQ_SNDBUF, &snd_buf, sizeof(snd_buf));
    assert (rc == 0 || errno == ETERM);

    /* eliminate FIN_WAIT1 in handover state */
    // we use keepalive
    int keepalive = 1;
    rc = zmq_setsockopt (socket, ZMQ_TCP_KEEPALIVE, &keepalive, sizeof(keepalive));
    // if no resp before we send 5, we drop conn
    int keepalive_cnt = 5;
    rc = zmq_setsockopt (socket, ZMQ_TCP_KEEPALIVE, &keepalive_cnt, sizeof(keepalive_cnt));
    // if in two sec with no msg, we check conn
    int keepalive_idle = 2;
    rc = zmq_setsockopt (socket, ZMQ_TCP_KEEPALIVE, &keepalive_idle, sizeof(keepalive_idle));
    // probe packet sended by 1 sec
    int keepalive_intvl = 1;
    rc = zmq_setsockopt (socket, ZMQ_TCP_KEEPALIVE, &keepalive_intvl, sizeof(keepalive_intvl));
}

/* caution!!! don't assign * by func(arg), just return and get */
struct event *add_fd_read_callback(struct event_base *evbase, void *zmq_sock, void (*cb)(evutil_socket_t, short, void *), void *arg)
{
    int sock_fd = 0;
    size_t opt_len = sizeof(sock_fd);

    if (zmq_getsockopt(zmq_sock, ZMQ_FD, &sock_fd, &opt_len) != 0) {
        fprintf(stderr, "err] cant getsockopt() from zmq_sock !!\n");
        exit(0);
    }

    /* zeromq use edge-trigger (not level-trigger), so add more time check for safe */
    //struct timeval tm_recheck = {0, TM_INTERVAL};
    struct timeval tm_recheck = {0, TM_POLLING};

    struct event *ev_assign = event_new(evbase, sock_fd, EV_ET|EV_READ|EV_PERSIST, cb, arg);
    event_add(ev_assign, &tm_recheck);
    return ev_assign;
}

int relay_snd_more(void *sock_from, void *sock_to, char *svc_host)
{
    int recv_byte = 0;
    int more = 0;
    size_t more_size = sizeof(more);

    if (sock_from == NULL || sock_to == NULL) {
        fprintf(stderr, "err] from|to sock is null!\n");
        return 0;
    }

    while (1) {
        char buffer[MAX_BUFF_SIZE] = {0,}; // TODO!!! modify size
        int res = 0;

        /* receive */
        if ((recv_byte = zmq_recv(sock_from, buffer, sizeof(buffer), ZMQ_DONTWAIT)) < 0)
            return 0;

        /* check last-part message & relay */
        zmq_getsockopt(sock_from, ZMQ_RCVMORE, &more, &more_size);
RETRY:
        res = zmq_send(sock_to, buffer, recv_byte, ZMQ_DONTWAIT| (more ? ZMQ_SNDMORE : 0));

        /* check send result, if routing fail occur, set unrouted svc host */
        if (res < 0) {
#if 1
            if (errno != EINTR && errno != EHOSTUNREACH) {
                fprintf(stderr, "err] (%s) errno (%d:%s) ", __func__, errno, zmq_strerror(errno));
            }
#else
            // TODO maybe we drain lest of message
            fprintf(stderr, "err] (%s) errno (%d:%s)\n", __func__, errno, zmq_strerror(errno));
#endif
            if (errno == EHOSTUNREACH && svc_host != NULL)  {
                buffer[recv_byte > 0 ? recv_byte : 0] = '\0';
                sprintf(svc_host, "%s", buffer);
                fprintf(stderr, "dbg!!!] we need svc host (%s)\n", svc_host);
                return (-1);
            } else if (errno == EINTR) {
                goto RETRY;
            } else {
                fprintf(stderr, "\n");
            }
            return (-1);
        }

        /* all message-part relayed~! */
        if (!more)
            return 0;
    }
}
