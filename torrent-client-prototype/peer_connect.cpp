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
 socket_(TcpConnect (peer.ip, peer.port, std::chrono::milliseconds(6000), std::chrono::milliseconds(6000))), pieceInProgress_(nullptr), pieceStorage_(pieceStorage), pendingBlock_(false) {
    //changing std::chrono::milliseconds(8000) breaks code or not 
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
    // std::string handshake_recieved = socket_.ReceiveData(68);
    std::string handshake_recieved = socket_.ReceiveFixedSize(68);
    
    if((unsigned char)handshake_recieved[0] != 19){
        l->info("Peer {} 1 Not a BIttorrent (pstrlen wrong)", socket_.GetIp());
        throw std::runtime_error("1 Not a BIttorrent (pstrlen wrong)");
    }
    if(handshake_recieved.substr(1, 19) != "BitTorrent protocol"){
        l->info("Peer {} 2 Not a BIttorrent (pstr mismatch)", socket_.GetIp());
        throw std::runtime_error("2 Not a BIttorrent (pstr mismatch)");
    }
    if(handshake_recieved.substr(28, 20) != tf_.infoHash){
        l->info("Peer {} 3 Not a BIttorrent InfoHash mismatch", socket_.GetIp());
        throw std::runtime_error("3 Not a BIttorrent InfoHash mismatch");
    }
    l->trace("Successful handshake with peer {}", socket_.GetIp());

}

bool PeerConnect::EstablishConnection() {
    try {
        PerformHandshake();
        // ReceiveBitfield();
        SendInterested();
        return true;
    } catch (const std::exception& e) {
        l->info("Failed to establish connection with peer {}:{} -- {}", socket_.GetIp(),socket_.GetPort(), e.what() );
        return false;
    }
}

void PeerConnect::ReceiveBitfield() {
    std::string messageData = socket_.ReceiveOneMessage();
    Message ms = Message::Parse(messageData);
    if (messageData.empty()) {
        l->info("Peer {} sent an empty message", socket_.GetIp());
        throw std::runtime_error("Empty message received");
    }
    switch (ms.id) {
        case MessageId::BitField:{
            std::string bitfield = messageData.substr(1);
            piecesAvailability_ = PeerPiecesAvailability(bitfield);
            l->trace("Received bitfield from peer {}", socket_.GetIp());
            break;
        }
        case MessageId::Unchoke:{
            choked_ = false;
            l->trace("Peer {} is unchoking us", socket_.GetIp());
            break;
        }
        case MessageId::Choke:{
            choked_ = true;
            l->trace("Peer {} is choking us", socket_.GetIp());
            break;
        }
        default:{
            l->info("Unexpected message type(not choke, unchoke, bitfield) {} from peer {}", (uint8_t)ms.id, socket_.GetIp());
            throw std::runtime_error("Unexpected message type");
        }
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
    l->trace("In RequestPiece, peer {}", socket_.GetIp());
    if(pieceInProgress_ == nullptr){
        l->trace("In RequestPiece, peer {}, piece == null,try call GetNextPieceToDownload", socket_.GetIp());
        pieceInProgress_ = pieceStorage_.GetNextPieceToDownload();
        l->trace("In RequestPiece, peer {}, piece was null, get piece index {}", socket_.GetIp(), pieceInProgress_->GetIndex());
    }else if(pieceInProgress_ && pieceInProgress_->AllBlocksRetrieved()){
        l->trace("In RequestPiece, peer {}, In else pieceInProgress", socket_.GetIp());
        pieceStorage_.PieceProcessed(pieceInProgress_);
        pieceInProgress_ = pieceStorage_.GetNextPieceToDownload();
    }
    l->trace("In RequestPiece, peer {}, after if/else pieceInProgress", socket_.GetIp());
    if(pieceInProgress_ == nullptr){
        l->trace("In RequestPiece, peer {}, pieceInProgress_ == nullptr", socket_.GetIp());
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
            receivedData = socket_.ReceiveOneMessage();
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
                size_t index = BytesToInt(ms.payload);
                piecesAvailability_.SetPieceAvailability(index);
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
            case MessageId::BitField: {
                std::string bitfield = ms.payload;
                piecesAvailability_ = PeerPiecesAvailability(bitfield);
                l->trace("Received bitfield from {}", socket_.GetIp());
                break;
            }
            case MessageId::Piece: {
                pendingBlock_ = false;
                size_t indexReceived = BytesToInt(ms.payload.substr(0, 4));
                size_t beginReceived = BytesToInt(ms.payload.substr(4, 4));
                std::string dataReceived = ms.payload.substr(8);

                if (pieceInProgress_) {
                    pieceInProgress_->SaveBlock(beginReceived, dataReceived);
                    l->trace("Saved piece data for index {} offset {}", indexReceived, beginReceived);
                }
                
                l->trace("In main loop, peer: {} index {} offset {} saved", socket_.GetIp(), indexReceived, beginReceived);
                break;
            }
            default: {
                l->error("{} peer BEFORE ERROR THROW: {}", socket_.GetIp(), ms.payload.empty() ? "empty payload" : ms.payload);
                throw std::runtime_error("Something bad occurred in main loop");
            }
        }

        if (!choked_ && !pendingBlock_) {
            l->trace("Unchoked, no pending, request piece call, peer {}", socket_.GetIp());
            RequestPiece();
        }

        l->trace("{} peer, requested piece, back to loop, terminated? {}", socket_.GetIp(), terminated_);
    }

    l->trace("{} peer, Main loop ended", socket_.GetIp());
}


