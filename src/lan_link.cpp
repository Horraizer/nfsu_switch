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
bool LanLink::accept_hello(const LanAddress& from,const LanMessage& message,std::uint64_t now,std::string& error){
    peer_=from;peer_known_=true;peer_nonce_=message.session;last_receive_=now;
    if(message.guest_ready&&!guest_ride_ready_&&!race_launched_){
        guest_ride_=message.guest_ride;guest_ride_ready_=true;
        race_options_.rides[1]=guest_ride_;++room_revision_;
    }
    return send_room(error);
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
void LanLink::retire_peer(const LanAddress& address,std::uint32_t nonce,std::uint64_t now)noexcept {
    retired_[retire_next_]={address,nonce,now};retire_next_=(retire_next_+1)%retired_.size();
}
bool LanLink::retired_peer(const LanAddress& address,std::uint32_t nonce,std::uint64_t now)const noexcept {
    for(const auto& old:retired_)
        if(old.nonce==nonce&&old.address==address&&now>=old.at&&now-old.at<=120000000)return true;
    return false;
}
void LanLink::release_lobby_peer(std::uint64_t now)noexcept {
    retire_peer(peer_,peer_nonce_,now);peer_={};peer_known_=false;peer_nonce_=0;
    local_ready_=remote_ready_=false;guest_ride_ready_=false;guest_ride_={};race_options_.rides[1]={};
    have_input_=false;remote_={};input_sequence_=0;last_input_=0;last_heartbeat_=0;++room_revision_;
    // A new attempt may reuse the same UDP endpoint. Rotate the opaque room
    // session so delayed old ready/input/bye packets cannot bind that attempt.
    if(!++session_)session_=1;
}
bool LanLink::poll(std::uint64_t now,std::string& error){
    if(closed_){error="LAN session closed";return false;}
    if(now<opened_||now<last_receive_){error="LAN clock moved backwards";return false;}
    std::unique_ptr<LanMessage> replacement;LanAddress replacement_address;
    // An input flood cannot turn this into an unbounded frame-time loop.
    for(unsigned i=0;i<64;++i){
        std::array<std::uint8_t,kLanDatagramLimit+1> bytes{};LanAddress from;
        const int n=socket_.receive(from,bytes.data(),bytes.size(),error);if(n<0)return false;if(!n)break;
        received_bytes_+=std::uint64_t(n);LanMessage message;std::string reason;
        if(!lan_decode(bytes.data(),std::size_t(n),message,reason)){++invalid_;continue;}
        if(host_&&!race_launched_&&message.kind==LanMessageKind::bye&&message.ack){
            // Before welcome, the guest knows only its own join nonce. The
            // cancellation also works before its hello due to UDP reordering.
            if(peer_known_&&from==peer_&&message.ack==peer_nonce_&&(!message.session||message.session==session_)){
                release_lobby_peer(now);continue;
            }
            if(!peer_known_&&!message.session){retire_peer(from,message.ack,now);continue;}
        }
        if(host_&&message.kind==LanMessageKind::hello&&message.session){
            if(retired_peer(from,message.session,now)){++invalid_;continue;}
            if(peer_known_&&(!(from==peer_)||peer_nonce_!=message.session)){
                if(!race_launched_&&now-last_receive_>3000000){
                    replacement=std::make_unique<LanMessage>(message);replacement_address=from;
                }else ++invalid_;
                continue;
            }
            if(!accept_hello(from,message,now,error))return false;
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
        if(message.kind==LanMessageKind::bye){
            if(host_&&!race_launched_){release_lobby_peer(now);continue;}
            closed_=true;error="LAN peer left";return false;
        }
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
                have_snapshot_=true;snapshot_sequence_=message.sequence;input_sequence_=message.ack;
                if(!snapshots_)snapshots_=std::make_unique<Snapshot[]>(snapshot_capacity_);
                if(snapshot_count_==snapshot_capacity_){
                    snapshot_first_=(snapshot_first_+1)%snapshot_capacity_;--snapshot_count_;
                }
                snapshots_[(snapshot_first_+snapshot_count_)%snapshot_capacity_]={message.frame,message.ack};
                ++snapshot_count_;
            }
        }else ++invalid_;
    }
    // Preserve a quiet existing lobby pair if nobody is replacing it. A new
    // attempt may take over after a lost cancel. Decide after draining so a
    // queued fresh old heartbeat during garage work still keeps that pair.
    if(replacement&&!race_launched_&&(!peer_known_||now-last_receive_>3000000)&&
       !retired_peer(replacement_address,replacement->session,now)){
        if(peer_known_)release_lobby_peer(now);
        if(!accept_hello(replacement_address,*replacement,now,error))return false;
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
bool LanLink::take_snapshot(LanRaceFrame& out,std::uint32_t* input_ack){
    if(!snapshot_count_)return false;
    const auto& snapshot=snapshots_[snapshot_first_];out=snapshot.frame;
    if(input_ack)*input_ack=snapshot.input_ack;
    snapshot_first_=(snapshot_first_+1)%snapshot_capacity_;--snapshot_count_;return true;
}
bool lan_recover_completed_results(LanLink& link,LanRaceFrame& latest) {
    LanRaceFrame accepted;
    while(link.take_snapshot(accepted))
        if(valid_lan_frame(accepted)&&
           (accepted.step>latest.step||(accepted.step==latest.step&&accepted.finish_ready&&!latest.finish_ready)))
            latest=accepted;
    if(!valid_lan_frame(latest)||latest.outcome!=1)return false;
    for(const auto& car:latest.cars)
        if(!(car.finished&&car.status==1)&&car.status!=3&&car.status!=6&&car.status!=7&&car.status!=8)return false;
    return true;
}
void LanLink::leave() noexcept {
    if(!closed_&&peer_known_&&(session_||(!host_&&nonce_))){try{
        LanMessage m;m.kind=LanMessageKind::bye;m.session=session_;m.ack=host_?0:nonce_;std::string error;send(m,error);
    }catch(...) {}}
    closed_=true;socket_.close();
}
}
