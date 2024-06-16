#include <iostream>
#include <sstream>

#include <SafetyTcpConn/SafetyTcpConn.hpp>

using namespace SafetyTcpConn;

int main(int, char**) {

    Endpoint endpoint(8080,
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Client Connected | FD:" << conn->m_fd_ << std::endl;

        },
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Message Come | FD:" << conn->m_fd_ << std::endl;

            while (true) {
                // recv full msg, msg is end with \r\n
                std::string fullMsg = conn->ReadString("\r\n");
                if (fullMsg.size() == 0)
                    break;

                std::cout << "recved msg: " << fullMsg << std::endl;
                conn->MsgEnqueue(fullMsg.c_str(), fullMsg.size());
            }
        },
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Client Disconnected | FD:" << conn->m_fd_ << std::endl;
        }
    );

    // while loop to keep endpoint running
    while(true) usleep(1000);

    return 0;
}