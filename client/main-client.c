#include "rdma-client.h"



int main(int argc, char **argv)
{
    printf("client start , argc : %d \n", argc);
    if(argc != 3)
    {
        printf("usage:  ./rdma-client  read/write/send_recv  [ip]\n");
        return 0;
    }
    printf("000 : %s\n", argv[1]);

    
   if (strcmp(argv[1], "write") == 0)
       set_test_type(test_write);
   else if (strcmp(argv[1], "read") == 0)
       set_test_type(test_read);
   else if (strcmp(argv[1], "send_recv") == 0)
       set_test_type(test_send_recv);
   else
       return 0 ;

    int port = DEFAULT_PORT;
    connect_server(argv[2],port);
   
    return 0;
}
