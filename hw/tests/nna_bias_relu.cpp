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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "hw_adaptor.h"
#include "mem_ctrl.h"

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

enum nna_cmds { nna_cmd_on, nna_cmd_off, nna_cmd_reset, nna_cmd_clk };

static int nna_fd;
static void *nna_nmap;

static void* gp_vaddr;
static void* gp_paddr;

static MEM_CTRL input_d_mem;
static MEM_CTRL output_d_mem;

void* xreg_open(unsigned int offset) {
  int fd;
  void *result;

  fd = open("/dev/mem", 2050);
  nna_fd = fd;
  if ( fd < 0 )
    return (void *)printf("open(/dev/mem) failed.[%x]", offset);
  result = mmap(0, 0x20000u, 3, 1, fd, offset);
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

signed int nna_configure(nna_cmds cmd, unsigned int value) {

  int fd;
  volatile uint32_t *mem;
  signed int ret = 0;

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

void nna_reset()
{
  nna_configure(nna_cmd_reset, 0);
}


signed int nna_wait_event_done(int event_mask, int event_value,
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

void nna_bias_mem_relu() {

  // Perform bias and relu on input data, bias values read from memory

  int w = 8; /* Width */
  int h = 8; /* Height */
  int c = 1; /* Channels */
  int line = w*c;
  int surface = w*h*c;

  void* tmp_paddr;
  void* tmp_vaddr;

  printf ("Running test %s ...\n", __FUNCTION__);

  mem_init(&input_d_mem);
  mem_alloc(&input_d_mem, 2048);

  mem_init(&output_d_mem);
  mem_alloc(&output_d_mem, 2048);

  memset(input_d_mem.data_addr,0,2048);
  memset(output_d_mem.data_addr,0,2048);

  // Generate test data with + & - numbers as multipliers of 4
  // Negative numbers will disappear with relu
  // Postive number should have bias applied
  for (int i=0;i<32;i++) {
    ((int8_t *)input_d_mem.data_addr)[i*2] = i*4;
    ((int8_t *)input_d_mem.data_addr)[(i*2)+1] = -i*4;
  }

  // Set bias values at next 1K block
  for(int i=0;i<64;i++) {
    ((int8_t *)input_d_mem.data_addr)[(i+1024)] = 3;
  }
  // Need to allocate some physical memory for NNA to use
  dma_mem_alloc(4096, (&tmp_vaddr), (&tmp_paddr));

  gp_paddr = tmp_paddr;
  gp_vaddr = tmp_vaddr;

  printf("gp_vaddr %x gp_paddr %x\n",(uint32_t)gp_vaddr,(uint32_t)gp_paddr);

  // Split memory in two halfs one input & other for output
  dma_loadin((char*)input_d_mem.data_addr, 2048, (uint32_t)(gp_paddr));
  dma_loadin((char*)output_d_mem.data_addr, 2048, ((uint32_t)(gp_paddr)+2048));

  memset(input_d_mem.data_addr,0,2048);
  dma_loadout((uint32_t)gp_paddr, 2048, (char*)input_d_mem.data_addr);
  printf("input >>>>>>> \n");
  for (int j=0;j<16;j++) {
    printf("{");
    for (int i=0;i<8;i++) {
      printf("%7d",((int8_t *)input_d_mem.data_addr)[(j*8)+i]);
      if (i<7) {
        printf(",");
      }
    }
   printf("}\n");
  }

  xregw(0x800c,w-1);                       // SDP_RDMA_D_DATA_CUBE_WIDTH_0
  xregw(0x8010,h-1);                       // SDP_RDMA_D_DATA_CUBE_HEIGHT_0
  xregw(0x8014,c-1);                       // SDP_RDMA_D_DATA_CUBE_CHANNEL_0
  xregw(0x8018,(uint32_t) gp_paddr);       // SDP_RDMA_D_SRC_BASE_ADDR_LOW_0
  xregw(0x801c,0x0);                       // SDP_RDMA_D_SRC_BASE_ADDR_HIGH_0
  xregw(0x8020,line);                      // SDP_RDMA_D_SRC_LINE_STRIDE_0
  xregw(0x8024,surface);                   // SDP_RDMA.D_SRC_SURFACE_STRIDE_0

  xregw(0x8028,0x32);                      // SDP_RDMA_D_BRDMA_CFG_0 BRDMA_DATA_MODE=PER_ELEMENT, BRDMA_DATA_SIZE=TWO_BYTE, BRDMA_DATA_USE=ALU, BRDMA_DISABLE=NO
  xregw(0x802c,(uint32_t)gp_paddr+1024);   // SDP_RDMA_D_BS_BASE_ADDR_LOW_0 Bias values from memory
  xregw(0x8030,0x0);                       // SDP_RDMA_D_BS_BASE_ADDR_HIGH_0
  xregw(0x8034,line);                      // SDP_RDMA_D_BS_LINE_STRIDE_0
  xregw(0x8038,surface);                   // SDP_RDMA_D_BS_SURFACE_STRIDE_0
  xregw(0x803c,0);

  xregw(0x8070,0x0);                       // SDP_RDMA_D_FEATURE_MODE_CFG_0 INT8
  xregw(0x8074,0x1);                       // SDP_RDMA_D_SRC_DMA_CFG_0 Use MC

  xregw(0x903c,w-1);                       // SDP_D_DATA_CUBE_WIDTH_0
  xregw(0x9040,h-1);                       // SDP_D_DATA_CUBE_HEIGHT_0
  xregw(0x9044,c-1);                       // SDP_D_DATA_CUBE_CHANNEL_0
  xregw(0x9048, (uint32_t)gp_paddr+2048);  // SDP_D_DST_BASE_ADDR_LOW_0 Output
  xregw(0x904c,0x0);                       // SDP_D_DST_BASE_ADDR_HIGH_0
  xregw(0x9050,line);                      // SDP_D_DST_LINE_STRIDE_0
  xregw(0x9054,surface);                   // SDP_D_DST_SURFACE_STRIDE_08*8*2

  xregw(0x9058,0x18);                      // SDP_D_DP_BS_CFG_0 BS_BYPASS=NO, BS_ALU_BYPASS=NO, BS_ALU_ALGO=SUM, BS_MUL_BYPASS=YES, BS_RELU_BYPASS=NO
  xregw(0x905c,0x1);                       // SDP_D_DP_BS_ALU_CFG_0 SHIFT VALUE=1, SRC=MEM
  xregw(0x9060,0x0);                       // SDP_D_DP_BS_ALU_SRC_VALUE_0
  xregw(0x9064,0x0);                       // SDP_D_DP_BS_MUL_CFG_0 SHIFT_VALUE=0, SRC=REG
  xregw(0x9068,0x1);                       // SDP_D_DP_BS_MUL_SRC_VALUE_0

  xregw(0x90b0,0x0);                       // SDP_D_FEATURE_MODE_CFG_0 FLYING_MODE=OFF, OUTPUT_DST=MEM, WINOGRAD=OFF, BATCH_NUMBER=0
  xregw(0x90b4,0x1);                       // SDP_D_DST_DMA_CFG_0 Use MC
  xregw(0x90b8,0x0);                       // SDP_D_DST_BATCH_STRIDE_0
  xregw(0x90bc,0x0);                       // SDP_D_DATA_FORMAT_0  INPUT_DATA=INT8, OUTPUT_DATA=INT8
  xregw(0x90c0,0x0);                       // SDP_D_CVT_OFFSET_0
  xregw(0x90c4,0x1);                       // SDP_D_CVT_SCALE_0 SCALE=1
  xregw(0x90c8,0x0);                       // SDP_D_CVT_SHIFT_0

  xregw(0x906c,0x5b);                      // SDP_D_DP_BN_CFG_0 BS_BYPASS=YES, BS_ALU_BYPASS=YES, BS_ALU_ALGO=SUM, BS_MUL_BYPASS=YES, BS_RELU_BYPASS=YES
  xregw(0x9080,0x5b);                      // SDP_D_DP_EW_CFG_0 BS_BYPASS=YES, BS_ALU_BYPASS=YES, BS_ALU_ALGO=SUM, BS_MUL_BYPASS=YES, BS_RELU_BYPASS=YES

  xregw(0x8008,0x1);                       // SDP_RDMA.D_OP_ENABLE_0
  xregw(0x9038,0x1);                       // SDP_D_OP_ENABLE_0

  nna_wait_done(0x1, 0x1);

  dma_loadout(((uint32_t)gp_paddr)+2048, 2048, (char*)output_d_mem.data_addr);
  printf("output >>>>>>> \n");
  for (int j=0;j<16;j++) {
    printf("{");
    for (int i=0;i<8;i++) {
      printf("%7d",((int8_t *)output_d_mem.data_addr)[(j*8)+i]);
      if (i<7) {
        printf(",");
      }
    }
    printf("}\n");
  }
  mem_free(&input_d_mem);
  mem_deinit(&input_d_mem);
  mem_free(&output_d_mem);
  mem_deinit(&output_d_mem);
  dma_mem_free(gp_vaddr);
}

void nna_bias_relu() {

  // Perform bias (with static value) and relu on input data

  int w = 8; /* Width */
  int h = 8; /* Height */
  int c = 1; /* Channels */
  int line = w*c;
  int surface = w*h*c;
  uint8_t bias = 1;  /* Bias value */

  void* tmp_paddr;
  void* tmp_vaddr;

  printf ("Running test %s ...\n", __FUNCTION__);

  mem_init(&input_d_mem);
  mem_alloc(&input_d_mem, 2048);

  mem_init(&output_d_mem);
  mem_alloc(&output_d_mem, 2048);

  memset(input_d_mem.data_addr,0,2048);
  memset(output_d_mem.data_addr,0,2048);

  // Generate test data with + & - numbers as multipliers of 4
  // Negative numbers will disappear with relu
  // Postive number should have bias applied
  for (int i=0;i<32;i++) {
    ((int8_t *)input_d_mem.data_addr)[i*2] = i*4;
    ((int8_t *)input_d_mem.data_addr)[(i*2)+1] = -i*4;
  }

  // Need to allocate some physical memory for NNA to use
  dma_mem_alloc(4096, (&tmp_vaddr), (&tmp_paddr));

  gp_paddr = tmp_paddr;
  gp_vaddr = tmp_vaddr;

  printf("gp_vaddr %x gp_paddr %x\n",(uint32_t)gp_vaddr,(uint32_t)gp_paddr);

  // Split memory in two halfs one input & other for output
  dma_loadin((char*)input_d_mem.data_addr, 2048, (uint32_t)(gp_paddr));
  dma_loadin((char*)output_d_mem.data_addr, 2048, ((uint32_t)(gp_paddr)+2048));

  memset(input_d_mem.data_addr,0,2048);
  dma_loadout((uint32_t)gp_paddr, 2048, (char*)input_d_mem.data_addr);
  printf("input >>>>>>> \n");
  for (int j=0;j<16;j++) {
    printf("{");
    for (int i=0;i<8;i++) {
      printf("%7d",((int8_t *)input_d_mem.data_addr)[(j*8)+i]);
      if (i<7) {
        printf(",");
      }
    }
   printf("}\n");
  }

  xregw(0x800c,w-1);                       // SDP_RDMA_D_DATA_CUBE_WIDTH_0
  xregw(0x8010,h-1);                       // SDP_RDMA_D_DATA_CUBE_HEIGHT_0
  xregw(0x8014,c-1);                       // SDP_RDMA_D_DATA_CUBE_CHANNEL_0
  xregw(0x8018,(uint32_t) gp_paddr);       // SDP_RDMA_D_SRC_BASE_ADDR_LOW_0
  xregw(0x801c,0x0);                       // SDP_RDMA_D_SRC_BASE_ADDR_HIGH_0
  xregw(0x8020,line);                      // SDP_RDMA_D_SRC_LINE_STRIDE_0
  xregw(0x8024,surface);                   // SDP_RDMA.D_SRC_SURFACE_STRIDE_0

  xregw(0x8028,0x13);                      // SDP_RDMA_D_BRDMA_CFG_0 BRDMA_DATA_MODE=PER_ELEMENT, BRDMA_DATA_SIZE=TWO_BYTE, BRDMA_DATA_USE=ALU, BRDMA_DISABLE=YES
  xregw(0x802c,0x0);                       // SDP_RDMA_D_BS_BASE_ADDR_LOW_0 Bias value in register
  xregw(0x8030,0x0);                       // SDP_RDMA_D_BS_BASE_ADDR_HIGH_0
  xregw(0x8034,line);                      // SDP_RDMA_D_BS_LINE_STRIDE_0
  xregw(0x8038,surface);                   // SDP_RDMA_D_BS_SURFACE_STRIDE_0
  xregw(0x803c,0);

  xregw(0x8070,0x0);                       // SDP_RDMA_D_FEATURE_MODE_CFG_0 INT8
  xregw(0x8074,0x1);                       // SDP_RDMA_D_SRC_DMA_CFG_0 Use MC

  xregw(0x903c,w-1);                       // SDP_D_DATA_CUBE_WIDTH_0
  xregw(0x9040,h-1);                       // SDP_D_DATA_CUBE_HEIGHT_0
  xregw(0x9044,c-1);                       // SDP_D_DATA_CUBE_CHANNEL_0
  xregw(0x9048, (uint32_t)gp_paddr+2048);  // SDP_D_DST_BASE_ADDR_LOW_0 Output
  xregw(0x904c,0x0);                       // SDP_D_DST_BASE_ADDR_HIGH_0
  xregw(0x9050,line);                      // SDP_D_DST_LINE_STRIDE_0
  xregw(0x9054,surface);                   // SDP_D_DST_SURFACE_STRIDE_08*8*2

  xregw(0x9058,0x18);                      // SDP_D_DP_BS_CFG_0 BS_BYPASS=NO, BS_ALU_BYPASS=NO, BS_ALU_ALGO=SUM, BS_MUL_BYPASS=YES, BS_RELU_BYPASS=NO
  xregw(0x905c,0x2);                       // SDP_D_DP_BS_ALU_CFG_0 SHIFT VALUE=1, SRC=REG
  xregw(0x9060,bias);                      // SDP_D_DP_BS_ALU_SRC_VALUE_0
  xregw(0x9064,0x0);                       // SDP_D_DP_BS_MUL_CFG_0 SHIFT_VALUE=0, SRC=REG
  xregw(0x9068,0x1);                       // SDP_D_DP_BS_MUL_SRC_VALUE_0

  xregw(0x90b0,0x0);                       // SDP_D_FEATURE_MODE_CFG_0 FLYING_MODE=OFF, OUTPUT_DST=MEM, WINOGRAD=OFF, BATCH_NUMBER=0
  xregw(0x90b4,0x1);                       // SDP_D_DST_DMA_CFG_0 Use MC
  xregw(0x90b8,0x0);                       // SDP_D_DST_BATCH_STRIDE_0
  xregw(0x90bc,0x0);                       // SDP_D_DATA_FORMAT_0  INPUT_DATA=INT8, OUTPUT_DATA=INT8
  xregw(0x90c0,0x0);                       // SDP_D_CVT_OFFSET_0
  xregw(0x90c4,0x1);                       // SDP_D_CVT_SCALE_0 SCALE=1
  xregw(0x90c8,0x0);                       // SDP_D_CVT_SHIFT_0

  xregw(0x906c,0x5b);                      // SDP_D_DP_BN_CFG_0 BS_BYPASS=YES, BS_ALU_BYPASS=YES, BS_ALU_ALGO=SUM, BS_MUL_BYPASS=YES, BS_RELU_BYPASS=YES
  xregw(0x9080,0x5b);                      // SDP_D_DP_EW_CFG_0 BS_BYPASS=YES, BS_ALU_BYPASS=YES, BS_ALU_ALGO=SUM, BS_MUL_BYPASS=YES, BS_RELU_BYPASS=YES

  xregw(0x8008,0x1);                       // SDP_RDMA.D_OP_ENABLE_0
  xregw(0x9038,0x1);                       // SDP_D_OP_ENABLE_0

  nna_wait_done(0x1, 0x1);

  dma_loadout(((uint32_t)gp_paddr)+2048, 2048, (char*)output_d_mem.data_addr);
  printf("output >>>>>>> \n");
  for (int j=0;j<16;j++) {
    printf("{");
    for (int i=0;i<8;i++) {
      printf("%7d",((int8_t *)output_d_mem.data_addr)[(j*8)+i]);
      if (i<7) {
        printf(",");
      }
    }
    printf("}\n");
  }
  mem_free(&input_d_mem);
  mem_deinit(&input_d_mem);
  mem_free(&output_d_mem);
  mem_deinit(&output_d_mem);
  dma_mem_free(gp_vaddr);
}

int main(int argc, char **argv) {

  hw_init();

  // Set clock to 400Mhz
  nna_configure(nna_cmd_clk, 400);

  // Turn on NNA
  nna_on();

  // Map NNA registers
  void* r = xreg_open(NNA_BASE);
  if (r) {
    printf("nmap 0x2400000u ok\n");

    nna_reset();
    nna_bias_relu();
    nna_reset();
    nna_bias_mem_relu();
    xreg_close();
  }

  nna_off();

  hw_deinit();
}
