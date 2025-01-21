#include "byte_tools.h"
#include "peer_connect.h"
#include "message.h"
#include <iostream>
#include <sstream>
#include <utility>
#include <cassert>

//debug
#include <fstream>
#include <cstddef>
#include <algorithm>
#include <cstring>


using namespace std::chrono_literals;
PeerPiecesAvailability::PeerPiecesAvailability() {}

PeerPiecesAvailability::PeerPiecesAvailability(std::string bitfield) : bitfield_(bitfield) {}

bool PeerPiecesAvailability::IsPieceAvailable(size_t pieceIndex) const{
    size_t pos = pieceIndex / 8;
    size_t bit_pos = pieceIndex % 8;
    if(bitfield_.size() <= pos){
        throw std::runtime_error("Invalid bitfield size");
    }
    if(int(bitfield_[pos]) & (1 << (7 - bit_pos))){
        return true;
    }else{
        return false;
    }
}

void PeerPiecesAvailability::SetPieceAvailability(size_t pieceIndex){
    size_t pos = pieceIndex / 8;
    size_t bit_pos = pieceIndex % 8;
    
    if(bitfield_.size() <= pos){

        throw std::runtime_error("Invalid bitfield size");
    }
    if(pos >= bitfield_.size()){// will never execute because of error above!!!
        bitfield_.resize(pos + 1, char(0));// 0 or char(0)?
    }
    bitfield_[pos]  |= (1 << (7 - bit_pos));
}

size_t PeerPiecesAvailability::Size() const{
    return bitfield_.size() * 8;

}

PeerConnect::PeerConnect(const Peer& peer, const TorrentFile &tf, std::string selfPeerId, PieceStorage& pieceStorage) :
 tf_(tf), selfPeerId_(selfPeerId), terminated_(false), choked_(true),
 socket_(TcpConnect (peer.ip, peer.port, std::chrono::milliseconds(2000), std::chrono::milliseconds(4000))), pieceInProgress_(nullptr), pieceStorage_(pieceStorage), pendingBlock_(false) {
    //changing std::chrono::milliseconds(8000) breaks code 
    std::cout << "RUN PEER WITH IP " << peer.ip << std::endl;
    if(selfPeerId_.size() != 20){
        throw std::runtime_error("Self id is not 20 bytes long");
    }
 }

void PeerConnect::Run() {
    while (!terminated_) {
        if (EstablishConnection()) {
            std::cout << "Connection established to peer" <<  std::endl;
            MainLoop();
        } else {
            std::cerr << "Cannot establish connection to peer" << std::endl;
            Terminate();
        }
    }
}

void PeerConnect::PerformHandshake() {
    socket_.EstablishConnection();
    std::string s(1, char(19));
    s += "BitTorrent protocol";
    for(int i = 0; i < 8; ++i){
        s += char(0);
    }
    s += tf_.infoHash;
    s += selfPeerId_; 
    socket_.SendData(s);
    std::string handshake_recieved = socket_.ReceiveData(68);
    
    if((unsigned char)handshake_recieved[0] != 19){
        throw std::runtime_error("1 Not a BIttorrent");
    }
    if(handshake_recieved.substr(1, 19) != "BitTorrent protocol"){
        throw std::runtime_error("2 Not a BIttorrent");
    }
    if(handshake_recieved.substr(28, 20) != tf_.infoHash){
        throw std::runtime_error("3 Not a BIttorrent");
    }
    

}

bool PeerConnect::EstablishConnection() {
    try {
        PerformHandshake();
        ReceiveBitfield();
        SendInterested();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to establish connection with peer " << socket_.GetIp() << ":" <<
            socket_.GetPort() << " -- " << e.what() << std::endl;
        return false;
    }
}

void PeerConnect::ReceiveBitfield() {
    std::string bitfield = socket_.ReceiveData();
    Message ms = Message::Parse(bitfield);
    if(bitfield.size() == 0){
        throw std::runtime_error("bitfield size < 0");
    }
    if(ms.id == MessageId::BitField){
        bitfield = bitfield.substr(1);
        piecesAvailability_ = PeerPiecesAvailability(bitfield);
    }else if(ms.id == MessageId::Unchoke){
        choked_ = false;
    }else{
    
        throw std::runtime_error("Message type Neither 1 nor 5");
    }
}

