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

void naa_sdp_set_producer(uint32_t group_id, uint32_t rdma_group_id) {

  xregw(0x9004u, group_id);
  xregw(0x8004u, rdma_group_id);
}

void naa_sdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed) {

  if (enable_stats) {
    xregw(0x90dcu, 0x0f);
  }

  if (is_rdma_needed) {
    xregw(0x8008u, 0x01);
  }

  xregw(0x9038u, 0x01);
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

  /* config x1 source */
  if (sdp_surface->x1_data.address) {
    xregw(0x802Cu, sdp_surface->x1_data.address);     // SDP_RDMA_D_BS_BASE_ADDR_LOW_0
    xregw(0x8034u, sdp_surface->x1_data.line_stride);
    xregw(0x8038u, sdp_surface->x1_data.surf_stride);
    xregw(0x803Cu, 0);
  } else {
    xregw(0x802Cu, 0);
  }

  /* config x2 source */
  if (sdp_surface->x2_data.address) {
    xregw(0x8044u, sdp_surface->x2_data.address);     // SDP_RDMA_D_BN_BASE_ADDR_LOW_0
    xregw(0x804Cu, sdp_surface->x2_data.line_stride);
    xregw(0x8050u, sdp_surface->x2_data.surf_stride);
    xregw(0x8054u, 0);
  } else {
    xregw(0x8044u, 0);
  }

  /* config y source */
  if (sdp_surface->y_data.address) {
    xregw(0x805Cu, sdp_surface->y_data.address);     // NNA_SDP_RDMA_D_EW_BASE_ADDR_LOW_0
    xregw(0x8064u, sdp_surface->y_data.line_stride);
    xregw(0x8068u, sdp_surface->y_data.surf_stride);
    xregw(0x806Cu, 0);
  } else {
    xregw(0x805Cu,0);
  }

  xregw(0x903Cu, sdp_surface->src_data.width - 1);    // SDP_D_DATA_CUBE_WIDTH_0
  xregw(0x9040u, sdp_surface->src_data.height - 1);   // SDP_D_DATA_CUBE_HEIGHT_0
  xregw(0x9044u, sdp_surface->src_data.channel - 1);  // SDP_D_DATA_CUBE_CHANNEL_0
  xregw(0x90B0u, fly_mode | (output_dst << 1));       // SDP_D_FEATURE_MODE_CFG
  xregw(0x90BCu, 0);                                  // SDP_D_DATA_FORMAT_0_0 int8
  xregw(0x90C0u, sdp_op->out_cvt.offset);             // SDP_D_CVT_OFFSET_0
  xregw(0x90C4u, sdp_op->out_cvt.scale);              // SDP_D_CVT_SCALE_0
  xregw(0x90C8u, sdp_op->out_cvt.truncate);           // SDP_D_CVT_SHIFT_0

  // Set x1 to bypass for now
  xregw(0x9058u, 0x53); // SDP_D_DP_BS_CFG_0

  // Set x2 to bypass for now
  xregw(0x906Cu, 0x53); // SDP_D_DP_BN_CFG_0

  // Set y to bypass for now
  xregw(0x9080u,0x53); // SDP_D_DP_EW_CFG_0

  return 0;
}

int nna_sdp_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface) {
  return processor_sdp_program(sdp_op,sdp_surface);
}
