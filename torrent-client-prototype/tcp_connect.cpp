#include "tcp_connect.h"
#include "byte_tools.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <chrono>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <netdb.h>


TcpConnect::TcpConnect(std::string ip, int port, std::chrono::milliseconds connectTimeout, std::chrono::milliseconds readTimeout) :
    ip_(ip), port_(port), connectTimeout_(connectTimeout), readTimeout_(readTimeout){
        sock_ = -1;
        l = spdlog::get("mainLogger");
    }

TcpConnect::~TcpConnect(){
    if (sock_ != -1) {
        close(sock_);
    }
}


void TcpConnect::EstablishConnection(){
    struct addrinfo hints;
    struct addrinfo* serverResult = NULL;
    struct addrinfo* receivedNode = NULL; 
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_flags = AI_PASSIVE;
    int getaddinfoStatus = getaddrinfo(ip_.c_str(), std::string(std::to_string(port_)).c_str(), &hints, &serverResult);
    if (getaddinfoStatus != 0) {
        l->error("getaddrinfo fail: {}", gai_strerror(getaddinfoStatus));
        throw std::runtime_error(
            "getaddrinfo fail: " + std::string(gai_strerror(getaddinfoStatus)));
    }

    
    for(receivedNode = serverResult; receivedNode != NULL; receivedNode = receivedNode->ai_next){
        sock_ = socket(receivedNode->ai_family, receivedNode->ai_socktype, receivedNode->ai_protocol);
        if(sock_ < 0){
            continue;
        }
        int yes = 1;
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        // Set nonblocking
        int fcntlStatus = fcntl(sock_, F_SETFL, O_NONBLOCK);
        if (fcntlStatus < 0) {
            l->error("fcntl error setting nonblock: {}", std::strerror(errno));
            close(sock_);
            sock_ = -1;
            // try next address
            continue;  
        }


        int connect_status = connect(sock_, receivedNode->ai_addr, receivedNode->ai_addrlen );
        if(connect_status < 0){
            // waiting for the socket 
            if(errno == EINPROGRESS){
                struct timeval tv;
                tv.tv_sec = connectTimeout_.count() / 1000;
                tv.tv_usec = (connectTimeout_.count() % 1000) * 1000;
                
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(sock_, &writefds);

                int selectStatus = select(sock_ + 1, NULL, &writefds, NULL, &tv); 
                if(selectStatus < 0 && errno != EINTR){
                    l->warn("Establish connection, select < 0");
                }else if(selectStatus > 0){
                    if (!FD_ISSET(sock_, &writefds)) {
                        l->error("select() indicated ready, but socket not in writefds");
                        close(sock_);
                        sock_ = -1;
                        continue;
                    }
                    // Check for connect errors via getsockopt
                    int valopt;
                    socklen_t lon = sizeof(valopt);
                    if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, &valopt, &lon) < 0) {
                        l->warn("getsockopt() error after select: {}", std::strerror(errno));
                        close(sock_);
                        sock_ = -1;
                        continue;
                    }
                    if (valopt != 0) {
                        l->warn("Delayed connection error: {}", std::strerror(valopt));   
                        close(sock_);
                        sock_ = -1;
                        continue;
                    }
                    // connection success!
                    freeaddrinfo(serverResult);
                    return;
                }else{
                    l->warn("Timeout in select() - Cancelling!");
                    // throw std::runtime_error("Timeout in select() - Cancelling!");
                } 
                close(sock_);
                sock_ = -1;
                continue;
            }else{
                // Error other than EINPROGRESS
                l->error("Immediate error in connect(): {}", std::strerror(errno));
                close(sock_);
                sock_ = -1;
                continue; 
            }
        }else {
            // connect() returned 0 immediately; we are connected
            freeaddrinfo(serverResult);
            return;
        }
    }

    // If we got here, we tried all addresses, but failed
    if (serverResult) {
        freeaddrinfo(serverResult);
    }
    l->error("Failed to connect to any resolved address");
    throw std::runtime_error("Unable to connect to the given IP/port");
}

void TcpConnect::SendData(const std::string& data) const{
    if (sock_ < 0) {
        l->error("Invalid socket in send");
        throw std::runtime_error("Invalid socket in send");
    }

    if(data.empty()){
        return;
    }
    size_t totalSent = 0;
    size_t dataSize = data.size();
    const char* rawDataPtr = data.data();
    while(totalSent < dataSize){
        size_t dataSent = send(sock_, rawDataPtr + totalSent, dataSize - totalSent, 0);
        if(totalSent < 0){
            if(errno == EINTR){ // signal interrupt, resend
                continue;
            }else if(errno == EAGAIN || errno == EWOULDBLOCK){
                l->error("Error in send data, EAGAIN or EWOULDBLOCK");
                throw std::runtime_error("Error in send data, EAGAIN or EWOULDBLOCK");
            }else{
                l->error("Error in send data");
                throw std::runtime_error("Error in send data ?");
            }
        }
        totalSent += (size_t)dataSent;
    }
}

