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

            // recv full msg, msg is end with \r\n
            std::string fullMsg = "";
            char buff[1024];
            while (fullMsg.size() < 2 || fullMsg.substr(fullMsg.size() - 2, 2) != "\r\n") {
                size_t readSize = conn->ReadString(buff, 1024);
                if (readSize == 0) {
                    if (conn->IsConn())
                        continue;
                    return;
                }

                // concat readed data into fullMsg
                fullMsg.append(buff, buff + readSize);
            }

            std::cout << fullMsg;
            conn->MsgEnqueue(fullMsg.c_str(), fullMsg.size());
        },
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Client Disconnected | FD:" << conn->m_fd_ << std::endl;
        }
    );

    // while loop to keep endpoint running
    while(true) usleep(1000);

    return 0;
}