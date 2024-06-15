import socket, time, subprocess

big_msg = "".join([f"{i}|\r\n" for i in range(8192 * 4)])

def test_cannot_detect_disconnection():
    '''
    This function can simulate undetectable disconnection.
    example: client loses power and then the vpn disconnects, the vpn router may not be able to detect it and the server may deadlock during transmission.
    goal: server can normally close the connection without deadlock if there are datas need to send
    '''
    conn: list[socket.socket] = []

    # create connection
    for _ in range(10):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 8080))
        conn.append(s)

    time.sleep(1)

    # drop server's message
    subprocess.run(["sudo", "iptables", "-A", "INPUT","-p", "tcp","-s", "127.0.0.1", "--sport", "8080", "-j", "DROP"])

    # send message
    for i in range(10):
        conn[i].send(big_msg.encode())

    # wait for server disconnection    
    time.sleep(10)

    # cleanup
    for i in range(10):
        conn[i].close()
    subprocess.run(["sudo", "iptables", "-D", "INPUT","-p", "tcp","-s", "127.0.0.1", "--sport", "8080", "-j", "DROP"])

def test_normal_close_connection():
    '''
    This function will perform as a normal connection with normally close.
    goal: server normally close connection
    '''
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", 8080))

    time.sleep(1)

    s.send(big_msg.encode())

    recv_len = len(big_msg.replace("\r\n", ""))
    recv_msg = ""
    while len(recv_msg) != recv_len:
        recv_msg += s.recv(65536).decode()
        print(len(recv_msg), recv_len)
    s.close()

if __name__ == "__main__":
    test_normal_close_connection()
    test_cannot_detect_disconnection()