std::string TcpConnect::ReceiveFixedSize(size_t bytesWanted) {

    if (bytesWanted == 0) {
        return {};
    }
  
    if (bytesWanted > ((1 << 19) - 1)) {
        throw std::runtime_error("Requested buffer size is too large");
    }

    std::string data;
    data.reserve(bytesWanted);

    // try looking in leftover buffer first
    if (!leftover_.empty()) {
        size_t canTake = std::min(bytesWanted, leftover_.size());
        data.append(leftover_, 0, canTake);
        leftover_.erase(0, canTake);

        if (data.size() == bytesWanted) {
            // We already have enough from leftover
            return data;
        }
    }

    while (data.size() < bytesWanted) {
        // Attempt to read more data into leftover_
        // If poll times out or there's no new data, ReadIntoLeftover() returns false
        if (!ReadIntoLeftover()) {
            // Timeout or no data arrived => can't proceed
            throw std::runtime_error("Timeout or no data while receiving fixed-size data");
        }
        // After reading, leftover_ might contain more bytes
        size_t needed = bytesWanted - data.size();
        size_t canTake = std::min(needed, leftover_.size());
        data.append(leftover_, 0, canTake);
        leftover_.erase(0, canTake);
        
    }
    return data; 
}
bool TcpConnect::ReadIntoLeftover(){

    pollfd p;
    p.fd = sock_;
    p.events = POLLIN;

    size_t pollRes = poll(&p, 1, static_cast<int>(readTimeout_.count()));
    if (pollRes < 0) {
        l->error("ReadIntoLeftover, poll < 0");
        throw std::runtime_error(std::string("Poll error: ") + std::strerror(errno));
    }
    if (pollRes == 0) {
        // Timed out
        l->warn("ReadIntoLeftover, poll timed out");
        return false;
    }

    if (!(p.revents & POLLIN)) {
        // events like POLLHUP, POLLERR, etc. 
        l->warn("ReadIntoLeftover, poll even is NOT pollin");
        return false;
    }

    bool didReadAnything = false;

    while (true) {
        char buf[4096];
        ssize_t received = recv(sock_, buf, sizeof(buf), MSG_DONTWAIT);

        if (received < 0) {
            // If no data is left, EAGAIN or EWOULDBLOCK => break out
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            l->error("ReadIntoLeftover, recv < 0");
            throw std::runtime_error(std::string("recv error: ") + std::strerror(errno));
        }
        else if (received == 0) {
            // Peer closed the connection
            l->error("ReadIntoLeftover, recv == 0, connection closed");
            throw std::runtime_error("Connection closed by peer");
        }
        else {
            // We got some data, append to leftover
            leftover_.append(buf, static_cast<size_t>(received));
            didReadAnything = true;
            // Keep reading until EAGAIN
        }
    }

    return didReadAnything;
}


std::string TcpConnect::ReceiveOneMessage(){
    // We need at least 4 bytes for the length prefix
    while (leftover_.size() < 4) {
        if (!ReadIntoLeftover()) {
            // Timeout or no data arrived
            throw std::runtime_error("Timeout or no data while waiting for length prefix");
        }
    }

    // Parse the 4-byte length
    uint32_t msgSize = BytesToInt(leftover_.substr(0, 4));
    leftover_.erase(0, 4);

    // Check size constraints
    if (msgSize > ((1 << 19) - 1)) {
        throw std::runtime_error("Message size too large");
    }

    // Now read until we have the entire message in leftover_
    while (leftover_.size() < msgSize) {
        if (!ReadIntoLeftover()) {
            throw std::runtime_error("Timeout or no data while receiving message payload");
        }
    }

    // Extract the message
    std::string message = leftover_.substr(0, msgSize);
    leftover_.erase(0, msgSize);

    return message;
}


std::string TcpConnect::ReceiveData(size_t bufferSize) {
    if (sock_ < 0) {
        throw std::runtime_error("Invalid socket in receive");
    }

    if (bufferSize != 0) {
        // read exact number of bytes
        return TcpConnect::ReceiveFixedSize(bufferSize);
    } else {
        // single length-prefixed message
        return TcpConnect::ReceiveOneMessage();
    }
}



