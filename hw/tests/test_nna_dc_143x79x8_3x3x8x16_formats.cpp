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
#include "nna_config.h"

extern void nna_conv_set_producer(uint32_t group_id, uint32_t rdma_group_id);
extern void nna_conv_enable(uint8_t enable_stats, uint8_t is_rdma_needed);
extern int nna_conv_program(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface);

extern void nna_sdp_set_producer(uint32_t group_id, uint32_t rdma_group_id);
extern void nna_sdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed);
extern int nna_sdp_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface);

static void* gp_vaddr;
static void* gp_paddr;

static MEM_CTRL input_d_mem;
static MEM_CTRL output_d_mem;
static MEM_CTRL weights_d_mem;

uint16_t calculate_eps(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface) {

  uint16_t eps = 0;
  uint8_t bpe = 1; // Bytes per element
  uint16_t memory_atomic_size = NNA_MEMORY_ATOMIC_SIZE;

  uint16_t channel = conv_surface->src_data.channel;
  uint16_t width = conv_surface->src_data.width;

  // Calculation of elements per slice is dependentant on input format
  if (conv_op->data_format == FORMAT_FEATURE) {
    // Pad channel atomics to the next memory boundary and  divide by memory_atomic_size
    uint16_t total_c_atomics = ((channel * bpe) + memory_atomic_size-1) >> 3 ;
    eps = width * (total_c_atomics >> 2);
    // For remainder channel atomics add additional width
    switch ( total_c_atomics & 3) {
      case 3:
        eps += width;
        break;
      case 2:
        eps += (width + 1) >> 1 ; // divide by 2
        break;
      case 1:
        eps +=  ((width + 3 >= 0) ? width+3:width+6) >> 2; // divide by 4
        break;
      default:
        break;
      }
  } else {
    // Add implementation for pixel formats
  }
  return eps;
}

uint32_t calculate_data_bank(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface) {
  // Data bank is 512 bytes
  return (uint32_t)(conv_op->entry_per_slice * conv_surface->src_data.height +
    NNA_CBUF_ENTRIES_PER_BANK-1) >> 9; // divide by 512 bytes
}

uint32_t calculate_weight_bank(nna_conv_surface_desc* conv_surface) {
   // Weight bank is 16K
   return ((conv_surface->weight_data.size & 0xFFFFFFE0) + 0x3FFF) >> 14; // divide by 16384 bytes
}

void set_input(uint16_t w, uint16_t h, uint8_t c, uint16_t in_w, uint16_t in_c, int8_t value, int8_t* feature_data ) {

  // Set an individual value within the 143x79x8 input cube

  // Feature data (packed) format is organised as 1x1x8 cubes following C'->W->H->C
  // where C' is a channel in a kernel group
  // Since our cube has 8 channels then we don't need to distrbute into kernel groups.

  uint32_t pos = ((h-1) * in_w * in_c)+ ((w-1)*in_c) + (c-1);

  *(feature_data+pos) = value;
}

int8_t get_output(uint16_t w, uint16_t h, uint8_t c, uint16_t out_w, uint16_t out_h, int8_t* feature_data) {

  // Return an individual element from the 141x77x16 output cube

  // Feature data (packed) format is organised as 1x1x8 cubes following C'->W->H->C
  // when C' is a channel in a kernel group.
  // Since our cube has 16 channels then we need to break cubes in kernel groups

  uint32_t start = 0; // Start of memory within kpg

  uint8_t c1 = c;
  int kpg = (c1-1) >> 3; // Divide by 8 to find kernel group
  if (kpg > 0 ) {
    start = kpg * out_w * out_h * 8; // Jump to start of memory for kernel group
    c1 = c - (kpg *8); // For 2nd kernel group subtract 8 from channel number
  }

  start = start+ out_w*(8*(h-1))+((w-1)*8)+c1-1;
  return *(feature_data+start);
}

