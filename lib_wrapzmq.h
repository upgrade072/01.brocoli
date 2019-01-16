
#ifndef __LIBZMQ_INVOKE__
#define __LIBZMQ_INVOKE__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include <event2/thread.h>
#include <event.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/dns.h>

#include <glib-2.0/gmodule.h>

#include "zhelpers.h"
#include "mq.h"

/* ------------------------- zmq_wrapper.c --------------------------- */
void    usage(int argc, char **argv, client_sock_t *cli_sock);
void    crt_res_tag(char *my_svc_name, mq_regi_t *svc_regi, client_sock_t *cli_sock);
void    make_addr_str(char *buff, int sock_type, mq_regi_t *svc_regi, client_sock_t *cli_sock);
void    crt_cli_sock(client_sock_t *cli_sock, mq_regi_t *svc_regi, char *sub_addr);
int     mq_regi(client_sock_t *cli_sock, mq_regi_t *svc_regi);
int     mq_send_req(client_sock_t *cli_sock, mq_regi_t *svc_regi, const char *tgsvc_name, const void *buf, size_t len);
int     mq_send_log(client_sock_t *cli_sock, mq_regi_t *svc_regi, char *format, ...);
int     mq_send_res(client_sock_t *cli_sock, mq_tag_t *dest_tag, const void *buf, size_t len);
int     mq_recv_req(client_sock_t *cli_sock, mq_tag_t *tag, void *buf, size_t len);
int     mq_recv_res(client_sock_t *cli_sock, void *buf, size_t len);
int     mq_recv_conf_name(client_sock_t *cli_sock, void *buff, size_t len);
int     mq_recv_conf_body(client_sock_t *cli_sock, void *buff, size_t len);
void    zero_linger (void *socket);
struct  event *add_fd_read_callback(struct event_base *evbase, void *zmq_sock, void (*cb)(evutil_socket_t, short, void *), void *arg);
int     relay_snd_more(void *sock_from, void *sock_to, char *svc_host);

#endif
