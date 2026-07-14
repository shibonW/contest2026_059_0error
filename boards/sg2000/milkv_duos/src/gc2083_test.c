/****************************************************************************
 * boards/sg2000/milkv_duos/src/gc2083_test.c
 *
 * GC2083 MCLK 产生 + I2C2 探测测试
 * 寄存器操作，验证 CAM_MCLK0 时钟输出与 GC2083 I2C 连接。
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <debug.h>
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>
#include "riscv_internal.h"

/****************************************************************************
 * SoC 基地址
 ****************************************************************************/

#define TOP_BASE           0x03000000
#define PINMUX_BASE        (TOP_BASE + 0x1000)   /* 0x03001000 */
#define CLKGEN_BASE        (TOP_BASE + 0x2000)   /* 0x03002000 */
#define RST_BASE           (TOP_BASE + 0x3000)   /* 0x03003000 */
#define I2C2_BASE          0x04020000

/****************************************************************************
 * Pinmux 寄存器（3-bit func 字段 [2:0]）
 ****************************************************************************/

#define REG_FUNCSEL_CAM_MCLK0  (PINMUX_BASE + 0x000)  /* 0x03001000 */
#define REG_FUNCSEL_PC17       (PINMUX_BASE + 0x1A8)  /* 0x030011A8 */
#define PINMUX_TXP1            (PINMUX_BASE + 0x1B0)  /* IIC2_SCL */
#define PINMUX_TXM1            (PINMUX_BASE + 0x1AC)  /* IIC2_SDA */

/****************************************************************************
 * 时钟寄存器（CLKGEN_BASE + 偏移）
 ****************************************************************************/

#define REG_CLK_CAM0_SRC_DIV   (CLKGEN_BASE + 0x8C0)  /* 0x030028C0 */
#define CLK_EN_1               (CLKGEN_BASE + 0x004)
#define CLK_EN_2               (CLKGEN_BASE + 0x008)
#define CLK_EN_3               (CLKGEN_BASE + 0x00C)
#define DIV_CLK_I2C            (CLKGEN_BASE + 0x104)

/****************************************************************************
 * 复位寄存器
 ****************************************************************************/

#define RST_REG                (RST_BASE + 0x000)

/****************************************************************************
 * GPIO Port C (PC17)
 ****************************************************************************/

#define GPIO_PORTC_BASE        0x03022000
#define GPIO_SWPORTA_DR        (GPIO_PORTC_BASE + 0x000)
#define GPIO_SWPORTA_DDR       (GPIO_PORTC_BASE + 0x004)

/****************************************************************************
 * DesignWare I2C 寄存器结构
 ****************************************************************************/

struct dw_i2c_regs
{
    volatile uint32_t ic_con;            /* 0x00 */
    volatile uint32_t ic_tar;            /* 0x04 */
    volatile uint32_t ic_sar;            /* 0x08 */
    volatile uint32_t ic_hs_maddr;       /* 0x0C */
    volatile uint32_t ic_cmd_data;       /* 0x10 */
    volatile uint32_t ic_ss_scl_hcnt;    /* 0x14 */
    volatile uint32_t ic_ss_scl_lcnt;    /* 0x18 */
    volatile uint32_t ic_fs_scl_hcnt;    /* 0x1C */
    volatile uint32_t ic_fs_scl_lcnt;    /* 0x20 */
    volatile uint32_t ic_hs_scl_hcnt;    /* 0x24 */
    volatile uint32_t ic_hs_scl_lcnt;    /* 0x28 */
    volatile uint32_t ic_intr_stat;      /* 0x2C */
    volatile uint32_t ic_intr_mask;      /* 0x30 */
    volatile uint32_t ic_raw_intr_stat;  /* 0x34 */
    volatile uint32_t ic_rx_tl;          /* 0x38 */
    volatile uint32_t ic_tx_tl;          /* 0x3C */
    volatile uint32_t ic_clr_intr;       /* 0x40 */
    volatile uint32_t ic_clr_rx_under;   /* 0x44 */
    volatile uint32_t ic_clr_rx_over;    /* 0x48 */
    volatile uint32_t ic_clr_tx_over;    /* 0x4C */
    volatile uint32_t ic_clr_rd_req;     /* 0x50 */
    volatile uint32_t ic_clr_tx_abrt;    /* 0x54 */
    volatile uint32_t ic_clr_rx_done;    /* 0x58 */
    volatile uint32_t ic_clr_activity;   /* 0x5C */
    volatile uint32_t ic_clr_stop_det;   /* 0x60 */
    volatile uint32_t ic_clr_start_det;  /* 0x64 */
    volatile uint32_t ic_clr_gen_call;   /* 0x68 */
    volatile uint32_t ic_enable;         /* 0x6C */
    volatile uint32_t ic_status;         /* 0x70 */
    volatile uint32_t ic_txflr;          /* 0x74 */
    volatile uint32_t ic_rxflr;          /* 0x78 */
    volatile uint32_t ic_sda_hold;       /* 0x7C */
    volatile uint32_t ic_tx_abrt_source; /* 0x80 */
    volatile uint32_t ic_slv_dat_nack;   /* 0x84 */
    volatile uint32_t ic_dma_cr;         /* 0x88 */
    volatile uint32_t ic_dma_tdlr;       /* 0x8C */
    volatile uint32_t ic_dma_rdlr;       /* 0x90 */
    volatile uint32_t ic_sda_setup;      /* 0x94 */
    volatile uint32_t ic_ack_gen_call;   /* 0x98 */
    volatile uint32_t ic_enable_status;  /* 0x9C */
    volatile uint32_t ic_fs_spklen;      /* 0xA0 */
    volatile uint32_t ic_hs_spklen;      /* 0xA4 */
};

