#include "byte_tools.h"
#include "piece.h"

constexpr size_t BLOCK_SIZE = 1 << 14;

Piece::Piece(size_t index, size_t length, std::string hash) : index_(index), length_(length), hash_(hash) {
    size_t len = length_;
    int times = 0;
    while (len >= BLOCK_SIZE){
        blocks_.emplace_back(Block(index, times * BLOCK_SIZE, BLOCK_SIZE, Block::Status::Missing, std::string()));
        times++;
        len -= BLOCK_SIZE;
    }
    if(len > 0){
        blocks_.emplace_back(Block(index, times * BLOCK_SIZE, len, Block::Status::Missing, std::string()));
    }


}

bool Piece::HashMatches() const{
    if(!AllBlocksRetrieved()){
        return false;
    }
    std::string my_own_hash = GetDataHash();
    std::string expected_hash = GetHash();
    return my_own_hash == expected_hash;

}

Block* Piece::FirstMissingBlock(){
    for(int i = 0; i < blocks_.size(); ++i){
        if(blocks_[i].status == Block::Status::Missing){
            return (&blocks_[i]);
        }
    }
    return nullptr;
}

size_t Piece::GetIndex() const{
    return index_;
}


void Piece::SaveBlock(size_t blockOffset, std::string data){//в каких единицах blockoffset????
    for(int i = 0; i < blocks_.size(); ++i){
        if(blocks_[i].offset == blockOffset){
            blocks_[i].data = data;
            blocks_[i].status = Block::Status::Retrieved;
            return;
        }
    }

}


bool Piece::AllBlocksRetrieved() const{
    for(int i = 0; i < blocks_.size(); ++i){
        if(blocks_[i].status == Block::Status::Missing){
            return false;
        }
    }
    return true;
}


std::string Piece::GetData() const{
    std::string res;
    for(auto blk : blocks_){
        res += blk.data;
    }
    return res;
}

std::string Piece::GetDataHash() const{
    std::string data = GetData();
    std::string hsh = CalculateSHA1(data);
    return hsh;
}

const std::string& Piece::GetHash() const{
    return hash_;
}

void Piece::Reset(){
    for(int i = 0; i < blocks_.size(); ++i){
        blocks_[i].data = "";
        blocks_[i].status = Block::Status::Missing;
    }        
}

