/*
 * Copyright (C) 2021  Jasbir Matharu, <jasknuj@gmail.com>
 * Copyright (C) 2010-2018 Arm Limited. All rights reserved.
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
 * cifar10 implementation based on the CMSIS_5 NN example (see
 * https://github.com/ARM-software/CMSIS_5) It provides a good reference point
 * for verifying that the correct register settings are being applied for int8
 * conv,sdp,pdp operations and output data is correct.
 *
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "nna_hw.h"
#include "nna_interface.h"
#include "nna_config.h"

#include "ion_alloc.h"

#include "nna_cifar10_weights.h"
#include "nna_cifar10_image.h"

#define CONV1_IM_DIM 32
#define CONV1_IM_CH 3
#define CONV1_KER_DIM 5
#define CONV1_PADDING 2
#define CONV1_STRIDE 1
#define CONV1_OUT_CH 32
#define CONV1_OUT_DIM 32

#define CONV1_BIAS_LSHIFT 6
#define CONV1_OUT_RSHIFT 9

#define POOL1_KER_DIM 3
#define POOL1_STRIDE 2
#define POOL1_PADDING 0
#define POOL1_OUT_DIM 16

#define CONV2_IM_DIM 16
#define CONV2_IM_CH 32
#define CONV2_KER_DIM 5
#define CONV2_PADDING 2
#define CONV2_STRIDE 1
#define CONV2_OUT_CH 16
#define CONV2_OUT_DIM 16

#define POOL2_KER_DIM 3
#define POOL2_STRIDE 2
#define POOL2_PADDING 0
#define POOL2_OUT_DIM 8

#define CONV2_BIAS_LSHIFT 4
#define CONV2_OUT_RSHIFT 9

#define CONV3_IM_DIM 8
#define CONV3_IM_CH 16
#define CONV3_KER_DIM 5
#define CONV3_PADDING 2
#define CONV3_STRIDE 1
#define CONV3_OUT_CH 32
#define CONV3_OUT_DIM 8

#define POOL3_KER_DIM 3
#define POOL3_STRIDE 2
#define POOL3_PADDING 0
#define POOL3_OUT_DIM 4

#define CONV3_BIAS_LSHIFT 1
#define CONV3_OUT_RSHIFT 7

#define CONV4_IM_DIM 4
#define CONV4_IM_CH 32
#define CONV4_KER_DIM 4
#define CONV4_PADDING 0
#define CONV4_STRIDE 1
#define CONV4_OUT_CH 10
#define CONV4_OUT_DIM 1

#define CONV4_BIAS_LSHIFT 1
#define CONV4_OUT_RSHIFT 8

static void* gp_vaddr;
static void* gp_paddr;

static int8_t image_data[8 * 32 * 32] = IMG_DATA;

static int8_t conv1_nhwc_wt[3 * 5 * 5 * 32] = CONV1_WT;
static int16_t conv1_bias[32] = CONV1_BIAS;

static int8_t conv2_nhwc_wt[16 * 5 * 5 * 32] = CONV2_WT;
static int16_t conv2_bias[16] = CONV2_BIAS;

static int8_t conv3_nhwc_wt[16 * 5 * 5 * 32] = CONV3_WT;
static int16_t conv3_bias[32] = CONV3_BIAS;

static int8_t conv4_nhwc_wt[32 * 4 * 4 * 10] = CONV4_WT;
static int16_t conv4_bias[10] = CONV4_BIAS;

static int8_t scratch_buffer[65536];

static char labels[][13] = {"airplane","automobile","bird","cat","deer","dog","frog","horse","ship","truck"};

/**
  * Signed Saturate
 */
int32_t ssat(int32_t val, uint32_t sat) {
  if ((sat >= 1U) && (sat <= 32U))
  {
    const int32_t max = (int32_t)((1U << (sat - 1U)) - 1U);
    const int32_t min = -1 - max ;
    if (val > max)
    {
      return max;
    }
    else if (val < min)
    {
      return min;
    }
  }
  return val;
}

/**
  * Unsigned Saturate
 */
uint32_t usat(int32_t val, uint32_t sat)
{
  if (sat <= 31U)
  {
    const uint32_t max = ((1U << sat) - 1U);
    if (val > (int32_t)max)
    {
      return max;
    }
    else if (val < 0)
    {
      return 0U;
    }
  }
  return (uint32_t)val;
}