/****************************************************************************
 * IC_CON / IC_STATUS / IC_CMD_DATA 位定义
 ****************************************************************************/

#define IC_CON_MM               (1 << 0)
#define IC_CON_SPEED_SS         (1 << 1)
#define IC_CON_RESTART_EN       (1 << 5)
#define IC_CON_SLV_DIS          (1 << 6)

#define IC_STATUS_TFNF          (1 << 1)
#define IC_STATUS_TFE           (1 << 2)
#define IC_STATUS_RFNE          (1 << 3)
#define IC_STATUS_MA            (1 << 5)

#define IC_CMD_READ             (1 << 8)
#define IC_CMD_STOP             (1 << 9)

#define IC_INTR_RX_FULL         (1 << 2)
#define IC_INTR_STOP_DET        (1 << 9)
#define IC_INTR_TX_ABRT         (1 << 6)

/****************************************************************************
 * GC2083 传感器参数
 ****************************************************************************/

#define GC2083_I2C_ADDR         0x37    /* 7-bit 地址 */
#define GC2083_CHIP_ID_H_ADDR   0x03F0
#define GC2083_CHIP_ID_L_ADDR   0x03F1
#define GC2083_CHIP_ID          0x2083

/****************************************************************************
 * Name: sg2002_pinmux_init
 *
 * Description:
 *   配置 CAM_MCLK0 引脚为原生 MCLK 功能，
 *   配置 PC17 为 GPIO 模式用于传感器复位。
 *
 ****************************************************************************/

void sg2002_pinmux_init(void)
{
    uint32_t val;

    /*
     * CAM_MCLK0 → 设为原生 MCLK 功能 (func=0)
     * 寄存器 0x03001000, [2:0] = 0
     */

    val = getreg32(REG_FUNCSEL_CAM_MCLK0);
    val &= ~0x7;
    val |= 0;
    putreg32(val, REG_FUNCSEL_CAM_MCLK0);

    /*
     * PC17 (XGPIOC_17) → 设为 GPIO 模式 (func=3)
     * 用于控制 GC2083 的 RESET# 引脚
     */

    val = getreg32(REG_FUNCSEL_PC17);
    val &= ~0x7;
    val |= 3;
    putreg32(val, REG_FUNCSEL_PC17);
}

/****************************************************************************
 * Name: sg2002_sensor_reset
 *
 * Description:
 *   通过 PC17 控制 GC2083 的 RESET# 引脚，完成一次硬件复位。
 *
 ****************************************************************************/

void sg2002_sensor_reset(void)
{
    uint32_t val;

    /* PC17 设为输出 */

    val = getreg32(GPIO_SWPORTA_DDR);
    val |= (1 << 17);
    putreg32(val, GPIO_SWPORTA_DDR);

    /* PC17 = LOW (复位生效) */

    val = getreg32(GPIO_SWPORTA_DR);
    val &= ~(1 << 17);
    putreg32(val, GPIO_SWPORTA_DR);
    usleep(1000);

    /* PC17 = HIGH (释放复位) */

    val = getreg32(GPIO_SWPORTA_DR);
    val |= (1 << 17);
    putreg32(val, GPIO_SWPORTA_DR);
    usleep(10000);  /* 等传感器稳定 */
}

