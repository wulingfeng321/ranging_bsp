#define APP_RANGE_JOINT_PEAKS 1
#define main original_test_main
#include "test_app_range.c"
#undef main
static double coordinates[4][2]={{-.065,.01},{-.065,-.01},{.065,.01},{.065,-.01}};
static double AngularError(double a,double b) { double d=fabs(a-b);return d>180 ? 360-d:d; }
static void RunAngle(int deg)
{
  double arrival[4],theta=deg*0.0174532925199433;
  double sx=sin(theta),sy=cos(theta),worst=0;
  double samplePeriod=(1e9/48000)*(APP_BOARD_ROLE==APP_BOARD_A ? 1.000075 : .999935);
  unsigned block,i,k,matched=0;uint32_t last=0,rng=17;
  int16_t pcm[APP_AUDIO_HALF_FRAMES*2];
  audioCount=audioAnchor=audioBlocks=0; ++audioEpoch;
  RangeAudioTime_Reset(&audioTime); PositionReset();
  appRangeStatus.locked=1; syncEpoch=peerEpoch=7;
  syncModel.slope=APP_BOARD_ROLE==APP_BOARD_B ? .00005 : 0;
  syncModel.offset=APP_BOARD_ROLE==APP_BOARD_B ? 700000000 : 0; syncModel.origin=0;
  for(k=0;k<4;++k) arrival[k]=.251237+(sqrt((sx-coordinates[k][0])*(sx-coordinates[k][0])+
    (sy-coordinates[k][1])*(sy-coordinates[k][1]))-1)/346.45;
  for(block=0;block<66;++block) {
    uint64_t begin=audioCount;
    for(i=0;i<APP_AUDIO_HALF_FRAMES;++i) for(k=0;k<2;++k) {
      unsigned index=(APP_BOARD_ROLE==APP_BOARD_A?0:2)+k;
      double t=(begin+i)*samplePeriod/1e9-arrival[index];
      if(t>=.5) t-=.5;
      rng=rng*1664525U+1013904223U;
      pcm[2*i+k]=(int16_t)(18000*Position_Tone(t)+(int)((rng>>16)%241)-120);
    }
    clockNs=(uint64_t)((10000000000.0+(begin+APP_AUDIO_HALF_FRAMES)*samplePeriod+syncModel.offset)/(1-syncModel.slope));
    AppRange_Audio(pcm,APP_AUDIO_HALF_FRAMES);ObservePageAudioTime();
    /* One consumer slice per DMA, plus noise ABOVE the energy gate. */
    PositionProcess();
    if(positionEventId && positionEventId!=last) {
      double expected=arrival[APP_BOARD_ROLE==APP_BOARD_A?0:2]+(matched?.5:0);
      double err=fabs((double)positionLocal[0]-1e10-expected*1e9);
      if(err>worst) worst=err;
      assert(err<12000);
      last=positionEventId;
      if(APP_BOARD_ROLE==APP_BOARD_A) {
        Inject(POSITION_EVENT,100+matched,7,10000000000ULL+(uint64_t)((arrival[2]+(matched?.5:0))*1e9),
          10000000000ULL+(uint64_t)((arrival[3]+(matched?.5:0))*1e9),950);
        PositionNetwork(HAL_GetTick());
        assert(appPositionStatus.valid);
        assert(AngularError(appPositionStatus.angleDeg,deg)<12);
      } else {
        PositionNetwork(HAL_GetTick()); assert(sent[5]==POSITION_EVENT);
      }
      ++matched;
    }
  }
  assert(matched==2);
  assert(appPositionStatus.backlogResets==0);
  printf("%s angle %d: 2 chirps, max arrival error %.1f ns\n",APP_BOARD_NAME,deg,worst);
}
static void RunQuietMinute(void)
{
  unsigned block,i;uint32_t rng=51;int16_t pcm[APP_AUDIO_HALF_FRAMES*2];
  audioCount=audioAnchor=audioBlocks=0;++audioEpoch;
  RangeAudioTime_Reset(&audioTime);PositionReset();
  appRangeStatus.locked=1;uiPage=APP_PAGE_POSITION;uiKnown=1;uiAckRevision=uiRevision;
  for(block=0;block<3750;++block) {
    for(i=0;i<APP_AUDIO_HALF_FRAMES*2;++i) {
      rng=rng*1664525U+1013904223U;
      pcm[i]=(int16_t)((int)((rng>>16)%1001)-500+1000); /* noisy room + DC */
    }
    clockNs+=16000000ULL;
    AppRange_Audio(pcm,APP_AUDIO_HALF_FRAMES);ObservePageAudioTime();PositionProcess();
  }
  assert(!appPositionStatus.events && !appPositionStatus.backlogResets);
  puts("PASS: 60 simulated seconds of above-gate noise, zero events/backlog resets");
}
/* Ring traversal must preserve the previous contiguous-window correlation,
 * including DC offsets, wraparound and clipped inputs. */
