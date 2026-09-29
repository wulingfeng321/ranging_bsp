/* Round recorder. Dedicated non-cacheable SDRAM, independent of DSP/LCD.
 * On-card RNG1 = 4096-byte NUL-padded JSON + stereo PCM + anchors + CSV log.
 * All numeric binary fields little endian. No native C structs go on the wire.
 * 16 s ring; first signature (either board) retains 12 s after detection.
 * Automatic freeze preserves audio only. Pressing A USER seals the diagnostics.
 * Files are never overwritten. COMPLETE.TXT is closed before reset is sent.
 */
#include "app_capture.h"
#include "capture_sd.h"
#include "app_range.h"
#include "app_net.h"
#include "app_board_config.h"
#include "range_dsp.h"
#include "main.h"
#include "sdmmc.h"
#include "fatfs.h"
#include "stm32746g_discovery.h"
#include "lwip/udp.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define BLOCKS 1000U
#define FRAMES (BLOCKS * APP_AUDIO_HALF_FRAMES)
#define PCM ((int16_t *)0xC0200000U)
#define ANCHORS ((uint8_t *)0xC0520000U)
#define LOG ((char *)0xC0530000U)
#define HEADER ((char *)0xC0550000U)
#define LOG_SIZE 131072U
#define HEADER_SIZE 4096U
#define CHUNK 1024U
#define PORT 5002U
#define PREFIX 40U
#define TRIGGER 1U
#define FREEZE 2U
#define INFO 3U
#define READ 4U
#define DATA 5U
#define RESET 6U
#define RESET_ACK 7U
#define ARM 8U
#define ARM_ACK 9U
enum { IDLE, WAIT_INFO, OPEN_A, WRITE_A, OPEN_B, READ_B, FINISH,
       WAIT_RESET, WAIT_ARM, ERROR_STATE };
static struct udp_pcb *socket;
static ip_addr_t peer;
static volatile uint32_t blocks, validBlocks, frozen, endBlock;
static uint32_t triggered, logBytes, logOverflow, totalBytes, firstBlock, savedBlocks;
static uint32_t state, transaction, peerTransaction, lastReset, lastArm;
static uint32_t lastSend, deadline, peerSize, offset, fileOpen, runNumber;
static uint32_t rxLength, rxOffset, remoteReady, resetAck, armAck, hold;
static uint32_t buttonRaw, buttonStable, buttonTick, lastStats, triggerTick;
static uint64_t roundPeer, sealedPeer;
static uint8_t rxData[CHUNK];
static uint32_t scratch[(PREFIX+CHUNK)/4]; /* aligned for SD polling HAL */
static FIL file;
static char directory[24], text[32]="CAP ARM";
static char detail[32];
static uint32_t aHash, bHash;
volatile uint32_t appCaptureSaveRequest;
extern const Diskio_drvTypeDef CaptureSD_Driver;