void set_weight(uint16_t w, uint16_t h, uint8_t c, uint8_t k, uint8_t k_w, uint8_t k_h,
    uint8_t k_c, uint8_t value, int8_t* weights) {

  // Sets an individual weight within a 3x3x8 (whc) kernel for k kernel number.

  // Short descripton of how weights are organised, firstly the total number of kernels
  // need to be distributed into groups. Each group has 8 (NNA_ATOMIC_K_SIZE) kernels.
  // Last group can have less kernels. Each kernel needs to be broken down into
  // 1x1xc cubes, where c has a max of 32. If the number of channels in a single
  // kernel is greater than 32 then distribute into groups of 1x1xc. The 1x1xc cubes
  // are then laid out WHC per kernel group,

  // In this test the 3x3x8 kernels are split into 1x1x8 cubes (we have 8 channels)
  // and kernel groups is 2 (number of kernels / 8).

  // Get number of kernel groups
  uint8_t kgs = ((k-1) >> 3);
  // Convert kernel WxH (3x3) coordinate to a value between 0 to 8
  uint8_t pos = ((h-1)*k_w)+(w-1);
  // Locate the row using kgs & pos, each row contains 8 * 1x1x8 bytes
  uint16_t row = (kgs*8* k_w * k_h * k_c) + ( 8 * k_c * pos);
  // Locate the kernel number within the row
  uint16_t col = row + (( k > 8 ? k-9: k-1)*8);
  // Locate the channel within the 1x1x8 cube
  uint32_t ch = col + (c-1);

  *(weights+ch)=value;
}

void dump_to_file(char* fname, uint32_t size, void * data) {
  FILE *out = fopen(fname, "wb");
  if(out != NULL) {
    size_t to_go = size;
    while(to_go > 0) {
      const size_t wrote = fwrite(data, 1,to_go, out);
      if(wrote == 0)
        break;
      to_go -= wrote;
    }
    fclose(out);
  }
}

