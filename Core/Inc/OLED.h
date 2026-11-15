#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

/* 软件 I2C + SSD1306 128x64，4 行 8x16 字符（每行最多 16 个字符） */

void OLED_Init(void);                            /* 上电初始化，内部会清屏并刷一次 */
void OLED_Clear(void);                           /* 清空显存并标记整屏刷新 */
void OLED_ShowLine(uint8_t line, const char *s); /* line: 0~3 */
uint8_t OLED_Refresh(void);                      /* 只刷有改动的页，返回 1 表示刷过 */

#endif /* __OLED_H */