static void P32(uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static uint32_t G32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void P64(uint8_t *p,uint64_t v) { P32(p,(uint32_t)v);P32(p+4,(uint32_t)(v>>32)); }
static uint64_t G64(const uint8_t *p) { return G32(p)|((uint64_t)G32(p+4)<<32); }
static uint32_t Hash(uint32_t h,const uint8_t *p,uint32_t n)
{ while(n--) { h^=*p++;h*=16777619U; } return h; }

int AppCapture_Busy(void) { return hold!=0; }
const char *AppCapture_Text(void) { return text; }
const char *AppCapture_Detail(void) { return detail; }

void AppCapture_Log(const char *format,...)
{
  char line[512];
  int n; va_list args;
  if(hold) return;
  va_start(args,format);n=vsnprintf(line,sizeof(line),format,args);va_end(args);
  if(n<0 || n>=(int)sizeof(line) || logBytes+(uint32_t)n>=LOG_SIZE) { ++logOverflow;return; }
  memcpy(LOG+logBytes,line,(uint32_t)n);logBytes+=(uint32_t)n;
}

void AppCapture_Audio(const volatile int16_t *pcm,uint32_t frames,
                      uint64_t count,uint64_t localNs,uint32_t epoch)
{
  uint32_t i,index,stop;
  uint8_t *anchor;
  if(frozen || frames!=APP_AUDIO_HALF_FRAMES) return;
  index=blocks%BLOCKS;
  for(i=0;i<frames*2U;++i) PCM[index*APP_AUDIO_HALF_FRAMES*2U+i]=pcm[i];
  anchor=ANCHORS+index*24U;
  P64(anchor,count);P64(anchor+8,localNs);P32(anchor+16,epoch);P32(anchor+20,frames);
  ++blocks;if(validBlocks<BLOCKS) ++validBlocks;
  stop=endBlock;
  if(stop && blocks>=stop) frozen=1;
}

static void Trigger(void)
{
  uint32_t mask;
  if(triggered || hold) return;
  mask=__get_PRIMASK();__disable_irq();
  endBlock=blocks+750U;triggered=1;
  __set_PRIMASK(mask);
  triggerTick=HAL_GetTick();
  AppCapture_Log("trigger,%lu\n",(unsigned long)triggerTick);
}
void AppCapture_Trigger(void) { if(appRangeStatus.locked) Trigger(); }

static void Stats(void)
{
  const AppRangeStatus *s=&appRangeStatus;
  const RangeDspDiagnostics *d=&rangeDspDiagnostics;
  AppCapture_Log("status,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%ld,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
    (unsigned long)HAL_GetTick(),(unsigned long)s->events,(unsigned long)s->eventRx,
    (unsigned long)s->results,(unsigned long)s->peakAmbiguous,(unsigned long)s->peakInconsistent,
    (unsigned long)s->distanceMm,(long)s->resultDeltaUs,s->valid,
    (unsigned long)s->batchStage,(unsigned long)s->batchCount,(unsigned long)s->batchUsed,
    (unsigned long)s->batchSpanMm,(unsigned long)s->syncErrorNs,(unsigned long)s->samplePeriodPs,
    (unsigned long)s->audioDrops,(unsigned long)s->audioGapDrops,(unsigned long)s->audioOverruns,
    (unsigned long)s->unlockedEvents,(unsigned long)s->distanceRejects,(unsigned long)s->batchCadenceRejected);
  AppCapture_Log("dsp,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.6f,%.6f,%.6f,%.6f,%.6f\n",
    (unsigned long)HAL_GetTick(),(unsigned long)d->coarsePassed,(unsigned long)d->failPulse[0],
    (unsigned long)d->failPulse[1],(unsigned long)d->failPulse[2],(unsigned long)d->gapRejected,
    (unsigned long)d->signatures,(unsigned long)d->noCandidates,(unsigned long)d->candidateOverflow,
    (double)d->coarseMax,(double)d->pulseMax[0],(double)d->pulseMax[1],(double)d->pulseMax[2],(double)d->gapMinRatio);
}

static void Seal(void)
{
  uint32_t mask; uint64_t master=0,local; int locked;
  if(hold) return;
  Stats();
  local=RangeClock_Now();locked=AppRange_MasterTime(&master);
  mask=__get_PRIMASK();__disable_irq();frozen=1;
  savedBlocks=validBlocks;firstBlock=(blocks-savedBlocks)%BLOCKS;
  __set_PRIMASK(mask);
  hold=1;
  sealedPeer=AppNet_PeerSession();
  memset(HEADER,0,HEADER_SIZE);
  (void)snprintf(HEADER,HEADER_SIZE,
    "{\"format\":\"RNG1\",\"firmware_base\":\"547e914+early1\",\"board\":\"%s\","
    "\"rate\":%lu,\"channels\":2,\"sample_bytes\":2,\"frames\":%lu,\"header_bytes\":4096,"
    "\"anchor_count\":%lu,\"anchor_bytes\":24,\"log_bytes\":%lu,\"log_overflow\":%lu,"
    "\"triggered\":%lu,\"temperature_deci_c\":%d,\"profile\":%d,\"joint\":%d,"
    "\"local_ns\":%llu,\"master_ns\":%llu,\"clock_locked\":%d,\"sync_error_ns\":%lu,"
    "\"sample_period_ps\":%lu,\"session\":%llu,\"peer_session\":%llu}",
    APP_BOARD_NAME,(unsigned long)APP_AUDIO_SAMPLE_RATE,(unsigned long)(savedBlocks*APP_AUDIO_HALF_FRAMES),
    (unsigned long)savedBlocks,(unsigned long)logBytes,(unsigned long)logOverflow,(unsigned long)triggered,
    APP_TEMPERATURE_DECI_C,APP_RANGE_AUDIO_PROFILE,APP_RANGE_JOINT_PEAKS,
    (unsigned long long)local,(unsigned long long)master,locked,(unsigned long)appRangeStatus.syncErrorNs,
    (unsigned long)appRangeStatus.samplePeriodPs,(unsigned long long)AppNet_LocalSession(),
    (unsigned long long)AppNet_PeerSession());
  totalBytes=HEADER_SIZE+savedBlocks*(APP_AUDIO_HALF_FRAMES*4U+24U)+logBytes;
}

/* Read the frozen ring as a chronological file, including wrap boundaries. */
static uint32_t ReadLocal(uint32_t pos,uint8_t *out,uint32_t size)
{
  uint32_t n,left,physical,pcmSize=savedBlocks*APP_AUDIO_HALF_FRAMES*4U;
  uint32_t result=0;
  if(pos>=totalBytes) return 0;
  if(size>totalBytes-pos) size=totalBytes-pos;
  while(size) {
    const uint8_t *source;
    if(pos<HEADER_SIZE) { source=(const uint8_t *)HEADER+pos;left=HEADER_SIZE-pos; }
    else if(pos<HEADER_SIZE+pcmSize) {
      physical=(firstBlock*APP_AUDIO_HALF_FRAMES*4U+pos-HEADER_SIZE)%(FRAMES*4U);
      source=(const uint8_t *)PCM+physical;
      left=FRAMES*4U-physical;if(left>HEADER_SIZE+pcmSize-pos) left=HEADER_SIZE+pcmSize-pos;
    } else if(pos<HEADER_SIZE+pcmSize+savedBlocks*24U) {
      physical=(firstBlock*24U+pos-HEADER_SIZE-pcmSize)%(BLOCKS*24U);
      source=ANCHORS+physical;left=BLOCKS*24U-physical;
      if(left>HEADER_SIZE+pcmSize+savedBlocks*24U-pos) left=HEADER_SIZE+pcmSize+savedBlocks*24U-pos;
    } else { source=(const uint8_t *)LOG+pos-HEADER_SIZE-pcmSize-savedBlocks*24U;left=totalBytes-pos; }
    n=left<size?left:size;memcpy(out+result,source,n);result+=n;pos+=n;size-=n;
  }
  return result;
}

static void Send(uint32_t type,uint32_t id,uint32_t value,uint32_t length)
{
  uint8_t *b=(uint8_t *)scratch;struct pbuf *p;
  if(!socket || !appNetStatus.online) return;
  memcpy(b,"CAP1",4);P32(b+4,type);P64(b+8,AppNet_LocalSession());P64(b+16,AppNet_PeerSession());
  P32(b+24,id);P32(b+28,value);P32(b+32,length);
  P32(b+36,Hash(Hash(2166136261U,b,36),b+PREFIX,length));
  p=pbuf_alloc(PBUF_TRANSPORT,(u16_t)(PREFIX+length),PBUF_RAM);
  if(p) { if(pbuf_take(p,b,(u16_t)(PREFIX+length))==ERR_OK) (void)udp_sendto(socket,p,&peer,PORT);pbuf_free(p); }
}

static void ResetRound(void)
{
  uint32_t mask=__get_PRIMASK();__disable_irq();
  frozen=1;blocks=0;validBlocks=0;endBlock=0;
  __set_PRIMASK(mask);
  triggered=0;logBytes=0;logOverflow=0;totalBytes=0;
  AppRange_ResetRound();
  /* Keep held until ARM handshake. Prevent duplicated RESET clearing new data. */
}

static void Receive(void *arg,struct udp_pcb *pcb,struct pbuf *p,const ip_addr_t *addr,u16_t port)
{
  uint8_t b[PREFIX+CHUNK];uint32_t type,id,value,n;
  (void)arg;(void)pcb;
  if(!p) return;
  if(port!=PORT || !ip_addr_cmp(addr,&peer) || p->tot_len<PREFIX || p->tot_len>sizeof(b)) { pbuf_free(p);return; }
  n=p->tot_len;(void)pbuf_copy_partial(p,b,(u16_t)n,0);pbuf_free(p);
  if(memcmp(b,"CAP1",4) || G64(b+8)!=AppNet_PeerSession() || G64(b+16)!=AppNet_LocalSession() ||
     G32(b+32)!=n-PREFIX || G32(b+36)!=Hash(Hash(2166136261U,b,36),b+PREFIX,n-PREFIX)) return;
  type=G32(b+4);id=G32(b+24);value=G32(b+28);n-=PREFIX;
  if(type==TRIGGER && id==lastArm && !n && !hold) { Trigger();return; }
  if(APP_BOARD_ROLE==APP_BOARD_B) {
    if(type==FREEZE && !n && id && id!=lastReset &&
       (!peerTransaction || id==peerTransaction || (int32_t)(id-peerTransaction)>0)) {
      Seal();peerTransaction=id;Send(INFO,id,totalBytes,0);strcpy(text,"CAP SEND TO A");
    } else if(type==READ && !n && hold && id==peerTransaction && id!=lastReset && value<totalBytes) {
      n=ReadLocal(value,(uint8_t *)scratch+PREFIX,CHUNK);Send(DATA,id,value,n);
    } else if(type==RESET && !n && id==peerTransaction && hold) {
      if(id!=lastReset) { ResetRound();lastReset=id; }
      Send(RESET_ACK,id,0,0);strcpy(text,"CAP SAVED / WAIT");
    } else if(type==RESET && !n && id==lastReset) Send(RESET_ACK,id,0,0);
    else if(type==ARM && !n && id==lastReset) {
      if(id!=lastArm) { lastArm=id;hold=0;frozen=0;strcpy(text,"CAP ARM"); }
      Send(ARM_ACK,id,0,0);
    }
  } else if(id==transaction && hold && roundPeer==AppNet_PeerSession()) {
    if(type==INFO && !n && state==WAIT_INFO && value>=HEADER_SIZE && value<4000000U) { peerSize=value;remoteReady=1; }
    if(type==DATA && state==READ_B && value==offset && n && n<=peerSize-offset && !rxLength) {
      memcpy(rxData,b+PREFIX,n);rxOffset=value;rxLength=n;
    }
    if(type==RESET_ACK && state==WAIT_RESET && !n) resetAck=1;
    if(type==ARM_ACK && state==WAIT_ARM && !n) armAck=1;
  }
}

static void Fail(uint32_t error)
{
  if(error>=100U && error<600U)
    (void)snprintf(detail,sizeof(detail),"%s",CaptureSD_ErrorText());
  if(fileOpen) { (void)f_close(&file);fileOpen=0; }
  state=ERROR_STATE;
  (void)snprintf(text,sizeof(text),"CAP ERR %lu / RETRY",(unsigned long)error);
}
static int Write(const uint8_t *data,uint32_t n)
{
  UINT written=0;FRESULT r=f_write(&file,data,n,&written);
  if(r!=FR_OK || written!=n) { Fail(r?100U+r:199U);return 0; }return 1;
}
static int Close(void)
{ FRESULT r=f_close(&file);fileOpen=0;if(r) { Fail(200U+r);return 0; }return 1; }
static int Open(const char *name)
{
  char path[40];FRESULT r;
  (void)snprintf(path,sizeof(path),"%s/%s",directory,name);
  r=f_open(&file,path,FA_WRITE|FA_CREATE_NEW);if(r) { Fail(300U+r);return 0; }fileOpen=1;return 1;
}

static void StartSave(void)
{
  if(!socket || !appNetStatus.online) { strcpy(text,"CAP NEED PEER");return; }
  if(hold && sealedPeer!=AppNet_PeerSession()) { strcpy(text,"CAP PEER RESET");return; }
  Seal();roundPeer=AppNet_PeerSession();
  detail[0]=0;
  ++transaction;if(!transaction) ++transaction;
  state=WAIT_INFO;remoteReady=0;rxLength=0;resetAck=0;armAck=0;
  deadline=HAL_GetTick();lastSend=deadline-250U;strcpy(text,"CAP FREEZING B");
}

void AppCapture_Init(void)
{
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    BSP_PB_Init(BUTTON_KEY,BUTTON_MODE_GPIO);
    buttonRaw=buttonStable=BSP_PB_GetState(BUTTON_KEY);buttonTick=HAL_GetTick();
    MX_SDMMC1_SD_Init();
    hsd1.Init.BusWide=SDMMC_BUS_WIDE_1B;hsd1.Init.ClockDiv=2; /* 12 MHz, then BSP switches to 4-bit */
    /* Select our bounded driver; do not modify CubeMX's library component. */
    retSD=FATFS_LinkDriver(&CaptureSD_Driver,SDPath);
  }
  IP_ADDR4(&peer,192,168,10,APP_PEER_HOST);socket=udp_new();
  if(socket && udp_bind(socket,IP_ADDR_ANY,PORT)==ERR_OK) udp_recv(socket,Receive,NULL);
  else { if(socket) udp_remove(socket);socket=NULL;strcpy(text,"CAP UDP ERROR"); }
}

