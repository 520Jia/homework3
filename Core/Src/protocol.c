#include "main.h"
#include "protocol.h"
#include "feedback.h"

extern UART_HandleTypeDef huart1;

#define PKT_LEN   6u
#define PKT_HEAD  0xAAu
#define RB_SIZE   64u          /* 必须是 2 的幂 */

/* ---------------- 环形缓冲：中断写、主循环读 ---------------- */
static uint8_t           rx_byte;
static volatile uint8_t  rb_buf[RB_SIZE];
static volatile uint16_t rb_head;   /* 只由中断改 */
static volatile uint16_t rb_tail;   /* 只由主循环改 */

static void rb_put(uint8_t b)
{
    uint16_t next = (uint16_t)((rb_head + 1u) & (RB_SIZE - 1u));

    if (next != rb_tail) {          /* 满了就丢新字节，不覆盖尚未处理的数据 */
        rb_buf[rb_head] = b;
        rb_head = next;
    }
}

static uint8_t rb_get(uint8_t *b)
{
    if (rb_tail == rb_head) {
        return 0;
    }
    *b = rb_buf[rb_tail];
    rb_tail = (uint16_t)((rb_tail + 1u) & (RB_SIZE - 1u));
    return 1;
}

/* ---------------- 拼帧 ---------------- */
static uint8_t frame[PKT_LEN];
static uint8_t frame_n;
static uint32_t ok_cnt;
static uint32_t bad_cnt;

/* 前 5 个字节累加，uint8_t 溢出即 mod 256 */
static uint8_t pkt_check(const uint8_t *p)
{
    uint8_t sum = 0;

    for (uint8_t i = 0; i < PKT_LEN - 1u; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

/* 校验失败时把窗口右移到下一个 0xAA，避免一个坏包拖垮后续所有包 */
static void frame_resync(void)
{
    uint8_t k = 1;
    uint8_t m = 0;

    while (k < PKT_LEN && frame[k] != PKT_HEAD) {
        k++;
    }
    while (k < PKT_LEN) {
        frame[m++] = frame[k++];
    }
    frame_n = m;
}

/* ---------------- 中断：只搬字节 ---------------- */
void protocol_start(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rb_put(rx_byte);
        (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1);   /* 必须重新挂载 */
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(huart);                   /* 清溢出标志 */
        (void)HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
        feedback_on_error();                               /* 顺带解锁发送忙标志 */
    }
}

/* ---------------- 主循环：解析 ---------------- */
uint8_t protocol_poll(uint8_t *run, uint8_t *dir, uint16_t *step_ms)
{
    uint8_t b;
    uint8_t got = 0;

    while (rb_get(&b)) {
        if (frame_n == 0) {
            if (b == PKT_HEAD) {
                frame[frame_n++] = b;
            }
            continue;
        }

        frame[frame_n++] = b;
        if (frame_n < PKT_LEN) {
            continue;
        }

        uint8_t valid = 0;

        if ((frame[5] == pkt_check(frame)) && (frame[1] <= 1u) && (frame[2] <= 1u)) {
            uint16_t step = (uint16_t)frame[3] | ((uint16_t)frame[4] << 8);

            if ((step >= PKT_STEP_MIN) && (step <= PKT_STEP_MAX)) {
                *run     = frame[1];
                *dir     = frame[2];
                *step_ms = step;
                ok_cnt++;
                got  = 1;
                valid = 1;
            } else {
                bad_cnt++;          /* 间隔越界，整包丢弃 */
            }
        } else {
            bad_cnt++;              /* 校验或标志位不合法 */
        }

        if (valid) {
            frame_n = 0;            /* 正常收完，等下一个 AA */
        } else {
            frame_resync();         /* 失败：找下一个 AA 重新对齐 */
        }
    }

    return got;
}

uint32_t protocol_ok_count(void)
{
    return ok_cnt;
}

uint32_t protocol_bad_count(void)
{
    return bad_cnt;
}