/****************************************************************************
 * Name: sg2002_cam0_mclk_enable_24m
 *
 * Description:
 *   配置 CAM0 时钟分频器，从 1188MHz CAM0PLL 分频得到约 24.24MHz MCLK。
 *
 ****************************************************************************/

void sg2002_cam0_mclk_enable_24m(void)
{
    uint32_t val;

    val = getreg32(REG_CLK_CAM0_SRC_DIV);
    val &= ~(0x3 << 8);     /* 清除 CLK_SRC [9:8] */
    val |=  (0x0 << 8);     /* 00 = clk_cam0pll */
    val &= ~(0x3F << 16);   /* 清除 DIV [21:16] */
    val |=  (49 << 16);     /* 分频因子 49 */
    putreg32(val, REG_CLK_CAM0_SRC_DIV);

    /* 使能 clk_cam0 时钟门控 (bit 16) */

    val = getreg32(CLK_EN_2);
    val |= (1 << 16);
    putreg32(val, CLK_EN_2);

    up_udelay(100);
}

/****************************************************************************
 * Name: i2c2_configure
 *
 * Description:
 *   I2C2 控制器完整配置：时钟 + 复位 + Pinmux + Standard Mode 100kHz。
 *
 ****************************************************************************/

void i2c2_configure(void)
{
    struct dw_i2c_regs *i2c = (struct dw_i2c_regs *)I2C2_BASE;
    uint32_t val;

    /* ================================================================
     * Step 1: 使能时钟
     * ================================================================
     *
     * 需要使能的时钟链：
     *   [父] clk_axi4 ──→ [子] clk_apb_i2c ──→ [子] clk_apb_i2c2
     *   CLK_EN_2[1]         CLK_EN_1[6]           CLK_EN_3[19]
     *
     *   [父] osc(25MHz) ──→ [子] clk_i2c（功能时钟，所有 I2C 共用）
     *                        CLK_EN_3[7]
     *                        分频: DIV_CLK_I2C[19:16] = 0 → ÷1 → 25MHz
     */

    /* 父时钟（防御性使能） */

    val = getreg32(CLK_EN_2);
    val |= (1 << 1) | (1 << 2);         /* clk_axi4 + clk_axi6 */
    putreg32(val, CLK_EN_2);

    val = getreg32(CLK_EN_1);
    val |= (1 << 6);                    /* clk_apb_i2c */
    putreg32(val, CLK_EN_1);

    /* I2C 功能时钟 + I2C2 APB 时钟 */

    val = getreg32(CLK_EN_3);
    val |= (1 << 7);                    /* clk_i2c（功能时钟） */
    val |= (1 << 19);                   /* clk_apb_i2c2（I2C2 专用） */
    putreg32(val, CLK_EN_3);

    /* I2C 功能时钟分频: DIV_CLK_I2C[19:16] = 0 → 分频=1 → 25MHz */

    val = getreg32(DIV_CLK_I2C);
    val &= ~(0xF << 16);
    putreg32(val, DIV_CLK_I2C);

    /* ================================================================
     * Step 2: 解除 I2C2 复位
     * ================================================================
     *
     * RST_REG (0x03003000), bit29 = RST_I2C2
     * 置 1 = deassert（解除复位）
     */

    val = getreg32(RST_REG);
    val |= (1 << 29);
    putreg32(val, RST_REG);
    up_udelay(100);

    /* ================================================================
     * Step 3: Pinmux 配置
     * ================================================================
     *
     * PAD_MIPI_TXP1 → IIC2_SCL (func=4)
     * PAD_MIPI_TXM1 → IIC2_SDA (func=4)
     */

    val = getreg32(PINMUX_TXP1);
    val &= ~0x7;
    val |= 0x4;                         /* func=4 → IIC2_SCL */
    putreg32(val, PINMUX_TXP1);

    val = getreg32(PINMUX_TXM1);
    val &= ~0x7;
    val |= 0x4;                         /* func=4 → IIC2_SDA */
    putreg32(val, PINMUX_TXM1);

    /* ================================================================
     * Step 4: 初始化 I2C2 控制器（Standard Mode 100kHz）
     * ================================================================
     *
     * IC_CLK = 25MHz, Standard Mode SCL 时序：
     *   hcnt = (25 × 4000 / 1000) - 7 = 93
     *   lcnt = (25 × 4700 / 1000) - 1 = 116
     *   实际 SCL = (93 + 116 + 3) / 25MHz ≈ 116kHz
     */

    /* 禁用 I2C */

    putreg32(0, (uint32_t)&i2c->ic_enable);

    /* IC_CON: Master Mode, Standard Speed, Restart Enable, Slave Disable */

    putreg32(0x63, (uint32_t)&i2c->ic_con);

    /* FIFO 阈值 */

    putreg32(0, (uint32_t)&i2c->ic_rx_tl);
    putreg32(0, (uint32_t)&i2c->ic_tx_tl);

    /* 屏蔽所有中断（轮询模式） */

    putreg32(0, (uint32_t)&i2c->ic_intr_mask);

    /* Standard Mode SCL 时序 */

    putreg32(93,  (uint32_t)&i2c->ic_ss_scl_hcnt);
    putreg32(116, (uint32_t)&i2c->ic_ss_scl_lcnt);

    /* 清空中断 */

    getreg32((uint32_t)&i2c->ic_clr_intr);

    /* 使能 I2C */

    putreg32(1, (uint32_t)&i2c->ic_enable);
    up_udelay(100);
}

