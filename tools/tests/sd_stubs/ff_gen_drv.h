#include <stdint.h>
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef unsigned UINT;
typedef BYTE DSTATUS;
typedef int DRESULT;
#define STA_NOINIT 1
#define STA_NODISK 2
#define RES_OK 0
#define RES_ERROR 1
#define RES_NOTRDY 3
#define RES_PARERR 4
#define CTRL_SYNC 0
#define GET_SECTOR_COUNT 1
#define GET_SECTOR_SIZE 2
#define GET_BLOCK_SIZE 3
typedef struct {
  DSTATUS (*initialize)(BYTE);
  DSTATUS (*status)(BYTE);
  DRESULT (*read)(BYTE,BYTE *,DWORD,UINT);
  DRESULT (*write)(BYTE,const BYTE *,DWORD,UINT);
  DRESULT (*ioctl)(BYTE,BYTE,void *);
} Diskio_drvTypeDef;
