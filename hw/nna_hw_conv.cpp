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

void nna_conv_set_producer(uint32_t group_id, uint32_t rdma_group_id) {

  // rdma_group_id unused ??
  // set producer pointer for all sub-modules
  xregw(0x7004u,group_id);  // CACC_S_POINTER_0
  xregw(0x5004u,group_id);  // CMAC_A_S_POINTER_0
  xregw(0x6004u,group_id);  // CMAC_B_S_POINTER_0
  xregw(0x4004u,group_id);  // CSC_S_POINTER_0
  xregw(0x3004u,group_id);  // CDMA_S_POINTER_0
}

void nna_conv_enable(uint8_t enable_stats, uint8_t is_rdma_needed) {

  // Wait for buffer to be flushed
  while ( !xregr(0x300Cu) );  // CDMA_S_CBUF_FLUSH_STATUS_0

  // See if we need to enable statistics
  xregw(0x30D4u, (enable_stats & 0x01));     // CDMA_D_PERF_ENABLE_0

  xregw(0x7008u, 1u);  // CACC_D_OP_ENABLE_0
  xregw(0x5008u, 1u);  // CMAC_A_D_OP_ENABLE_0
  xregw(0x6008u, 1u);  // CMAC_B_D_OP_ENABLE_0
  xregw(0x4008u, 1u);  // CSC_D_OP_ENABLE_0
  xregw(0x3010u, 1u);  // CDMA_D_OP_ENABLE_0
}

