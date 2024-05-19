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

namespace SafetyTcpConn {

class Endpoint;

class Connection {
private:
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
    const int           m_fd;

    Connection(int fd) :
        m_fd(fd), m_connected(true),
        m_send_buff_size_(0), m_send_buff_allcasize_(kDefaultSize), m_send_buff_(new char[kDefaultSize])
    {}

    /// @brief 
    /// Get the alive status of the connection
    /// @return is connection alive
    bool IsConn() {
        return m_connected.load();
    }

    /// @brief 
    /// close socket fd in thread-safe way
    void CloseConn() {
        bool conn_state = m_connected.load();
        // no need to close connection
        if (!conn_state) return;

        // atomic to set m_connected to false
        while (!m_connected.compare_exchange_weak(conn_state, false)) {
            // connection will be close by another thread
            if (conn_state == false)
                return;
        }

        // close connection
        close(m_fd);
    }

    /// @brief 
    /// @param buff the buff for recv msg string
    /// @param buff_len the length of the buffer
    /// @return byte count which recved from connection
    size_t ReadString(char* buff, size_t buff_len) {
        if (!m_connected.load())
            return 0;

        memset(buff, '\0', buff_len);

        while (true) {
            int r = recv(m_fd, buff, buff_len, MSG_DONTWAIT);

            switch (r) {
                case 0:
                    CloseConn();
                    return 0;
                case -1:
                    if (errno != EAGAIN && errno != EINTR)
                        CloseConn();
                    return 0;
                default:
                    return r;
            };
        }
    }

    /// @brief 
    /// @param size the length you want to recv
    /// @return byte array of message which recved from connection
    char* ReadBytes(const size_t size) {
        if (!m_connected.load())
            return nullptr;

        char* buff = new char[size];
        size_t recved = 0;

        while (recved < size) {
            int r = recv(m_fd, buff + recved, size - recved, MSG_DONTWAIT);

            switch (r) {
                case -1:
                {
                    if (errno == EAGAIN || errno == EINTR)
                        break;
                }
                case 0:
                {
                    CloseConn();
                    delete [] buff;
                    return nullptr;
                }
                default:
                {
                    recved += r;
                    break;
                }
            };
        }
    }
    
    /// @brief 
    /// All the message need to push into the send buff by this method, then the `Endpoint` will send your `msg` if it can.
    /// @param msg message you want to send
    /// @param len length of message
    void MsgEnqueue(const char* msg, const size_t len) {
        if (!IsConn()) return;

        std::unique_lock<std::mutex> lck(m_send_buff_mtx_);

        // calculate the total size of data
        const size_t total_data_len = m_send_buff_size_ + len;

        // check if buff size is enough, if not then extend it
        if (m_send_buff_allcasize_ < total_data_len) {
            const size_t new_buff_allocsize = (total_data_len / kDefaultSize + (size_t)(total_data_len % kDefaultSize > 0)) * kDefaultSize;
            if (new_buff_allocsize > kMaxSize) {
                CloseConn();
                return;
            }

            char* old_buff = m_send_buff_;
            char* new_buff = new char[new_buff_allocsize];

            // copy old buff's data to new buff
            memcpy(new_buff, old_buff, m_send_buff_size_);

            // clean old buff
            delete [] old_buff;

            // replace buff ptr and allocated size
            m_send_buff_ = new_buff;
            m_send_buff_allcasize_ = new_buff_allocsize;
        }

        // copy msg's data into the end of buff
        memcpy(m_send_buff_ + m_send_buff_size_, msg, len);
        m_send_buff_size_ = total_data_len;
    }

private:
    friend class Endpoint;

    /// @brief
    /// Check If this connection need to send message. This method is only for `Endpoint`.
    /// @return  `true`: there are some data need to send  `false`: no data need to send
    bool NeedSend() {
        return IsConn() && m_send_buff_size_ > 0;
    }

    /// @brief 
    /// Send message in send buffer with non-blocking mode. This method is only for `Endpoint`.
    /// @return `>0`: sent byte count `=0`: connection closed `<0`: can't send currently
    int TrySend() {
        if (!IsConn())
            return 0;

        std::unique_lock<std::mutex> lck(m_send_buff_mtx_);
        if (m_send_buff_size_ == 0)
            return -1;

        // get the len need to send and copy msg to tmp_buff
        size_t len = m_send_buff_size_ > 1500 ? 1500 : m_send_buff_size_;

        // send with non-blocking mode
        int sent = send(m_fd, m_send_buff_, len, MSG_DONTWAIT);
        switch (sent)
        {
            case 0: // disconnected
            {
                CloseConn();
                break;
            }
            case -1: // error when send
            {
                // not expected error, close connection
                if (errno != EAGAIN && errno != EINTR)
                    CloseConn();

                // can't send currently
                break;
            }
            default: // send success
            {
                m_send_buff_size_ -= sent;
                memcpy(m_send_buff_, m_send_buff_ + sent, m_send_buff_size_);
                break;
            }
        };

        return sent;
    }
};

}

#endif