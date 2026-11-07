#pragma once
#include "nfsu/lan_protocol.h"
#include <cstdint>
#include <string>

namespace nfsu {
struct LanAddress {
    std::uint32_t ipv4=0; // Opaque network-byte-order address, never serialized.
    std::uint16_t port=0;
    bool operator==(const LanAddress& b)const noexcept{return ipv4==b.ipv4&&port==b.port;}
};
class LanSocket {
public:
    LanSocket()=default;~LanSocket();
    LanSocket(const LanSocket&)=delete;LanSocket& operator=(const LanSocket&)=delete;
    bool open(std::uint16_t port,std::string&);
    void close() noexcept;
    bool send(const LanAddress&,const std::vector<std::uint8_t>&,std::string&);
    // Zero means no datagram; -1 a terminal socket failure. Always nonblocking.
    int receive(LanAddress&,std::uint8_t*,std::size_t,std::string&);
    std::uint16_t port()const noexcept{return port_;}
    static bool address(const std::string&,std::uint16_t,LanAddress&,std::string&);
private:
    std::intptr_t handle_=-1;std::uint16_t port_=0;bool runtime_=false;
};
// Single-threaded nonblocking room. Both ready before simulation. Host owns
// world state; only the guest's sequenced controls cross in the other direction.
class LanLink {
public:
    ~LanLink(){leave();}
    bool host(std::uint16_t port,std::uint32_t nonce,std::uint64_t now,std::string&);
    bool join(const std::string& ip,std::uint16_t port,std::uint32_t nonce,std::uint64_t now,std::string&);
    bool poll(std::uint64_t now,std::string&);
    bool paired()const noexcept{return peer_known_&&session_&&!closed_;}
    bool configure_race(const LanRaceOptions&,bool launch,std::string&);
    bool confirm_guest_ride(const LanRide&,std::string&);
    bool guest_ride_ready()const noexcept{return guest_ride_ready_;}
    const LanRaceOptions& race_options()const noexcept{return race_options_;}
    bool race_launched()const noexcept{return race_launched_;}
    void set_ready()noexcept{local_ready_=true;}
    bool connected()const noexcept{return peer_known_&&session_&&local_ready_&&remote_ready_&&!closed_;}
    bool send_input(const GamepadState&,std::string&,std::uint32_t* sequence=nullptr);
    bool publish(const LanRaceFrame&,std::string&);
    GamepadState remote_input(std::uint64_t now)const noexcept;
    bool take_snapshot(LanRaceFrame&);
    void leave() noexcept;
    bool is_host()const noexcept{return host_;}
    std::uint16_t port()const noexcept{return socket_.port();}
    bool has_remote_input()const noexcept{return host_&&have_input_;}
    std::uint32_t input_ack()const noexcept{return input_sequence_;}
    std::uint64_t invalid_packets()const noexcept{return invalid_;}
    std::uint64_t received_bytes()const noexcept{return received_bytes_;}
    std::uint64_t sent_bytes()const noexcept{return sent_bytes_;}
private:
    bool send(LanMessage,std::string&);
    bool send_room(std::string&);
    LanSocket socket_;LanAddress peer_;
    bool host_=false,peer_known_=false,local_ready_=false,remote_ready_=false,closed_=false;
    bool have_input_=false,have_snapshot_=false,unread_snapshot_=false;
    LanRaceOptions race_options_;
    LanRide guest_ride_;
    bool race_launched_=false,guest_ride_ready_=false;
    std::uint32_t room_revision_=0;
    std::uint32_t nonce_=0,peer_nonce_=0,session_=0,input_sequence_=0,snapshot_sequence_=0,send_sequence_=0;
    std::uint64_t opened_=0,last_receive_=0,last_heartbeat_=0,last_input_=0;
    std::uint64_t invalid_=0,received_bytes_=0,sent_bytes_=0;
    GamepadState remote_;LanRaceFrame snapshot_;
};
}
