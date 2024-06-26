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
#include "Connection.hpp"

namespace SafetyTcpConn {

class Endpoint {
private:
    friend class Connection;

    const int                               m_port_;
    int                                     m_sock_fd_;
    int                                     m_epoll_fd_;

    sockaddr_in                             m_sockaddr_;

    std::atomic_bool                        m_running_;
    std::thread                             m_epoll_thread_;
    std::thread                             m_send_thread_;
    const std::function<void(ConnectionPtr)>    m_coninit_func_;
    const std::function<void(ConnectionPtr)>    m_process_func_;
    const std::function<void(ConnectionPtr)>    m_cleanup_func_;

    /// @brief A mutex for locking `Endpoint::m_fd_2_connptrs_`
    /// @warning `Endpoint::Accept` and `Endpoint::Remove` will lock this mutex and they are calling in `Endpoint::EpollLoop`. Don't lock this mutex inside `Endpoint::EpollLoop` method, it is a safe behave.
    std::mutex                              m_mtx_connptrs_;
    std::condition_variable                 m_cond_connptrs_;
    /// @brief A map for storing fd and ConnectionPtr pairs
    /// @note Only `Endpoint::Accept` and `Endpoint::Remove` methods can modify this map. Other methods can only read this map.
    std::unordered_map<int, ConnectionPtr>  m_fd_2_connptrs_;
public:
    Endpoint(int port, std::function<void(ConnectionPtr)> coninit_func, std::function<void(ConnectionPtr)> process_func, std::function<void(ConnectionPtr)> cleanup_func)
        : m_port_(port), m_running_(true), m_coninit_func_(coninit_func), m_process_func_(process_func), m_cleanup_func_(cleanup_func)
    {
        if (m_port_ < 1 || m_port_ > 65535){
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Port: " << m_port_ << " is not Avaliable." << std::endl;
            exit(EXIT_FAILURE);
        }

        m_sockaddr_.sin_port = htons(m_port_);
        m_sockaddr_.sin_family = AF_INET;
        m_sockaddr_.sin_addr.s_addr = htons(INADDR_ANY);

        // create socket
        m_sock_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        // set address reuse
        const int reuse_addr = 1;
        if (setsockopt(m_sock_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(int)) < 0) {
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Set SO_REUSEADDR Failure." << std::endl;
            exit(EXIT_FAILURE);
        }

        // bind socket
        if (bind(m_sock_fd_, (sockaddr *)&m_sockaddr_, sizeof(m_sockaddr_)) < 0) {
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Bind Failure." << std::endl;
            exit(EXIT_FAILURE);
        }

        // listen socket
        if (listen(m_sock_fd_, 16) == -1){
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Listen Failure." << std::endl;
            exit(EXIT_FAILURE);
        }

        // init epoll
        m_epoll_fd_ = epoll_create(1);
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = m_sock_fd_;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, m_sock_fd_, &event);

        m_epoll_thread_ = std::thread(EpollLoop, this);
        m_send_thread_ = std::thread(SendLoop, this);
    }

    ~Endpoint() {
        // set running state to false
        m_running_.store(false);

        // close all connection
        {
            std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
            for (auto it = m_fd_2_connptrs_.begin(); it != m_fd_2_connptrs_.end(); it++)
                it->second->CloseConn();
        }

        // wake send thread up
        StartTrySend();

        // wait all thread stop
        m_epoll_thread_.join();
        m_send_thread_.join();

        // close epoll and socket fd
        close(m_sock_fd_);
        close(m_epoll_fd_);
    }

private:
    /// @brief Wake the sending loop up.
    void StartTrySend();

private:
    /// @brief Accept a new connection.
    /// @note This method is only for `Endpoint::EpollLoop`.
    void Accept();
    /// @brief Remove a disconnected connection.
    /// @note This method is only for `Endpoint::EpollLoop`.
    void Remove(int fd);
    /// @brief Receive all message in system rx buffer, then run process function.
    /// @note This method is only for `Endpoint::EpollLoop`.
    void Process(int fd);
    /// @brief Set connection's send flag to `true`, then call the `Endpoint::StartTrySend` for sending data if send buffer has data.
    /// @note This method is only for `Endpoint::EpollLoop`.
    void SetSendFlag(int fd);

private:
    /// @brief Running epoll process. Also control the connection map.
    /// @param endpoint the endpoint need to process
    static void EpollLoop(Endpoint* endpoint);
    /// @brief Running sending process.
    /// @param endpoint the endpoint need to process
    static void SendLoop(Endpoint* endpoint);
};

}

#endif