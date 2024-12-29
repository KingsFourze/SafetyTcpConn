#ifndef STC_ENDPOINT_HPP
#define STC_ENDPOINT_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "Classes.hpp"
#include "Core.hpp"
#include "Connection.hpp"

namespace SafetyTcpConn {

class Endpoint {
private:
    friend class Core;
    friend class Connection;

    Core*                                   m_core_;
    const int                               m_port_;
    int                                     m_fd_;

    sockaddr_in                             m_sockaddr_;

    const std::function<void(ConnectionPtr)>    m_coninit_func_;
    const std::function<void(ConnectionPtr)>    m_process_func_;
    const std::function<void(ConnectionPtr)>    m_cleanup_func_;

    std::mutex                              m_mtx_connptrs_;
    std::unordered_map<int, ConnectionPtr>  m_fd_2_connptrs_;
public:
    Endpoint(Core* core, int port, std::function<void(ConnectionPtr)> coninit_func, std::function<void(ConnectionPtr)> process_func, std::function<void(ConnectionPtr)> cleanup_func);
    ~Endpoint();

private:
    ConnectionPtr Accept();
    void Remove(int fd);
};

}

#endif