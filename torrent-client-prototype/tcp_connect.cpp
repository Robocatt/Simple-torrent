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
#include <limits>
#include <utility>


#include <netdb.h>
#include <vector>
#include <string>
#include <limits>


TcpConnect::TcpConnect(std::string ip, int port, std::chrono::milliseconds connectTimeout, std::chrono::milliseconds readTimeout) :
    ip_(ip), port_(port), connectTimeout_(connectTimeout), readTimeout_(readTimeout){}

TcpConnect::~TcpConnect(){
    close(sock_);

}


void TcpConnect::EstablishConnection(){
    struct addrinfo hints;
    struct addrinfo* server_result;
    struct addrinfo* recieved_node = NULL; 

    
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int getaddinfo_status = getaddrinfo(ip_.c_str(), std::string(std::to_string(port_)).c_str(), &hints, &server_result);
    if(getaddinfo_status < 0){
        throw std::runtime_error("getaddrinfo fail");
    }

    
    int sock;
    for(recieved_node = server_result; recieved_node != NULL; recieved_node = server_result->ai_next){
        sock = socket(recieved_node->ai_family, recieved_node->ai_socktype, recieved_node->ai_protocol);
        if(sock < 0){
            continue;
        }
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        break;
    }

    if(recieved_node == NULL){
        throw std::runtime_error("Socket is null");
    }

    sock_ = socket(recieved_node->ai_family, recieved_node->ai_socktype, recieved_node->ai_protocol);
   
    int fcntl_status = fcntl(sock_, F_SETFL, O_NONBLOCK);
    if(fcntl_status < 0){
        throw std::runtime_error("fcntl error");
    }

    int connect_status = connect(sock_, server_result->ai_addr, server_result->ai_addrlen );
    freeaddrinfo(recieved_node);
    if(connect_status < 0){
        if(errno == EINPROGRESS){
            // do{
                struct timeval tv;
                tv.tv_sec = connectTimeout_.count() / 1000;
                tv.tv_usec = (connectTimeout_.count() % 1000) * 1000;
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(sock_, &writefds);

                int select_status = select(sock_ + 1, NULL, &writefds, NULL, &tv); 
                if(select_status < 0 && errno != EINTR){
                    throw std::runtime_error("Error connecting");
                 }else if(select_status > 0){
                    socklen_t l = sizeof(int);
                    int valopt; 
                    if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &l) < 0) { 
                        throw std::runtime_error("Error in getsockopt()");
                    } 
                    if (valopt) { 
                        throw std::runtime_error("Error in delayed connection");
                    } 
                    
                    // break; 
                }else{
                    throw std::runtime_error("Timeout in select() - Cancelling!");
                } 
            // }while(1);
        }else{
            throw std::runtime_error("Error connecting"); 
        }
    }
    
}

void TcpConnect::SendData(const std::string& data) const{
    
    char buffer[data.size()];
    for(int i = 0; i < data.size(); ++i){
        buffer[i] = data[i];
    }
    if(data.size() == 0){
        return;
    }
    int send_status = send(sock_, buffer, data.size(), 0);
    if(send_status < 0){
        throw std::runtime_error("Send error"); 
    }    
    

}

std::string TcpConnect::ReceiveData(size_t bufferSize) const{
    if(bufferSize > ((2 << 18) - 1)){
        throw std::runtime_error("Buffer size will be too large upper");
    }

    struct pollfd p;
    p.fd = sock_;
    p.events = POLLIN;

    int poll_status = poll(&p, 1, readTimeout_.count());
    // std::cerr << "Raw tcp code pure receive, peer " << GetIp() << "poll: " << poll_status << std::endl;
    if(poll_status < 0){
        throw std::runtime_error("poll status < 0");
    }else if(poll_status == 0){
        throw std::runtime_error("poll time out buffersize != 0 ");
    }else{
        if(p.revents & POLLIN){
            if(bufferSize != 0){
                // std::cerr << "Raw tcp code pure receive, buffersize != 0 peer " << GetIp() << std::endl;
                char buffer[bufferSize]; 
                size_t recv_status = recv(sock_, buffer, sizeof(buffer), MSG_DONTWAIT );
                if(recv_status < 0){
                    throw std::runtime_error("recv < 0 error");
                }else if(recv_status > bufferSize){
                    throw std::runtime_error("recv > buffersize");
                }
                std::string s;
                for(size_t i = 0; i < recv_status; ++i){
                    s += buffer[i];
                }

                return s;
            }else{
                // std::cerr << "Raw tcp code pure receive, buffersize 0 peer " << GetIp() << std::endl;
                char buffer[4];  
                
                size_t recv_status = recv(sock_, buffer, sizeof(buffer), 0);
                // std::cerr << "Raw tcp code pure receive, bf = 0,  peer " << GetIp()<< "recv status "<< recv_status << std::endl;
                if(recv_status == 0){
                    return std::string();
                }
                if(recv_status < 0){
                    throw std::runtime_error("recv error");
                }

                std::string buffer_str;
                for(int i = 0; i < 4; ++i){
                    buffer_str += buffer[i];
                }

                size_t recieved_size = BytesToInt(buffer_str);
                
                if(recieved_size > ((2 << 18) - 1)){
                    throw std::runtime_error("Buffer size will be too large");
                }
                char new_buffer[recieved_size];
                std::string data;
                size_t bytesRead = 0;
                size_t bytesToRead = recieved_size;

                int fcntl_status = fcntl(sock_, F_GETFL, 0);
                fcntl_status &= ~O_NONBLOCK;
                fcntl(sock_, F_SETFL, fcntl_status);

                // std::cerr << "Raw tcp code pure receive, bf = 0, fcntl set, before loop, peer " << GetIp() << std::endl;
                auto startTime = std::chrono::steady_clock::now();
                do{
                    auto diff = std::chrono::steady_clock::now() - startTime;
                    if (std::chrono::duration_cast<std::chrono::milliseconds> (diff) > readTimeout_ / 2){
                        throw std::runtime_error("Read timeout");
                    }
                    
                    bytesRead = recv(sock_, new_buffer, recieved_size, 0);
                    // std::cerr << "Raw tcp code pure receive, peer " << GetIp() << "in loop, cur diff in time " << diff << " cur read " << bytesRead << std::endl;
                    if (bytesRead <= 0){
                        throw std::runtime_error("recv < 0");
                    }
                    bytesToRead -= bytesRead;
                    for (int i = 0; i < bytesRead; ++i){
                        data += new_buffer[i];
                    }
                }
                while (bytesToRead > 0);

                // std::cerr << "Raw tcp code pure receive,  AFTER loop, peer " << GetIp() << std::endl;
                fcntl_status |= O_NONBLOCK;
                fcntl(sock_, F_SETFL, fcntl_status);
                return data;
            }
        }else{
            throw std::runtime_error("POLLIN failed");
        }
    }


}

void TcpConnect::CloseConnection(){
    close(sock_);
}


const std::string &TcpConnect::GetIp() const {
    return ip_;
}

int TcpConnect::GetPort() const {
    return port_;
}

