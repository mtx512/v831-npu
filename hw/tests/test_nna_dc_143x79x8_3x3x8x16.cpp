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
#include "nna_interface.h"

extern void nna_conv_set_producer(uint32_t group_id, uint32_t rdma_group_id);
extern void nna_conv_enable(uint8_t enable_stats);
extern int nna_conv_program(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface);

extern void nna_sdp_set_producer(uint32_t group_id, uint32_t rdma_group_id);
extern void nna_sdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed);
extern int nna_sdp_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface);

static void* gp_vaddr;
static void* gp_paddr;

static MEM_CTRL input_d_mem;
static MEM_CTRL output_d_mem;
static MEM_CTRL weights_d_mem;

void nna_dc_143x79x8_3x3x8x16() {

  // Perform DC convolution using feature data as the input format (int8).
  // The input cube is 143x79x8 and weights consist of 16 kernels each a 3x3x8 cube.
  // The resulting output cube should be 141x77x16, however see comment below as
  // output is split into 2 blocks.
  // This is a very basic test so no padding, stride, dilation, bias, relu, etc.

  nna_conv_op_desc conv_op;
  nna_conv_surface_desc conv_surface;
  nna_sdp_op_desc sdp_op;
  nna_sdp_surface_desc sdp_surface;

  int entry_per_slice;
  int weight_bytes;

  int in_w = 143; /* Input width */
  int in_h = 79; /* Input height */
  int in_c = 8; /* Input channels */
  int k_w  = 3; /* Kernel width */
  int k_h  = 3; /* Kernel height */
  int k = 16; /* No of kernels */

  int in_bytes = in_w * in_h * in_c;
  int k_bytes = k_w * k_h * in_c * k;

  // No padding, stride or dilation to contended with
  int out_w = (in_w - k_w) + 1;
  int out_h = (in_h - k_h) + 1;

  int out_bytes = out_w * out_h * k;

  void* tmp_paddr;
  void* tmp_vaddr;

  printf ("Running test %s ...\n", __FUNCTION__);

  mem_init(&input_d_mem);
  mem_alloc(&input_d_mem, in_bytes);

	mem_init(&weights_d_mem);
	mem_alloc(&weights_d_mem, k_bytes);

	mem_init(&output_d_mem);
	mem_alloc(&output_d_mem, out_bytes);

  // Set input to all ones, easier for debugging
  memset(input_d_mem.data_addr,1,in_bytes);
  // Zero out all weights
  memset(weights_d_mem.data_addr,0,k_bytes);
  // Zero out output
  memset(output_d_mem.data_addr,0,out_bytes);

  // Set the first value in each 3x3 kernel, incrementing from 1
  for (int i=0;i<16;i++) {
    memset((uint8_t*)weights_d_mem.data_addr+(k_w*k_h*in_c*i),i+1,1);
  }

  // Allocate 512K of physical memory
	dma_mem_alloc(0x80000, (&tmp_vaddr), (&tmp_paddr));

  gp_paddr = tmp_paddr;
  gp_vaddr = tmp_vaddr;

  printf("gp_vaddr %x gp_paddr %x\n",(uint32_t)gp_vaddr,(uint32_t)gp_paddr);

  dma_loadin((char*)input_d_mem.data_addr, in_bytes, (uint32_t)(gp_paddr));
  dma_loadin((char*)weights_d_mem.data_addr, k_bytes, ((uint32_t)(gp_paddr)+0x20000));
  dma_loadin((char*)output_d_mem.data_addr, out_bytes, ((uint32_t)(gp_paddr)+0x40000));

  nna_conv_set_producer(0,0);
  nna_sdp_set_producer(0,0);

  // Configure conv parameters

  memset(&conv_op,0,sizeof(conv_op));
  memset(&conv_surface,0,sizeof(conv_surface));

  conv_surface.src_data.width = in_w;
  conv_surface.src_data.height = in_h;
  conv_surface.src_data.channel = in_c;
  conv_surface.src_data.address =  (uint32_t)(gp_paddr);

  // For format feature input need to multiple by 8 (not completely sure why??)
  conv_surface.src_data.line_stride = 8 * conv_surface.src_data.width;
  conv_surface.src_data.surf_stride = conv_surface.src_data.height * 8 * conv_surface.src_data.width ;

  conv_surface.weight_data.width = k_w;
  conv_surface.weight_data.height = k_h;
  conv_surface.weight_data.channel = conv_surface.src_data.channel;
  conv_surface.weight_data.address = (uint32_t)(gp_paddr)+0x20000;

  // Input data is feature format
  conv_op.data_format = FORMAT_FEATURE;

  conv_op.input_width_csc = in_w;
  conv_op.input_height_csc = in_h;
  conv_op.input_channel_csc = in_c;

  // Need to default to 1
  conv_op.stride_x = 1;
  conv_op.stride_y = 1;

  // Need to default to 1
  conv_op.dilation_x = 1;
  conv_op.dilation_y = 1;

  conv_op.pad_x_left = 0;
  conv_op.pad_x_right = 0;
  conv_op.pad_y_top = 0;
  conv_op.pad_y_bottom = 0;

  conv_op.kernel_width_csc = conv_surface.weight_data.width;
  conv_op.kernel_height_csc = conv_surface.weight_data.height;
  conv_op.kernel_channel_csc = conv_surface.src_data.channel;

  conv_surface.dst_data.width = out_w;
  conv_surface.dst_data.height = out_h;
  conv_surface.dst_data.channel = k;

  conv_op.input_width_cmac = conv_surface.dst_data.width;
  conv_op.input_height_cmac = conv_surface.dst_data.height;

  // Entry per slice is a calculated value, add calculation as some point
  entry_per_slice = 36;

  conv_op.entry_per_slice = entry_per_slice;

  conv_op.bytes_per_kernel = conv_surface.weight_data.channel * conv_surface.weight_data.width *
    conv_surface.weight_data.height;

  // total weight bytes need to padded to next 32 byte boundary
  weight_bytes = (k * conv_op.bytes_per_kernel) + 31;

  conv_surface.weight_data.size= weight_bytes;

  // data bank is 512 bytes and weight bank is 16K
  conv_op.data_bank = (uint32_t)(entry_per_slice * conv_surface.src_data.height + 511) >> 9; // divide by 512 bytes
  conv_op.weight_bank = ((weight_bytes & 0xFFFFFFE0) + 0x3FFF) >> 14; // divide by 16384 bytes

  // for feature format we can leave release as zero
  conv_op.release = 0;

  nna_conv_program(&conv_op,&conv_surface);

  // Configure SDP parameters

  memset(&sdp_op,0,sizeof(sdp_op));
  memset(&sdp_surface,0,sizeof(sdp_surface));

  sdp_surface.src_data.address = 0; // Input is from conv hw
  sdp_surface.src_data.width = out_w;
  sdp_surface.src_data.height = out_h;
  sdp_surface.src_data.channel = k;
  // Need to multiply by 8 for feature format (not sure why ??)
  sdp_surface.src_data.line_stride = 8 * sdp_surface.src_data.width;
  sdp_surface.src_data.surf_stride = sdp_surface.src_data.height * 8 * sdp_surface.src_data.width;

  sdp_surface.dst_data.address = (uint32_t)(gp_paddr)+0x40000;
  sdp_surface.dst_data.width = out_w;
  sdp_surface.dst_data.height = out_h;
  sdp_surface.dst_data.channel = k;
  // Need to multiply by 8 for feature format (not sure why ??)
  sdp_surface.dst_data.line_stride = 8 * sdp_surface.dst_data.width;
  sdp_surface.dst_data.surf_stride = sdp_surface.dst_data.height * 8 * sdp_surface.dst_data.width;

  // Default to 1 as minimum
  sdp_op.out_cvt.scale=1;

  // Bypass x1
  sdp_op.x1_op.enable = 0;

  // Bypass x2
  sdp_op.x2_op.enable = 0;

  // Bypass y
  sdp_op.y_op.enable = 0;

  nna_sdp_program(&sdp_op,&sdp_surface);

  nna_conv_enable(0);
  nna_sdp_enable(0,1);

  nna_wait_done(0x150001,0x150001);

  printf(">>>>>>> input\n");
  for (int i=0;i<32;i++) {
    printf("%2x ",((uint8_t *)input_d_mem.data_addr)[i]);
  }
  printf("\n");

  printf(">>>>>>> weights for 1st kernel (per channel)\n");
  for (int j=0;j<16;j++) {
    for (int i=0;i<9;i++) {
      printf("%2x ",((uint8_t *)weights_d_mem.data_addr)[(j*9*8)+i]);
    }
    printf("\n");
  }

  dma_loadout(((uint32_t)gp_paddr)+0x40000, out_bytes, (char*)output_d_mem.data_addr);

  // Ouput seems to be processed as two blocks, each block is 141x77x8
  // Hence display 1st and last 4K blocks as the output is split into 2 group of 8 channels
  // Need to create a secondary test to verify if this output can be fed as feature data
  // to another DC convolution.
  printf(">>>>>>> output\n");

  // display first 4k block
  printf(">>>>>>> First 4k block\n");
  for (int j=0;j<128;j++) {
    printf("%3d ",j);
    for (int i=0;i<32;i++) {
      printf("%2x ",((int8_t *)output_d_mem.data_addr)[(j*32)+i]);
    }
    printf("\n");
  }

  printf(">>>>>>> Last 4k block\n");
  // display last 4k block
  for (int j=0;j<128;j++) {
    printf("%3d ",j);
    for (int i=0;i<32;i++) {
      printf("%2x ",((int8_t *)output_d_mem.data_addr)[(j*32)+i+(out_bytes-4096)]);
    }
    printf("\n");
  }

  // Dump to file
  FILE *out = fopen("dc_143x79x8_3x3x8x16.out", "wb");
  if(out != NULL) {
   size_t to_go = out_bytes;
   while(to_go > 0) {
     const size_t wrote = fwrite(output_d_mem.data_addr, 1,to_go, out);
     if(wrote == 0)
       break;
     to_go -= wrote;
   }
   fclose(out);
 }


  mem_free(&input_d_mem);
  mem_deinit(&input_d_mem);
  mem_free(&output_d_mem);
  mem_deinit(&output_d_mem);
  mem_free(&weights_d_mem);
  mem_deinit(&weights_d_mem);

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
    nna_dc_143x79x8_3x3x8x16();
    xreg_close();
  }

  nna_off();

  hw_deinit();
}
