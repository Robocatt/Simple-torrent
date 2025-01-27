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
    ms.l = spdlog::get("mainLogger");
    if(messageString[0] == char(0)){
        ms.id = MessageId::Choke;
        ms.l->trace("Message Parse, type: Choke");
    }else if(messageString[0] == char(1)){
        ms.id = MessageId::Unchoke;
        ms.l->trace("Message Parse, type: Unchoke");
    }else if(messageString[0] == char(2)){
        ms.id = MessageId::Interested;
        ms.l->trace("Message Parse, type: Interested");
    }else if(messageString[0] == char(3)){
        ms.id = MessageId::NotInterested;
        ms.l->trace("Message Parse, type: NotInterested");
    }else if(messageString[0] == char(4)){
        ms.id = MessageId::Have;
        ms.l->trace("Message Parse, type: Have");
    }else if(messageString[0] == char(5)){
        ms.id = MessageId::BitField;
        ms.l->trace("Message Parse, type: BitField");
    }else if(messageString[0] == char(6)){
        ms.id = MessageId::Request;
        ms.l->trace("Message Parse, type: Request");
    }else if(messageString[0] == char(7)){
        ms.id = MessageId::Piece;
        ms.l->trace("Message Parse, type: Piece");
    }else if(messageString[0] == char(8)){
        ms.id = MessageId::Cancel;
        ms.l->trace("Message Parse, type: Cancel");
    }else if(messageString[0] == char(9)){
        ms.id = MessageId::Port;
        ms.l->trace("Message Parse, type: Port");
    }else{
        ms.l->error("Message Parse Received incorrect id");
        throw std::runtime_error("Message Parse Received incorrect id");
    }
    ms.payload = messageString.substr(1);
    ms.messageLength = 1 + ms.payload.size();
    
    return ms;
}

Message Message::Init(MessageId id, const std::string& payload){
    Message ms;
    ms.l = spdlog::get("mainLogger");
    ms.payload = payload;
    ms.id = id;
    if(id != MessageId::KeepAlive){
        ms.messageLength = payload.size() + 1;
    }else{
        ms.messageLength = 0;
    }
    ms.l->trace("Message init success");
    return ms;
}   


std::string Message::ToString() const{  
    std::string result;
    if(id == MessageId::KeepAlive){
        for(int i = 0; i < 4; ++i){
            result += char(0);
        }
        l->trace("Message ToString keepalive");
        return result;
    }
    
    
    
    if(id == MessageId::Choke){
        result += IntToBytes(1);
        result += char(0);
        l->trace("Message ToString Choke");
    }else if(id == MessageId::Unchoke){
        result += IntToBytes(1);
        result += char(1);
        l->trace("Message ToString Unchoke");
    }else if(id == MessageId::Interested){
        result += IntToBytes(1);
        result += char(2);
        l->trace("Message ToString Interested");
    }else if(id == MessageId::NotInterested){
        result += IntToBytes(1);
        result += char(3);
        l->trace("Message ToString NotInterested");
    }else if(id == MessageId::Have){
        result += IntToBytes(5);
        result += char(4);
        l->trace("Message ToString Have");
    }else if(id == MessageId::BitField){
        result += char(5);
        l->trace("Message ToString BitField");
    }else if(id == MessageId::Request){
        result += IntToBytes(13);
        result += char(6);
        l->trace("Message ToString Request");
    }else if(id == MessageId::Piece){
        result += char(7);
        l->trace("Message ToString Piece");
    }else if(id == MessageId::Cancel){
        result += IntToBytes(13);
        result += char(8);
        l->trace("Message ToString Cancel");
    }else if(id == MessageId::Port){
        result += IntToBytes(3);
        result += char(9);
        l->trace("Message ToString Port");
    }else{
        l->error("Message ToString Cancel");
        throw std::runtime_error("Message ToString Received incorrect id");
    }
    result += payload;
    return result;

}