void run_conv_pool(nna_conv_op_desc* conv_op,
                   nna_conv_surface_desc* conv_surface,
                   nna_sdp_op_desc* sdp_op,
                   nna_sdp_surface_desc* sdp_surface,
                   nna_pdp_op_desc* pdp_op,
                   nna_pdp_surface_desc* pdp_surface) {

  // Perform convolution and pooling operation in one step

  nna_conv_set_producer(0,0);
  nna_sdp_set_producer(0,0);
  nna_pdp_set_producer(0,0);

  nna_conv_program(conv_op,conv_surface);
  nna_sdp_program(sdp_op,sdp_surface);
  nna_pdp_program(pdp_op,pdp_surface);

  nna_conv_enable(0,0);
  nna_sdp_enable(0,1);
  nna_pdp_enable(0,1);

  nna_wait_done(0x150011,0x150011);
  nna_reset();
}

void run_conv(nna_conv_op_desc* conv_op,
              nna_conv_surface_desc* conv_surface,
              nna_sdp_op_desc* sdp_op,
              nna_sdp_surface_desc* sdp_surface) {

  // Perform convolution

  nna_conv_set_producer(0,0);
  nna_sdp_set_producer(0,0);

  nna_conv_program(conv_op,conv_surface);
  nna_sdp_program(sdp_op,sdp_surface);

  nna_conv_enable(0,0);
  nna_sdp_enable(0,1);

  nna_wait_done(0x150001,0x150001);
  nna_reset();
}

void set_conv(int in_dim,
              int in_c,
              int k_dim,
              int k,
              int out_dim,
              int pad,
              int stride,
              uint32_t in_offset,
              uint32_t wgt_offset,
              nna_conv_op_desc* conv_op,
              nna_conv_surface_desc* conv_surface ) {

  int in_w = in_dim; // Input width
  int in_h = in_dim; // Input height
  int k_w  = k_dim;  // Kernel width
  int k_h  = k_dim;  // Kernel height

  int entry_per_slice;
  int weight_bytes;

  // Calculate output size
  int out_w = ((in_w - k_w + pad + pad) / stride) + 1;
  int out_h = ((in_h - k_h + pad + pad) / stride) + 1;

  // For int8 channelsPerGroup is atomic k
  int channelsPerGroup = NNA_ATOMIC_K_SIZE;

  memset(conv_op,0,sizeof(nna_conv_op_desc));
  memset(conv_surface,0,sizeof(nna_conv_surface_desc));

  conv_surface->src_data.width = in_w;
  conv_surface->src_data.height = in_h;
  conv_surface->src_data.channel = in_c;
  conv_surface->src_data.address =  (uint32_t)(gp_paddr)+in_offset;

  // For format feature input need to multiple by channelsPerGroup
  conv_surface->src_data.line_stride = channelsPerGroup * conv_surface->src_data.width;
  conv_surface->src_data.surf_stride = conv_surface->src_data.height * channelsPerGroup * conv_surface->src_data.width ;

  conv_surface->weight_data.width = k_w;
  conv_surface->weight_data.height = k_h;
  conv_surface->weight_data.channel = conv_surface->src_data.channel;
  conv_surface->weight_data.address = (uint32_t)(gp_paddr)+wgt_offset;

  // Input data is feature format
  conv_op->data_format = FORMAT_FEATURE;

  conv_op->input_width_csc = in_w;
  conv_op->input_height_csc = in_h;
  conv_op->input_channel_csc = in_c;

  conv_op->stride_x = stride;
  conv_op->stride_y = stride;

  // Default to 1
  conv_op->dilation_x = 1;
  conv_op->dilation_y = 1;

  conv_op->pad_x_left = pad;
  conv_op->pad_x_right = pad;
  conv_op->pad_y_top = pad;
  conv_op->pad_y_bottom = pad;

  conv_op->kernel_width_csc = conv_surface->weight_data.width;
  conv_op->kernel_height_csc = conv_surface->weight_data.height;
  conv_op->kernel_channel_csc = conv_surface->src_data.channel;

  conv_surface->dst_data.width = out_w;
  conv_surface->dst_data.height = out_h;
  conv_surface->dst_data.channel = k;

  conv_op->input_width_cmac = conv_surface->dst_data.width;
  conv_op->input_height_cmac = conv_surface->dst_data.height;

  // Calculate entries per slice
  entry_per_slice = calculate_eps(conv_op,conv_surface);

  conv_op->entry_per_slice = entry_per_slice;

  conv_op->bytes_per_kernel = conv_surface->weight_data.channel * conv_surface->weight_data.width *
    conv_surface->weight_data.height;

  // Total weight bytes need to padded to next 32 byte boundary
  weight_bytes = (k * conv_op->bytes_per_kernel) + 31;

  conv_surface->weight_data.size= weight_bytes;

  conv_op->data_bank = calculate_data_bank(conv_op,conv_surface);
  conv_op->weight_bank = calculate_weight_bank(conv_surface);

  // For feature format we can leave release as zero
  conv_op->release = 0;

}

