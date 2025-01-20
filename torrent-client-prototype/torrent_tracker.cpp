#include "torrent_tracker.h"
#include "bencode.h"
#include "byte_tools.h"
#include <iostream>
#include <string>
#include <cpr/cpr.h>


TorrentTracker::TorrentTracker(const std::string& url){
    long long pos = url.find("?");
    // fix for bt.t-ru.org
    // parse pk value and set as param
    std::cout << "pos = " << pos <<"\n";
    if(pos != std::string::npos){
        url_ = url.substr(0, pos);
        pk = url.substr(pos + 4);
    }else {
        // general case 
        url_ = url;
        pk = "";
    }
}

const std::vector<Peer> &TorrentTracker::GetPeers() const {
    return peers_;
}


cpr::Proxies proxies{
        {"http", "socks5://127.0.0.1:20170"},  // Proxy for HTTP traffic
        {"https", "socks5://127.0.0.1:20170"}  // Proxy for HTTPS traffic
    };

// class AddParamsInterceptor : public cpr::Interceptor {
//     private:
//         std::string pkIntercept;
//   public:
//     AddParamsInterceptor(const std::string& pk) 
//         : pkIntercept(pk) {}
//     cpr::Response intercept(cpr::Session& session) override {
//         // if(pkIntercept != ""){ 
//         // }
            
//         // Log the request URL
//         std::cout << "Request url: " << session.GetFullRequestUrl() << std::endl;

//         // Proceed the request and save the response
//         cpr::Response response = proceed(session);

//         // Log response status code
//         std::cout << "Response status code: " << response.status_code << std::endl;

//         // Return the stored response
//         return response;
//     }
// };


void TorrentTracker::UpdatePeers(const TorrentFile& tf, std::string peerId, int port){
    std::cerr << "Before update peers call"  << std::endl;
        cpr::Session session;
        cpr::Url url{url_};
        cpr::Header header{
                {"Connection", "Close"},
                {"User-Agent", "Custom-user-agent/1.0"}
            };
        cpr::Timeout timeout{2000};
        cpr::Parameters beforeGeneralParams = {
            {"pk", pk}
        };
        
        session.SetUrl(url);
        // session.AddInterceptor(std::make_shared<AddParamsInterceptor>());
        session.SetHeader(header);
        session.SetTimeout(timeout);
        // session.SetProxies(proxies);
        session.SetParameters(cpr::Parameters{
                    {"pk", pk}, // remove
                    {"info_hash", tf.infoHash},
                    {"peer_id", peerId},
                    {"port", std::to_string(port)},
                    {"uploaded", std::to_string(0)},
                    {"downloaded", std::to_string(0)},
                    {"left", std::to_string(tf.length)},
                    {"compact", std::to_string(1)}
                    });
        cpr::Response res = session.Get();
        if(res.status_code != 200){
            std::cerr << "!!!Update peers\n";
            std::cerr <<"" << tf.infoHash << "\n";
            std::cerr << res.text << std::endl;
            throw std::runtime_error("status code " + std::to_string(res.status_code));
        }
        std::cout << "Update peers, get respsonse, status code " << res.status_code << std::endl;
        std::cout << res.text<< std::endl;

        auto dict_response = Bencode::ParseDictRec(res.text); // Parse response
        auto& map_from_response = dict_response.first->elements; // get data out of pair 
        std::string peers;
        try{
            peers = std::get<std::string>(map_from_response["peers"]); // get raw peers string
        }catch(const std::exception& e){
            std::cerr << "Torrent tracker peers not string\n";
            throw std::runtime_error("123123 torrent tracker failed");
        }
        int peers_position = res.text.find("peers");    // shortcuts for a correct determination of number of peers
        int peers_colon_position = res.text.find(":", peers_position);
        int len_of_peers = std::stoi(res.text.substr(peers_position + 5, peers_colon_position - peers_position - 5 ));

        // loop over all peers
        for(int k = 0; k < len_of_peers; k += 6){
            long long id = 0;
            std::string id_str;

            // Convert chars to a 4 byte hex
            for(int i = 0; i < 4; ++i){
                unsigned long long x = (unsigned char)(peers[i + k]);
                id += (x << (24 - 8 * i));
            }

            //convert each byte to it's corresponding digit
            id_str += std::to_string((id & 0xFF000000) >> 24);
            id_str += ".";
            id_str += std::to_string((id & 0x00FF0000) >> 16);
            id_str += ".";
            id_str += std::to_string((id & 0x0000FF00) >> 8);
            id_str += ".";
            id_str += std::to_string((id & 0x000000FF));
               
            // std::cout << id_str << std::endl << std::endl;

            // convert port just to hex
            int port_ = 0;
            for(int i = 4; i < 6; ++i){
                unsigned long long x = (unsigned char)(peers[i + k]);
                port_ += (x << (8 - 8 * (i - 4)));
            }

            // construct Peer from data provided
            Peer current_peer;
            current_peer.ip = id_str;
            current_peer.port = port_;
            peers_.push_back(current_peer);
        }
        
}


// cpr::Response res = cpr::Get(
//             cpr::Url{url_},
//             cpr::Parameters {
//                     {"info_hash", tf.infoHash},
//                     {"peer_id", peerId},
//                     {"port", std::to_string(port)},
//                     {"uploaded", std::to_string(0)},
//                     {"downloaded", std::to_string(0)},
//                     {"left", std::to_string(tf.length)},
//                     {"compact", std::to_string(1)}
//             },
//             cpr::Timeout{2000}
//         );


//d8:intervali3097e12:min intervali3097e5:peers6:�;��0914:failure reason17:Invalid info_hashe