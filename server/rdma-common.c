#include "rdma-common.h"


static completion_cb_fn s_on_completion_cb = NULL;


void set_on_compltion_fn(completion_cb_fn comp)
{
    s_on_completion_cb = comp;
}

void rdma_read_write(struct rdma_cm_id *id, enum ibv_wr_opcode  opcode)
{
    struct connection *conn = (struct connection *)id->context;
    
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    memset(&wr, 0, sizeof(wr));
            
    wr.wr_id = (uintptr_t)id;
    wr.opcode = opcode;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = conn->peer_addr;
    wr.wr.rdma.rkey = conn->peer_rkey;
            
    sge.addr = (uintptr_t)conn->buffer;
    sge.length = BIG_BUFFER_SIZE;
    sge.lkey = conn->buffer_mr->lkey;

    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));

    return; 
}


void send_message(struct rdma_cm_id *id)
{
    struct connection *conn = (struct connection *)id->context;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    
    //这两步操作只有在客户端发送read write请求时会用到，用以向对端提供本身内存的操作信息，允许对方操作该内存
    if(conn->send_msg->msg_type == MSG_READ_REQUEST || conn->send_msg->msg_type == MSG_WRITE_REQUEST)
    {
        conn->send_msg->remote_info.addr = (uintptr_t)conn->buffer_mr->addr;
        conn->send_msg->remote_info.rkey = conn->buffer_mr->rkey;
    }

    sge.addr = (uintptr_t)conn->send_msg;
    sge.length = sizeof(struct message);
    sge.lkey = conn->send_msg_mr->lkey;


    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
    
    return;
}

void post_receive(struct rdma_cm_id *id)
{
    struct connection *conn = (struct connection *)id->context;

    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)conn->recv_msg;
    sge.length = sizeof(*conn->recv_msg);
    sge.lkey = conn->recv_msg_mr->lkey;

    TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
    
    return;
}




void * poll_cq(void *arg)
{
    
    //完成队列事件监听，负责监听数据交换结果信息
    struct ibv_comp_channel *comp_channel = (struct ibv_comp_channel *) arg;

    struct ibv_cq *cq;
    struct ibv_wc wc;
    void *cq_context = NULL;

    while (1) 
    {
        TEST_NZ(ibv_get_cq_event(comp_channel, &cq, &cq_context));
        ibv_ack_cq_events(cq, 1);
        TEST_NZ(ibv_req_notify_cq(cq, 0));

        while(ibv_poll_cq(cq, 1, &wc))
        {
            if (wc.status == IBV_WC_SUCCESS)
                s_on_completion_cb(&wc);
            else
                die("poll_cq: status is not IBV_WC_SUCCESS");
        }
    }

  return NULL;
}

void on_disconnect(struct rdma_cm_id *id)
{
    struct connection *conn = (struct connection *) id->context;
    
    rdma_destroy_qp(id);
    
    ibv_dereg_mr(conn->buffer_mr);
    ibv_dereg_mr(conn->send_msg_mr);
    ibv_dereg_mr(conn->recv_msg_mr);
    
    free(conn->buffer);
    free(conn->send_msg);
    free(conn->recv_msg);
    
    rdma_destroy_id(id);
    free(conn);
    
    return;
}


void die(const char *reason)
{
  fprintf(stderr, "%s\n", reason);
  exit(EXIT_FAILURE);
}

void print_msg(struct message *msg)
{
    switch(msg->msg_type)
    {
        case MSG_INIT:
            printf("msg init\n");
            break;
        case MSG_SEND_RECV:
            printf("msg_send_recv, data is %s \n",msg->app_data);
            break;
        case MSG_READ_REQUEST:
            printf("msg read request\n");
            break;
        case MSG_WRITE_REQUEST:
            printf("msg write request\n");
            break;
        case MSG_MR_INFO:
            printf("msg mr info\n");
            break;
        case MSG_CONTINUE:
            printf("msg continue\n");
            break;
        case MSG_DONE:
            printf("msg done\n");
            break;
        default:
            break;
    }
    
    
}
