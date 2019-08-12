#include "rdma-server.h"


//处理连接请求，即连接创建之前的准备工作
static void on_connect_request(struct rdma_cm_id *id);

//连接创建之后的操作
static void on_connection(struct rdma_cm_id *id);




void create_server(int port)
{
    /***
    * 创建监听服务， 可以指定端口，但不指定ip，因此从cc获取wc的线程个数跟设备个数一致
    ****/
    struct sockaddr_in addr;
    struct rdma_cm_id *listener = NULL;
    struct rdma_event_channel *ec = NULL;
    struct rdma_cm_event *event = NULL;
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
     
    TEST_Z(ec = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP));
    TEST_NZ(rdma_bind_addr(listener, (struct sockaddr *)&addr));
    TEST_NZ(rdma_listen(listener, 10)); /* backlog=10 is arbitrary */
    
    //注册wc处理函数
    set_on_compltion_fn(server_on_completion);

    uint16_t sport = ntohs(rdma_get_src_port(listener));

    printf("listening on port %d.\n", sport);
    
    
    /*** 因为会维持长连接，因此连接初始化资源和线程的开销可以忽略不计
    //初始化svr_ctx, 获取每一个设备上的ibv_context，并基于设备创建事件通道cc, 然后基于cc创建ibv事件监听线程
    int device_num = 0;
    struct ibv_device **ibb = ibv_get_device_list(&device_num);
    if(!device_num)
    {
        die("device number is 0 , there is no rdma card");
    }
    int i = 0;
    
    svr_ctx = (struct context *)malloc(sizeof(struct context));
    struct context  *tmp_ctx = NULL;
    while(i++ < device_num)
    {
        struct context  *new_ctx = (struct context *)malloc(sizeof(struct context));
        new_ctx->ctx = bv_open_device(*ibb);
        new_ctx->guid = ibv_get_device_guid(*ibb);
        new_ctx->next = NULL;
        new_ctx->comp_channel = ibv_create_comp_channel(new_ctx->ctx);
        new_ctx->pd = ibv_alloc_pd(new_ctx->ctx);
        new_ctx->cq = ibv_create_cq(new_ctx->ctx, 10, NULL, new_ctx->comp_channel, 0);
        TEST_NZ(ibv_req_notify_cq(new_ctx->cq, 0));
        
        pthread_t cq_poller_thread;
        TEST_NZ(pthread_create(&cq_poller_thread, NULL, poll_cq, comp_channel));
        
        ibb++;
        
        if(i == 1)
        {
            svr_ctx = new_ctx;
        }
        else
        {
            tmp_ctx->next = new_ctx;
        }
        
        tmp->ctx = new_ctx;
    }
    //初始化svr_ctx结束
    ***/
    

    
    
    while(rdma_get_cm_event(ec, &event) == 0)
    {
        struct rdma_cm_event event_copy;
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        

        if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST)
            on_connect_request(event_copy.id);
        else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
            on_connection(event_copy.id);
        else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED) 
            on_disconnect(event_copy.id);
        else
            die("on_event: unknown event.");
    }
    
    rdma_destroy_event_channel(ec);
    
    return;
}

static void on_connect_request(struct rdma_cm_id *id)
{
    /***长连接场景下暂不启用
    //获取该连接绑定的网卡设备的 guid
    uint64_t  cur_guid = ibv_get_device_guid(id->verbs->device);
    
    struct context *tmp_ctx = svr_ctx;
    while(tmp_ctx)
    {
        if(cur_guid == tmp_ctx->guid)
            break;

        tmp_ctx = tmp_ctx->next;
    }
    
    if(!tmp_ctx)
    {
        die("can not find the ibv_context");
    }
    **/
    
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_comp_channel *comp_channel;
    
    TEST_Z(pd = ibv_alloc_pd(id->verbs));
    TEST_Z(comp_channel = ibv_create_comp_channel(id->verbs));
    TEST_Z(cq = ibv_create_cq(id->verbs, 10, NULL, comp_channel, 0)); /* cqe=10 is arbitrary */
    TEST_NZ(ibv_req_notify_cq(cq, 0));
    
    //创建完成队列的事件监听线程
    pthread_t cq_poller_thread;
    TEST_NZ(pthread_create(&cq_poller_thread, NULL, poll_cq, comp_channel));   

    
    
    //build qp_attr
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    
    
    TEST_NZ(rdma_create_qp(id, pd, &qp_attr));
    
    //build connection
    struct connection *conn;
    id->context = conn = (struct connection *)malloc(sizeof(struct connection));
    
    //用于存放发送消息的缓存， 设置local_read是默认的
    conn->send_msg = malloc(sizeof(struct message));
    TEST_Z(conn->send_msg_mr = ibv_reg_mr(pd,conn->send_msg,sizeof(struct message),0));
    
    //用于存放接收消息的缓存， 设置local_write用来存放接收到的消息
    conn->recv_msg = malloc(sizeof(struct message));
    TEST_Z(conn->recv_msg_mr = ibv_reg_mr(pd,conn->recv_msg,sizeof(struct message),IBV_ACCESS_LOCAL_WRITE));
    
    
    //存放数据, read操作时从对端内存拉取（poll）数据，write操作时向对端内存推送（push)数据,因此本地的缓存不会让对端访问
    conn->buffer = malloc(BIG_BUFFER_SIZE);
    TEST_Z(conn->buffer_mr = ibv_reg_mr(pd,conn->buffer,BIG_BUFFER_SIZE,IBV_ACCESS_LOCAL_WRITE));
    
    
    //todo  post receive 
    post_receive(id);
    
    
    //设置 cm_parms
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.initiator_depth = cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 7; /* infinite retry */
    
    //
    TEST_NZ(rdma_accept(id, &cm_params));
    
    return;
    
}


