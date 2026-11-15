#include "main.h"
#include "feedback.h"

#include <stdio.h>

extern UART_HandleTypeDef huart1;

static char             tx_buf[64];      /* 必须是静态/全局：IT 发送期间硬件一直读它 */
static volatile uint8_t tx_busy;         /* 1 = 上一次发送还没结束 */
static volatile uint8_t fb_dirty;        /* 1 = 有新状态待发送 */

void feedback_mark(void)
{
    fb_dirty = 1;
}

/* run/dir/step/led 由主循环从 sys 里取出来传进来，保证只有一份状态 */
void feedback_poll(uint8_t run, uint8_t dir, uint16_t step_ms, uint8_t led)
{
    int n;

    if (!fb_dirty || tx_busy) {
        return;                          /* 没标记，或上一帧还没发完：等下一轮 */
    }

    n = snprintf(tx_buf, sizeof tx_buf, "STATE=%s,DIR=%s,STEP=%u,LED=%u\r\n",
                 run ? "RUN" : "STOP",
                 dir ? "REV" : "FWD",
                 (unsigned)step_ms,
                 (unsigned)led);
    if (n <= 0) {
        fb_dirty = 0;                    /* 格式化失败，丢掉这次标记 */
        return;
    }
    if (n >= (int)sizeof tx_buf) {
        n = (int)sizeof tx_buf;          /* 截断保护 */
    }

    fb_dirty = 0;                        /* 先清标记：发送期间再变化会重新置 1 */
    tx_busy  = 1;

    if (HAL_UART_Transmit_IT(&huart1, (uint8_t *)tx_buf, (uint16_t)n) != HAL_OK) {
        tx_busy  = 0;                    /* 启动失败（比如串口忙），下一轮重试 */
        fb_dirty = 1;
    }
}

/* 一次发送完成：只解锁，不在中断里发下一帧 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tx_busy = 0;
    }
}

/* 串口出错时解锁，避免 tx_busy 永远卡在 1 导致反馈"消失" */
void feedback_on_error(void)
{
    tx_busy = 0;
}
