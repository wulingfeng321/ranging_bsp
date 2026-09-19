#ifndef APP_BOARD_CONFIG_H
#define APP_BOARD_CONFIG_H

#define APP_BOARD_A 1
#define APP_BOARD_B 2
/* Change ONLY this default for the other board, or define it in IAR. */
#ifndef APP_BOARD_ROLE
#define APP_BOARD_ROLE APP_BOARD_B
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
/* Experimental ranging configuration: same on BOTH boards. */
#define APP_RANGE_PORT 5001U
#define APP_TEMPERATURE_DECI_C 200 /* 20.0 degrees Celsius; manually set ambient */
#define APP_RANGE_BIAS_NS 0LL /* calibrated fixed (B-A) audio delay, ns */
#define APP_RANGE_RESULT_HOLD_MS 15000U /* Time to read a test result. */
#define APP_RANGE_CHANNEL 0U /* corresponding physical L microphone on each board */
/* Bring-up profile: build BOTH boards with the same setting. Set to 0 to
 * restore the original detector thresholds after acoustic validation. */
#define APP_RANGE_RELAXED_DETECTION 1
#if APP_RANGE_RELAXED_DETECTION
#define APP_RANGE_MIN_QUALITY 300U
#define APP_RANGE_MIN_RMS 32.0f
#define APP_RANGE_COARSE_SCORE 0.08f
#define APP_RANGE_MAX_GAP_ENERGY_RATIO 1.0f
#else
#define APP_RANGE_MIN_QUALITY 450U
#define APP_RANGE_MIN_RMS 64.0f
#define APP_RANGE_COARSE_SCORE 0.08f
#define APP_RANGE_MAX_GAP_ENERGY_RATIO 0.6f
#endif
#endif
