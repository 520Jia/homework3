#ifndef __FEEDBACK_H
#define __FEEDBACK_H

#include <stdint.h>

/* 串口状态反馈：STATE=RUN,DIR=FWD,STEP=300,LED=1\r\n */

void feedback_mark(void);   /* 状态变化时调用：标记"待发送" */
void feedback_poll(uint8_t run, uint8_t dir, uint16_t step_ms, uint8_t led);
                            /* 主循环调用；空闲且有标记时启动一次 IT 发送 */
void feedback_on_error(void);  /* 串口出错时由错误回调调用，解锁发送 */

#endif /* __FEEDBACK_H */