/****************************************************************************
 * Name: i2c2_wait_bus_free / i2c2_wait_stop
 *
 * Description:
 *   I2C 总线等待辅助函数。
 *
 ****************************************************************************/

static int i2c2_wait_bus_free(struct dw_i2c_regs *i2c)
{
    int timeout = 1000;

    while (timeout-- > 0)
    {
        uint32_t status = getreg32((uint32_t)&i2c->ic_status);
        if (!(status & IC_STATUS_MA) && (status & IC_STATUS_TFE))
        {
            return 0;
        }

        up_udelay(10);
    }

    return -1;
}

static int i2c2_wait_stop(struct dw_i2c_regs *i2c)
{
    int timeout = 10000;

    while (timeout-- > 0)
    {
        if (getreg32((uint32_t)&i2c->ic_raw_intr_stat) & IC_INTR_STOP_DET)
        {
            getreg32((uint32_t)&i2c->ic_clr_stop_det);
            return 0;
        }

        up_udelay(10);
    }

    return -1;
}

/****************************************************************************
 * Name: gc2083_read_reg
 *
 * Description:
 *   GC2083 读寄存器: [S][0x37+W][A][Addr_H][A][Addr_L][A][Sr][0x37+R][A][Data][P]
 *
 ****************************************************************************/

static int gc2083_read_reg(struct dw_i2c_regs *i2c, uint16_t reg_addr)
{
    int ret;
    int timeout;
    uint8_t data = 0;

    ret = i2c2_wait_bus_free(i2c);
    if (ret < 0)
    {
        return ret;
    }

    /* 禁用 -> 设地址 -> 使能 */

    putreg32(0, (uint32_t)&i2c->ic_enable);
    putreg32(GC2083_I2C_ADDR, (uint32_t)&i2c->ic_tar);
    putreg32(1, (uint32_t)&i2c->ic_enable);
    up_udelay(10);

    /* 写寄存器地址（2 字节） */

    putreg32((reg_addr >> 8) & 0xFF, (uint32_t)&i2c->ic_cmd_data);
    putreg32(reg_addr & 0xFF, (uint32_t)&i2c->ic_cmd_data);

    /* 读请求: Repeated Start + READ + STOP */

    putreg32(IC_CMD_READ | IC_CMD_STOP, (uint32_t)&i2c->ic_cmd_data);

    /* 等待 RX 数据或 TX_ABRT（设备无应答） */

    timeout = 10000;
    while (timeout-- > 0)
    {
        uint32_t intr = getreg32((uint32_t)&i2c->ic_raw_intr_stat);

        if (intr & IC_INTR_TX_ABRT)
        {
            /* 设备无应答，清掉 abort 标志，返回错误 */

            getreg32((uint32_t)&i2c->ic_clr_tx_abrt);
            i2c2_wait_stop(i2c);
            return -1;
        }

        if (intr & IC_INTR_RX_FULL)
        {
            data = (uint8_t)getreg32((uint32_t)&i2c->ic_cmd_data);
            break;
        }

        up_udelay(10);
    }

    if (timeout <= 0)
    {
        syslog(LOG_ERR, "GC2083: I2C read timeout, raw_intr=0x%08x\n",
               getreg32((uint32_t)&i2c->ic_raw_intr_stat));
        i2c2_wait_stop(i2c);
        return -1;
    }

    ret = i2c2_wait_stop(i2c);
    return (ret < 0) ? ret : (int)data;
}

