#ifndef STC_ENDPOINT_FUNC_HPP
#define STC_ENDPOINT_FUNC_HPP

#include "Endpoint.hpp"

namespace SafetyTcpConn {

Endpoint::Endpoint(Core* core, int port, std::function<void(ConnectionPtr)> coninit_func, std::function<void(ConnectionPtr)> process_func, std::function<void(ConnectionPtr)> cleanup_func)
        : m_core_(core), m_port_(port), m_open_(true), m_coninit_func_(coninit_func), m_process_func_(process_func), m_cleanup_func_(cleanup_func)
{
    if (m_port_ < 1 || m_port_ > 65535) {
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

    std::cout << "SafetyTcpConn >> Endpoint >> Start | FD: " << m_fd_ << std::endl;
}

Endpoint::~Endpoint() {
    CloseEndpoint();
    std::cout << "SafetyTcpConn >> Endpoint >> Safety Clean | FD: " << m_fd_ << " | Port: " << m_port_ << std::endl;
}

inline EndpointPtr Endpoint::CreateEndpoint(Core* core, int port, std::function<void(ConnectionPtr)> coninit_func, std::function<void(ConnectionPtr)> process_func, std::function<void(ConnectionPtr)> cleanup_func) {
    EndpointPtr endpoint = std::shared_ptr<Endpoint>(
        new Endpoint(core, port, coninit_func, process_func, cleanup_func)
    );

    core->RegisterEndpoint(endpoint);
    return endpoint;
}

inline bool Endpoint::IsOpen() {
    return m_open_.load();
}

inline void Endpoint::CloseEndpoint() {
    bool is_open = m_open_.load();
    // no need to close endpoint
    if (!is_open) return;

    // atomic to set m_open_ to false
    while (!m_open_.compare_exchange_weak(is_open, false)) {
        // endpoint will be close by another thread
        if (is_open == false)
            return;
    }

    // unregister from core, stop accept new connection
    m_core_->UnregisterEndpoint(m_fd_);

    // close all connection
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        for (auto it = m_fd_2_connptrs_.begin(); it != m_fd_2_connptrs_.end(); it++)
            it->second->CloseConn();
        m_fd_2_connptrs_.clear();
    }

    // close socket fd
    close(m_fd_);
}

//==============================
// Endpoint Control Area
//==============================

inline ConnectionPtr Endpoint::Accept(EndpointPtr& endpoint) {
    if (!endpoint->IsOpen()) return nullptr;
    std::unique_lock<std::mutex> lck(endpoint->m_mtx_connptrs_);

    // accept connection
    sockaddr_in client_sockaddr{};
    socklen_t length = sizeof(client_sockaddr);
    int client_fd = accept(endpoint->m_fd_, (sockaddr *) &client_sockaddr, &length);

    // create connection instance
    ConnectionPtr conn = std::shared_ptr<Connection>(new Connection(client_fd, endpoint));
    endpoint->m_fd_2_connptrs_[conn->m_fd_] = conn;

    return std::move(conn);
}

inline void Endpoint::Remove(EndpointPtr& endpoint, int fd) {
    if (!endpoint->IsOpen()) return;
    std::unique_lock<std::mutex> lck(endpoint->m_mtx_connptrs_);

    // try get connection ptr
    auto it = endpoint->m_fd_2_connptrs_.find(fd);
    if (it == endpoint->m_fd_2_connptrs_.end())
        return;

    // remove from connection ptr map
    endpoint->m_fd_2_connptrs_.erase(it);
}

}

#endif