int processor_conv_program(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface) {

  uint32_t misc_cfg;
  uint32_t padding;

  misc_cfg = ((conv_op->skip_weight_rls & 0x01) << 28) | ((conv_op->skip_data_rls  & 0x01) << 24) |
  ((conv_op->weight_reuse & 0x01) << 20) | ((conv_op->data_reuse  & 0x01) << 16);

  padding = (conv_op->pad_y_bottom << 24) | (conv_op->pad_y_top << 20) |
  (conv_op->pad_x_right << 16) | conv_op->pad_x_left;

  /* cmac */
  xregw(0x500Cu, 0);  // CMAC_A_D_MISC_CFG_0
  xregw(0x600Cu, 0);  // CMAC_B_D_MISC_CFG_0

  /* csc */
  xregw(0x400Cu, misc_cfg);  // CSC_D_MISC_CFG_0
  xregw(0x4010u, conv_op-> data_format != FORMAT_FEATURE ); // CSC_D_DATAIN_FORMAT_0
  xregw(0x4014u, (conv_op->input_width_csc - 1) | ((conv_op->input_height_csc - 1) << 16));  // CSC_D_DATAIN_SIZE_EXT_0_0
  xregw(0x4018u, conv_op->input_channel_csc - 1);
  xregw(0x401Cu, 0);
  xregw(0x4020u, conv_op->post_extension); // CSC_D_POST_Y_EXTENSION_0
  xregw(0x4024u, conv_op->entry_per_slice -1); // CSC_D_ENTRY_PER_SLICE_0
  xregw(0x4028u, 0);  // Uncompressed weights

  if ( conv_op->data_format != FORMAT_FEATURE ) {
    // input is pixel data
    xregw(0x402Cu, (conv_op->kernel_height_csc - 1) << 16);
    xregw(0x4030u, (conv_op->kernel_channel_csc * conv_op->kernel_width_csc - 1) |
    ((conv_surface->dst_data.channel - 1) << 16));
  } else {
    xregw(0x402Cu, (conv_op->kernel_width_csc - 1) | ((conv_op->kernel_height_csc - 1) << 16));   // CSC_D_WEIGHT_SIZE_EXT_0_0
    xregw(0x4030u, (conv_op->kernel_channel_csc - 1) | ((conv_surface->dst_data.channel - 1) << 16));
  }

  xregw(0x4034u, conv_surface->weight_data.size & 0xFFFFFFE0); // CSC_D_WEIGHT_BYTES_0
  xregw(0x403Cu, (conv_op->input_width_cmac - 1) | ((conv_op->input_height_cmac  - 1) << 16)); // CSC_D_DATAOUT_SIZE_0_0
  xregw(0x4040u, conv_surface->dst_data.channel - 1); // CSC_D_DATAOUT_SIZE_1_0
  xregw(0x4044u, conv_surface->dst_data.width * conv_surface->dst_data.height- 1); // // CSC_D_ATOMICS_0
  xregw(0x4048u, conv_op->release); // CSC_D_RELEASE_0
  xregw(0x404Cu, (conv_op->stride_x - 1) | ((conv_op->stride_y - 1) << 16));
  xregw(0x4050u, (conv_op->dilation_x - 1) | ((conv_op->dilation_x - 1) << 16));
  xregw(0x4054u, (conv_op->pad_y_top << 16)  | conv_op->pad_x_left); // CSC_D_ZERO_PADDING_0
  xregw(0x4058u, 0);
  xregw(0x405Cu, (conv_op->data_bank - 1) | ((conv_op->weight_bank - 1) << 16)); // CSC_D_BANK_0
  xregw(0x4060u, 0); // CSC_D_PRA_CFG_0

  /* cdma */
  xregw(0x3008u, 0x10001u); // CDMA_S_ARBITER_0
  xregw(0x3014u, misc_cfg); // CDMA_D_MISC_CFG_0

  if (conv_op-> data_format != FORMAT_FEATURE  ) {
    xregw(0x3018u, (conv_op->data_format << 8) | 1);
    if ( conv_op->mean_format==MEAN_FORMAT_ENABLE ) {
      xregw(0x3098u, 1u);
      xregw(0x309Cu, (conv_op->mean_ry) | (conv_op->mean_gu << 16));
      xregw(0x30A0u, (conv_op->mean_bv) | (conv_op->mean_ax << 16));
      xregw(0x30ACu, 1u);
      xregw(0x30A4u, 1u);
    }
    else {
      xregw(0x3098u, 0);
    }
  } else {
    xregw(0x3018u, 0x101000u);
  }

  xregw(0x304Cu, 1u); // CDMA_D_DAIN_MAP_0 line packed true
  xregw(0x301Cu, (conv_surface->src_data.width - 1) | ((conv_surface->src_data.height - 1) << 16));
  xregw(0x3020u, conv_surface->src_data.channel - 1);
  xregw(0x3024u, (conv_op->input_width_csc - 1) | ((conv_op->input_height_csc - 1) << 16)); // CDMA_D_DATAIN_SIZE_EXT_0_0
  xregw(0x302Cu, 1u);
  xregw(0x3030u, 0);
  xregw(0x3034u, conv_surface->src_data.address);
  xregw(0x3040u, conv_surface->src_data.line_stride);
  xregw(0x3048u, conv_surface->src_data.surf_stride);

  xregw(0x304Cu, 1u);
  xregw(0x3058u, 0);
  xregw(0x3060u, conv_op->entry_per_slice - 1); // CDMA_D_ENTRY_PER_SLICE_0
  xregw(0x3068u, 0);
  xregw(0x306Cu, conv_op->bytes_per_kernel - 1); // CDMA_D_WEIGHT_SIZE_0_0
  xregw(0x3070u, conv_surface->dst_data.channel - 1);
  xregw(0x3074u, 0x01); // Ram type MC
  xregw(0x3078u, 0);
  xregw(0x307Cu, conv_surface->weight_data.address); // CDMA_D_WEIGHT_ADDR_LOW_0
  xregw(0x3080u, conv_surface->weight_data.size & 0xFFFFFFE0); // CDMA_D_WEIGHT_BYTES_0
  xregw(0x30B0u, (conv_op->stride_x - 1) | ((conv_op->stride_y -1) << 16));
  xregw(0x30B4u, padding);  // // CDMA_D_ZERO_PADDING_0
  xregw(0x30B8u, 0);
  xregw(0x30BCu, (conv_op->data_bank - 1) | ((conv_op->weight_bank - 1) << 16));
  return 0;
}

int nna_conv_program(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface) {
  return processor_conv_program(conv_op,conv_surface);
}
