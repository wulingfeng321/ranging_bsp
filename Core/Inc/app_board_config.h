#ifndef APP_BOARD_CONFIG_H
#define APP_BOARD_CONFIG_H

#define APP_BOARD_A 1
#define APP_BOARD_B 2
/* Change ONLY this default for the other board, or define it in IAR. */
#ifndef APP_BOARD_ROLE
#define APP_BOARD_ROLE APP_BOARD_A
#endif

#if APP_BOARD_ROLE == APP_BOARD_A
#define APP_BOARD_NAME "A"
#define APP_LOCAL_HOST 10
#define APP_PEER_HOST 11
#define APP_PEER_ROLE APP_BOARD_B
#elif APP_BOARD_ROLE == APP_BOARD_B
#define APP_BOARD_NAME "B"
#define APP_LOCAL_HOST 11
#define APP_PEER_HOST 10
#define APP_PEER_ROLE APP_BOARD_A
#else
#error APP_BOARD_ROLE must be APP_BOARD_A or APP_BOARD_B
#endif

#define APP_UDP_PORT 5000U
#endif
