#ifndef FDMANAGER_H
#define FDMANAGER_H

#include <memory>
#include <vector>

#include "thread.h"
#include "singleton.h"

namespace sylar
{
    
class FdCtx : public std::enable_shared_from_this<FdCtx>
{
public:
    using ptr = std::shared_ptr<FdCtx>;

    /**
     * @brief 通过文件句柄构造FdCtx
     * 
     * @param fd 
     */
    FdCtx(int fd);

    /**
     * @brief 析构函数
     */
    ~FdCtx();

    /**
     * @brief 是否初始化完成
     * 
     * @return true 
     * @return false 
     */
    bool isInit() const { return m_isInit; }


    /**
     * @brief 是否是socket
     * 
     * @return true 
     * @return false 
     */
    bool isSocket() const { return m_isSocket; }

    /**
     * @brief 是否已关闭
     * 
     * @return true 
     * @return false 
     */
    bool isClosed() const { return m_isClosed; }

    /**
     * @brief 设置用户主动设置非阻塞
     * 
     * @param[in] v 是否阻塞
     */
    void setUserNonblock(bool v) { m_userNonblock = v; }

    /**
     * @brief 获取用户是否主动设置的非阻塞
     * 
     * @return true 
     * @return false 
     */
    bool getUserNonblock() const { return m_userNonblock; }

    /**
     * @brief 设置系统非阻塞
     * 
     * @param[in] v 
     */
    void setSysNonblock(bool v) { m_sysNonblock = v; }

    /**
     * @brief 获取系统非阻塞
     * 
     * @return true 
     * @return false 
     */
    bool getSysNonblock() const { return m_sysNonblock; }

    /**
     * @brief 设置超时时间
     * 
     * @param[in] type SO_RCVTIMEO(读超时)，SO_SNDTIMEO(写超时)
     * @param[in] timeout 超时时间毫秒
     */
    void setTimeout(int type, uint64_t timeout);

    /**
     * @brief 获取超时时间
     * 
     * @param[in] type SO_RCVTIMEO(读超时)，SO_SNDTIMEO(写超时)
     * @return uint64_t 超时时间毫秒
     */
    uint64_t getTimeout(int type);
private:
    /**
     * @brief 初始化
     * 
     * @return true 
     * @return false 
     */
    bool init();
private:
    // 是否初始化
    bool m_isInit: 1;
    // 是否是socket
    bool m_isSocket: 1;
    // 是否是hook非阻塞
    bool m_sysNonblock: 1;
    // 是否是用户主动设置非阻塞
    bool m_userNonblock: 1;
    // 是否关闭
    bool m_isClosed: 1;
    // 文件句柄
    int m_fd;
    // 读超时时间毫秒
    uint64_t m_recvTimeout;
    // 写超时时间毫秒
    uint64_t m_sendTimeout;
};

class FdManager
{
public:
    using RWMutexType = RWMutex;

    FdManager();

    /**
     * @brief 获取/创建文件句柄类FdCtx
     * 
     * @param[in] fd 文件句柄
     * @param[in] auto_create 是否自动创建
     * @return FdCtx::ptr 返回对应文件句柄类的智能指针
     */
    FdCtx::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除文件句柄类
     * 
     * @param fd 文件句柄
     */
    void del(int fd);
private:
    // 读写锁
    RWMutexType m_mutex;
    // 文件描述符集合
    std::vector<FdCtx::ptr> m_datas;
};

// 文件句柄单例
using FdMgr = Singleton<FdManager>;

} // namespace sylar



#endif // FDMANAGER_H