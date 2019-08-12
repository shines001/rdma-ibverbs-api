#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

//如果结果是为 非NULL 或者 非0， 则报错
#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
//如果结果是为 NULL 或者 0， 则报错
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)


//大数据块大小，默认16M,16*1024*1024 用于read和write
#define BIG_BUFFER_SIZE  16777216 
//小数据块大小, 默认 4k,4*1024 用于send和recv
#define SMALL_BUFFER_SIZE  4096

//服务端默认监听端口
#define   DEFAULT_PORT  12345
//超时默认500ms
#define   TIMEOUT_MS  500


//控制消息的操作类型，用户可自定义
enum message_type
{
    //初始化状态
    MSG_INIT = 0,
    
    //普通消息,单独一次即可完成,用于短消息的发送,比如执行一条sql语句、发送结果集等等
    MSG_SEND_RECV,
    
    //动作控制命令,客户端发送, 之后会期待服务端发送一个MR信息，即提供客户端请求数据的远程内存地址, 用以执行read操作
    MSG_READ_REQUEST,
    
    //动作控制命令,客户端发送，之后会期待服务端发送一个MR信息，即提供空白的远程内存地址, 用以执行write操作
    MSG_WRITE_REQUEST,
    
    //服务端发送内存信息，以供客户端远程直接访问，可读可写
    MSG_MR_INFO,
    
    //用以告知对端继续执行  read-imm 或者 write-imm 操作
    MSG_CONTINUE,
    
    //对于需要拆分发送的大块数据,用MSG_DONE来表示发送完成
    MSG_DONE
    
};

//控制消息体,用以定义接下来如何操作
struct message
{
    //通讯级操作控制类型
    enum message_type  msg_type;
  
    struct
    {
        //远程主机的地址和操作key，用于 read 或者 write
        uint64_t addr;
        uint32_t rkey;
    }remote_info;
    
    //最多4k大小的应用数据，比如SQL文本、返回结果集等，由应用自定义
    char  app_data[SMALL_BUFFER_SIZE];
};

//连接信息，保存了mr和缓存
struct connection
{
    //用于 read  或 write的缓存
    char  *buffer;
    //用于 read 或 write的mr
    struct ibv_mr *buffer_mr;

    //用于发送的控制消息
    struct message *send_msg;
    //存放发送控制消息的缓存
    struct ibv_mr *send_msg_mr;
    
    //用于接收的控制消息
    struct message *recv_msg;
    //存放接收控制消息的缓存
    struct ibv_mr *recv_msg_mr;
    
    //服务端的内存地址,从message的remote_mr获取,只有在客户端才会用到该字段
    uint64_t peer_addr;
    //服务端的操作key,从message的remote_mr获取,只有在客户端才会用到该字段
    uint32_t peer_rkey;
    
    void  *app_info;
};

//全局上下文信息，暂不使用，如果是短连接，则有必要在服务端启用，用以降低连接创建是 pd,cq, comp_channel和线程的创建开销
struct context
{
    //网卡设备的唯一标识符
    uint64_t                 guid;
    //某一个网卡设备对应的 ibv_context
    struct ibv_context      *ctx;
    
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    //cc,用来执行通知机制
    struct ibv_comp_channel *comp_channel;
    
    //下一个节点的地址，该指针只有在服务端会被使用，因为服务端要在所有的设备上都创建监听，客户端会连接指定的服务端，故此只有一个
    struct context   *next;
};


//定义一个完成任务处理函数，客户端和服务端各自实现
typedef void (*completion_cb_fn)(struct ibv_wc *wc);

void set_on_compltion_fn(completion_cb_fn comp);



//cq事件监听函数，运行在子线程中
void * poll_cq(void *arg);


//read  write 操作
void rdma_read_write(struct rdma_cm_id *id, enum ibv_wr_opcode  opcode);



//发送消息函数
void send_message(struct rdma_cm_id *id);

//接收函数，可以用于接收消息，也可以用于接收 write immediate事件
void post_receive(struct rdma_cm_id *id);

//断开连接操作
void on_disconnect(struct rdma_cm_id *id);

//报错函数
void die(const char *reason);

//打印message信息函数
void print_msg(struct message *msg);





#endif
