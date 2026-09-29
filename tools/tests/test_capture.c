/* Exercise actual recorder state machine and SDRAM ring on Windows. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdlib.h>
#include "../../Core/Src/app_capture.c"
AppRangeStatus appRangeStatus;
AppNetStatus appNetStatus;
RangeDspDiagnostics rangeDspDiagnostics;
SD_HandleTypeDef hsd1;
uint8_t retSD;
char SDPath[4];
FATFS SDFatFS;
const Diskio_drvTypeDef CaptureSD_Driver=0;
const char *CaptureSD_ErrorText(void) { return "SD W H1 E00000010"; }
static AppPage testPage=APP_PAGE_STANDARD;
AppPage AppRange_Page(void) { return testPage; }
static uint32_t tick=10000,button,resets,shortWrite,closeFailure,completeClosed,dropMask;
static uint32_t tickAdvance;
static struct udp_pcb fakeSocket;
static uint8_t pending[PREFIX+CHUNK];
static uint32_t pendingSize;
static uint8_t *files[3];
static uint32_t sizes[3];
static uint8_t *peerBlob;
static uint32_t peerBlobSize;
uint32_t HAL_GetTick(void) { uint32_t result=tick;tick+=tickAdvance;return result; }
uint64_t RangeClock_Now(void) { return (uint64_t)tick*1000000ULL; }
int32_t AppRange_Temperature(void) { return 255; }
int AppRange_MasterTime(uint64_t *ns) { *ns=RangeClock_Now();return 1; }
void AppRange_ResetRound(void) { ++resets;memset(&appRangeStatus,0,sizeof(appRangeStatus)); }
uint64_t AppNet_LocalSession(void) { return 11; }
uint64_t AppNet_PeerSession(void) { return 22; }
void MX_SDMMC1_SD_Init(void) {}
void BSP_PB_Init(int b,int mode) { (void)b;(void)mode; }
uint32_t BSP_PB_GetState(int b) { (void)b;return button; }
uint8_t FATFS_LinkDriver(const Diskio_drvTypeDef *d,char *path) { (void)d;strcpy(path,"0:");return 0; }
FRESULT f_mount(FATFS *f,const char *path,int immediate) { (void)f;(void)path;(void)immediate;return 0; }
FRESULT f_mkdir(const char *path) { (void)path;return 0; }
FRESULT f_open(FIL *f,const char *path,int flags)
{ (void)flags;f->index=strstr(path,"A.RNG")?0:(strstr(path,"B.RNG")?1:2);sizes[f->index]=0;return 0; }
FRESULT f_write(FIL *f,const void *p,UINT n,UINT *written)
{
  if(shortWrite) { shortWrite=0;*written=0;return 0; }
  assert(sizes[f->index]+n<4000000U);memcpy(files[f->index]+sizes[f->index],p,n);
  sizes[f->index]+=n;*written=n;return 0;
}
FRESULT f_close(FIL *f)
{ if(closeFailure) { closeFailure=0;return 1; }if(f->index==2) completeClosed=1;return 0; }
struct udp_pcb *udp_new(void) { return &fakeSocket; }
err_t udp_bind(struct udp_pcb *p,const ip_addr_t *a,u16_t port) { (void)p;(void)a;assert(port==PORT);return 0; }
void udp_remove(struct udp_pcb *p) { (void)p; }
void udp_recv(struct udp_pcb *p,void (*cb)(void *,struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t),void *arg)
{ (void)p;(void)cb;(void)arg; }
struct pbuf *pbuf_alloc(int layer,u16_t n,int type)
{ struct pbuf *p=malloc(sizeof(*p));(void)layer;(void)type;assert(n<=sizeof(p->bytes));p->tot_len=n;return p; }
void pbuf_free(struct pbuf *p) { free(p); }
err_t pbuf_take(struct pbuf *p,const void *data,u16_t n) { memcpy(p->bytes,data,n);return 0; }
u16_t pbuf_copy_partial(struct pbuf *p,void *data,u16_t n,u16_t off) { memcpy(data,p->bytes+off,n);return n; }
err_t udp_sendto(struct udp_pcb *p,struct pbuf *b,const ip_addr_t *a,u16_t port)
{ (void)p;(void)a;assert(port==PORT);memcpy(pending,b->bytes,b->tot_len);pendingSize=b->tot_len;return 0; }
static void Inject(uint32_t type,uint32_t id,uint32_t value,const uint8_t *data,uint32_t n,int corrupt)
{
  struct pbuf *p=pbuf_alloc(0,(u16_t)(PREFIX+n),0);uint8_t *b=p->bytes;
  memcpy(b,"CAP1",4);P32(b+4,type);P64(b+8,22);P64(b+16,11);P32(b+24,id);P32(b+28,value);P32(b+32,n);
  if(n) memcpy(b+PREFIX,data,n);
  P32(b+36,Hash(Hash(2166136261U,b,36),b+PREFIX,n)^(corrupt?1:0));
  Receive(NULL,&fakeSocket,p,&peer,PORT);
}
static void SimulatePeer(void)
{
  uint32_t type,id,pos,n;uint8_t data[CHUNK];
  if(!pendingSize) return;
  type=G32(pending+4);id=G32(pending+24);pos=G32(pending+28);pendingSize=0;
  if((dropMask&(1U<<type))!=0) { dropMask&=~(1U<<type);return; }
  if(type==FREEZE) Inject(INFO,id,peerBlobSize,NULL,0,0);
  else if(type==READ) { assert(pos<peerBlobSize);n=peerBlobSize-pos;if(n>CHUNK)n=CHUNK;memcpy(data,peerBlob+pos,n);Inject(DATA,id,pos,data,n,0); }
  else if(type==RESET) { assert(completeClosed);Inject(RESET_ACK,id,0,NULL,0,0); }
  else if(type==ARM) Inject(ARM_ACK,id,0,NULL,0,0);
}
static void Pump(unsigned limit)
{ while(limit-- && state!=IDLE && state!=ERROR_STATE) { tick+=2;AppCapture_Process();SimulatePeer(); } }
int main(int argc,char **argv)
{
  unsigned i,j;int16_t audio[APP_AUDIO_HALF_FRAMES*2];uint8_t probe[32];uint32_t before;
  assert(VirtualAlloc((void *)0xC0200000U,0x360000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==(void *)0xC0200000U);
  for(i=0;i<3;++i) { files[i]=malloc(4000000);assert(files[i]); }
  appNetStatus.online=1;appRangeStatus.locked=1;AppCapture_Init();
  /* Trigger after a ring wrap, retain 12 s and verify oldest block/anchors. */
  for(i=0;i<1900;++i) {
    if(i==1100) AppCapture_Trigger();
    for(j=0;j<APP_AUDIO_HALF_FRAMES*2;++j) audio[j]=(int16_t)i;
    AppCapture_Audio(audio,APP_AUDIO_HALF_FRAMES,(uint64_t)(i+1)*APP_AUDIO_HALF_FRAMES,(uint64_t)i*16000000,0);
  }
  assert(frozen && blocks==1850 && validBlocks==1000);
  Seal();assert(savedBlocks==1000 && firstBlock==850);
  assert(ReadLocal(HEADER_SIZE,probe,4)==4 && (int16_t)(probe[0]|(probe[1]<<8))==850);
  ReadLocal(HEADER_SIZE+savedBlocks*APP_AUDIO_HALF_FRAMES*4U,probe,24);
  assert(G64(probe)==851ULL*APP_AUDIO_HALF_FRAMES);
  assert(strstr(HEADER,"\"frames\":768000")!=NULL);
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    peerBlob=malloc(totalBytes);assert(peerBlob);peerBlobSize=totalBytes;
    assert(ReadLocal(0,peerBlob,totalBytes)==totalBytes);
    { char *board=strstr((char *)peerBlob,"\"board\":\"A\"");assert(board);board[9]='B'; }
    appCaptureSaveRequest=APP_CAPTURE_SAVE_REQUEST;AppCapture_Process();
    assert(!appCaptureSaveRequest && state==WAIT_INFO);
    shortWrite=1;Pump(30);assert(state==ERROR_STATE && hold && frozen && !resets);
    before=totalBytes;StartSave();closeFailure=1;Pump(10000);assert(state==ERROR_STATE && hold && totalBytes==before && !resets);
    dropMask=(1U<<FREEZE)|(1U<<READ)|(1U<<RESET)|(1U<<ARM);
    StartSave();Pump(15000);assert(state==IDLE && !hold && !frozen && resets==1 && completeClosed && !dropMask);
    assert(sizes[0]==before && sizes[1]==before && memcmp(files[1],peerBlob,before)==0);
    assert(strstr((char *)files[2],"RNG1 complete")!=NULL);
    /* A stale trigger from the preceding round must not arm a new recording. */
    Inject(TRIGGER,0,0,NULL,0,0);assert(!triggered);
    if(argc>1) {
      char path[1024];FILE *f;
      for(i=0;i<3;++i) {
        snprintf(path,sizeof(path),"%s/%s",argv[1],i==0?"A.RNG":(i==1?"B.RNG":"COMPLETE.TXT"));
        f=fopen(path,"wb");assert(f);assert(fwrite(files[i],1,sizes[i],f)==sizes[i]);fclose(f);
      }
    }
    testPage=APP_PAGE_WAVE;
    button=1;AppCapture_Process();tick+=41;AppCapture_Process();
    appCaptureSaveRequest=APP_CAPTURE_SAVE_REQUEST;AppCapture_Process();
    assert(state==IDLE && !hold && !appCaptureSaveRequest);
    testPage=APP_PAGE_POSITION;
    button=0;AppCapture_Process();tick+=41;AppCapture_Process();
    button=1;AppCapture_Process();tick+=41;AppCapture_Process();
    appCaptureSaveRequest=APP_CAPTURE_SAVE_REQUEST;AppCapture_Process();
    assert(state==IDLE && !hold && !appCaptureSaveRequest);
    testPage=APP_PAGE_STANDARD;AppCapture_Process();assert(state==IDLE);
    button=0;AppCapture_Process();tick+=41;AppCapture_Process();
    button=1;AppCapture_Process();tick+=20;button=0;AppCapture_Process();tick+=50;AppCapture_Process();
    assert(state==IDLE && !hold); /* contact bounce does not trigger save */
    /* Real hardware ticks during Seal/StartSave. A cached pre-button tick must
     * not underflow against the newly stamped deadline and raise error 902. */
    button=1;AppCapture_Process();tick+=41;tickAdvance=1;
    pendingSize=0;AppCapture_Process();tickAdvance=0;assert(state==WAIT_INFO && hold);
    assert(pendingSize && G32(pending+4)==FREEZE); /* request actually leaves A */
    before=transaction;tick+=1000;AppCapture_Process();assert(transaction==before); /* held button: one save */
    tick=deadline+9999U;AppCapture_Process();assert(state==WAIT_INFO);
    tick=deadline+10001U;AppCapture_Process();
    assert(state==ERROR_STATE && hold && strstr(text,"902")); /* genuine timeout still detected */
    /* Retry via the actual debounced button, including millisecond wrap. */
    button=0;AppCapture_Process();tick+=41;AppCapture_Process();
    button=1;AppCapture_Process();tick=UINT32_MAX-2U;tickAdvance=1;
    AppCapture_Process();tickAdvance=0;
    assert(state==WAIT_INFO && transaction==before+1U && hold && tick<10U);
    tick=deadline+9999U;AppCapture_Process();assert(state==WAIT_INFO);
    tick=deadline+10001U;AppCapture_Process();assert(state==ERROR_STATE && strstr(text,"902"));
  } else {
    hold=0;Inject(FREEZE,5,0,NULL,0,1);assert(!hold); /* corrupt frame */
    Inject(FREEZE,5,0,NULL,0,0);assert(hold && G32(pending+4)==INFO);
    Inject(READ,4,0,NULL,0,0);assert(G32(pending+4)==INFO); /* wrong transaction */
    Inject(READ,5,0,NULL,0,0);assert(G32(pending+4)==DATA && !memcmp(pending+PREFIX,HEADER,CHUNK));
    Inject(RESET,5,0,NULL,0,0);assert(resets==1 && hold);
    Inject(RESET,5,0,NULL,0,0);assert(resets==1 && hold);
    Inject(ARM,5,0,NULL,0,0);assert(!hold && !frozen);
    Inject(RESET,5,0,NULL,0,0);assert(!hold && resets==1); /* late reset is ACK only */
    Inject(FREEZE,5,0,NULL,0,0);assert(!hold); /* late freeze */
    Inject(FREEZE,4,0,NULL,0,0);assert(!hold); /* freeze from an older retry */
    Inject(TRIGGER,0,0,NULL,0,0);assert(!triggered);
  }
  puts("Capture ring, transaction, failure retention, replay tests passed.");return 0;
}
