/****************************************************************************
 * vendor/sg2000/boards/sg2000/milkv_duos/src/sg2000_appinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <nuttx/board.h>
#include <nuttx/drivers/ramdisk.h>
#include <sys/mount.h>
#include <sys/boardctl.h>
#include <arch/board/board_memorymap.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SECTORSIZE   512
#define NSECTORS(b)  (((b) + SECTORSIZE - 1) / SECTORSIZE)
#define RAMDISK_DEVICE_MINOR 0

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int mount_ramdisk(void)
{
  int ret;
  struct boardioc_romdisk_s desc;

  desc.minor    = RAMDISK_DEVICE_MINOR;
  desc.nsectors = NSECTORS((ssize_t)__ramdisk_size);
  desc.sectsize = SECTORSIZE;
  desc.image    = __ramdisk_start;

  ret = boardctl(BOARDIOC_ROMDISK, (uintptr_t)&desc);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Ramdisk register failed: %s\n", strerror(errno));
      syslog(LOG_ERR, "Ramdisk mountpoint /dev/ram%d\n",
             RAMDISK_DEVICE_MINOR);
      syslog(LOG_ERR, "Ramdisk length %lu, origin %lx\n",
             (ssize_t)__ramdisk_size, (uintptr_t)__ramdisk_start);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sg2000_boardinitialize
 *
 * Description:
 *   All SG2000 architectures must provide the following entry point.
 *   This entry point is called early in the initialization -- after all
 *   memory has been configured and mapped but before any devices have been
 *   initialized.
 *
 ****************************************************************************/

void sg2000_boardinitialize(void)
{
  /* Initialize on-board LEDs */

  board_autoled_initialize();

  /* The 16550 driver's u16550_setup() reads IER but never clears it.
   * U-Boot may leave UART interrupts enabled (e.g. ETBEI for TX empty).
   * When uart_open() later calls uart_attach() + uart_enablerxint(),
   * the IER gets written with ERBFI OR'd onto the stale value, and the
   * instant uart_spinunlock() restores CPU interrupts, a spurious UART
   * interrupt fires.  The ISR triggers TTY echo processing which blocks.
   *
   * Fix: explicitly disable all UART interrupts here, before MMU init.
   * UART0 base = 0x04140000, IER = offset 1 (word index with REGINCR=4).
   */

  *(volatile uint32_t *)0x04140004 = 0;   /* Write 0 to UART0 IER */

  /* Drain any residual RX data so the FIFO is empty */

  while (*(volatile uint32_t *)0x04140014 & 1)  /* LSR bit0 = Data Ready */
    {
      (void)*(volatile uint32_t *)0x04140000;  /* Read RBR to discard */
    }
}

int board_app_initialize(uintptr_t arg)
{
#ifdef CONFIG_BOARD_LATE_INITIALIZE
  /* Board initialization already performed by board_late_initialize() */

  return OK;
#else
#ifdef CONFIG_NSH_ARCHINIT
  mount(NULL, "/proc", "procfs", 0, NULL);
#endif
  return OK;
#endif
}

void board_late_initialize(void)
{
  int ret;

  /* Mount the RAM Disk */

  syslog(LOG_INFO, "board_late_initialize: mount ramdisk begin\n");
#ifdef CONFIG_DEBUG_FEATURES
  up_putc('1');  /* board_late: before mount_ramdisk */
#endif
  ret = mount_ramdisk();
#ifdef CONFIG_DEBUG_FEATURES
  up_putc('2');  /* board_late: after mount_ramdisk */
#endif
  syslog(LOG_INFO, "board_late_initialize: mount ramdisk done: %d\n", ret);

  /* Initialize Ethernet driver */

#ifdef CONFIG_SG2000_ETH
  extern int sg2000_eth_initialize(void);
  syslog(LOG_INFO, "board_late_initialize: eth init begin\n");
#ifdef CONFIG_DEBUG_FEATURES
  up_putc('3');  /* board_late: before eth init */
#endif
  ret = sg2000_eth_initialize();
#ifdef CONFIG_DEBUG_FEATURES
  up_putc('4');  /* board_late: after eth init */
#endif
  syslog(LOG_INFO, "board_late_initialize: eth init done: %d\n", ret);
#endif

  /* Initialize GC2083 MCLK test */

  extern void gc2083_bringup(void);
  syslog(LOG_INFO, "board_late_initialize: gc2083 MCLK test begin\n");
  gc2083_bringup();
  syslog(LOG_INFO, "board_late_initialize: gc2083 MCLK test done\n");

  /* Perform board-specific initialization */

#ifdef CONFIG_NSH_ARCHINIT
  syslog(LOG_INFO, "board_late_initialize: mount procfs begin\n");
#ifdef CONFIG_DEBUG_FEATURES
  up_putc('5');  /* board_late: before mount procfs */
#endif
  mount(NULL, "/proc", "procfs", 0, NULL);
#ifdef CONFIG_DEBUG_FEATURES
  up_putc('6');  /* board_late: after mount procfs */
#endif
  syslog(LOG_INFO, "board_late_initialize: mount procfs done\n");
#endif
}
