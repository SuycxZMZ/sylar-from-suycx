#include "zookeeperutil.h"
#include "rpcapplication.h"
#include "../log.h"

namespace sylar
{
namespace rpc
{
static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
/**
 * @SuycxZMZ
 * @brief 类型为 watcher_fn 全局观察器，用于处理 ZooKeeper 客户端的会话事件。
 * 在连接建立后，通过信号量 sem_post() 通知 start() 函数，连接已建立。
 * @param zh ZooKeeper 客户端的句柄，即 zhandle_t 类型的指针，表示与 ZooKeeper 服务器的连接。
 * @param type 事件类型，可以是以下值之一：
 *          ZOO_SESSION_EVENT：会话事件，表示与 ZooKeeper 服务器的连接状态发生变化。
 *          ZOO_CREATED_EVENT：节点创建事件。
 *          ZOO_DELETED_EVENT：节点删除事件。
 *          ZOO_CHANGED_EVENT：节点数据变化事件。
 *          ZOO_CHILD_EVENT：子节点变化事件。
 *          ZOO_SESSION_EVENT：会话事件。
 * @param state 事件状态，可以是以下值之一：
 *          ZOO_EXPIRED_SESSION_STATE：会话过期状态。
 *          ZOO_AUTH_FAILED_STATE：认证失败状态。
 *          ZOO_CONNECTING_STATE：连接中状态。
 *          ZOO_ASSOCIATING_STATE：关联中状态。
 *          ZOO_CONNECTED_STATE：已连接状态。
 *          ZOO_CONNECTEDREADONLY_STATE：只读已连接状态。
 * @param path 事件路径，表示发生事件的节点路径。
 * @param watcherCtx 观察者上下文，用于传递额外的信息。
*/
void global_watcher(zhandle_t * zh, int type, 
                    int state, const char * path, void * watcherCtx)
{
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            sem_t * sem = (sem_t *)zoo_get_context(zh);
            sem_post(sem); 
        }
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr)
{

}

ZkClient::~ZkClient()
{
    if (nullptr != m_zhandle)
    {
        zookeeper_close(m_zhandle);
    }
}

void ZkClient::start()
{
    std::string zookeeperip = MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
    std::string zookeeperport= MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
    std::string conn_str = zookeeperip + ":" + zookeeperport;

    /**
     * 异步 
     * 多线程版的API: 
     * API调用线程
     * 网络IO线程
     * global_watcher 回调线程
     * zookeeper_init 直接返回不阻塞，先设置句柄
    */
    m_zhandle = zookeeper_init(conn_str.c_str(), global_watcher, 30000, nullptr, nullptr, 0);
    if (nullptr == m_zhandle)
    {
        // LOG_ERROR("in ZkClient::start() : zookeeper_init error");
        exit(EXIT_FAILURE);
    }

    sem_t sem;
    sem_init(&sem, 0, 0);
    // 设置句柄信息 用于在 m_zhandle 上等待
    // 回调线程负责 post
    zoo_set_context(m_zhandle, &sem);

    sem_wait(&sem);
    // LOG_INFO("ZkClient init success");
    SYLAR_LOG_INFO(g_logger) << "ZkClient init success !!!";
}

void ZkClient::create(const char * path, const char * data, int datalen, int state)
{
    char path_buffer[128];
    int buff_len = sizeof(path_buffer);
    int flag = 0;
    flag = zoo_exists(m_zhandle, path, 0, nullptr);
    if (ZNONODE == flag)
    {
        std::cout << "\n before create \n";
        flag = zoo_create(m_zhandle, path, data, datalen, 
                          &ZOO_OPEN_ACL_UNSAFE, 
                          state, path_buffer, buff_len);
        std::cout << "\n after create \n";
        if (ZOK == flag)
        {
            SYLAR_LOG_INFO(g_logger) << "znode create success. path : " << std::string(path);
            // LOG_INFO("znode create success. path : %s", path);
        }
        else
        {
            SYLAR_LOG_ERROR(g_logger) << "znode create error at path : " << std::string(path)
                << "error flag : " << flag;
            // LOG_ERROR("znode create error. path : %s, flag : %d", path, flag);
            exit(EXIT_FAILURE);
        }
    }
}

std::string ZkClient::GetData(const char * path)
{
    char buffer[128];
    int buffer_len = sizeof(buffer);
    int flag = zoo_get(m_zhandle, path, 0, buffer, &buffer_len, nullptr);

    if (ZOK != flag)
    {
        // LOG_ERROR("get znode error path : %s", path);
        return "";
    }
    else
    {
        return std::string(buffer);
    }
}
} // namespace rpc

} // namespace sylar
