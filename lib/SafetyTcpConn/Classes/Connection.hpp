#ifndef STC_CONNECTION_HPP
#define STC_CONNECTION_HPP

#include <iostream>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "Classes.hpp"

namespace SafetyTcpConn {

class Connection {
private:
    friend class Endpoint;
    static constexpr size_t kDefaultSize = 65536;
    static constexpr size_t kMaxSize     = 65536 * 16;
private:
    std::atomic_bool    m_connected;

    // for sending
    std::mutex          m_send_buff_mtx_;
    char*               m_send_buff_;
    size_t              m_send_buff_size_;
    size_t              m_send_buff_allcasize_;
public:
    Endpoint*           m_endpoint_;
    const int           m_fd;

    Connection(int fd, Endpoint* endpoint) :
        m_fd(fd), m_endpoint_(endpoint), m_connected(true),
        m_send_buff_size_(0), m_send_buff_allcasize_(kDefaultSize), m_send_buff_(new char[kDefaultSize])
    {}

    /// @brief 
    /// Get the alive status of the connection
    /// @return is connection alive
    bool IsConn();

    /// @brief 
    /// close socket fd in thread-safe way
    void CloseConn();

    /// @brief 
    /// @param buff the buff for recv msg string
    /// @param buff_len the length of the buffer
    /// @return byte count which recved from connection
    size_t ReadString(char* buff, size_t buff_len);

    /// @brief 
    /// @param size the length you want to recv
    /// @return byte array of message which recved from connection
    char* ReadBytes(const size_t size);
    
    /// @brief 
    /// All the message need to push into the send buff by this method, then the `Endpoint` will send your `msg` if it can.
    /// @param msg message you want to send
    /// @param len length of message
    void MsgEnqueue(const char* msg, const size_t len);

private:
    /// @brief
    /// Check If this connection need to send message. This method is only for `Endpoint`.
    /// @return  `true`: there are some data need to send  `false`: no data need to send
    bool NeedSend();

    /// @brief 
    /// Send message in send buffer with non-blocking mode. This method is only for `Endpoint`.
    /// @return `>0`: sent byte count `=0`: connection closed `<0`: can't send currently
    int TrySend();
};

}

#endif