#include "byte_tools.h"
#include "peer_connect.h"
#include "message.h"
#include <sstream>
#include <utility>

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
    bitfield_[pos]  |= (1 << (7 - bit_pos));
}

size_t PeerPiecesAvailability::Size() const{
    return bitfield_.size() * 8;
}

PeerConnect::PeerConnect(const Peer& peer, const TorrentFile &tf, std::string selfPeerId, PieceStorage& pieceStorage) :
 tf_(tf), selfPeerId_(selfPeerId), terminated_(false), choked_(true),
 socket_(TcpConnect (peer.ip, peer.port, std::chrono::milliseconds(2000), std::chrono::milliseconds(4000))), pieceInProgress_(nullptr), pieceStorage_(pieceStorage), pendingBlock_(false) {
    //changing std::chrono::milliseconds(8000) breaks code 
    l = spdlog::get("mainLogger");
    l->trace("RUN PEER WITH IP : {}", peer.ip);
    if(selfPeerId_.size() != 20){
        l->error("Self id is not 20 bytes long");
        throw std::runtime_error("Self id is not 20 bytes long");
    }
 }

void PeerConnect::Run() {
    while (!terminated_) {
        if (EstablishConnection()) {
            l->info("Connection established to peer {}", socket_.GetIp());
            MainLoop();
        } else {
            l->info("Cannot establish connection to peer {}", socket_.GetIp());
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
        l->info("Peer {} 1 Not a BIttorrent", socket_.GetIp());
        throw std::runtime_error("1 Not a BIttorrent");
    }
    if(handshake_recieved.substr(1, 19) != "BitTorrent protocol"){
        l->info("Peer {} 2 Not a BIttorrent", socket_.GetIp());
        throw std::runtime_error("2 Not a BIttorrent");
    }
    if(handshake_recieved.substr(28, 20) != tf_.infoHash){
        l->info("Peer {} 3 Not a BIttorrent", socket_.GetIp());
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
        l->info("Failed to establish connection with peer {}:{} -- {}", socket_.GetIp(),socket_.GetPort(), e.what() );
        return false;
    }
}

void PeerConnect::ReceiveBitfield() {
    std::string bitfield = socket_.ReceiveData();
    Message ms = Message::Parse(bitfield);
    if(bitfield.size() == 0){
        l->info("Peer {} bitfield size < 0", socket_.GetIp());
        throw std::runtime_error("bitfield size < 0");
    }
    if(ms.id == MessageId::BitField){
        bitfield = bitfield.substr(1);
        piecesAvailability_ = PeerPiecesAvailability(bitfield);
    }else if(ms.id == MessageId::Unchoke){
        choked_ = false;
    }else{
        l->info("Message type Neither 1 nor 5");
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
    l->warn("Terminate, peer {}", socket_.GetIp());
    terminated_ = true;
}

bool PeerConnect::Failed() const{
    return failed_;
}


void PeerConnect::RequestPiece() {
    // here is bug somewhere. See output.txt
    // why so many same requests? From where are they? 
    if(pieceInProgress_ == nullptr){
        pieceInProgress_ = pieceStorage_.GetNextPieceToDownload();
    }else if(pieceInProgress_ && pieceInProgress_->AllBlocksRetrieved()){
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
    l->trace("{} peer, requested piece index {} with offset {}",socket_.GetIp(), pieceInProgress_->GetIndex(), pieceInProgress_->FirstMissingBlock()->offset);
    
    pieceInProgress_->FirstMissingBlock()->status = Block::Status::Pending;
    
    Message ms;
    ms = ms.Init(MessageId::Request, payload);
    socket_.SendData(ms.ToString());
    l->trace("{} peer after data send", socket_.GetIp());
    pendingBlock_ = true;
    
}


void PeerConnect::MainLoop() {
    while (!terminated_) {
        std::string receivedData;
        l->trace("{} peer, PeerConnect::MainLoop BEFORE receive from socket", socket_.GetIp());

        try {
            receivedData = socket_.ReceiveData();
        } catch (const std::exception& e) {
            l->error("{} peer, Error in receiveData, del piece, term the peer: {}", socket_.GetIp(), e.what());

            // Handle mismatched hash and return the piece to the queue
            if (pieceInProgress_) {
                pieceStorage_.PieceProcessed(pieceInProgress_);
            }

            Terminate();
            break;
        }

        l->trace("{} peer, AFTER receive from socket", socket_.GetIp());

        Message ms = ms.Parse(receivedData);
        
        switch (ms.id) {
            case MessageId::Have: {
                size_t index_of_bit_to_be_set = BytesToInt(ms.payload);
                piecesAvailability_.SetPieceAvailability(index_of_bit_to_be_set);
                break;
            }
            case MessageId::KeepAlive: {
                socket_.updateConnectionTimeout();
                l->trace("PeerConnect main loop, received keep-alive, updated connection timeout for peer {}", socket_.GetIp());
                break;
            }
            case MessageId::Choke: {
                choked_ = true;
                break;
            }
            case MessageId::Unchoke: {
                choked_ = false;
                break;
            }
            case MessageId::Piece: {
                pendingBlock_ = false;
                size_t index_received = BytesToInt(ms.payload.substr(0, 4));
                size_t begin_received = BytesToInt(ms.payload.substr(4, 4));
                std::string data_received = ms.payload.substr(8);

                pieceInProgress_->SaveBlock(begin_received, data_received);

                l->trace("In main loop, peer: {} index {} offset {} saved", socket_.GetIp(), index_received, begin_received);
                break;
            }
            default: {
                l->error("{} peer BEFORE ERROR THROW: {}", socket_.GetIp(), ms.payload.empty() ? "empty payload" : ms.payload);
                throw std::runtime_error("Something bad occurred in main loop");
            }
        }

        if (!choked_ && !pendingBlock_) {
            RequestPiece();
        }

        l->trace("{} peer, requested piece, back to loop, terminated? {}", socket_.GetIp(), terminated_);
    }

    l->trace("{} peer, Main loop ended", socket_.GetIp());
}


