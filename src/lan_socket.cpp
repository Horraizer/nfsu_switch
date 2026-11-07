#include "nfsu/lan_link.h"
#include <array>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace nfsu {
namespace {
unsigned runtime_users=0; // All access is on the application thread.
#ifdef _WIN32
using NativeSocket=SOCKET;
int socket_error(){return WSAGetLastError();}
void close_native(NativeSocket s){closesocket(s);}
bool empty_error(int e){return e==WSAEWOULDBLOCK||e==WSAECONNRESET||e==WSAEMSGSIZE;}
#else
using NativeSocket=int;
int socket_error(){return errno;}
void close_native(NativeSocket s){::close(s);}
bool empty_error(int e){return e==EAGAIN||e==EWOULDBLOCK||e==EINTR||e==EMSGSIZE;}
#endif
bool runtime_open(std::string& error){
    if(runtime_users){++runtime_users;return true;}
#ifdef _WIN32
    WSADATA data{};const int result=WSAStartup(MAKEWORD(2,2),&data);
    if(result){error="LAN WSA startup: "+std::to_string(result);return false;}
#elif defined(__SWITCH__)
    const auto result=socketInitializeDefault();
    if(R_FAILED(result)){error="LAN BSD startup: "+std::to_string(result);return false;}
#endif
    ++runtime_users;return true;
}
void runtime_close(){
    if(!runtime_users||--runtime_users)return;
#ifdef _WIN32
    WSACleanup();
#elif defined(__SWITCH__)
    socketExit();
#endif
}
sockaddr_in native(const LanAddress& a){sockaddr_in n{};n.sin_family=AF_INET;n.sin_port=htons(a.port);n.sin_addr.s_addr=a.ipv4;return n;}
}
LanSocket::~LanSocket(){close();}
void LanSocket::close() noexcept {
    if(handle_!=-1){close_native(static_cast<NativeSocket>(handle_));handle_=-1;}
    if(runtime_){runtime_close();runtime_=false;}port_=0;
}
bool LanSocket::open(std::uint16_t port,std::string& error){
    if(handle_!=-1){error="LAN socket already open";return false;}
    if(!runtime_open(error))return false;
    runtime_=true;
    auto fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);handle_=static_cast<std::intptr_t>(fd);
    if(handle_==-1){error="LAN socket: "+std::to_string(socket_error());close();return false;}
#ifdef _WIN32
    u_long nonblocking=1;const int changed=ioctlsocket(fd,FIONBIO,&nonblocking);
#else
    const auto flags=fcntl(fd,F_GETFL,0);const int changed=flags<0?-1:fcntl(fd,F_SETFL,flags|O_NONBLOCK);
#endif
    auto local=native({0,port});
    if(changed<0||::bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))<0){error="LAN bind/nonblocking: "+std::to_string(socket_error());close();return false;}
    socklen_t size=sizeof(local);
    if(getsockname(fd,reinterpret_cast<sockaddr*>(&local),&size)<0){error="LAN local address failed";close();return false;}
    port_=ntohs(local.sin_port);error.clear();return true;
}
bool LanSocket::address(const std::string& ip,std::uint16_t port,LanAddress& out,std::string& error){
    in_addr a{};if(!port||inet_pton(AF_INET,ip.c_str(),&a)!=1||!a.s_addr){error="invalid LAN IPv4 address";return false;}
    out={a.s_addr,port};error.clear();return true;
}
bool LanSocket::send(const LanAddress& address,const std::vector<std::uint8_t>& bytes,std::string& error){
    if(handle_==-1||bytes.empty()||bytes.size()>kLanDatagramLimit){error="invalid LAN send";return false;}
    const auto to=native(address);
    auto n=sendto(static_cast<NativeSocket>(handle_),reinterpret_cast<const char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof(to));
    if(n<0&&empty_error(socket_error()))return true; // Bounded UDP loss; never stall the frame.
    if(n!=int(bytes.size())){error="LAN send: "+std::to_string(socket_error());return false;}
    error.clear();return true;
}
int LanSocket::receive(LanAddress& address,std::uint8_t* bytes,std::size_t capacity,std::string& error){
    if(handle_==-1||!bytes||capacity>65536){error="invalid LAN receive";return -1;}
    sockaddr_in from{};socklen_t size=sizeof(from);
    auto n=recvfrom(static_cast<NativeSocket>(handle_),reinterpret_cast<char*>(bytes),int(capacity),0,reinterpret_cast<sockaddr*>(&from),&size);
    if(n<0){if(empty_error(socket_error()))return 0;error="LAN receive: "+std::to_string(socket_error());return -1;}
    address={from.sin_addr.s_addr,ntohs(from.sin_port)};error.clear();return int(n);
}
}
