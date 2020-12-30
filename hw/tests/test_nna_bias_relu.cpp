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
#include <stdlib.h>

#include "hw_adaptor.h"
#include "mem_ctrl.h"

#include "nna_hw.h"

static void* gp_vaddr;
static void* gp_paddr;

static MEM_CTRL input_d_mem;
static MEM_CTRL output_d_mem;

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
  xregw(0x9054,surface);                   // SDP_D_DST_SURFACE_STRIDE_08*8*1

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
  xregw(0x9054,surface);                   // SDP_D_DST_SURFACE_STRIDE_08*8*1

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
  void* r = xreg_open();
  if (r) {
    printf("xreg_open ok\n");

    nna_reset();
    nna_bias_relu();
    nna_reset();
    nna_bias_mem_relu();
    xreg_close();
  }

  nna_off();

  hw_deinit();
}