/****************************************************************************
 * Name: gc2083_write_reg
 *
 * Description:
 *   GC2083 写寄存器: [S][0x37+W][A][Addr_H][A][Addr_L][A][Data][P]
 *
 ****************************************************************************/

static int gc2083_write_reg(struct dw_i2c_regs *i2c,
                            uint16_t reg_addr, uint8_t data)
{
    int ret;

    ret = i2c2_wait_bus_free(i2c);
    if (ret < 0)
    {
        return ret;
    }

    /* 禁用 -> 设地址 -> 使能 */

    putreg32(0, (uint32_t)&i2c->ic_enable);
    putreg32(GC2083_I2C_ADDR, (uint32_t)&i2c->ic_tar);
    putreg32(1, (uint32_t)&i2c->ic_enable);
    up_udelay(10);

    /* 写寄存器地址高字节 */

    putreg32((reg_addr >> 8) & 0xFF, (uint32_t)&i2c->ic_cmd_data);

    /* 写寄存器地址低字节 */

    putreg32(reg_addr & 0xFF, (uint32_t)&i2c->ic_cmd_data);

    /* 写数据 + STOP */

    putreg32(data | IC_CMD_STOP, (uint32_t)&i2c->ic_cmd_data);

    ret = i2c2_wait_stop(i2c);

    /* 清空 RX FIFO */

    while (getreg32((uint32_t)&i2c->ic_status) & IC_STATUS_RFNE)
    {
        getreg32((uint32_t)&i2c->ic_cmd_data);
    }

    return ret;
}

/****************************************************************************
 * Name: gc2083_probe
 *
 * Description:
 *   读 GC2083 Chip ID (0x2083) 验证 I2C 连接。
 *   返回 0 = 成功，-1 = 失败。
 *
 ****************************************************************************/

int gc2083_probe(void)
{
    struct dw_i2c_regs *i2c = (struct dw_i2c_regs *)I2C2_BASE;
    int id_h;
    int id_l;
    uint16_t chip_id;

    id_h = gc2083_read_reg(i2c, GC2083_CHIP_ID_H_ADDR);
    id_l = gc2083_read_reg(i2c, GC2083_CHIP_ID_L_ADDR);

    if (id_h < 0 || id_l < 0)
    {
        syslog(LOG_ERR, "GC2083: read chip id failed (h=%d, l=%d)\n",
               id_h, id_l);
        return -1;
    }

    chip_id = ((id_h & 0xFF) << 8) | (id_l & 0xFF);
    syslog(LOG_INFO, "GC2083: Chip ID = 0x%04X (expect 0x%04X)\n",
           chip_id, GC2083_CHIP_ID);

    if (chip_id != GC2083_CHIP_ID)
    {
        syslog(LOG_ERR, "GC2083: chip id mismatch!\n");
        return -1;
    }

    return 0;
}

/****************************************************************************
 * Name: gc2083_bringup
 *
 * Description:
 *   完整的 GC2083 bringup 流程：
 *     1. 配置 Pinmux（MCLK + GPIO）
 *     2. 使能 24MHz MCLK
 *     3. 复位传感器
 *     4. 配置 I2C2 控制器
 *     5. 探测 GC2083（读 Chip ID）
 *
 ****************************************************************************/

void gc2083_bringup(void)
{
    int ret;

    /* 1. Pinmux */

    sg2002_pinmux_init();

    /* 2. MCLK 24MHz */

    sg2002_cam0_mclk_enable_24m();
    usleep(10000);

    /* 3. 复位传感器 */

    sg2002_sensor_reset();

    /* 4. I2C2 初始化（复位后延时等待传感器稳定） */

    usleep(10000);
    syslog(LOG_INFO, "gc2083: i2c2 configure begin\n");
    i2c2_configure();
    syslog(LOG_INFO, "gc2083: i2c2 configure done\n");

    /* 5. 探测 GC2083（I2C 初始化后延时再探测） */

    usleep(10000);
    syslog(LOG_INFO, "gc2083: probe begin\n");
    ret = gc2083_probe();
    if (ret < 0)
    {
        syslog(LOG_ERR, "gc2083: probe FAILED\n");
        return;
    }

    syslog(LOG_INFO, "gc2083: bringup complete, MCLK + I2C OK\n");
}
