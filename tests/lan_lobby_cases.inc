void check_lobby_cancellation(std::string& error) {
    for(unsigned mode=0;mode<4;++mode){
        LanLink host,first,next;LanSocket relay;std::uint64_t now=1000000;
        LanAddress authority,first_address,next_address;
        check(host.host(0,199,now,error)&&relay.open(0,error)&&
              LanSocket::address("127.0.0.1",host.port(),authority,error)&&
              first.join("127.0.0.1",relay.port(),77,now,error),"cancel/rejoin real UDP endpoints open");
        std::vector<std::vector<std::uint8_t>> old_welcomes;
        const auto receive=[&](LanMessageKind wanted,std::vector<std::uint8_t>& packet,LanAddress& sender){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
            do {
                std::array<std::uint8_t,kLanDatagramLimit+1> raw{};LanAddress from;
                const int size=relay.receive(from,raw.data(),raw.size(),error);
                if(size<0)return false;
                if(size){LanMessage m;std::string reason;
                    if(lan_decode(raw.data(),std::size_t(size),m,reason)){
                        std::vector<std::uint8_t> bytes(raw.begin(),raw.begin()+size);
                        if(m.kind==LanMessageKind::welcome)old_welcomes.push_back(bytes);
                        if(m.kind==wanted){packet=std::move(bytes);sender=from;return true;}
                    }
                }else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }while(std::chrono::steady_clock::now()<deadline);
            return false;
        };
        check(first.poll(now,error),"initial guest sends discovery");
        std::vector<std::uint8_t> old_hello,cancel;LanAddress sender;
        const bool hello=receive(LanMessageKind::hello,old_hello,first_address);
        check(hello,"cancel test captures the actual initial hello");if(!hello)continue;
        if(mode!=3){
            check(relay.send(authority,old_hello,error)&&await_lan(host,now,error,[&]{return host.paired();})&&!first.paired(),
                  "host reserves a slot while its first welcome is delayed");
        }
        if(mode==2){std::vector<std::uint8_t> welcome;
            check(receive(LanMessageKind::welcome,welcome,sender)&&relay.send(first_address,welcome,error)&&await_lan(first,now,error,[&]{return first.paired();}),
                  "paired guest cancellation also happens before launch");}
        first.leave();
        const bool cancelled=receive(LanMessageKind::bye,cancel,sender);
        check(cancelled,"a guest can cancel before learning the host session");
        bool room_ok=true;
        if(mode!=1&&cancelled)room_ok=relay.send(authority,cancel,error)&&host.poll(now,error);
        check(room_ok,"guest cancellation keeps the unlaunched host room open");
        if(mode==3){
            room_ok=room_ok&&relay.send(authority,old_hello,error)&&host.poll(now,error);
            check(room_ok&&!host.paired(),"cancel delivered before hello prevents an obsolete slot reservation");
        }
        check(next.join("127.0.0.1",relay.port(),78,now,error),"replacement guest starts with a fresh nonce");
        const auto pump=[&]{
            for(unsigned n=0;n<64;++n){
                std::array<std::uint8_t,kLanDatagramLimit+1> raw{};LanAddress from;
                const int size=relay.receive(from,raw.data(),raw.size(),error);if(size<=0)break;
                std::vector<std::uint8_t> packet(raw.begin(),raw.begin()+size);
                if(from==authority){if(next_address.port)check(relay.send(next_address,packet,error),"relay forwards replacement welcome");}
                else {next_address=from;check(relay.send(authority,packet,error),"relay forwards replacement controls");}
            }
        };
        const auto await_room=[&](const auto& received){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            do {
                now+=100000;
                if(!next.poll(now,error))return false;
                pump();
                if(!host.poll(now,error))return false;
                pump();
                if(!next.poll(now,error))return false;
                if(received())return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }while(std::chrono::steady_clock::now()<deadline);
            return false;
        };
        for(unsigned attempt=0;attempt<80&&room_ok&&!next.paired();++attempt){
            now+=100000;room_ok=next.poll(now,error);pump();room_ok=room_ok&&host.poll(now,error);pump();
            room_ok=room_ok&&next.poll(now,error);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(room_ok&&host.paired()&&next.paired(),"fresh guest can rejoin the same open room even when cancellation was lost");
        if(!room_ok||!next.paired())continue;
        check(now-1000000<=3500000,"lost cancellation releases its stale lobby lease promptly");
        check(relay.send(authority,old_hello,error),"old hello is replayed after replacement");
        if(cancelled)check(relay.send(authority,cancel,error),"old cancel is replayed from the replacement's same relay endpoint");
        check(host.poll(now,error)&&host.paired(),"obsolete hello/cancel cannot remove the replacement slot");
        for(const auto& welcome:old_welcomes)check(relay.send(next_address,welcome,error),"old welcome is replayed after replacement");
        check(next.poll(now,error)&&next.paired(),"old welcome cannot bind a new join nonce");
        check(next.confirm_guest_ride({},error),"replacement confirms its own ride");
        check(await_room([&]{return host.guest_ride_ready();}),"replacement ride confirmation pumps its full route");
        const auto retained=host.race_options();now+=5000000;
        check(host.poll(now,error),"quiet lobby host survives a five-second network pause");pump();
        check(next.poll(now,error),"same guest resumes its paused open lobby");pump();check(host.poll(now,error),"resumed old heartbeat refreshes the existing pair");pump();
        check(host.paired()&&next.paired()&&host.guest_ride_ready()&&host.race_options()==retained,
              "temporary lobby silence preserves the same pair, ride and settings without a new entry");
        check(host.guest_ride_ready()&&host.configure_race({},true,error),"replacement launches normally after cancellation");pump();
        check(await_room([&]{return next.race_launched();}),"replacement receives the frozen launch");
        host.set_ready();
        if(!old_welcomes.empty()){
            LanMessage old_welcome;check(lan_decode(old_welcomes[0].data(),old_welcomes[0].size(),old_welcome,error),"old session identity decodes");
            LanMessage stale;stale.kind=LanMessageKind::ready;stale.session=old_welcome.session;stale.ack=77;
            std::vector<std::uint8_t> bytes;
            check(lan_encode(stale,bytes,error)&&relay.send(authority,bytes,error)&&host.poll(now,error)&&!host.connected(),
                  "old ready at a reused peer address cannot release the new loading gate");pump();
        }
        next.set_ready();
        check(await_room([&]{return host.connected()&&next.connected();}),"replacement readiness pumps its full route");
        check(host.connected()&&next.connected(),"normal ready handshake still starts the replacement race");
        next.leave();bool closed=false;
        for(unsigned attempt=0;attempt<10&&!closed;++attempt){pump();closed=!host.poll(now,error);
            if(!closed)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(closed&&error=="LAN peer left","leaving a launched race still closes the session");
    }
}
