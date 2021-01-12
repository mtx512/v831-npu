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

#include "nna_hw.h"
#include "nna_interface.h"

void nna_sdp_set_producer(uint32_t group_id, uint32_t rdma_group_id) {

  xregw(0x9004u, group_id);
  xregw(0x8004u, rdma_group_id);
}

void nna_sdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed) {

  if (enable_stats) {
    xregw(0x90dcu, 0x0f);
  }

  if (is_rdma_needed) {
    xregw(0x8008u, 0x01);
  }

  xregw(0x9038u, 0x01);
}

void processor_x1_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface) {

  struct nna_sdp_op *x1_op;

  uint8_t bypass;
  uint8_t alu_bypass;
  uint8_t mul_bypass;
  uint8_t alu_algo;
  uint8_t mul_prelu;
  uint8_t relu_bypass;
  uint8_t alu_src;

  // Configure x1

  x1_op = &sdp_op->x1_op;

  bypass =  !x1_op->enable;
  alu_bypass = (x1_op->type == SDP_OP_MUL) || (x1_op->type == SDP_OP_NONE);
  mul_bypass = (x1_op->type == SDP_OP_ADD  || x1_op->type == SDP_OP_NONE);
  alu_algo = x1_op->alu_type & 0x03;
  mul_prelu = x1_op->act == ACTIVATION_PRELU;
  relu_bypass =  !(x1_op->act == ACTIVATION_RELU);

  xregw(0x9058u,  bypass | alu_bypass << 1 | alu_algo << 2 | mul_bypass << 4 |
    mul_prelu <<5 | relu_bypass << 6 ); // SDP_D_DP_BS_CFG_0

  if (x1_op->enable) {

    // Check if we need to perform addition
    if (x1_op->type == SDP_OP_ADD || x1_op->type == SDP_OP_BOTH) {
      // Use reg if per layer
      alu_src = (x1_op->mode != SDP_OP_PER_LAYER);
      xregw(0x905cu, alu_src | x1_op->shift_value << 8); // SDP_D_DP_BS_ALU_CFG_0
    }

    /* config x1 input source */
    if (x1_op->mode == SDP_OP_PER_LAYER) {
      xregw(0x9060u,x1_op->alu_operand); // SDP_D_DP_BS_ALU_SRC_VALUE_0
      xregw(0x9068u,x1_op->mul_operand); // SDP_D_DP_BS_MUL_SRC_VALUE_0
    } else {
      if (sdp_surface->x1_data.address) {
        xregw(0x802Cu, sdp_surface->x1_data.address);     // SDP_RDMA_D_BS_BASE_ADDR_LOW_0
        xregw(0x8034u, sdp_surface->x1_data.line_stride);
        xregw(0x8038u, sdp_surface->x1_data.surf_stride);
        xregw(0x803Cu, 0);
      } else {
        xregw(0x802Cu, 0);
      }
    }

    // Trucate value always takes effect
    xregw(0x9064u, alu_src | x1_op->truncate << 8); // SDP_D_DP_BS_MUL_CFG_0
  }
}

void processor_x2_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface) {

  struct nna_sdp_op *x2_op;

  uint8_t bypass;
  uint8_t alu_bypass;
  uint8_t mul_bypass;
  uint8_t alu_algo;
  uint8_t mul_prelu;
  uint8_t relu_bypass;
  uint8_t alu_src;

  // Configure x2

  x2_op = &sdp_op->x2_op;

  bypass =  !x2_op->enable;
  alu_bypass = (x2_op->type == SDP_OP_MUL) || (x2_op->type == SDP_OP_NONE);
  mul_bypass = (x2_op->type == SDP_OP_ADD  || x2_op->type == SDP_OP_NONE);
  alu_algo = x2_op->alu_type & 0x03;
  mul_prelu = x2_op->act == ACTIVATION_PRELU;
  relu_bypass =  !(x2_op->act == ACTIVATION_RELU);
  xregw(0x906Cu,  bypass | alu_bypass << 1 | alu_algo << 2 | mul_bypass << 4 |
    mul_prelu <<5 | relu_bypass << 6 ); // SDP_D_DP_BN_CFG_0

  if (x2_op->enable) {

    // Check if we need to perform addition
    if (x2_op->type == SDP_OP_ADD || x2_op->type == SDP_OP_BOTH) {
      // Use reg if per layer
      alu_src = (x2_op->mode != SDP_OP_PER_LAYER);
      xregw(0x9070u, alu_src | x2_op->shift_value << 8); // SDP_D_DP_BN_ALU_CFG_0
    }

    /* config x2 source */
    if (x2_op->mode == SDP_OP_PER_LAYER) {
      xregw(0x9074u,x2_op->alu_operand); // SDP_D_DP_BN_ALU_SRC_VALUE_0
      xregw(0x907cu,x2_op->mul_operand); // SDP_D_DP_BN_MUL_SRC_VALUE_0
    } else {
      if (sdp_surface->x2_data.address) {
        xregw(0x8044u, sdp_surface->x2_data.address);     // SDP_RDMA_D_BN_BASE_ADDR_LOW_0
        xregw(0x804Cu, sdp_surface->x2_data.line_stride);
        xregw(0x8050u, sdp_surface->x2_data.surf_stride);
        xregw(0x8054u, 0);
      } else {
        xregw(0x8044u, 0);
      }
    }
    // Trucate value always takes effect
    xregw(0x9078u, alu_src | x2_op->truncate << 8); // SDP_D_DP_BN_MUL_CFG_0
  }
}

