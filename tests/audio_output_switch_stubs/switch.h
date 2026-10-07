#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <chrono>
using u8=uint8_t; using s16=int16_t; using s32=int32_t; using u64=uint64_t; using Result=int;
#define R_FAILED(x) ((x)!=0)
#define AUDREN_MEMPOOL_ALIGNMENT 4096
#define AUDREN_DEFAULT_DEVICE_NAME "default"
#define AUDREN_FINAL_MIX_ID 0
#define AudioRendererOutputRate_48kHz 48000
#define PcmFormat_Int16 0
#define memalign(a,n) malloc(n)
struct AudioRendererConfig {int a,b,c,d,e,f;};
enum AudioDriverWaveBufState { AudioDriverWaveBufState_Free,AudioDriverWaveBufState_Waiting,AudioDriverWaveBufState_Queued,AudioDriverWaveBufState_Playing,AudioDriverWaveBufState_Done };
struct AudioDriverWaveBuf {s16* data_pcm16=nullptr; u64 size=0; s32 start_sample_offset=0,end_sample_offset=0; AudioDriverWaveBufState state=AudioDriverWaveBufState_Free;};
struct AudioDriver {unsigned registered=0;};
struct Thread {std::thread* native=nullptr;void(*entry)(void*)=nullptr;void* context=nullptr;};
inline std::atomic<unsigned> probe_submitted{0};
inline std::atomic<bool> probe_fail_update{false};
inline std::atomic<bool> probe_fail_initialize{false};
inline int audrenInitialize(const AudioRendererConfig*) {return probe_fail_initialize ? 1 : 0;}
inline int audrvCreate(AudioDriver* d,const AudioRendererConfig*,int) {*d={};return 0;}
inline int audrvMemPoolAdd(AudioDriver*,void*,std::size_t) {return 0;}
inline bool audrvMemPoolAttach(AudioDriver*,int) {return true;}
inline int audrvDeviceSinkAdd(AudioDriver*,const char*,int,const u8*) {return 0;}
inline int audrvUpdate(AudioDriver* d) {return d->registered && probe_fail_update ? 1 : 0;}
inline int audrenStartAudioRenderer() {return 0;}
inline bool audrvVoiceInit(AudioDriver*,int,int,int,int) {return true;}
inline void audrvVoiceSetDestinationMix(AudioDriver*,int,int) {}
inline void audrvVoiceSetMixFactor(AudioDriver*,int,float,int,int) {}
inline void audrvVoiceStart(AudioDriver*,int) {}
inline bool audrvVoiceAddWaveBuf(AudioDriver* d,int,AudioDriverWaveBuf* b) {b->state = d->registered == 0 ? AudioDriverWaveBufState_Playing : (d->registered == 1 ? AudioDriverWaveBufState_Queued : AudioDriverWaveBufState_Waiting);++d->registered;++probe_submitted;return true;}
inline uint32_t audrvVoiceGetPlayedSampleCount(AudioDriver*,int) {return 1024;}
inline void audrenStopAudioRenderer() {}
// Matches libnx audrvClose ownership: free/zero driver, not caller wave buffers.
inline void audrvClose(AudioDriver* d) {*d={};}
inline void audrenExit() {}
inline u64 armGetSystemTick() {return 1;}
inline u64 armTicksToNs(u64 t) {return t;}
inline void armDCacheFlush(void*,std::size_t) {}
inline int audrenGetFrameEvent() {return 0;}
inline void eventWait(int,u64) {std::this_thread::sleep_for(std::chrono::milliseconds(1));}
inline int svcGetCurrentProcessorNumber() {return 0;}
inline int threadCreate(Thread* t,void(*entry)(void*),void* context,void*,std::size_t,int,int) {t->entry=entry;t->context=context;return 0;}
inline int threadStart(Thread* t) {t->native=new std::thread(t->entry,t->context);return 0;}
inline void threadWaitForExit(Thread* t) {t->native->join();}
inline void threadClose(Thread* t) {delete t->native;t->native=nullptr;}