// std::string TcpConnect::ReceiveData(size_t bufferSize) const{
//     if (sock_ < 0) {
//         l->error("Invalid socket in receive");
//         throw std::runtime_error("Invalid socket in receive");
//     }

//     if (bufferSize > ((1 << 19) - 1)) {
//         l->error("Buffer size will be too large upper");
//         throw std::runtime_error("Buffer size will be too large upper");
//     }

//     struct pollfd p;
//     p.fd = sock_;
//     p.events = POLLIN;

//     int poll_status = poll(&p, 1, readTimeout_.count());
//     l->trace("Raw tcp code pure receive, peer {} poll: {}", GetIp(), poll_status);

//     if(poll_status < 0){
//         l->error("poll status < 0");
//         throw std::runtime_error("poll status < 0");
//     }else if(poll_status == 0){
//         l->error("poll time out 0");
//         throw std::runtime_error("poll time out 0 ");
//     }else{
//         if(p.revents & POLLIN){
//             if(bufferSize != 0){
//                 l->trace("Raw tcp code pure receive, buffersize != 0 peer {}", GetIp());
//                 char buffer[bufferSize]; 
//                 size_t recv_status = recv(sock_, buffer, sizeof(buffer), MSG_DONTWAIT );
//                 if (recv_status < 0) {
//                     l->error("recv < 0 error");
//                     throw std::runtime_error("recv < 0 error");
//                 } else if (static_cast<size_t>(recv_status) > bufferSize) {
//                     l->error("recv > buffersize");
//                     throw std::runtime_error("recv > buffersize");
//                 }
//                 return std::string(buffer, recv_status);
//             }else{
//                 l->trace("Raw tcp code pure receive, buffersize 0 peer {}", GetIp());
//                 char buffer[4];  
//                 ssize_t recv_status = recv(sock_, buffer, sizeof(buffer), 0);
               
//                 if (recv_status == 0) {
//                     l->error("recv = 0, buffersize = 4");
//                     throw std::runtime_error("recv = 0, buffersize = 4");
//                 } else if (recv_status < 0) {
//                     l->error("recv < 0, buffersize = 4");
//                     throw std::runtime_error("recv < 0, buffersize = 4");
//                 }

//                 std::string buffer_str = std::string(buffer, 4);
//                 size_t received_size = BytesToInt(buffer_str);
                
//                 if(received_size > ((1 << 19) - 1)){
//                     l->error("Buffer size will be too large");
//                     throw std::runtime_error("Buffer size will be too large");
//                 }
//                 char new_buffer[received_size];
//                 std::string data;
//                 size_t bytesRead = 0;
//                 size_t bytesToRead = received_size;

//                 int fcntlStatus = fcntl(sock_, F_GETFL, 0);
//                 fcntlStatus &= ~O_NONBLOCK;
//                 fcntl(sock_, F_SETFL, fcntlStatus);

//                 l->trace("Raw tcp code pure receive, bf = 0, fcntl set, before loop, peer {}", GetIp());
//                 auto startTime = std::chrono::steady_clock::now();
//                 do{
//                     auto diff = std::chrono::steady_clock::now() - startTime;
//                     if (std::chrono::duration_cast<std::chrono::milliseconds>(diff) > (readTimeout_ / 2)) {
//                         l->error("Read timeout");
//                         throw std::runtime_error("Read timeout");
//                     }
                    
//                    ssize_t readNow = recv(sock_, new_buffer, received_size, 0);
//                     l->trace("Raw tcp code pure receive, peer {}, in loop, cur diff in time {} ms, cur read {}",
//                         GetIp(),
//                         std::chrono::duration_cast<std::chrono::milliseconds>(diff).count(),
//                         readNow);

//                     if (readNow <= 0) {
//                         l->error("recv < 0");
//                         throw std::runtime_error("recv < 0");
//                     }
//                     bytesToRead -= static_cast<size_t>(readNow);
//                     data.append(new_buffer, static_cast<size_t>(readNow));
//                 } while (bytesToRead > 0);

//                 l->trace("Raw tcp code pure receive, AFTER loop, peer {}", GetIp());
//                 fcntlStatus |= O_NONBLOCK;
//                 fcntl(sock_, F_SETFL, fcntlStatus);
//                 return data;
//             }
//         }else{
//             l->error("POLLIN failed");
//             throw std::runtime_error("POLLIN failed");
//         }
//     }
//     return "";

// }

void TcpConnect::CloseConnection(){
    if (sock_ != -1) {
        close(sock_);
        sock_ = -1;
    }
}


const std::string &TcpConnect::GetIp() const {
    return ip_;
}

int TcpConnect::GetPort() const {
    return port_;
}

