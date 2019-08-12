# rdma-ibverbs-example

刚开始接触rdma编程，为了加深对rdma的理解，特编写此例子；  


============代码说明=====================  
rdma-common.c   rdma-common.h  为客户端和服务端共享的代码，用以减少维护工作量  
rdma-client.c   rdma-client.h  客户端的实现  
rdma-server.c   rdma-server.h  服务端的实现  
main-client.c  和 main-server.c 分别为客户端服务端 调用的者， 在生产中需要有一定的封装  
  
    

==========  example ==============

client 实现了一个客户端程序，  运行：  ./rdma-client    send_recv/read/write   [ipv4]

server 实现了一个服务端程序,   运行：  ./rdma-server

通过客户端控制可分别测试   send recv 操作，  read 操作, write操作；

其中read , write均是 客户端提供内存，供服务端进行相应操作

ipv4可以是服务器上所有 rdma 网卡的ip
监听端口写死了 12345（可修改）






============ to do ==========

1,  目前的代码为测试代码，还没有加入生产环境中的日志系统

2， 需要界定短消息和长消息的大小限制,  目前假设 4k以下为短消息，采用 send/recv模式

3,  缺少连接报错或者操作报错处理机制

4,  对于大文件的读写需要更精细的切片传输控制

5,  struct message 需要根据业务类型来定义操作，另外需要在多线程间共享数据链表，需加锁

6,  并发测试
