#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/board_temperature.c"
FakeTim fakeTim;FakeExti fakeExti;FakeGpio fakeGpio;FakeRcc fakeRcc;FakeSyscfg fakeSyscfg;
static uint32_t tick,hclk=200000000U,initCalls;
uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return hclk; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 50000000U; }
void HAL_GPIO_Init(FakeGpio *gpio,GPIO_InitTypeDef *init)
{ assert(gpio==GPIOG && init->Pin==GPIO_PIN_6 && init->Mode==GPIO_MODE_OUTPUT_OD);++initCalls; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq==TIM7_IRQn || irq==EXTI9_5_IRQn); }
void HAL_NVIC_SetPriority(int irq,int preempt,int sub)
{ assert((irq==TIM7_IRQn || irq==EXTI9_5_IRQn) && preempt==3 && !sub); }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq==TIM7_IRQn || irq==EXTI9_5_IRQn); }
static void Wave(uint16_t *out,const uint8_t *bytes)
{
  unsigned i;out[0]=25;out[1]=108;out[2]=195;
  for(i=0;i<40;++i) {
    out[3+2*i]=out[2+2*i]+54;
    out[4+2*i]=out[3+2*i]+((bytes[i/8]&(128U>>(i%8))) ? 71 : 26);
  }
}
static void Decoder(void)
{
  uint16_t w[DHT11_EDGE_COUNT];int32_t t=999;uint32_t h=999;
  uint8_t good[]={53,0,24,4,81},negative[]={53,0,10,129,192},integer[]={60,0,25,0,85};
  uint8_t bad[]={53,0,24,4,80},decimal[]={53,0,24,10,87},range[]={53,0,61,0,114};
  Wave(w,good);assert(Dht11_Decode(w,&t,&h)==DHT11_OK && t==244 && h==530);
  Wave(w,negative);assert(Dht11_Decode(w,&t,&h)==DHT11_OK && t==-101);
  Wave(w,integer);assert(Dht11_Decode(w,&t,&h)==DHT11_OK && t==250);
  Wave(w,bad);assert(Dht11_Decode(w,&t,&h)==DHT11_CHECKSUM_ERROR && t==250 && h==600);
  Wave(w,decimal);assert(Dht11_Decode(w,&t,&h)==DHT11_DATA_ERROR);
  Wave(w,range);assert(Dht11_Decode(w,&t,&h)==DHT11_DATA_ERROR);
  Wave(w,good);w[1]=w[0]+20;assert(Dht11_Decode(w,&t,&h)==DHT11_TIMING_ERROR);
  Wave(w,good);w[4]=w[3]+50;assert(Dht11_Decode(w,&t,&h)==DHT11_TIMING_ERROR);
  Wave(w,good);w[4]=w[3]-1;assert(Dht11_Decode(w,&t,&h)==DHT11_TIMING_ERROR);
}
static void Edge(uint32_t at,unsigned level)
{ TIM7->CNT=at;GPIOG->IDR=level ? GPIO_PIN_6 : 0;EXTI->PR=GPIO_PIN_6;BoardTemperature_EdgeIRQ(); }
static void Start(void)
{
  tick+=2500;BoardTemperature_Process(1);assert(phase==START_LOW && TIM7->ARR==19999);
  assert(GPIOG->BSRR==(GPIO_PIN_6<<16));
  /* No main-loop call is needed to release at 20 ms. */
  tick+=20;TIM7->SR=TIM_SR_UIF;BoardTemperature_TimerIRQ();
  assert(phase==RECEIVING && TIM7->ARR==5999 && GPIOG->BSRR==GPIO_PIN_6);
  Edge(0,1);assert(edgeCount==0);
}
static void Frame(int corrupt)
{
  uint16_t w[DHT11_EDGE_COUNT];unsigned i;uint8_t bytes[]={53,0,24,4,81};
  bytes[4]-=(uint8_t)corrupt;Wave(w,bytes);
  for(i=0;i<DHT11_EDGE_COUNT;++i) Edge(w[i],i&1);
  assert(phase==COMPLETE && !(EXTI->IMR&GPIO_PIN_6) && !TIM7->CR1);
  tick+=6;BoardTemperature_Process(1);
}
int main(void)
{
  int32_t t=777;uint32_t count;
  Decoder();BoardTemperature_Init();
  if(APP_BOARD_ROLE==APP_BOARD_B) {
    assert(!initialized && !initCalls);tick=10000;BoardTemperature_Process(1);
    BoardTemperature_TimerIRQ();BoardTemperature_EdgeIRQ();assert(!BoardTemperature_Read(&t));
    puts("PASS: DHT11 decoder and B hardware isolation");return 0;
  }
  assert(initialized && TIM7->PSC==99 && ((SYSCFG->EXTICR[1]>>8)&15)==6);
  tick=1499;BoardTemperature_Process(1);assert(!status.attempts);
  tick=1500;BoardTemperature_Process(0);assert(!status.attempts);
  Start();Frame(0);assert(!BoardTemperature_Read(&t) && t==777 && primed);
  Start();Frame(0);assert(BoardTemperature_Read(&t) && t==244 && status.goodFrames==2);
  Start();Frame(1);assert(!BoardTemperature_Read(&t) && status.checksumErrors==1);
  Start();Frame(0);assert(!BoardTemperature_Read(&t));
  Start();Frame(0);assert(BoardTemperature_Read(&t));
  tick+=7500;assert(!BoardTemperature_Read(&t));
  Start();Frame(0);assert(!BoardTemperature_Read(&t));
  Start();Frame(0);assert(BoardTemperature_Read(&t));
  Start();Edge(25,0);Edge(108,1); /* Truncated response. */
  TIM7->SR=TIM_SR_UIF;BoardTemperature_TimerIRQ();assert(phase==TIMED_OUT);
  BoardTemperature_Process(1);assert(!BoardTemperature_Read(&t) && status.timeouts==1);
  Start();Edge(25,0);Edge(30,0);BoardTemperature_Process(1);assert(status.timingErrors==1);
  count=status.attempts;tick+=10000;BoardTemperature_Process(0);assert(status.attempts==count);
  tick=UINT32_MAX-2000;BoardTemperature_Init();Start();Frame(0);Start();Frame(0);
  assert(BoardTemperature_Read(&t)); /* Poll scheduling and freshness survive tick wrap. */
  hclk=100000000;BoardTemperature_Init();assert(status.state==BOARD_TEMP_CLOCK && !initialized);
  puts("PASS: DHT11 asynchronous timer/edge capture, priming, failures, stale cache and tick wrap");return 0;
}
