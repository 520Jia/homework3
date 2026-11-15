#include "main.h"
#include "oled.h"
#include "oled_font.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* 软件 I2C：PB8 = SCL, PB9 = SDA（CubeMX 里已配成开漏输出、初始高电平）        */
/* -------------------------------------------------------------------------- */
#define OLED_WIDTH      128
#define OLED_PAGES      8
#define OLED_ADDR       0x78     /* SSD1306 的 8 位地址（7 位 0x3C << 1）      */

/* I2C 速率调节：数值越大越慢。4≈600kHz，12≈250kHz，20≈150kHz
   花屏/不稳就调大，屏幕反应慢就调小 */
#define I2C_DELAY_LOOPS 12

static uint8_t gram[OLED_PAGES][OLED_WIDTH];   /* 显存 1024 字节 */
static uint8_t dirty_pages;                    /* 每 bit 对应一页 */

static void scl(uint8_t v)
{
    HAL_GPIO_WritePin(OLED_SCK_GPIO_Port, OLED_SCK_Pin, v ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void sda(uint8_t v)
{
    HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, v ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void i2c_dly(void)
{
    for (volatile uint32_t i = 0; i < I2C_DELAY_LOOPS; i++) {
        __NOP();
    }
}

static void i2c_start(void)
{
    sda(1); i2c_dly();
    scl(1); i2c_dly();
    sda(0); i2c_dly();
    scl(0); i2c_dly();
}

static void i2c_stop(void)
{
    sda(0); i2c_dly();
    scl(1); i2c_dly();
    sda(1); i2c_dly();
}

/* 返回 1 = 从机应答（ACK），0 = 无应答 */
static uint8_t i2c_write(uint8_t b)
{
    uint8_t ack;

    for (uint8_t i = 0; i < 8; i++) {
        sda((b & 0x80u) ? 1u : 0u);
        b = (uint8_t)(b << 1);
        i2c_dly();
        scl(1); i2c_dly();
        scl(0); i2c_dly();
    }

    /* 第 9 个时钟：释放 SDA，读 ACK */
    sda(1); i2c_dly();
    scl(1); i2c_dly();
    ack = (HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin) == GPIO_PIN_RESET) ? 1u : 0u;
    scl(0); i2c_dly();

    return ack;
}

static void oled_cmd(uint8_t c)
{
    i2c_start();
    i2c_write(OLED_ADDR);
    i2c_write(0x00);          /* 控制字节：命令 */
    i2c_write(c);
    i2c_stop();
}

static void oled_write_page(uint8_t page, const uint8_t *buf, uint16_t len)
{
    oled_cmd((uint8_t)(0xB0 | page));   /* 页地址 */
    oled_cmd(0x00);                     /* 列低 4 位 */
    oled_cmd(0x10);                     /* 列高 4 位 */

    i2c_start();
    i2c_write(OLED_ADDR);
    i2c_write(0x40);                    /* 控制字节：显存数据 */
    for (uint16_t i = 0; i < len; i++) {
        i2c_write(buf[i]);
    }
    i2c_stop();
}

/* -------------------------------------------------------------------------- */
/* 初始化                                                                      */
/* -------------------------------------------------------------------------- */
void OLED_Init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,               /* 关显示 */
        0xD5, 0x80,         /* 时钟分频 */
        0xA8, 0x3F,         /* 复用率 1/64 */
        0xD3, 0x00,         /* 显示偏移 0 */
        0x40,               /* 起始行 0 */
        0x8D, 0x14,         /* 电荷泵开 */
        0x20, 0x02,         /* 页寻址模式 */
        0xA1,               /* 段重映射 */
        0xC8,               /* COM 扫描方向 */
        0xDA, 0x12,         /* COM 引脚配置 */
        0x81, 0x7F,         /* 对比度 */
        0xD9, 0xF1,         /* 预充电 */
        0xDB, 0x30,         /* VCOMH */
        0xA4,               /* 按显存显示 */
        0xA6,               /* 正常显示（非反色） */
        0xAF                /* 开显示 */
    };

    HAL_Delay(100);                     /* 等模块电源稳定 */

    for (uint32_t i = 0; i < sizeof(init_seq); i++) {
        oled_cmd(init_seq[i]);
    }

    OLED_Clear();
    (void)OLED_Refresh();
}

void OLED_Clear(void)
{
    memset(gram, 0x00, sizeof(gram));
    dirty_pages = 0xFF;
}

/* 画一整行（8x16 字体，最多 16 个字符），只动这一行的两页 */
void OLED_ShowLine(uint8_t line, const char *s)
{
    const unsigned char *glyph;
    uint8_t page;

    if (line > 3u) {
        return;
    }
    page = (uint8_t)(line * 2u);

    memset(&gram[page][0], 0x00, OLED_WIDTH);
    memset(&gram[page + 1u][0], 0x00, OLED_WIDTH);

    for (uint8_t i = 0; i < 16u && s[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)s[i];

        if (ch < 0x20u || ch > 0x7Eu) {
            ch = ' ';
        }
        glyph = oled_font8x16[ch - 0x20u];

        for (uint8_t col = 0; col < 8u; col++) {
            gram[page][i * 8u + col]      = glyph[col];
            gram[page + 1u][i * 8u + col] = glyph[col + 8u];
        }
    }

    dirty_pages |= (uint8_t)((1u << page) | (1u << (page + 1u)));
}

uint8_t OLED_Refresh(void)
{
    uint8_t flushed = 0;

    if (dirty_pages == 0u) {
        return 0;
    }

    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        if (dirty_pages & (1u << page)) {
            oled_write_page(page, gram[page], OLED_WIDTH);
            dirty_pages = (uint8_t)(dirty_pages & ~(1u << page));
            flushed = 1;
        }
    }

    return flushed;
}
