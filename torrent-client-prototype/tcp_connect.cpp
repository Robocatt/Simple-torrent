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
    hints.ai_flags = AI_PASSIVE;
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
        break;
    }

    if (receivedNode == NULL) {
        freeaddrinfo(serverResult);
        l->error("Creating socket is not possible with current getaddrinfo");
        throw std::runtime_error("Creating socket is not possible with current getaddrinfo");
    }

    int fcntlStatus = fcntl(sock_, F_SETFL, O_NONBLOCK);
    if(fcntlStatus < 0){
        close(sock_);
        freeaddrinfo(serverResult);
        l->error("fcntl error");
        throw std::runtime_error("fcntl error");
    }

    int connect_status = connect(sock_, receivedNode->ai_addr, receivedNode->ai_addrlen );
    freeaddrinfo(serverResult);
    if(connect_status < 0){
        if(errno == EINPROGRESS){
            struct timeval tv;
            tv.tv_sec = connectTimeout_.count() / 1000;
            tv.tv_usec = (connectTimeout_.count() % 1000) * 1000;
            
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(sock_, &writefds);

            int selectStatus = select(sock_ + 1, NULL, &writefds, NULL, &tv); 
            if(selectStatus < 0 && errno != EINTR){
                l->error("Error connecting");
                throw std::runtime_error("Error connecting");
            }else if(selectStatus > 0){
                socklen_t le = sizeof(int);
                int valopt; 
                if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &le) < 0) { 
                    l->error("Error in getsockopt()");
                    throw std::runtime_error("Error in getsockopt()");
                } 
                if (valopt) { 
                    l->error("Error in delayed connection");
                    throw std::runtime_error("Error in delayed connection");
                } 
            }else{
                l->error("Timeout in select() - Cancelling!");
                throw std::runtime_error("Timeout in select() - Cancelling!");
            } 
        }else{
            l->error("Error connecting");
            throw std::runtime_error("Error connecting"); 
        }
    }
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

std::string TcpConnect::ReceiveData(size_t bufferSize) const{
    if (sock_ < 0) {
        l->error("Invalid socket in receive");
        throw std::runtime_error("Invalid socket in receive");
    }

    if (bufferSize > ((1 << 19) - 1)) {
        l->error("Buffer size will be too large upper");
        throw std::runtime_error("Buffer size will be too large upper");
    }

    struct pollfd p;
    p.fd = sock_;
    p.events = POLLIN;

    int poll_status = poll(&p, 1, readTimeout_.count());
    l->trace("Raw tcp code pure receive, peer {} poll: {}", GetIp(), poll_status);

    if(poll_status < 0){
        l->error("poll status < 0");
        throw std::runtime_error("poll status < 0");
    }else if(poll_status == 0){
        l->error("poll time out 0");
        throw std::runtime_error("poll time out 0 ");
    }else{
        if(p.revents & POLLIN){
            if(bufferSize != 0){
                l->trace("Raw tcp code pure receive, buffersize != 0 peer {}", GetIp());
                char buffer[bufferSize]; 
                size_t recv_status = recv(sock_, buffer, sizeof(buffer), MSG_DONTWAIT );
                if (recv_status < 0) {
                    l->error("recv < 0 error");
                    throw std::runtime_error("recv < 0 error");
                } else if (static_cast<size_t>(recv_status) > bufferSize) {
                    l->error("recv > buffersize");
                    throw std::runtime_error("recv > buffersize");
                }
                return std::string(buffer, recv_status);
            }else{
                l->trace("Raw tcp code pure receive, buffersize 0 peer {}", GetIp());
                char buffer[4];  
                ssize_t recv_status = recv(sock_, buffer, sizeof(buffer), 0);
               
                if (recv_status == 0) {
                    l->error("recv = 0, buffersize = 4");
                    throw std::runtime_error("recv = 0, buffersize = 4");
                } else if (recv_status < 0) {
                    l->error("recv < 0, buffersize = 4");
                    throw std::runtime_error("recv < 0, buffersize = 4");
                }

                std::string buffer_str = std::string(buffer, 4);
                size_t received_size = BytesToInt(buffer_str);
                
                if(received_size > ((1 << 19) - 1)){
                    l->error("Buffer size will be too large");
                    throw std::runtime_error("Buffer size will be too large");
                }
                char new_buffer[received_size];
                std::string data;
                size_t bytesRead = 0;
                size_t bytesToRead = received_size;

                int fcntlStatus = fcntl(sock_, F_GETFL, 0);
                fcntlStatus &= ~O_NONBLOCK;
                fcntl(sock_, F_SETFL, fcntlStatus);

                l->trace("Raw tcp code pure receive, bf = 0, fcntl set, before loop, peer {}", GetIp());
                auto startTime = std::chrono::steady_clock::now();
                do{
                    auto diff = std::chrono::steady_clock::now() - startTime;
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(diff) > (readTimeout_ / 2)) {
                        l->error("Read timeout");
                        throw std::runtime_error("Read timeout");
                    }
                    
                   ssize_t readNow = recv(sock_, new_buffer, received_size, 0);
                    l->trace("Raw tcp code pure receive, peer {}, in loop, cur diff in time {} ms, cur read {}",
                        GetIp(),
                        std::chrono::duration_cast<std::chrono::milliseconds>(diff).count(),
                        readNow);

                    if (readNow <= 0) {
                        l->error("recv < 0");
                        throw std::runtime_error("recv < 0");
                    }
                    bytesToRead -= static_cast<size_t>(readNow);
                    data.append(new_buffer, static_cast<size_t>(readNow));
                } while (bytesToRead > 0);

                l->trace("Raw tcp code pure receive, AFTER loop, peer {}", GetIp());
                fcntlStatus |= O_NONBLOCK;
                fcntl(sock_, F_SETFL, fcntlStatus);
                return data;
            }
        }else{
            l->error("POLLIN failed");
            throw std::runtime_error("POLLIN failed");
        }
    }
    return "";

}

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

