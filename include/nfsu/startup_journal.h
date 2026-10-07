#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#endif

namespace nfsu {
inline int startup_journal_sync(int fd) noexcept {
#if defined(_WIN32)
    return ::_commit(fd);
#else
    return ::fsync(fd);
#endif
}
// Opt-in crash localization only. An exclusive file preserves earlier runs.
// Each successful mark is written and synced before the next operation starts.
class StartupJournal {
    int fd_=-1;
    unsigned sequence_=0;
    int (*sync_)(int)=startup_journal_sync;
    char error_[160]{};
    bool fail(const char* operation) noexcept {
        const int code=errno;
        std::snprintf(error_,sizeof error_,"startup journal %s failed errno=%d",operation,code);
        errno=code;
        return false;
    }
public:
    StartupJournal()=default;
    explicit StartupJournal(int (*sync)(int)) noexcept:sync_(sync) {}
    StartupJournal(const StartupJournal&)=delete;
    StartupJournal& operator=(const StartupJournal&)=delete;
    ~StartupJournal() { if(fd_>=0) ::close(fd_); }
    bool open(const char* path) noexcept {
        if(fd_>=0) {errno=EBUSY;return fail("open");}
        int flags=O_WRONLY|O_CREAT|O_EXCL;
#if defined(_WIN32)
        flags|=O_BINARY;
#endif
        fd_=::open(path,flags,0600);
        return fd_>=0 || fail("open");
    }
    const char* error() const noexcept {return error_;}
    bool mark(const char* stage,const char* detail) noexcept {
        if(fd_<0) {errno=EBADF;return fail("mark");}
        char line[768];
        const int size=std::snprintf(line,sizeof line,"MENU STARTUP seq=%u stage=%s %s\n",++sequence_,stage,detail);
        if(size<0 || static_cast<unsigned>(size)>=sizeof line) {errno=EOVERFLOW;return fail("format");}
        // Refuse control/non-ASCII bytes instead of corrupting the journal.
        for(int i=0;i<size-1;++i) if(line[i]<32 || static_cast<unsigned char>(line[i])>126) {errno=EILSEQ;return fail("format");}
        std::size_t done=0;
        while(done<static_cast<std::size_t>(size)) {
            const auto count=::write(fd_,line+done,size-done);
            if(count<0 && errno==EINTR)continue;
            if(count<=0) {if(!count)errno=EIO;return fail("write");}
            done+=static_cast<std::size_t>(count);
        }
        if(sync_(fd_)!=0)return fail("fsync");
        if(std::fwrite(line,1,size,stdout)!=static_cast<std::size_t>(size) || std::fflush(stdout)!=0)
            return fail("stdout");
        return true;
    }
    bool close() noexcept {
        if(fd_<0)return true;
        const int fd=fd_;fd_=-1;
        if(sync_(fd)!=0) {const int code=errno;::close(fd);errno=code;return fail("final fsync");}
        return ::close(fd)==0 || fail("close");
    }
};
}
