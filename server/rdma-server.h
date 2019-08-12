#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H


#include "rdma-common.h"



//创建监听服务
void create_server(int port);


//服务端wc处理函数
void server_on_completion(struct ibv_wc *wc);



#endif