void set_bias_relu(int in_dim,
                   int k,
                   int out_dim,
                   uint8_t out_shift,
                   uint8_t bias_shift,
                   uint32_t bias_offset,
                   nna_sdp_op_desc* sdp_op,
                   nna_sdp_surface_desc* sdp_surface) {

  // For int8 channelsPerGroup is atomic k
  int channelsPerGroup = NNA_ATOMIC_K_SIZE;

  int in_w = in_dim;
  int in_h = in_dim;
  int out_w = out_dim;
  int out_h = out_dim;

  memset(sdp_op,0,sizeof(nna_sdp_op_desc));
  memset(sdp_surface,0,sizeof(nna_sdp_surface_desc));

  sdp_surface->src_data.address = 0; // input from conv hw
  sdp_surface->src_data.width = in_w;
  sdp_surface->src_data.height = in_h;
  sdp_surface->src_data.channel = k;
  sdp_surface->src_data.line_stride = channelsPerGroup * sdp_surface->src_data.width;
  sdp_surface->src_data.surf_stride = sdp_surface->src_data.height * channelsPerGroup * sdp_surface->src_data.width;

  sdp_surface->dst_data.address = 0; // destination is pdp
  sdp_surface->dst_data.width = out_w;
  sdp_surface->dst_data.height = out_h;
  sdp_surface->dst_data.channel = k;
  sdp_surface->dst_data.line_stride = channelsPerGroup * sdp_surface->dst_data.width;
  sdp_surface->dst_data.surf_stride = sdp_surface->dst_data.height * channelsPerGroup * sdp_surface->dst_data.width;

  // Default to 1 as minimum
  sdp_op->out_cvt.scale=1;
  sdp_op->out_cvt.truncate=out_shift;

  // Bypass x1
  sdp_op->x1_op.enable = 1;
  sdp_op->x1_op.type = SDP_OP_ADD;
  sdp_op->x1_op.alu_type = SDP_ALU_OP_SUM;
  sdp_op->x1_op.shift_value = bias_shift;
  sdp_op->x1_op.truncate = 0;

  sdp_op->x1_op.mode = SDP_OP_PER_KERNEL;
  sdp_surface->x1_data.address = (uint32_t)(gp_paddr)+bias_offset;

  sdp_op->x1_op.act = ACTIVATION_RELU;

  // Bypass x2
  sdp_op->x2_op.enable = 0;

  // Bypass y
  sdp_op->y_op.enable = 0;
}

