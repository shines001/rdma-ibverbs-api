#include "rdma-client.h"



//处理连接请求，即连接创建之前的准备工作
static void on_addr_resolved(struct rdma_cm_id *id);

//连接远端服务
static void on_route_resolved(struct rdma_cm_id *id);

//连接创建之后的操作
static void on_connection(struct rdma_cm_id *id);

static int cnt = 0;

static enum  opr_test  test_type = test_send_recv;

void set_test_type(enum  opr_test  tst)
{
    test_type = tst;
}


void connect_server(char *ip, int port)
{
    printf("ready to connect to server  %s:%d\n",ip, port);
    struct addrinfo *addr;
    struct rdma_cm_id *conn_id = NULL;
    struct rdma_event_channel *ec = NULL;
    struct rdma_cm_event *event = NULL;
    
    char s_port[32] = {0};
    sprintf(s_port,"%d",port);

    TEST_NZ(getaddrinfo((const char *)ip, (const char *)s_port, NULL, &addr));

    TEST_Z(ec = rdma_create_event_channel());
    TEST_NZ(rdma_create_id(ec, &conn_id, NULL, RDMA_PS_TCP));
    TEST_NZ(rdma_resolve_addr(conn_id, NULL, addr->ai_addr, TIMEOUT_MS));
    
    freeaddrinfo(addr);

    
    //注册wc处理函数
    set_on_compltion_fn(client_on_completion);
    
    
    //连接状态事件监听，负责处理连接的管理
    while(rdma_get_cm_event(ec, &event) == 0)
    {
        struct rdma_cm_event event_copy;
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        //printf("client  event %s come \n",rdma_event_str(event_copy.event));
        

        if(event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED)
            on_addr_resolved(event_copy.id);
        else if(event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED)
            on_route_resolved(event_copy.id);
        else if(event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
            on_connection(event_copy.id);
        else if(event_copy.event == RDMA_CM_EVENT_DISCONNECTED){
            on_disconnect(event_copy.id);
            break;
        } else
            die("on_event: unknown event.");
    }
    
    rdma_destroy_event_channel(ec);
    
    return;

}

static void on_addr_resolved(struct rdma_cm_id *id)
{
    struct ibv_pd *pd = NULL;
    struct ibv_cq *cq = NULL;
    struct ibv_comp_channel *comp_channel = NULL;

    
    TEST_Z(pd = ibv_alloc_pd(id->verbs));
    TEST_Z(comp_channel = ibv_create_comp_channel(id->verbs));
    TEST_Z(cq = ibv_create_cq(id->verbs, 10, NULL, comp_channel, 0)); /* cqe=10 is arbitrary */
    TEST_NZ(ibv_req_notify_cq(cq, 0));
    
    //创建完成队列的事件监听线程
    pthread_t cq_poller_thread;
    TEST_NZ(pthread_create(&cq_poller_thread, NULL, poll_cq, (void *) comp_channel));    
    
    
    /** build qp addr, 为创建qp做准备 **/
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    /** build qp attr end **/
    
    //创建 qp
    TEST_NZ(rdma_create_qp(id, pd, &qp_attr));
    
    //创建连接的用户数据，包括注册内存区，申请缓存
    struct connection *conn;
    id->context = conn = (struct connection *)malloc(sizeof(struct connection));
    
    //用于存放发送消息的缓存， 设置local_read是默认的
    conn->send_msg = malloc(sizeof(struct message));
    TEST_Z(conn->send_msg_mr = ibv_reg_mr(pd,conn->send_msg,sizeof(struct message),0));
    
    //用于存放接收消息的缓存， 设置local_write用来存放接收到的消息
    conn->recv_msg = malloc(sizeof(struct message));
    TEST_Z(conn->recv_msg_mr = ibv_reg_mr(pd,conn->recv_msg,sizeof(struct message),IBV_ACCESS_LOCAL_WRITE));
    
    
    //存放数据, 作为读写的发起者，发送请求之后，即允许对方操作自己的缓存,允许对端的读和写
    conn->buffer = malloc(BIG_BUFFER_SIZE);
    TEST_Z(conn->buffer_mr = ibv_reg_mr(pd,conn->buffer,BIG_BUFFER_SIZE,(IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE)));
    
    
    //post receive 接收控制消息
    post_receive(id); 
    
    
    TEST_NZ(rdma_resolve_route(id, TIMEOUT_MS));
    
    return;
}

static void on_route_resolved(struct rdma_cm_id *id)
{
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.initiator_depth = cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 7; /* infinite retry */

    TEST_NZ(rdma_connect(id, &cm_params));

  return;   
}

static void on_connection(struct rdma_cm_id *id)
{
    printf("****************** ^_^ connect ok ^_^ ********************** \n");
    //使用场景为局域网，因此当客户端和服务端创建连接后，服务端这边维持一个死循环，对一个线程间共享链表进行轮询
    //共享链表保存需要完成的动作、数据, 该链表需要加锁
    struct connection *conn = (struct connection *)id->context;

    if(test_type == test_send_recv)
    {
        memset(conn->send_msg->app_data,0x00,SMALL_BUFFER_SIZE);
        sprintf(conn->send_msg->app_data,"我是客户端端的第一条数据，这是我向服务端send的数据,编号:  %d", cnt++);
        conn->send_msg->msg_type = MSG_SEND_RECV;
        send_message(id);
    }
    else if(test_type == test_read) 
    {
        //read 
        memset(conn->buffer,0x00,SMALL_BUFFER_SIZE);
        sprintf(conn->buffer,"我是客户端端的第一条数据，这是服务端从我这读取到的数据,编号:  read-read");
        conn->send_msg->msg_type = MSG_READ_REQUEST;
        send_message(id);
    }
    else if(test_type == test_write) 
    {
        //write 
        conn->send_msg->msg_type = MSG_WRITE_REQUEST;
        send_message(id);
    }
    
    return;
}


void client_on_completion(struct ibv_wc *wc)
{
    printf("================ one work completion event=======================\n"); 
    struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
    struct connection *conn = (struct connection *)id->context;
    
    
    if(wc->opcode & IBV_WC_RECV)
    {
        //接收到服务端发送的mr信息，在read/write操作中主要是为了获取对端内存的addr,rkey
        printf("receive ok \n");
        print_msg(conn->recv_msg);
        
        if(conn->recv_msg->msg_type == MSG_SEND_RECV)
        {
            //普通的发送操作,需要根据业务类型执行操作
            //todo
            if(cnt < 3) {
                memset(conn->send_msg->app_data,0x00,SMALL_BUFFER_SIZE);
                sprintf(conn->send_msg->app_data,"我是客户端端的中间数据，这是我向服务端send的数据,编号:  %d", cnt++);
                conn->send_msg->msg_type = MSG_SEND_RECV;
                send_message(id);
             }
        }
        else if(conn->recv_msg->msg_type == MSG_DONE)
        {
            //接收到来自客户端的完成标识,这个操作应该是在服务端执行完毕 read 或者 write之后发送的
            printf("the client has read or write  ok, data is : %s\n",conn->buffer);
        }
        post_receive(id);
    }
    else if(wc->opcode == IBV_WC_SEND)
    {
        //消息发送  send 动作,只需打印输出即可
        printf("send ok\n");
        print_msg(conn->send_msg);

        
    }
    else
    {
        //error
        printf("opcode error: %d\n",wc->opcode);
    }
    
    printf("========================================================================\n");
    

    return;
}
