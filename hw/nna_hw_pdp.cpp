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
#include <stdint.h>

#include "nna_hw.h"
#include "nna_interface.h"

/* The reciprocal of kernel width: 1/1, 1/2, 1/3, ... */
static const uint32_t recip_kernel_size[8] =
	/*
	 * INT8
	 * 1      1/2     1/3     1/4     1/5     1/6     1/7     1/8
	 */
	{0x10000, 0x8000, 0x5555, 0x4000, 0x3333, 0x2aaa, 0x2492, 0x2000};

void nna_pdp_set_producer(uint32_t group_id, uint32_t rdma_group_id) {

  xregw(0xB004u, group_id);  // PDP_S_POINTER_0
  xregw(0xA004u, rdma_group_id); // PDP_RDMA_S_POINTER_0
}

void nna_pdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed) {

  if (enable_stats) {
    xregw(0xB094u, 0x01); // PDP_D_PERF_ENABLE_0
  }

  if (is_rdma_needed) {
    xregw(0xA008u, 0x01); // PDP_RDMA_D_OP_ENABLE_0
  }

  xregw(0xB008u, 0x01); // PDP_D_OP_ENABLE_0
}

int processor_pdp_program(nna_pdp_op_desc* pdp_op, nna_pdp_surface_desc* pdp_surface) {


  uint8_t fly_mode_off;

  fly_mode_off = pdp_surface->src_data.address != 0;

  xregw(0xA018u, fly_mode_off);  // PDP_RDMA_D_FLYING_MODE_0  (input from sdp or memory)

  if (fly_mode_off) {
    xregw(0xA00Cu, pdp_surface->src_data.width - 1); // PDP_RDMA_D_DATA_CUBE_IN_WIDTH_0
    xregw(0xA010u, pdp_surface->src_data.height - 1); // PDP_RDMA_D_DATA_CUBE_IN_HEIGHT_0
    xregw(0xA014u, pdp_surface->src_data.channel - 1); // PDP_RDMA_D_DATA_CUBE_IN_CHANNEL_0
    xregw(0xA01Cu, pdp_surface->src_data.address);  // PDP_RDMA_D_SRC_BASE_ADDR_LOW_0
    xregw(0xA024u, pdp_surface->src_data.line_stride);  // PDP_RDMA_D_SRC_LINE_STRIDE_0
    xregw(0xA028u, pdp_surface->src_data.surf_stride);  // PDP_RDMA_D_SRC_SURFACE_STRIDE_0
    xregw(0xA02Cu, 1u); // PDP_RDMA_D_SRC_RAM_CFG_0 (from MC)
    xregw(0xA030u, 0u); //PDP_RDMA_D_DATA_FORMAT_0 (int8)
    xregw(0xA038u, (pdp_op->pool_width - 1) | ((pdp_op->stride_x - 1) << 4));  // PDP_RDMA_D_POOLING_KERNEL_CFG_0
    xregw(0xA03Cu, pdp_op->pad_left); // PDP_RDMA_D_POOLING_PADDING_CFG_0
    xregw(0xB068u, pdp_surface->src_data.line_stride); // PDP_D_SRC_LINE_STRIDE_0
    xregw(0xB06Cu, pdp_surface->src_data.surf_stride); // PDP_D_SRC_SURFACE_STRIDE_0
  }

  xregw(0xB00Cu, pdp_surface->src_data.width - 1); // PDP_D_DATA_CUBE_IN_WIDTH_0
  xregw(0xB010u, pdp_surface->src_data.height - 1); // PDP_D_DATA_CUBE_IN_HEIGHT_0
  xregw(0xB014u, pdp_surface->src_data.channel - 1); // PDP_D_DATA_CUBE_IN_CHANNEL_0
  xregw(0xB018u, pdp_surface->dst_data.width - 1); // PDP_D_DATA_CUBE_OUT_WIDTH_0
  xregw(0xB01Cu, pdp_surface->dst_data.height - 1); // PDP_D_DATA_CUBE_OUT_HEIGHT_0
  xregw(0xB020u, pdp_surface->dst_data.channel - 1); // PDP_D_DATA_CUBE_OUT_CHANNEL_0

  xregw(0xB024u, pdp_op->pool_mode | (fly_mode_off << 4) | ((pdp_op->split_num - 1) << 8));  // PDP_D_OPERATION_MODE_CFG_0
  xregw(0xB034u, (pdp_op->pool_width - 1) | ((pdp_op->pool_height - 1) << 8) | ((pdp_op->stride_x - 1) << 16 ) |
    (pdp_op->stride_y - 1) <<20 ); // PDP_D_POOLING_KERNEL_CFG_0

  xregw(0xB038u, recip_kernel_size[pdp_op->pool_width-1]); // PDP_D_RECIP_KERNEL_WIDTH_0
  xregw(0xB03Cu, recip_kernel_size[pdp_op->pool_height-1]); // PDP_D_RECIP_KERNEL_HEIGHT_0
//  xregw(0xB038u, pdp_op->pool_width-1); // PDP_D_RECIP_KERNEL_WIDTH_0
//  xregw(0xB03Cu, pdp_op->pool_height-1); // PDP_D_RECIP_KERNEL_HEIGHT_0

  xregw(0xB040u, pdp_op->pad_left | (pdp_op->pad_top << 4) | (pdp_op->pad_right << 8) |
    (pdp_op->pad_bottom << 12)); // PDP_D_POOLING_PADDING_CFG_0

  // Set all padding values to zeros for (now)
  xregw(0xB044u, 0); // PDP_D_POOLING_PADDING_VALUE_1_CFG_0
  xregw(0xB048u, 0); // PDP_D_POOLING_PADDING_VALUE_2_CFG_0
  xregw(0xB04Cu, 0); // PDP_D_POOLING_PADDING_VALUE_3_CFG_0
  xregw(0xB050u, 0); // PDP_D_POOLING_PADDING_VALUE_4_CFG_0
  xregw(0xB054u, 0); // PDP_D_POOLING_PADDING_VALUE_5_CFG_0
  xregw(0xB058u, 0); // PDP_D_POOLING_PADDING_VALUE_6_CFG_0
  xregw(0xB05Cu, 0); // PDP_D_POOLING_PADDING_VALUE_7_CFG_0

  xregw(0xB028u, 0); // PDP_D_NAN_FLUSH_TO_ZERO_0 ( 0 - disable, 1 - enable )

  xregw(0xB070u, pdp_surface->dst_data.address);  // PDP_D_DST_BASE_ADDR_LOW_0
  xregw(0xB074u, 0); // PDP_D_DST_BASE_ADDR_HIGH_0
  xregw(0xB078u, pdp_surface->dst_data.line_stride);  // PDP_D_DST_LINE_STRIDE_0
  xregw(0xB07Cu, pdp_surface->dst_data.surf_stride);  // PDP_D_DST_SURFACE_STRIDE_0
  xregw(0xB080u, 1u); // PDP_D_DST_RAM_CFG_0  (set to MC)
  xregw(0xB084u, 0); // PDP_D_DATA_FORMAT_0 (set to int8)

  return 0;
}

int nna_pdp_program(nna_pdp_op_desc* pdp_op, nna_pdp_surface_desc* pdp_surface) {
  return processor_pdp_program(pdp_op,pdp_surface);
}