void PeerConnect::SendInterested() {
    Message ms;
    ms = ms.Init(MessageId::Interested, std::string());
    std::string send = ms.ToString();
    socket_.SendData(send);
}

void PeerConnect::Terminate() {
    std::cerr << "Terminate, peer "<< socket_.GetIp() << std::endl;
    terminated_ = true;
}

bool PeerConnect::Failed() const{
    return failed_;
}


void PeerConnect::RequestPiece() {
    if(pieceInProgress_ == nullptr){
        pieceInProgress_ = pieceStorage_.GetNextPieceToDownload();
    }
    if(pieceInProgress_->AllBlocksRetrieved()){
        pieceStorage_.PieceProcessed(pieceInProgress_);
        pieceInProgress_ = pieceStorage_.GetNextPieceToDownload();
    }

    if(pieceInProgress_ == nullptr){
        terminated_ = true;
        return;
    }

    std::string payload;

    payload += IntToBytes(pieceInProgress_->GetIndex());
    payload += IntToBytes(pieceInProgress_->FirstMissingBlock()->offset);
    payload += IntToBytes(pieceInProgress_->FirstMissingBlock()->length);
    
    std::cout << socket_.GetIp() << " peer, requested piece index " << pieceInProgress_->GetIndex() << " offset " << pieceInProgress_->FirstMissingBlock()->offset<< std::endl;
    
    pieceInProgress_->FirstMissingBlock()->status = Block::Status::Pending;
    
    Message ms;
    ms = ms.Init(MessageId::Request, payload);
    socket_.SendData(ms.ToString());
    std::cout << socket_.GetIp() << " peer after data send" << std::endl;
    pendingBlock_ = true;
    
}


void PeerConnect::MainLoop() {
    while (!terminated_) {
        std::string received_data;
        std::cout << socket_.GetIp() << " peer, BEFORE receive from socket" << std::endl;
        try{
            received_data = socket_.ReceiveData();
        }catch(const std::exception& e){
            std::cerr << "Error in receive del piece, term the peer " << socket_.GetIp() << " " << e.what() << std::endl;
            // will be mismatch in hash and the piece ll be returned to queue
            if(pieceInProgress_){
                pieceStorage_.PieceProcessed(pieceInProgress_);
            }
            Terminate();
        }
        std::cout << socket_.GetIp() << " peer, after receive from socket" << std::endl;
        // if(received_data == "tcp_fail"){
        //     pieceStorage_.PieceProcessed(pieceInProgress_);
        //     throw std::runtime_error("Error in PeerConnect main loop, tcp failed"); 
        // }
        Message ms = ms.Parse(received_data);
        if(ms.id == MessageId::Have){
            size_t index_of_bit_to_be_set = BytesToInt(ms.payload);
            piecesAvailability_.SetPieceAvailability(index_of_bit_to_be_set);
        }else if(ms.id == MessageId::Choke){
            choked_ = true;
        }else if(ms.id == MessageId::Unchoke){
            choked_ = false;
        }else if(ms.id == MessageId::Piece){
            pendingBlock_ = false;
            size_t index_recieved = BytesToInt(ms.payload.substr(0,4));
            size_t begin_recieved = BytesToInt(ms.payload.substr(4,4));
            std::string data_recieved = ms.payload.substr(8);
            // if(!pieceInProgress_->AllBlocksRetrieved()){
                pieceInProgress_->SaveBlock(begin_recieved, data_recieved);
            // }
            std::cout << "In main loop, peer: "<< socket_.GetIp() << " ";
            std::cout << "index " << index_recieved << " offset " << begin_recieved << " saved "<< std::endl;
        }else{
            std::cout << socket_.GetIp() << "BEFORE ERROR THROW" << std::endl;
            throw std::runtime_error("Sth bad occured in main loop");
        }

        if (!choked_ && !pendingBlock_) {
            RequestPiece();
        }
        std::cout << socket_.GetIp() << " peer, requested piece, back to loop, terminated?"<< terminated_ << std::endl;
    }
    std::cout << socket_.GetIp() << " peer, Main loop ended"   << std::endl;
}

