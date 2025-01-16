#include "message.h"
#include "byte_tools.h"
#include <sstream>
//debug
#include <iostream>


Message Message::Parse(const std::string& messageString){
    Message ms;
    if(messageString.size() == 0){
        ms.id = MessageId::KeepAlive;
        ms.messageLength = 0;
        ms.payload = "";
       return ms;
    }

    if(messageString[0] == char(0)){
        ms.id = MessageId::Choke;
        // std::cout << "id set to" << " choke " << std::endl;
    }else if(messageString[0] == char(1)){
        ms.id = MessageId::Unchoke;
        // std::cout << "id set to" << " unchoke " << std::endl;
    }else if(messageString[0] == char(2)){
        ms.id = MessageId::Interested;
        // std::cout << "id set to" << " Interested " << std::endl;
    }else if(messageString[0] == char(3)){
        ms.id = MessageId::NotInterested;
        // std::cout << "id set to" << " NotInterested " << std::endl;
    }else if(messageString[0] == char(4)){
        ms.id = MessageId::Have;
        // std::cout << "id set to" << " Have " << std::endl;
    }else if(messageString[0] == char(5)){
        ms.id = MessageId::BitField;
        // std::cout << "id set to" << " BitField " << std::endl;
    }else if(messageString[0] == char(6)){
        ms.id = MessageId::Request;
        // std::cout << "id set to" << " Request " << std::endl;
    }else if(messageString[0] == char(7)){
        ms.id = MessageId::Piece;
        // std::cout << "id set to" << " Piece " << std::endl;
    }else if(messageString[0] == char(8)){
        ms.id = MessageId::Cancel;
        // std::cout << "id set to" << " Cancel " << std::endl;
    }else if(messageString[0] == char(9)){
        ms.id = MessageId::Port;
        // std::cout << "id set to" << " Port " << std::endl;
    }else{
        // std::cout << "id WAS NOT SET" << std::endl;
        throw "bad type of message";
    }
    ms.payload = messageString.substr(1);
    ms.messageLength = 1 + ms.payload.size();
    
    return ms;
}

Message Message::Init(MessageId id, const std::string& payload){
    Message ms;
    ms.payload = payload;
    ms.id = id;
    if(id != MessageId::KeepAlive){
        ms.messageLength = payload.size() + 1;
    }else{
        ms.messageLength = 0;
    }
    return ms;
}   


std::string Message::ToString() const{  
    std::string result;
    if(id == MessageId::KeepAlive){
        for(int i = 0; i < 4; ++i){
            result += char(0);
        }
        return result;
    }
    
    
    
    if(id == MessageId::Choke){
        result += IntToBytes(1);
        result += char(0);
        // std::cout << "to string added char 0 " << std::endl;
    }else if(id == MessageId::Unchoke){
        result += IntToBytes(1);
        result += char(1);
        // std::cout << "to string added char 1 " << std::endl;
    }else if(id == MessageId::Interested){
        result += IntToBytes(1);
        result += char(2);
        // std::cout << "to string added char 2 " << std::endl;
    }else if(id == MessageId::NotInterested){
        result += IntToBytes(1);
        result += char(3);
        // std::cout << "to string added char 3 " << std::endl;
    }else if(id == MessageId::Have){
        result += IntToBytes(5);
        result += char(4);
        // std::cout << "to string added char 4 " << std::endl;
    }else if(id == MessageId::BitField){
        result += char(5);
        // std::cout << "to string added char 5 " << std::endl;
    }else if(id == MessageId::Request){
        result += IntToBytes(13);
        result += char(6);
        // std::cout << "to string added char 6 " << std::endl;
    }else if(id == MessageId::Piece){
        result += char(7);
        // std::cout << "to string added char 7 " << std::endl;   
    }else if(id == MessageId::Cancel){
        result += IntToBytes(13);
        result += char(8);
        // std::cout << "to string added char 8 " << std::endl;
    }else if(id == MessageId::Port){
        result += IntToBytes(3);
        result += char(9);
        // std::cout << "to string added char 9 " << std::endl;
    }else{
        // std::cout << "to string NOTHING added" << std::endl;
        throw "Bad id tostring";
    }
    result += payload;
    return result;

}