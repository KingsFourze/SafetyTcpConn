# Change Log

## v0.3.1 @2025-06-01
Release v0.3.1
1. version set to `0.3.1`
1. rename *_Func.hpp to *.impl.hpp
1. call start try send function when connection is avaliable to send only

## v0.3.0 @2025-02-23
Release v0.3.0
1. version set to `0.3.0`
1. add `Core`
    - which is required.
    - manage multiple endpoints and connections in one epoll
1. fix the memory leak bug in `Connection::ExtendBuffer`

## v0.2.2 @2024-06-16
1. version set to `0.2.2`
1. add method `Connection::MsgEnqueue(const std::string)`
    - no need to care about the string length any more when sending
1. modify comment for `Connection::MsgEnqueue`

## v0.2.1 @2024-06-16
1. version set to `0.2.1`
1. Connection::ReadString(const string& delimitor) change to Connection::ReadString(const string& delimitor, `bool& keep_recv`) 
    - help to check if there any message in `recv buff` is not read.