void AppCapture_Process(void)
{
  uint32_t now=HAL_GetTick(),n;FRESULT r;
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    uint32_t raw=BSP_PB_GetState(BUTTON_KEY);
    if(raw!=buttonRaw) { buttonRaw=raw;buttonTick=now; }
    if(now-buttonTick>=40U && raw!=buttonStable) {
      buttonStable=raw;
      if(raw && (state==IDLE || state==ERROR_STATE)) StartSave();
    }
    if(appCaptureSaveRequest==APP_CAPTURE_SAVE_REQUEST && (state==IDLE || state==ERROR_STATE)) {
      appCaptureSaveRequest=0;StartSave();
    }
  }
  /* StartSave seals/logs the round and stamps deadline with a fresh tick.
   * Never compare that deadline against the pre-button tick: even a 1 ms
   * difference underflows the unsigned subtraction and falsely raises 902
   * before the first FREEZE request is sent. */
  now=HAL_GetTick();
  if(!hold) {
    if(triggered && !frozen && now-lastStats>=500U) { lastStats=now;Stats(); }
    if(triggered && now-triggerTick<13000U && now-lastSend>=250U) { lastSend=now;Send(TRIGGER,lastArm,0,0); }
    if(frozen) strcpy(text,"CAP HELD / A USER");
    else if(triggered) strcpy(text,"CAP RECORDING");
    return;
  }
  if(APP_BOARD_ROLE!=APP_BOARD_A || state==ERROR_STATE) return;
  if(roundPeer!=AppNet_PeerSession()) { Fail(901);return; }
  if(state==WAIT_INFO) {
    if(remoteReady) { state=OPEN_A;return; }
    if(now-deadline>10000U) { Fail(902);return; }
    if(now-lastSend>=250U) { lastSend=now;Send(FREEZE,transaction,0,0); }
  } else if(state==OPEN_A) {
    (void)f_mount(NULL,SDPath,0);
    r=f_mount(&SDFatFS,SDPath,1);if(r) { Fail(400U+r);return; }
    /* A mkdir attempt per loop: bounded even on cards with many saved rounds. */
    do {
      if(++runNumber>999999U) { Fail(903);return; }
      (void)snprintf(directory,sizeof(directory),"%sR%06lu",SDPath,(unsigned long)runNumber);
      r=f_mkdir(directory);
      if(r==FR_EXIST) return;
    } while(0);
    if(r) { Fail(500U+r);return; }
    if(!Open("A.RNG")) return;
    offset=0;aHash=2166136261U;state=WRITE_A;
  } else if(state==WRITE_A) {
    n=ReadLocal(offset,(uint8_t *)scratch,CHUNK);
    if(n && !Write((uint8_t *)scratch,n)) return;
    aHash=Hash(aHash,(uint8_t *)scratch,n);offset+=n;
    (void)snprintf(text,sizeof(text),"CAP SAVE A %lu%%",(unsigned long)(offset*100U/totalBytes));
    if(offset==totalBytes && Close()) state=OPEN_B;
  } else if(state==OPEN_B) {
    if(!Open("B.RNG")) return;
    offset=0;bHash=2166136261U;rxLength=0;state=READ_B;deadline=now;lastSend=now-100U;
  } else if(state==READ_B) {
    if(rxLength && rxOffset==offset) {
      n=rxLength;memcpy(scratch,rxData,n);rxLength=0;
      if(!Write((uint8_t *)scratch,n)) return;
      bHash=Hash(bHash,(uint8_t *)scratch,n);offset+=n;deadline=now;lastSend=now-100U;
      (void)snprintf(text,sizeof(text),"CAP SAVE B %lu%%",(unsigned long)(offset*100U/peerSize));
    }
    if(offset==peerSize) { if(Close()) state=FINISH;return; }
    if(now-deadline>10000U) { Fail(904);return; }
    if(now-lastSend>=100U) { lastSend=now;Send(READ,transaction,offset,0); }
  } else if(state==FINISH) {
    if(!Open("COMPLETE.TXT")) return;
    n=(uint32_t)snprintf((char *)scratch,sizeof(scratch),
      "RNG1 complete\r\nA bytes=%lu fnv1a=%08lX\r\nB bytes=%lu fnv1a=%08lX\r\n",
      (unsigned long)totalBytes,(unsigned long)aHash,(unsigned long)peerSize,(unsigned long)bHash);
    if(!Write((uint8_t *)scratch,n) || !Close()) return;
    state=WAIT_RESET;lastSend=now-250U;
  } else if(state==WAIT_RESET) {
    strcpy(text,"CAP SAVED / RESET B");
    if(resetAck) { ResetRound();state=WAIT_ARM;lastSend=now-250U;return; }
    if(now-lastSend>=250U) { lastSend=now;Send(RESET,transaction,0,0); }
  } else if(state==WAIT_ARM) {
    strcpy(text,"CAP SAVED / ARM B");
    if(armAck) {
      lastArm=transaction;state=IDLE;hold=0;frozen=0;
      (void)snprintf(text,sizeof(text),"SAVED R%06lu / ARM",(unsigned long)runNumber);return;
    }
    if(now-lastSend>=250U) { lastSend=now;Send(ARM,transaction,0,0); }
  }
}
