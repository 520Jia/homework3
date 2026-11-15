#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

/* 数据包：AA RUN DIR STEP_L STEP_H CHECK（共 6 字节） */
#define PKT_STEP_MIN  100u
#define PKT_STEP_MAX  2000u

void    protocol_start(void);   /* 挂上串口接收中断 */
uint8_t protocol_poll(uint8_t *run, uint8_t *dir, uint16_t *step_ms);
                                /* 主循环调用；返回 1 表示收到一个合法包 */
uint32_t protocol_ok_count(void);
uint32_t protocol_bad_count(void);

#endif /* __PROTOCOL_H */
