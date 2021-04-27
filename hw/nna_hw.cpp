/*
 * Copyright (C) 2020  Jasbir Matharu, <jasknuj@gmail.com>
 *
 * This file is part of v381-nna.
 *
 * v381-nna is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 * v381-nna is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with v381-nna.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "nna_hw.h"

#define NNA_BASE 0x2400000

#define NNA_CLK_100  0xc100000b
#define NNA_CLK_200  0xc1000005
#define NNA_CLK_300  0xc1000003
#define NNA_CLK_400  0xc1000002
#define NNA_CLK_600  0xc1000001
#define NNA_CLK_800  0xc5000000
#define NNA_CLK_1200 0xc1000000

#define NNA_ON  0x10001
#define NNA_OFF 0x00000

static int nna_fd;
static void *nna_nmap;

void* xreg_open(void) {
  int fd;
  void *result;

  fd = open("/dev/mem", 2050);
  nna_fd = fd;
  if ( fd < 0 )
    return (void *)printf("open(/dev/mem) failed.[%x]", fd);
  result = mmap(0, 0x20000u, 3, 1, fd, 0x2400000);
  nna_nmap = result;
  return result;
}

int xreg_close(void) {
  int result;

  if ( munmap((void *)nna_nmap, 0x20000u) == -1 )
  {
    printf("munmap failed\n");
    return -1;
  }

  if (nna_fd)
    result = close(nna_fd);
  return result;
}


int xregr(int reg) {
  return *(volatile uint32_t *)(nna_nmap + reg);
}


int xregw(int reg, unsigned int value) {
  *(volatile uint32_t *)(nna_nmap + reg) = value;
  return reg;
}

int nna_clean_interrupt() {
  return xregw(0x100Cu, 0xFFFFFFFF);
}

int nna_configure(nna_cmds cmd, unsigned int value) {

  int fd;
  volatile uint32_t *mem;
  int ret = 0;

  fd = open("/dev/mem", O_RDWR|O_SYNC);
  if ( fd < 0 ) {
    printf("nna_configure - open(/dev/mem) failed %d.\n",fd);
    ret = -1;
  } else {
    mem = (volatile uint32_t *) mmap(0, 0x10000u, 3, 1, fd, 0x03001000);
    switch ( cmd ) {
      // Source clock speed
      case nna_cmd_clk:
        switch ( value ) {
          case 100: // 100 Mhz
            mem[440] = NNA_CLK_100;
            break;
          case 200: // 200 Mhz
            mem[440] = NNA_CLK_200;
            break;
          case 300: // 300 Mhz
            mem[440] = NNA_CLK_300;
            break;
          case 400: // 400 Mhz
            mem[440] = NNA_CLK_400;
            break;
          case 600: // 600 Mhz
            mem[440] = NNA_CLK_600;
            break;
          case 800: // 800 Mhz
            mem[440] = NNA_CLK_800;
            break;
          case 1200: // 1200 Mhz
            mem[440] = NNA_CLK_1200;
            break;
          default:
            printf("nna_configure - unsupported CLK %d ! set to 400MHz.\n",value);
            mem[440] = -1056964606;
            break;
        }
        break;
      case nna_cmd_reset: // Reset
        mem[443] = 0;
        mem[443] = NNA_ON;
      case nna_cmd_on: // Turn on
        mem[443] = NNA_ON;
        break;
      case nna_cmd_off: // Turn off
        mem[443] = NNA_OFF;
        break;
      default:
        ret = -2;
        printf("nna_configure - unsupported CMD %d \n",cmd);
    }
    if ( munmap((void *)mem, 0x10000u) == -1 ) {
      ret = -3;
      printf("nna_configure - failure munmap!\n");
    }
    if (fd)
    close(fd);
  }
  return ret;
}

int nna_on() {
  return nna_configure(nna_cmd_on, 0);
}


int nna_off() {
  return nna_configure(nna_cmd_off, 0);
}

void nna_reset() {
  nna_configure(nna_cmd_reset, 0);
}


int nna_wait_event_done(int event_mask, int event_value,
  int event_timeout_us) {

  int status;
  int timeleft;
  int increments;
  signed int result;

  timeleft = event_timeout_us / 20 - 1;
  if ( event_timeout_us / 20 == 1 ) {
    goto wait_event_timeout;
  } else {
    increments = 0;
    while ( 1 ) {
      ++increments;
      status = xregr(0x100C);
      if ( !((status & event_mask) ^ event_value) )
        break;
      usleep(0x14u);
      if ( !--timeleft ) {
        timeleft = 20 * increments;
        goto wait_event_timeout;
      }
    }
    result = (uint8_t)(status & (event_mask ^ event_value));
    if ( timeleft )
      return result;
    timeleft = 20 * increments;
  }
wait_event_timeout:
  printf("\n# NNA wait timeout status:%08X event_value:%08X loop:%d us:%d\n",
    status, event_value, increments, timeleft);
  return -1;
}

int nna_wait_done(int event_mask, int event_value) {

  nna_wait_event_done(event_mask, event_value, 400000);
  nna_clean_interrupt();
  return 0;
}