void set_bias(int in_dim,
              int k,
              int out_dim,
              uint8_t out_shift,
              uint8_t bias_shift,
              uint32_t out_offset,
              uint32_t bias_offset,
              nna_sdp_op_desc* sdp_op,
              nna_sdp_surface_desc* sdp_surface) {

  // For int8 channelsPerGroup is atomic k
  int channelsPerGroup = NNA_ATOMIC_K_SIZE;

  int in_w = in_dim;
  int in_h = in_dim;
  int out_w = out_dim;
  int out_h = out_dim;

  memset(sdp_op,0,sizeof(nna_sdp_op_desc));
  memset(sdp_surface,0,sizeof(nna_sdp_surface_desc));

  sdp_surface->src_data.address = 0; // input from conv hw
  sdp_surface->src_data.width = in_w;
  sdp_surface->src_data.height = in_h;
  sdp_surface->src_data.channel = k;
  sdp_surface->src_data.line_stride = channelsPerGroup * sdp_surface->src_data.width;
  sdp_surface->src_data.surf_stride = sdp_surface->src_data.height * channelsPerGroup * sdp_surface->src_data.width;

  sdp_surface->dst_data.address = (uint32_t)(gp_paddr)+out_offset;; // destination is memory
  sdp_surface->dst_data.width = out_w;
  sdp_surface->dst_data.height = out_h;
  sdp_surface->dst_data.channel = k;
  sdp_surface->dst_data.line_stride = channelsPerGroup * sdp_surface->dst_data.width;
  sdp_surface->dst_data.surf_stride = sdp_surface->dst_data.height * channelsPerGroup * sdp_surface->dst_data.width;

  // Default to 1 as minimum
  sdp_op->out_cvt.scale=1;
  sdp_op->out_cvt.truncate=out_shift;

  // Bypass x1
  sdp_op->x1_op.enable = 1;
  sdp_op->x1_op.type = SDP_OP_ADD;
  sdp_op->x1_op.alu_type = SDP_ALU_OP_SUM;
  sdp_op->x1_op.shift_value = bias_shift;
  sdp_op->x1_op.truncate = 0;

  sdp_op->x1_op.mode = SDP_OP_PER_KERNEL;
  sdp_surface->x1_data.address = (uint32_t)(gp_paddr)+bias_offset;
  // Bias value is int16 so mutliple by 2
  sdp_surface->x1_data.line_stride = sdp_surface->src_data.line_stride *2;
  sdp_surface->x1_data.surf_stride = sdp_surface->src_data.height * channelsPerGroup * sdp_surface->src_data.width *2;

  // Bypass x2
  sdp_op->x2_op.enable = 0;

  // Bypass y
  sdp_op->y_op.enable = 0;
}

void set_max_pool(int in_dim,
                  int k_dim,
                  int k,
                  int out_dim,
                  int pad,
                  int stride,
                  uint32_t out_offset,
                  nna_pdp_op_desc* pdp_op,
                  nna_pdp_surface_desc* pdp_surface) {

  // For int8 channelsPerGroup is atomic k
  int channelsPerGroup = NNA_ATOMIC_K_SIZE;

  memset(pdp_op,0,sizeof(nna_pdp_op_desc));
  memset(pdp_surface,0,sizeof(nna_pdp_surface_desc));

  pdp_op->split_num = 1;
  pdp_op->pool_mode = POOL_MODE_MAX;
  pdp_op->pool_width = k_dim;
  pdp_op->pool_height = k_dim;
  pdp_op->stride_x = stride;
  pdp_op->stride_y = stride;
  pdp_op->pad_left =  pad;
  pdp_op->pad_top = pad;
  pdp_op->pad_right = pad;
  pdp_op->pad_bottom = pad;

  int in_w = in_dim;
  int in_h = in_dim;
  int out_w = out_dim;
  int out_h = out_dim;

  pdp_op->pad_right = ((out_w - 1)*pdp_op->stride_x +   pdp_op->pool_width) - (in_w + pdp_op->pad_left);
  pdp_op->pad_bottom = ((out_h - 1)*pdp_op->stride_y + pdp_op->pool_height) - (in_h + pdp_op->pad_top);

  pdp_surface->src_data.address = 0; // input is from sdp
  pdp_surface->src_data.width = in_w;
  pdp_surface->src_data.height = in_h;
  pdp_surface->src_data.channel = k;
  pdp_surface->src_data.line_stride = channelsPerGroup * pdp_surface->src_data.width;
  pdp_surface->src_data.surf_stride = pdp_surface->src_data.height * channelsPerGroup * pdp_surface->src_data.width;

  pdp_surface->dst_data.address = (uint32_t)(gp_paddr)+out_offset;
  pdp_surface->dst_data.width = out_w;
  pdp_surface->dst_data.height = out_h;
  pdp_surface->dst_data.channel = k;
  pdp_surface->dst_data.line_stride = channelsPerGroup * pdp_surface->dst_data.width;
  pdp_surface->dst_data.surf_stride = pdp_surface->dst_data.height * channelsPerGroup * pdp_surface->dst_data.width;

}

