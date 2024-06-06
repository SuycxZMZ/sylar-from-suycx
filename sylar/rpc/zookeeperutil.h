#ifndef ZOOKEEPERUTIL_H
#define ZOOKEEPERUTIL_H

#include <semaphore.h>
#include <zookeeper/zookeeper.h>
#include <string>

namespace sylar
{
namespace rpc
{
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    /**
     * @brief 函数：启动连接到 ZooKeeper 服务器。
     * 通过配置文件获取 ZooKeeper 服务器的地址和端口，
     * 调用 zookeeper_init() 函数初始化 ZooKeeper 客户端，
     * 并设置了全局观察器 global_watcher。
     * 然后创建了一个信号量 sem，并将其与 m_zhandle 关联。
     * 最后通过信号量等待连接建立。
     * 这样当与 ZooKeeper 服务器的连接状态发生变化时，会自动调用 global_watcher() 函数进行处理。
    */
    void start();
    
    /**
     * @brief 在指定路径下创建节点。首先检查节点是否存在，若不存在则调用
     * zoo_create() 函数创建节点，并根据返回的状态信息输出日志。 
     * @param path 指定路径
     * @param data 节点数据
     * @param datalen 数据长度
     * @param state 节点状态
    */
    void create(const char * path, const char * data, int datalen, int state = 0);

    /**
     * @brief 获取指定路径节点的数据。
     * 调用 zoo_get() 函数获取节点数据，并根据返回的状态信息输出日志。
     * @param path 指定路径
    */
    std::string GetData(const char * path);
private:
    // zk 客户端句柄
    zhandle_t * m_zhandle;
};
} // namespace rpc

} // namespace sylar


#endif