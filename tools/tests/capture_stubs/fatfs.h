#include <stdint.h>
typedef int FRESULT;
typedef int FATFS;
typedef unsigned UINT;
typedef struct { int index; } FIL;
typedef int Diskio_drvTypeDef;
#define FR_OK 0
#define FR_EXIST 8
#define FA_WRITE 1
#define FA_CREATE_NEW 4
extern uint8_t retSD;
extern char SDPath[4];
extern FATFS SDFatFS;
uint8_t FATFS_LinkDriver(const Diskio_drvTypeDef *d,char *path);
FRESULT f_mount(FATFS *f,const char *path,int immediate);
FRESULT f_mkdir(const char *path);
FRESULT f_open(FIL *f,const char *path,int flags);
FRESULT f_write(FIL *f,const void *data,UINT count,UINT *written);
FRESULT f_close(FIL *f);
