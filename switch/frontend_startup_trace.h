#pragma once
#include "nfsu/startup_journal.h"
#include <switch.h>
#include <malloc.h>
#include <cstdint>
#include <string>

namespace nfsu {
inline bool trace_frontend_startup(StartupJournal* journal,const char* stage,
    std::string& error,const char* name="none",std::uint64_t requested=0,
    std::uint64_t gpu_bytes=0) {
    if(!journal)return true;
    const auto heap=mallinfo();
    u64 total=0,used=0;
    const auto total_result=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    const auto used_result=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    char detail[512];
    std::snprintf(detail,sizeof detail,
        "name=%.96s requested=%llu gpu_bytes=%llu heap_arena=%llu heap_used=%llu heap_free=%llu process_total=%llu process_used=%llu total_rc=%lu used_rc=%lu",
        name,(unsigned long long)requested,(unsigned long long)gpu_bytes,
        (unsigned long long)heap.arena,(unsigned long long)heap.uordblks,(unsigned long long)heap.fordblks,
        (unsigned long long)total,(unsigned long long)used,(unsigned long)total_result,(unsigned long)used_result);
    if(journal->mark(stage,detail))return true;
    error=journal->error();return false;
}
}
