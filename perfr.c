#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include "lib_wrapzmq.h"
#include "zhelpers.h"
#include "mq.h"

char MYSVC_NAME[] = "PERFR_V100";
char LOCALHOST[] = "localhost";

extern char *__progname;

typedef struct sample_recv {
    mq_tag_t from_tag;
    char occupied;
    char body[MAX_BUFF_SIZE];
} sample_recv_t;

int main(int argc, char **argv)
{
    client_sock_t main_sock = {0,};
    mq_regi_t svc_regi = {0,};

    usage(argc, argv, &main_sock);
    crt_res_tag(MYSVC_NAME, &svc_regi, &main_sock);
    crt_cli_sock(&main_sock, &svc_regi, (argc == 2) ? LOCALHOST : argv[2]);

    while(1) {
        // regi & heartbeat
        mq_regi(&main_sock, &svc_regi);

        int job_is_comming = true;
        while (job_is_comming) {
            // recv request
            sample_recv_t rcv_data = {0, };
            if (mq_recv_req(&main_sock, &rcv_data.from_tag, rcv_data.body, sizeof(rcv_data.body)) >= 0)  {
                rcv_data.occupied = true;
                job_is_comming = true;

                mq_send_res(&main_sock, &rcv_data.from_tag, rcv_data.body, strlen(rcv_data.body));
            } else {
                rcv_data.occupied = false;
                job_is_comming = false;
            }
        }
        usleep(1);
    }
}