static void on_connection(struct rdma_cm_id *id)
{
    struct   sockaddr_in  *addr = (struct sockaddr_in  *) rdma_get_peer_addr(id);
    uint16_t port = rdma_get_dst_port(id);

    printf("***************connected from client  %s:%d ***************\n",inet_ntoa(addr->sin_addr),ntohs(port));
}



void server_on_completion(struct ibv_wc *wc)
{
    
    printf("================ one work completion event=======================\n");
    struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
    struct connection *conn = (struct connection *)id->context;
    
    if(wc->opcode & IBV_WC_RECV)
    {
        //接收到消息, receive 动作
        printf("receive ok\n");
        print_msg(conn->recv_msg);
        
        if(conn->recv_msg->msg_type == MSG_SEND_RECV)
        {
            //普通的发送操作,需要根据业务类型执行操作
            //todo
            sprintf(conn->send_msg->app_data,"我是服务端端的数据，我向客户端send的数据编号: 250");
            conn->send_msg->msg_type = MSG_SEND_RECV;
            send_message(id);

            post_receive(id);
        }
        else if(conn->recv_msg->msg_type == MSG_READ_REQUEST)
        {
            //客户端请求读取数据同时把自己的内存地址和操作key发送给了服务端
            conn->peer_addr = conn->recv_msg->remote_info.addr;
            conn->peer_rkey = conn->recv_msg->remote_info.rkey;
            
            enum ibv_wr_opcode  opcode= IBV_WR_RDMA_READ;
            rdma_read_write(id,opcode);     
        }
        else if(conn->recv_msg->msg_type == MSG_WRITE_REQUEST)
        {
            //客户端请求写数据同时把自己的内存地址和操作key发送给了服务端
            conn->peer_addr = conn->recv_msg->remote_info.addr;
            conn->peer_rkey = conn->recv_msg->remote_info.rkey;
            
            sprintf(conn->buffer,"我是服务端的数据，供服务端向客户端write数据！！！！");
            
            enum ibv_wr_opcode  opcode= IBV_WR_RDMA_WRITE;
            rdma_read_write(id,opcode);
        }
        else
        {
            printf("recevie a error msg_type : %d\n",conn->recv_msg->msg_type);
        }
        
    }
    else if(wc->opcode == IBV_WC_SEND)
    {
        //消息发送  send 动作,只需打印输出即可
        printf("send ok \n");
        print_msg(conn->send_msg);
    }
    else if(wc->opcode == IBV_WC_RDMA_WRITE)
    {
        //执行write动作完毕, 对端不会有任何事件，除非用 write immediate
        //打印所写的数据
        post_receive(id);
        printf("write  ok, data is : %s\n",conn->buffer);
        conn->send_msg->msg_type = MSG_DONE;
        send_message(id);
    }
    else if(wc->opcode == IBV_WC_RDMA_READ)
    {
        //执行read动作完毕，对端不会有任何事件, 除非用read immediate
        //打印读取到的信息
        post_receive(id);
        printf("read ok, data is : %s\n",conn->buffer);
        conn->send_msg->msg_type = MSG_DONE;
        send_message(id);
    }
    else
    {
        //error
        printf("opcode error:%d\n",wc->opcode);
    }
    
    return;

}
