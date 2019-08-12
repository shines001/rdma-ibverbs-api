#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H


#include "rdma-common.h"


enum  opr_test{
    test_send_recv = 0,
    test_read,
    test_write
};


//创建监听服务
void connect_server(char *ip, int port);


//客户端wc处理函数
void client_on_completion(struct ibv_wc *wc);

void set_test_type(enum  opr_test  tst);




#endif
