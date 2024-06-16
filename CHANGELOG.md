# Change Log

## v0.2.2 @2024-06-16
1. version set to `0.2.2`
1. add method `Connection::MsgEnqueue(const std::string)`
    - no need to care about the string length any more when sending
1. modify comment for `Connection::MsgEnqueue`

## v0.2.1 @2024-06-16
1. version set to `0.2.1`
1. Connection::ReadString(const string& delimitor) change to Connection::ReadString(const string& delimitor, `bool& keep_recv`) 
    - help to check if there any message in `recv buff` is not read.