void processor_y_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface) {

  struct nna_sdp_op *y_op;

  uint8_t bypass;
  uint8_t alu_bypass;
  uint8_t mul_bypass;
  uint8_t alu_algo;
  uint8_t mul_prelu;
  uint8_t lut_bypass;
  uint8_t alu_src;
  uint8_t mul_src;


  y_op = &sdp_op->y_op;

  // Not sure if y is present or not for small config, so bypass until we can test it
  y_op->enable = 0;

  bypass =  !y_op->enable;
  alu_bypass = (y_op->type == SDP_OP_MUL) || (y_op->type == SDP_OP_NONE);
  mul_bypass = (y_op->type == SDP_OP_ADD) || (y_op->type == SDP_OP_NONE);
  alu_algo = y_op->alu_type & 0x03;
  mul_prelu = y_op->act == ACTIVATION_PRELU;
  lut_bypass = 1; // For now bypass
  xregw(0x9080u,  bypass | alu_bypass << 1 | alu_algo << 2 | mul_bypass << 4 |
    mul_prelu <<5 | lut_bypass << 6 ); // SDP_D_DP_EW_CFG_0

  if (y_op->enable) {

    // Check if we need to perform addition
    if (y_op->type == SDP_OP_ADD || y_op->type == SDP_OP_BOTH) {
      // Use reg if per layer
      alu_src = (y_op->mode != SDP_OP_PER_LAYER);
      xregw(0x9084u, alu_src | (!y_op->cvt.alu_cvt.enable) << 1); // SDP_D_DP_EW_ALU_CFG_0

      if (y_op->mode == SDP_OP_PER_LAYER) {
        xregw(0x9088u,y_op->alu_operand); // SDP_D_DP_EW_ALU_SRC_VALUE_0
      } else {
        xregw(0x908cu,y_op->cvt.alu_cvt.offset);   // SDP_D_DP_EW_ALU_CVT_OFFSET_VALUE_0
        xregw(0x9090u,y_op->cvt.alu_cvt.scale);    // SDP_D_DP_EW_ALU_CVT_SCALE_VALUE_0
        xregw(0x9094u,y_op->cvt.alu_cvt.truncate); // SDP_D_DP_EW_ALU_CVT_TRUNCATE_VALUE_0
      }
    }

    // Check if we need to perform multipication
    if (y_op->type == SDP_OP_MUL || y_op->type == SDP_OP_BOTH) {
      // Use reg if per layer
      mul_src = (y_op->mode != SDP_OP_PER_LAYER);
      xregw(0x9098u, mul_src | (!y_op->cvt.mul_cvt.enable) << 1); // SDP_D_DP_EW_MUL_CFG_0

      if (y_op->mode == SDP_OP_PER_LAYER) {
        xregw(0x909cu,y_op->mul_operand); // SDP_D_DP_EW_MUL_SRC_VALUE_0
      } else {
        xregw(0x90a0u,y_op->cvt.mul_cvt.offset);   // SDP_D_DP_EW_MUL_CVT_OFFSET_VALUE_0
        xregw(0x90a4u,y_op->cvt.mul_cvt.scale);    // SDP_D_DP_EW_MUL_CVT_SCALE_VALUE_0
        xregw(0x90a8u,y_op->cvt.mul_cvt.truncate); // SDP_D_DP_EW_MUL_CVT_TRUNCATE_VALUE_0
      }
    }

    /* config y source address if required */
    if (y_op->mode != SDP_OP_PER_LAYER) {
      if (sdp_surface->y_data.address) {
        xregw(0x805Cu, sdp_surface->y_data.address);     // SDP_RDMA_D_EW_BASE_ADDR_LOW_0
        xregw(0x8064u, sdp_surface->y_data.line_stride);
        xregw(0x8068u, sdp_surface->y_data.surf_stride);
        xregw(0x806Cu, 0);
      } else {
        xregw(0x805Cu,0);
      }
    }

    xregw(0x90ac,y_op->truncate); // SDP_D_DP_EW_TRUNCATE_VALUE_0

  }
}

