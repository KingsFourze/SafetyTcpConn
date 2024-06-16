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
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "Classes.hpp"

namespace SafetyTcpConn {

class Connection {
private:
    friend class Endpoint;
    static constexpr size_t kDefaultSize = 16384;
    static constexpr size_t kMaxSize     = 65536 * 16;
private:
    std::atomic_bool    m_connected_;
    std::atomic_bool    m_send_flag_;
    time_t              m_prev_sendtime_;

    // for receiving
    std::mutex          m_recv_buff_mtx_;
    char*               m_recv_buff_;
    size_t              m_recv_buff_size_;
    size_t              m_recv_buff_allcasize_;
    // for sending
    std::mutex          m_send_buff_mtx_;
    char*               m_send_buff_;
    size_t              m_send_buff_size_;
    size_t              m_send_buff_allcasize_;
public:
    Endpoint*           m_endpoint_;
    const int           m_fd_;

    Connection(int fd, Endpoint* endpoint) :
        m_fd_(fd), m_endpoint_(endpoint), m_connected_(true), m_send_flag_(true),
        m_recv_buff_size_(0), m_recv_buff_allcasize_(kDefaultSize), m_recv_buff_(new char[kDefaultSize]),
        m_send_buff_size_(0), m_send_buff_allcasize_(kDefaultSize), m_send_buff_(new char[kDefaultSize])
    {
        int send_buff_size = 8192;
        if (setsockopt(m_fd_, SOL_SOCKET, SO_SNDBUF, &send_buff_size, sizeof(send_buff_size)) < 0) {
            std::cerr << "SafetyTcpConn >> Connection >> Error >> Set Socket Send Buffer Size Failure." << std::endl;
            CloseConn();
            return;
        }

        int cork = 1;
        if (setsockopt(m_fd_, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork)) < 0) {
            std::cerr << "SafetyTcpConn >> Connection >> Error >> Set Socket Send Buffer Size Failure." << std::endl;
            CloseConn();
            return;
        }
    }

    ~Connection() {
        // close connection if not close
        CloseConn();
        // release buffer
        delete [] m_recv_buff_;
        delete [] m_send_buff_;
    }

    /// @brief Get the alive status of the connection
    /// @return `bool`: connection alive(`true`) / closed(`false`)
    bool IsConn();

    /// @brief Close socket fd in thread-safe way
    void CloseConn();

    /// @brief Read a `std::string` message from connection's recv buff splited by `delimiter`
    /// @param delimiter the delimiter for msg string. example: \\r\\n
    /// @param keep_read return the status of whether the program needs to continue reading
    /// @return `std::string`: a string message
    std::string ReadString(const std::string delimiter, bool& keep_read);

    /// @brief Read byte(s) of message from connection's recv buff
    /// @param size the length of message you want
    /// @return `char*`: a byte-array message
    char* ReadBytes(const size_t size);
    
    /// @brief Enqueue your message to connection's send buffer
    /// @param msg message you want to send
    /// @param len length of message
    /// @note All the char array message need to push into the send buff by this method, then the `Endpoint` will send your `msg` if it can.
    void MsgEnqueue(const char* msg, const size_t len);

    /// @brief Enqueue your string message to connection's send buffer
    /// @param msg message you want to send
    /// @note All the std::string message need to push into the send buff by this method, then the `Endpoint` will send your `msg` if it can.
    void MsgEnqueue(const std::string msg);

private:
    /// @brief
    /// Check and extend buffer if needed. When reach max buffer size, `Connection::CloseConn` will also run inside this method. This method is only for `Connection`.
    /// @return `bool`: buffer allocated or no need to extend(`true`) / reach max buffer size(`false`)
    bool ExtendBuffer(char*& buff_ptr, size_t target_size, size_t& curr_size, size_t& allocsize);

    /// @brief Recevie message with non-blocking mode.
    /// @note This method is only for `Endpoint`.
    /// @return `bool`: recieving process is success(`true`) / failure(`false`)
    bool TryRecv();

    /// @brief Set send flag when the connection is avaliable to send.
    /// @note This method is only for `Endpoint`.
    void SetSendFlag();

    /// @brief Check If this connection need to send message.
    /// @note This method is only for `Endpoint`.
    /// @return `bool`: is there are any data need to send
    bool NeedSend();

    /// @brief Send message in send buffer with non-blocking mode.
    /// @note This method is only for `Endpoint`.
    /// @return `int`: count of sent bytes(`>0`) / connection closed(`0`) / can't send currently(`<0`)
    int TrySend();
};

}

#endif