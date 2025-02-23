#ifndef STC_CONTAINER_HPP
#define STC_CONTAINER_HPP

#include "Classes.hpp"

namespace SafetyTcpConn {

enum class ContainerType {
    kEndpoint,
    kConnection
};

class Container {
public:
    const ContainerType m_type_;

    Container(ContainerType type) : m_type_(type) {};
    virtual ~Container() {};
};

}

#endif