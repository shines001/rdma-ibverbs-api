#include "rdma-server.h"



int main()
{
    printf("start server\n");
    create_server(DEFAULT_PORT);
   
    return 0;
}
