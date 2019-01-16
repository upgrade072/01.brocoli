#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>

#include "lib_wrapzmq.h"
#include "zhelpers.h"
#include "mq.h"

char MYSVC_NAME[] = "AHIF_V100";
char LOCALHOST[] = "localhost";

char TGSVC_NAME[12][128] = {
    "REGIB_V100",
    "PSIB_V100",
    "FEAIB_V090",
    "SMSIB_V100" // no send
};
char DATA[] = "1234567890123456789012345678901234567890";

char LINEFOLD[] = "=========================================================================\n";

extern char *__progname;

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

#if 1
        // send request
        for (int i = 0; i < 9; i++) {
            srand(time(NULL) + i);
            int dest_index = rand() % 3;
            mq_send_req(&main_sock, &svc_regi, TGSVC_NAME[dest_index], DATA, strlen(DATA));
        }
#else
            mq_send_req(&main_sock, &svc_regi, "PSIB_V100", DATA, strlen(DATA));
#endif

        // recv res
        char buff[1024] = {0,};
        while (mq_recv_res(&main_sock, buff, sizeof(buff)) == 0) {
            fprintf(stderr, "recv res %s\n", buff);

            // send log (maybe trace also)
            mq_send_log(&main_sock, &svc_regi, "recv res %s", buff);
        }

        // config change
        if (mq_recv_conf_name(&main_sock, buff, sizeof(buff)) == 0) {
            fprintf(stderr, LINEFOLD);
            fprintf(stderr, "recv conf name [%s]\n", buff);
            fprintf(stderr, LINEFOLD);
            while (mq_recv_conf_body(&main_sock, buff, sizeof(buff)) == 0) {
                fprintf(stderr, "%s", buff);
                memset(buff, 0x00, sizeof(buff));
            }
            fprintf(stderr, LINEFOLD);
        }

        sleep(1);
    }
}