int processor_sdp_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface) {

  struct nna_sdp_op *x1_op;
  struct nna_sdp_op *x2_op;
  struct nna_sdp_op *y_op;

  uint8_t fly_mode;
  uint8_t output_dst;

  x1_op = &sdp_op->x1_op;
  x2_op = &sdp_op->x2_op;
  y_op = &sdp_op->y_op;

  fly_mode = sdp_surface->src_data.address == 0;
  output_dst = sdp_surface->dst_data.address == 0; // Memory or PDP

  xregw(0x8070u, fly_mode);                         // SDP_RDMA_D_FEATURE_MODE_CFG_0
  xregw(0x8074u, 1u);                               // SDP_RDMA_D_SRC_DMA_CFG_0

  xregw(0x800Cu, sdp_surface->src_data.width - 1);   // SDP_RDMA_D_DATA_CUBE_WIDTH_0
  xregw(0x8010u, sdp_surface->src_data.height - 1);  // SDP_RDMA_D_DATA_CUBE_HEIGHT_0
  xregw(0x8014u, sdp_surface->src_data.channel - 1); // SDP_RDMA_D_DATA_CUBE_CHANNEL_0

  if (!fly_mode) {
    xregw(0x8018u, sdp_surface->src_data.address);     // SDP_RDMA_D_SRC_BASE_ADDR_LOW_0
    xregw(0x8020u, sdp_surface->src_data.line_stride); // SDP_RDMA_D_SRC_LINE_STRIDE_0
    xregw(0x8024u, sdp_surface->src_data.surf_stride); // SDP_RDMA_D_SRC_SURFACE_STRIDE_0
  }

  if (sdp_surface->dst_data.address) {
    xregw(0x9048u, sdp_surface->dst_data.address);     // SDP_D_DST_BASE_ADDR_LOW_0
    xregw(0x9050u, sdp_surface->dst_data.line_stride); // SDP_D_DST_LINE_STRIDE_0
    xregw(0x9054u, sdp_surface->dst_data.surf_stride); // SDP_D_DST_SURFACE_STRIDE_0
    xregw(0x90B4u, 1u);                                // SDP_D_DST_DMA_CFG_0
  }

  xregw(0x903Cu, sdp_surface->src_data.width - 1);    // SDP_D_DATA_CUBE_WIDTH_0
  xregw(0x9040u, sdp_surface->src_data.height - 1);   // SDP_D_DATA_CUBE_HEIGHT_0
  xregw(0x9044u, sdp_surface->src_data.channel - 1);  // SDP_D_DATA_CUBE_CHANNEL_0
  xregw(0x90B0u, fly_mode | (output_dst << 1));       // SDP_D_FEATURE_MODE_CFG
  xregw(0x90BCu, 0);                                  // SDP_D_DATA_FORMAT_0_0 int8
  xregw(0x90C0u, sdp_op->out_cvt.offset);             // SDP_D_CVT_OFFSET_0
  xregw(0x90C4u, sdp_op->out_cvt.scale);              // SDP_D_CVT_SCALE_0
  xregw(0x90C8u, sdp_op->out_cvt.truncate);           // SDP_D_CVT_SHIFT_0

  processor_x1_program(sdp_op,sdp_surface);
  processor_x2_program(sdp_op,sdp_surface);
  processor_y_program(sdp_op,sdp_surface);

  return 0;
}

int nna_sdp_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface) {
  return processor_sdp_program(sdp_op,sdp_surface);
}