void softmax_q7(int8_t *vec_in, uint16_t dim_vec, int8_t *p_out) {

  /*, Not your normal softmax as we use power of 2 softmax here, i.e.,:
   *
   *  y_i = 2^(x_i) / sum(2^x_j)
   *
   *  The relative output will be different here. But mathematically, the
   *  gradient will be the same with a log(2) scaling factor.
   */

  int32_t sum;
  int16_t i;
  uint8_t shift;
  int16_t base;
  base = -128;

  /* We first search for the maximum */
  for (i = 0; i < dim_vec; i++)
  {
    if (vec_in[i] > base)
    {
      base = vec_in[i];
    }
  }

  /*
   * So the base is set to max-8, meaning
   * that we ignore really small values.
   * anyway, they will be 0 after shrinking to q7 format.
   */
  base = base - (1 << 3);

  sum = 0;

  for (i = 0; i < dim_vec; i++) {
    shift = usat(vec_in[i] - base, 3);
    sum += 0x1 << shift;
  }

  /* This is effectively (0x1 << 20) / sum */
  int output_base = (1 << 20) / sum;

  for (i = 0; i < dim_vec; i++) {
    /* Here minimum value of 13+base-vec_in[i] will be 5 */
    shift = usat(13 + base - vec_in[i], 5);
    p_out[i] = ssat((output_base >> shift), 8);
  }
}

