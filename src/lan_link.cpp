#include "nfsu/lan_link.h"
#include <array>

namespace nfsu {
bool LanLink::host(std::uint16_t port,std::uint32_t nonce,std::uint64_t now,std::string& error){
    if(!nonce||!socket_.open(port,error))return false;
    host_=true;nonce_=session_=nonce;opened_=last_receive_=now;return true;
}
bool LanLink::join(const std::string& ip,std::uint16_t port,std::uint32_t nonce,std::uint64_t now,std::string& error){
    if(!nonce||!socket_.open(0,error))return false;
    if(!LanSocket::address(ip,port,peer_,error))return false;
    nonce_=nonce;peer_known_=true;opened_=last_receive_=now;return true;
}
bool LanLink::send(LanMessage message,std::string& error){
    message.config=kLanConfig;std::vector<std::uint8_t> bytes;
    if(!lan_encode(message,bytes,error)||!socket_.send(peer_,bytes,error))return false;
    sent_bytes_+=bytes.size();return true;
}
bool LanLink::send_room(std::string& error){
    LanMessage welcome;welcome.kind=LanMessageKind::welcome;
    welcome.session=session_;welcome.ack=peer_nonce_;welcome.sequence=room_revision_;
    welcome.race=race_options_;welcome.launch=race_launched_;
    return send(welcome,error);
}
bool LanLink::configure_race(const LanRaceOptions& options,bool launch,std::string& error){
    if(!host_||closed_||race_launched_||!valid_lan_options(options)||(launch&&(!paired()||!guest_ride_ready_))){
        error="invalid LAN room setup";return false;
    }
    race_options_=options;race_options_.rides[1]=guest_ride_;
    race_launched_=launch;++room_revision_;
    if(paired())return send_room(error);
    error.clear();return true;
}
bool LanLink::confirm_guest_ride(const LanRide& ride,std::string& error){
    if(host_||!paired()||guest_ride_ready_){error="invalid LAN guest ride confirmation";return false;}
    guest_ride_=ride;guest_ride_ready_=true;last_heartbeat_=0;error.clear();return true;
}
bool LanLink::poll(std::uint64_t now,std::string& error){
    if(closed_){error="LAN session closed";return false;}
    if(now<opened_||now<last_receive_){error="LAN clock moved backwards";return false;}
    // An input flood cannot turn this into an unbounded frame-time loop.
    for(unsigned i=0;i<64;++i){
        std::array<std::uint8_t,kLanDatagramLimit+1> bytes{};LanAddress from;
        const int n=socket_.receive(from,bytes.data(),bytes.size(),error);if(n<0)return false;if(!n)break;
        received_bytes_+=std::uint64_t(n);LanMessage message;std::string reason;
        if(!lan_decode(bytes.data(),std::size_t(n),message,reason)){++invalid_;continue;}
        if(host_&&message.kind==LanMessageKind::hello&&message.session){
            if(peer_known_&&(!(from==peer_)||peer_nonce_!=message.session)){++invalid_;continue;}
            peer_=from;peer_known_=true;peer_nonce_=message.session;last_receive_=now;
            if(message.guest_ready&&!guest_ride_ready_&&!race_launched_){
                guest_ride_=message.guest_ride;guest_ride_ready_=true;
                race_options_.rides[1]=guest_ride_;++room_revision_;
            }
            if(!send_room(error))return false;
            continue;
        }
        if(!peer_known_||!(from==peer_)){++invalid_;continue;}
        if(!host_&&message.kind==LanMessageKind::welcome&&message.ack==nonce_&&message.session){
            if(session_&&session_!=message.session){++invalid_;continue;}
            if(!session_||lan_newer(message.sequence,room_revision_)){
                room_revision_=message.sequence;race_options_=message.race;race_launched_=message.launch;
            }
            session_=message.session;last_receive_=now;continue;
        }
        if(!session_||message.session!=session_){++invalid_;continue;}
        if(message.kind==LanMessageKind::bye){closed_=true;error="LAN peer left";return false;}
        if(message.kind==LanMessageKind::ready){remote_ready_=true;last_receive_=now;continue;}
        if(!connected()){++invalid_;continue;}
        if(host_&&message.kind==LanMessageKind::input){
            last_receive_=now;
            if(!have_input_||lan_newer(message.sequence,input_sequence_)){
                have_input_=true;input_sequence_=message.sequence;remote_=message.input;last_input_=now;
            }
        }else if(!host_&&message.kind==LanMessageKind::snapshot){
            last_receive_=now;
            if(!have_snapshot_||lan_newer(message.sequence,snapshot_sequence_)){
                have_snapshot_=unread_snapshot_=true;snapshot_sequence_=message.sequence;input_sequence_=message.ack;snapshot_=message.frame;
            }
        }else ++invalid_;
    }
    const auto timeout=connected()?3000000ULL:120000000ULL;
    if(peer_known_&&now-last_receive_>timeout){closed_=true;error="LAN peer timed out";return false;}
    if(!last_heartbeat_||now-last_heartbeat_>=100000){
        last_heartbeat_=now;
        if(peer_known_){
            LanMessage m;m.session=session_;m.ack=peer_nonce_;
            if(!host_&&(!session_||!local_ready_)){
                m.kind=LanMessageKind::hello;m.session=nonce_;
                m.guest_ready=guest_ride_ready_;m.guest_ride=guest_ride_;
                if(!send(m,error))return false;
            }
            else {
                if(host_&&!remote_ready_&&!send_room(error))return false;
                if(local_ready_){m.kind=LanMessageKind::ready;if(!send(m,error))return false;}
            }
        }
    }
    error.clear();return true;
}
bool LanLink::send_input(const GamepadState& input,std::string& error,std::uint32_t* sequence){
    if(host_||!connected()){error="LAN input requires connected guest";return false;}
    LanMessage m;m.kind=LanMessageKind::input;m.session=session_;m.sequence=++send_sequence_;m.input=input;
    if(!send(m,error))return false;
    if(sequence)*sequence=m.sequence;
    return true;
}
bool LanLink::publish(const LanRaceFrame& frame,std::string& error){
    if(!host_||!connected()){error="LAN snapshot requires connected host";return false;}
    LanMessage m;m.kind=LanMessageKind::snapshot;m.session=session_;m.sequence=++send_sequence_;m.ack=input_sequence_;m.frame=frame;return send(m,error);
}
GamepadState LanLink::remote_input(std::uint64_t now)const noexcept {
    if(have_input_&&now>=last_input_&&now-last_input_<=250000)return remote_;
    GamepadState safe;safe.connected=true;safe.buttons=gamepad_button_bit(GamepadButton::trigger_left);return safe;
}
bool LanLink::take_snapshot(LanRaceFrame& out){if(!unread_snapshot_)return false;out=snapshot_;unread_snapshot_=false;return true;}
void LanLink::leave() noexcept {
    if(!closed_&&session_&&peer_known_){try{LanMessage m;m.kind=LanMessageKind::bye;m.session=session_;std::string error;send(m,error);}catch(...) {}}
    closed_=true;socket_.close();
}
}
