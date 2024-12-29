#ifndef STC_ENDPOINT_FUNC_HPP
#define STC_ENDPOINT_FUNC_HPP

#include "Endpoint.hpp"

namespace SafetyTcpConn {

Endpoint::Endpoint(Core* core, int port, std::function<void(ConnectionPtr)> coninit_func, std::function<void(ConnectionPtr)> process_func, std::function<void(ConnectionPtr)> cleanup_func)
        : m_core_(core), m_port_(port), m_coninit_func_(coninit_func), m_process_func_(process_func), m_cleanup_func_(cleanup_func)
{
    if (m_port_ < 1 || m_port_ > 65535){
        std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Port: " << m_port_ << " is not Avaliable." << std::endl;
        exit(EXIT_FAILURE);
    }

    m_sockaddr_.sin_port = htons(m_port_);
    m_sockaddr_.sin_family = AF_INET;
    m_sockaddr_.sin_addr.s_addr = htons(INADDR_ANY);

    // create socket
    m_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // set address reuse
    const int reuse_addr = 1;
    if (setsockopt(m_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(int)) < 0) {
        std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Set SO_REUSEADDR Failure." << std::endl;
        exit(EXIT_FAILURE);
    }

    // bind socket
    if (bind(m_fd_, (sockaddr *)&m_sockaddr_, sizeof(m_sockaddr_)) < 0) {
        std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Bind Failure." << std::endl;
        exit(EXIT_FAILURE);
    }

    // listen socket
    if (listen(m_fd_, 16) == -1) {
        std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Listen Failure." << std::endl;
        exit(EXIT_FAILURE);
    }

    m_core_->RegisterEndpoint(this);
}

Endpoint::~Endpoint(){
    // unregister from core
    m_core_->UnregisterEndpoint(m_fd_);

    // close socket fd
    close(m_fd_);

    // close all connection
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        for (auto it = m_fd_2_connptrs_.begin(); it != m_fd_2_connptrs_.end(); it++)
            it->second->CloseConn();
    }

    // wake send thread up
    m_core_->StartTrySend();
}

//==============================
// Endpoint Control Area
//==============================

inline ConnectionPtr Endpoint::Accept() {
    // accept connection
    sockaddr_in client_sockaddr{};
    socklen_t length = sizeof(client_sockaddr);
    int client_fd = accept(m_fd_, (sockaddr *) &client_sockaddr, &length);

    // create connection instance
    ConnectionPtr conn = std::make_shared<Connection>(client_fd, this);

    // add into connection ptr map
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        m_fd_2_connptrs_[conn->m_fd_] = conn;
    }

    return conn;
}

inline void Endpoint::Remove(int fd) {
    ConnectionPtr conn;

    // try get connection ptr
    auto it = m_fd_2_connptrs_.find(fd);
    if (it == m_fd_2_connptrs_.end())
        return;
    conn = it->second;

    // remove from connection ptr map
    std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
    m_fd_2_connptrs_.erase(it);
}

}

#endif