static void CheckCoarseRing(void)
{
  PositionDetector d;int16_t ring[POS_RING],v[POS_N];unsigned i,trial;uint32_t rng=97;
  Position_Init(&d);
  for(trial=0;trial<80;++trial) {
    float sum=0,energy=0,a=0,b=0,var,score,expected;unsigned clipped=0;
    uint32_t start=POS_RING-140+trial;
    for(i=0;i<POS_N;++i) {
      rng=rng*1664525U+1013904223U;
      v[i]=(int16_t)(5000+10000*d.templ[i]+(int)((rng>>16)%2001)-1000);
      if(trial==79 && i==POS_N-1) v[i]=32767;
      ring[(start+i)&(POS_RING-1)]=v[i];
      { float x=v[i];sum+=x;energy+=x*x;a+=x*d.templ[i];b+=x*d.quad[i];
        if(x<=-32700 || x>=32700) clipped=1; }
    }
    energy-=sum*sum/POS_N;
    expected=clipped ? 0 : (a*a/d.energy+b*b/d.quadEnergy)/(energy+1.0f);
    score=Position_CoarseRing(&d,ring,start,POS_RING-1,&var);
    assert(fabsf(score-expected)<.00001f);
    assert(fabsf(var-energy/POS_N)<1.0f);
  }
}
int main(void)
{
  int angle;uint32_t last;
  CheckCoarseRing();
  AppRange_Init();appNetStatus.online=1;AppRange_Process();
  uiKnown=1;uiAckRevision=uiRevision;uiPage=APP_PAGE_POSITION;
  for(angle=0;angle<360;angle+=45) RunAngle(angle);
  RunQuietMinute();
  last=appPositionStatus.updatedMs;
  if(APP_BOARD_ROLE==APP_BOARD_B) {
    testPayloadU=1000;
    Inject(POSITION_RESULT,1234,7,45,900,1);assert(appPositionStatus.valid && appPositionStatus.angleDeg==45);
    last=appPositionStatus.updatedMs;clockNs+=100000000ULL;
    Inject(POSITION_RESULT,1234,7,45,900,1);assert(appPositionStatus.updatedMs==last);
    Inject(POSITION_RESULT,1235,8,90,900,1);assert(appPositionStatus.angleDeg==45);
    testRevision=uiRevision+1;Inject(POSITION_RESULT,1235,7,90,900,1);assert(appPositionStatus.angleDeg==45);testRevision=0;
  }
  clockNs+=2000000000ULL;PositionNetwork(HAL_GetTick());assert(!appPositionStatus.valid);
  ApplyUi(APP_PAGE_STANDARD,250);assert(!positionEventId && !appPositionStatus.valid);
  { double t[4]={0,100000,200000,0};int32_t a;uint32_t r;assert(!Position_Solve(t,346.45f,&a,&r)); }

  { PositionDetector detector; int16_t v[POS_N];unsigned i;
    Position_Init(&detector);memset(v,0,sizeof(v));assert(Position_Score(&detector,v)==0);
    for(i=0;i<POS_N;++i) v[i]=(int16_t)(12000*sin(i*6.283185307179586*500/POS_FS));
    assert(Position_Score(&detector,v)<0.30f);
    for(i=0;i<POS_N;++i) v[i]=(int16_t)(15000*detector.templ[i]);
    assert(Position_Score(&detector,v)>.99f);v[100]=32767;assert(Position_Score(&detector,v)==0);
    /* Every sub-sample phase of an 8-sample coarse grid must stay detectable. */
    for(i=0;i<64;++i) {
      unsigned j;double shift=-4.0+i/8.0;
      for(j=0;j<POS_N;++j) v[j]=(int16_t)(9000*(Position_Tone((2*j-2*shift)/48000.0)+
        Position_Tone((2*j+1-2*shift)/48000.0)));
      assert(Position_Coarse(&detector,v)>.08f);
    }

  }
  if(APP_BOARD_ROLE==APP_BOARD_A) {
    double expected=(sqrt(.065*.065+.51*.51)-sqrt(.065*.065+.49*.49))*1e9/346.45;
    unsigned i;
    uiPage=APP_PAGE_POSITION;uiAckRevision=uiRevision;appRangeStatus.locked=1;
    assert(AppRange_PositionCalibrate());uiAckRevision=uiRevision;
    for(i=0;i<8;++i) {
      uint64_t base=clockNs-100000000ULL;
      positionLocal[0]=base;positionLocal[1]=(uint64_t)((double)base+expected+8000);
      positionRemote[0]=base+17000;positionRemote[1]=(uint64_t)((double)base+expected+12000);
      positionHaveLocal=positionHaveRemote=1;positionLocalQ=positionRemoteQ=950;
      PositionNetwork(HAL_GetTick());clockNs+=500000000ULL;
    }
    assert(appPositionStatus.calibration==2 && appPositionStatus.calibrationCount==8);
    assert(fabs(positionBias[0]-8000)<2 && fabs(positionBias[1]-17000)<2 && fabs(positionBias[2]-12000)<2);
    positionHaveLocal=positionHaveRemote=1;PositionNetwork(HAL_GetTick());
    assert(appPositionStatus.valid && AngularError(appPositionStatus.angleDeg,0)<2);
    ApplyUi(APP_PAGE_STANDARD,250);assert(appPositionStatus.calibration==0 && positionBias[0]==0);
  }

  /* Backlog recovery is not a user cancellation, and never rebuilds template. */
  uiPage=APP_PAGE_POSITION;uiKnown=1;uiAckRevision=uiRevision;appRangeStatus.locked=1;
  appPositionStatus.calibration=1;appPositionStatus.calibrationCount=3;
  positionCalSum[0]=12345;audioTime.ready=1;
  audioCount=20000;positionCursor=0;positionEpoch=audioEpoch;
  PositionProcess();assert(appPositionStatus.overruns && appPositionStatus.calibration==1);
  assert(appPositionStatus.calibrationCount==3 && positionCalSum[0]==12345);
  assert(positionCursor==audioCount/2);
  /* Warm-up/real capture gap preserves calibration intent, restarting samples. */
  ++audioEpoch;++audioBlocks;audioAnchor=clockNs;ObservePageAudioTime();
  assert(appPositionStatus.calibration==1 && appPositionStatus.calibrationCount==0);
  assert(!audioTime.ready);
  /* Even with no settings ACK and a large backlog, the waiting UI is admitted. */
  uiAckRevision=0;uiKnown=0;positionCursor=0;audioTime.ready=1;
  assert(AppRange_DisplayReady());

  /* A pending peak at the fine-window boundary must not be erased by coarse gating. */
  uiKnown=1;uiAckRevision=uiRevision;PositionReset();
  audioTime.ready=1;audioTime.count=8;audioTime.next=8;
  audioTime.sample[7]=audioCount;audioTime.anchorNs=(double)clockNs;audioTime.periodNs=APP_AUDIO_SAMPLE_NS;
  memset((void *)positionRing,0,sizeof(positionRing));
  positionCursor=audioCount/2-600;positionDetector.active=1;
  positionDetector.start=positionCursor-47;
  positionDetector.peak[0]=positionCursor-6;positionDetector.peak[1]=positionCursor-5;
  positionDetector.best[0]=positionDetector.best[1]=.81f;
  positionDetector.left[0]=positionDetector.left[1]=.80f;
  positionDetector.right[0]=positionDetector.right[1]=.80f;
  positionSearchLeft=0;PositionProcess();assert(appPositionStatus.events==1);
  puts("PASS: 8 directions, actual stereo DMA + chirp detector + transport, stale result and geometry rejection");
  return 0;
}
