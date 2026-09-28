#define main original_test_main
#include "test_app_range.c"
#undef main
int main(void)
{
  uint64_t ns=0;
  AppRange_Init();
  assert(!AppRange_MasterTime(&ns));
  appNetStatus.online=1; appRangeStatus.locked=1;
  syncModel.offset=700000000.0; syncModel.origin=(double)clockNs; syncModel.slope=0.0001;
  assert(AppRange_MasterTime(&ns));
  assert(ns==clockNs-(APP_BOARD_ROLE==APP_BOARD_B ? 700000000ULL:0));
  clockNs+=1000000000ULL;
  assert(AppRange_MasterTime(&ns));
  assert(ns==clockNs-(APP_BOARD_ROLE==APP_BOARD_B ? 700100000ULL:0));
  appRangeStatus.locked=0; assert(!AppRange_MasterTime(&ns));
  appRangeStatus.locked=1; appNetStatus.online=0; assert(!AppRange_MasterTime(&ns));
  appNetStatus.online=1; clockReady=0; assert(!AppRange_MasterTime(&ns));
  printf("PASS: master clock %s mapping and availability\n",APP_BOARD_NAME);
  return 0;
}