void cifar10() {

  nna_conv_op_desc conv_op;
  nna_conv_surface_desc conv_surface;
  nna_sdp_op_desc sdp_op;
  nna_sdp_surface_desc sdp_surface;

  nna_pdp_op_desc pdp_op;
  nna_pdp_surface_desc pdp_surface;

  printf ("Running %s ...\n", __FUNCTION__);

  sunxi_ion_alloc_open();
  sunxi_ion_alloc_palloc(0x80000,&gp_vaddr,&gp_paddr);

  uint32_t  wgt_offset  = 0x00000; // weight data
  uint32_t  bias_offset = 0x20000; // bias data
  uint32_t  buf1_offset = 0x40000; // temp buffer for input/output data
  uint32_t  buf2_offset = 0x60000; // temp buffer for input/output data

  sunxi_ion_loadin((char*)image_data, sizeof(image_data), (uint32_t)(gp_paddr)+buf1_offset);
  sunxi_ion_loadin((char*)conv1_nhwc_wt, sizeof(conv1_nhwc_wt), ((uint32_t)(gp_paddr)+wgt_offset));
  sunxi_ion_loadin((char*)conv1_bias, sizeof(conv1_bias), ((uint32_t)(gp_paddr)+bias_offset));

  // 1st layer
  set_conv(CONV1_IM_DIM,
           CONV1_IM_CH,
           CONV1_KER_DIM,
           CONV1_OUT_CH,
           CONV1_OUT_DIM,
           CONV1_PADDING,
           CONV1_STRIDE,
           buf1_offset,
           wgt_offset,
           &conv_op,
           &conv_surface);

  set_bias_relu(CONV1_OUT_DIM,
                CONV1_OUT_CH,
                CONV1_OUT_DIM,
                CONV1_OUT_RSHIFT,
                CONV1_BIAS_LSHIFT,
                bias_offset,
                &sdp_op,
                &sdp_surface);

  set_max_pool(CONV1_OUT_DIM,
               POOL1_KER_DIM,
               CONV1_OUT_CH,
               POOL1_OUT_DIM,
               POOL1_PADDING,
               POOL1_STRIDE,
               buf2_offset,
               &pdp_op,
               &pdp_surface);

  run_conv_pool(&conv_op,
                &conv_surface,
                &sdp_op,
                &sdp_surface,
                &pdp_op,
                &pdp_surface);

  // 2nd layer
  sunxi_ion_loadin((char*)conv2_nhwc_wt, sizeof(conv2_nhwc_wt), ((uint32_t)(gp_paddr)+wgt_offset));
  sunxi_ion_loadin((char*)conv2_bias, sizeof(conv2_bias), ((uint32_t)(gp_paddr)+bias_offset));

  set_conv(CONV2_IM_DIM,
           CONV2_IM_CH,
           CONV2_KER_DIM,
           CONV2_OUT_CH,
           CONV2_OUT_DIM,
           CONV2_PADDING,
           CONV2_STRIDE,
           buf2_offset,
           wgt_offset,
           &conv_op,
           &conv_surface);

  set_bias_relu(CONV2_OUT_DIM,
                CONV2_OUT_CH,
                CONV2_OUT_DIM,
                CONV2_OUT_RSHIFT,
                CONV2_BIAS_LSHIFT,
                bias_offset,
                &sdp_op,
                &sdp_surface);

  set_max_pool(CONV2_OUT_DIM,
               POOL2_KER_DIM,
               CONV2_OUT_CH,
               POOL2_OUT_DIM,
               POOL2_PADDING,
               POOL2_STRIDE,
               buf1_offset,
               &pdp_op,
               &pdp_surface);

  run_conv_pool(&conv_op,
                &conv_surface,
                &sdp_op,
                &sdp_surface,
                &pdp_op,
                &pdp_surface);

  // 3rd layer
  sunxi_ion_loadin((char*)conv3_nhwc_wt, sizeof(conv3_nhwc_wt), ((uint32_t)(gp_paddr)+wgt_offset));
  sunxi_ion_loadin((char*)conv3_bias, sizeof(conv3_bias), ((uint32_t)(gp_paddr)+bias_offset));


  set_conv(CONV3_IM_DIM,
           CONV3_IM_CH,
           CONV3_KER_DIM,
           CONV3_OUT_CH,
           CONV3_OUT_DIM,
           CONV3_PADDING,
           CONV3_STRIDE,
           buf1_offset,
           wgt_offset,
           &conv_op,
           &conv_surface);

  set_bias_relu(CONV3_OUT_DIM,
                CONV3_OUT_CH,
                CONV3_OUT_DIM,
                CONV3_OUT_RSHIFT,
                CONV3_BIAS_LSHIFT,
                bias_offset,
                &sdp_op,
                &sdp_surface);

  set_max_pool(CONV3_OUT_DIM,
               POOL3_KER_DIM,
               CONV3_OUT_CH,
               POOL3_OUT_DIM,
               POOL3_PADDING,
               POOL3_STRIDE,
               buf2_offset,
               &pdp_op,
               &pdp_surface);

  run_conv_pool(&conv_op,
                &conv_surface,
                &sdp_op,
                &sdp_surface,
                &pdp_op,
                &pdp_surface);

  sunxi_ion_loadin((char*)conv4_nhwc_wt, sizeof(conv4_nhwc_wt), ((uint32_t)(gp_paddr)+wgt_offset));
  sunxi_ion_loadin((char*)conv4_bias, sizeof(conv4_bias), ((uint32_t)(gp_paddr)+bias_offset));

  set_conv(CONV4_IM_DIM,
           CONV4_IM_CH,
           CONV4_KER_DIM,
           CONV4_OUT_CH,
           CONV4_OUT_DIM,
           CONV4_PADDING,
           CONV4_STRIDE,
           buf2_offset,
           wgt_offset,
           &conv_op,
           &conv_surface);

  set_bias(CONV4_OUT_DIM,
           CONV4_OUT_CH,
           CONV4_OUT_DIM,
           CONV4_OUT_RSHIFT,
           CONV4_BIAS_LSHIFT,
           buf1_offset,
           bias_offset,
           &sdp_op,
           &sdp_surface);

  run_conv(&conv_op,
           &conv_surface,
           &sdp_op,
           &sdp_surface);

  // Result is 1x1x10 cube
  sunxi_ion_loadout(((uint32_t)gp_paddr)+buf1_offset, CONV4_OUT_DIM*CONV4_OUT_DIM*CONV4_OUT_CH, (char*)scratch_buffer);

  // Clear some space in scratch buffer for the softmax result starting at
  // position 20
  memset((int8_t*)scratch_buffer+20,0,CONV4_OUT_DIM*CONV4_OUT_DIM*CONV4_OUT_CH);
  softmax_q7((int8_t*)scratch_buffer,CONV4_OUT_DIM*CONV4_OUT_DIM*CONV4_OUT_CH,(int8_t*)scratch_buffer+20);

  for (int c=1;c<CONV4_OUT_CH;c++) {
       int8_t value = scratch_buffer[20+c];
       printf("%-12s : %4d\n",labels[c],value);
  }
  printf("\n");

  sunxi_ion_alloc_free();
  sunxi_ion_alloc_close();
}

int main(int argc, char **argv) {

  // Set clock to 400Mhz (DDR2 memory speed ??)
  nna_configure(nna_cmd_clk, 400);

  // Turn on NNA
  nna_on();

  // Map NNA registers
  void* r = xreg_open();
  if (r) {
    nna_reset();
    cifar10();
    xreg_close();
  }

  nna_off();

}