void nna_dc_143x79x8_3x3x8x16_formats() {

  // Perform DC convolution using feature data as the input format (int8).
  // The input cube is 143x79x8 and weights consist of 16 kernels each a 3x3x8 cube.
  // The resulting output cube will be 141x77x16.
  // This test demonstrates how feature format data and weight data is structed
  // in memory, therefore we only utilise a 3x3x8 section of input cube even though
  // the input cube is 143x79x8. It can be used a testing tool to validate
  // the input, weight and output data is correct.
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

  // For int8 channelsPerGroup is atomic k
  int channelsPerGroup = NNA_ATOMIC_K_SIZE;

  void* tmp_paddr;
  void* tmp_vaddr;

  uint16_t w1;
  uint16_t h1;
  uint8_t k1;
  uint8_t c1;

  printf ("Running test %s ...\n", __FUNCTION__);

  mem_init(&input_d_mem);
  mem_alloc(&input_d_mem, in_bytes);

	mem_init(&weights_d_mem);
	mem_alloc(&weights_d_mem, k_bytes);

	mem_init(&output_d_mem);
	mem_alloc(&output_d_mem, out_bytes);

  // Zero out all input data
  memset(input_d_mem.data_addr,0,in_bytes);
  // Zero out all weights
  memset(weights_d_mem.data_addr,0,k_bytes);
  // Zero out output
  memset(output_d_mem.data_addr,0,out_bytes);

  // Set first 3x3x8 part of the input cube to 1 for this test
  for (h1=1;h1<4;h1++) {
    for (w1=1;w1<4;w1++) {
      for (c1=1;c1<9;c1++) {
        set_input(w1, h1, c1, in_w, in_c, 1, (int8_t*) input_d_mem.data_addr );
      }
    }
  }

  // Set 1st kernel to all ones, and 2nd kernel to -1
  k1 = 1;
  for (h1=1;h1<4;h1++) {
    for (w1=1;w1<4;w1++) {
      for (c1=1;c1<9;c1++) {
        set_weight(w1, h1, c1, k1, k_w, k_h, in_c, 1, (int8_t*)weights_d_mem.data_addr);
        set_weight(w1, h1, c1, k1+1, k_w, k_h, in_c, -1, (int8_t*)weights_d_mem.data_addr);
      }
    }
  }

  // 3rd kernel interlaced 0 and 1
  k1 = 3;
  for (h1=1;h1<4;h1++) {
    for (w1=1;w1<4;w1++) {
      for (c1=1;c1<9;c1++) {
        int8_t value = (c1%2 ==0) ? 1 : 0;
        set_weight(w1, h1, c1, k1, k_w, k_h, in_c, value, (int8_t*)weights_d_mem.data_addr);
      }
    }
  }

 // 4th kernel set last channel weights to 1
 k1 = 4;
 for (h1=1;h1<4;h1++) {
   for (w1=1;w1<4;w1++) {
       set_weight(w1, h1, 8, k1, k_w, k_h, in_c, 1, (int8_t*)weights_d_mem.data_addr);
   }
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

  // For format feature input need to multiple by channelsPerGroup
  conv_surface.src_data.line_stride = channelsPerGroup * conv_surface.src_data.width;
  conv_surface.src_data.surf_stride = conv_surface.src_data.height * channelsPerGroup * conv_surface.src_data.width ;

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

  // Calculate entries per slice
  entry_per_slice = calculate_eps(&conv_op,&conv_surface);

  conv_op.entry_per_slice = entry_per_slice;

  conv_op.bytes_per_kernel = conv_surface.weight_data.channel * conv_surface.weight_data.width *
    conv_surface.weight_data.height;

  // total weight bytes need to padded to next 32 byte boundary
  weight_bytes = (k * conv_op.bytes_per_kernel) + 31;

  conv_surface.weight_data.size= weight_bytes;

  conv_op.data_bank = calculate_data_bank(&conv_op,&conv_surface);
  conv_op.weight_bank = calculate_weight_bank(&conv_surface);

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
  sdp_surface.src_data.line_stride = channelsPerGroup * sdp_surface.src_data.width;
  sdp_surface.src_data.surf_stride = sdp_surface.src_data.height * channelsPerGroup * sdp_surface.src_data.width;

  sdp_surface.dst_data.address = (uint32_t)(gp_paddr)+0x40000;
  sdp_surface.dst_data.width = out_w;
  sdp_surface.dst_data.height = out_h;
  sdp_surface.dst_data.channel = k;
  sdp_surface.dst_data.line_stride = channelsPerGroup * sdp_surface.dst_data.width;
  sdp_surface.dst_data.surf_stride = sdp_surface.dst_data.height * channelsPerGroup * sdp_surface.dst_data.width;

  // Default to 1 as minimum
  sdp_op.out_cvt.scale=1;

  // Bypass x1
  sdp_op.x1_op.enable = 0;

  // Bypass x2
  sdp_op.x2_op.enable = 0;

  // Bypass y
  sdp_op.y_op.enable = 0;

  nna_sdp_program(&sdp_op,&sdp_surface);

  printf("0x9058u %8x\n",xregr(0x9058u));

  nna_conv_enable(0,0);
  nna_sdp_enable(0,1);

  nna_wait_done(0x150001,0x150001);

  printf(">>>>>>> input\n");
  for (int i=0;i<32;i++) {
    printf("%2x ",((uint8_t *)input_d_mem.data_addr)[i]);
  }
  printf("\n");

  printf(">>>>>>> weights for the first 8 kernels and for channesl 1 & 2\n");
  for (int j=0;j<16;j++) {
    for (int i=0;i<8;i++) {
      printf("%2x ",((uint8_t *)weights_d_mem.data_addr)[(j*8)+i]);
    }
    printf("\n");
  }

  dma_loadout(((uint32_t)gp_paddr)+0x40000, out_bytes, (char*)output_d_mem.data_addr);

  // Ouput is two 141x77x8 cubes
  printf(">>>>>>> output\n");

  // printout first 3x3 cube from each channel
  for (c1=1;c1<5;c1++) {
    printf("3x3 cube channel %d\n",c1);
    for (h1=1;h1<4;h1++) {
      for (w1=1;w1<4;w1++) {
       int8_t value = get_output(w1, h1, c1, out_w, out_h, (int8_t *)output_d_mem.data_addr);
       printf("%4d ",value);
      }
      printf("\n");
    }
  }

  printf("\n");

  // Dump to files
 dump_to_file((char*)"dc_143x79x8_3x3x8x16_formats.in", in_bytes, input_d_mem.data_addr);
 dump_to_file((char*)"dc_143x79x8_3x3x8x16_formats.wgt", k_bytes, weights_d_mem.data_addr);
 dump_to_file((char*)"dc_143x79x8_3x3x8x16_formats.out", out_bytes, output_d_mem.data_addr);

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
    nna_dc_143x79x8_3x3x8x16_formats();
    xreg_close();
  }

  nna_off();

  hw_deinit();
}
