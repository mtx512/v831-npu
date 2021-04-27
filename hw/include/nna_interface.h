/*
 * Copyright (C) 2020  Jasbir Matharu, <jasknuj@gmail.com>
 * Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
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

#ifndef NNA_INTERFACE_H
#define NA_INTERFACE_H

#define KERNEL_PER_GROUP 8

#define FORMAT_T_R8			             0
#define FORMAT_FEATURE			        36

#define MEAN_FORMAT_DISABLE     0
#define MEAN_FORMAT_ENABLE      1

#define ACTIVATION_NONE   0
#define ACTIVATION_RELU   1
#define ACTIVATION_PRELU  3

struct nna_cvt_param {
  int16_t  scale;
  uint8_t  truncate;
  uint8_t  enable;

  int32_t  offset;
};

struct nna_data_cube {

  uint8_t type; // 0 is CV, 1 is type MC
  uint32_t address; // pyhsical memory address

  uint32_t size;

  /* cube dimensions */
  uint16_t width;
  uint16_t height;

  uint16_t channel;

  uint32_t line_stride; // For pixel its width, feature its 8 * width (atom_size = 8 ??)
  uint32_t surf_stride; // For pixel its width * height, feature its 8 * width * height
};

struct nna_conv_surface_desc {

  /* Data cubes */
  struct nna_data_cube src_data;
  struct nna_data_cube dst_data;
  struct nna_data_cube weight_data;
};

struct nna_conv_op_desc {

  uint8_t data_reuse;    // 0 disable or 1 enable
  uint8_t weight_reuse;  // 0 disable or 1 enable
  uint8_t skip_data_rls; // 0 disable or 1 enable
  uint8_t skip_weight_rls; // 0 disable or 1 enable

  uint16_t entry_per_slice; // Default to 1

  uint8_t data_format; // Either feature or pixel data
  uint8_t pixel_mapping;

  uint8_t batch; // batch number


  uint8_t weight_format;   // Default to 0 (Uncompressed)
  uint8_t data_bank;
  uint8_t weight_bank;

  uint8_t post_extension;
  uint8_t pixel_sign_override;

  uint16_t release; // number of slices need to be released

  /* The input cube dimension for CSC */
  uint16_t input_width_csc;
  uint16_t input_height_csc;
  uint16_t input_channel_csc;

  uint16_t kernel_width_csc;
  uint16_t kernel_height_csc;
  uint16_t kernel_channel_csc;

  /* The input cube dimension for CMAC */
  uint16_t input_width_cmac;
  uint16_t input_height_cmac;

  uint32_t bytes_per_kernel; // actual size in bytes

  int16_t mean_ry; /* mean value for red in RGB or Y in YUV */
  int16_t mean_gu; /* mean value for green in RGB or U in YUV */

  int16_t mean_bv; /* mean value for blue in RGB or V in YUV */
  int16_t mean_ax;

  uint8_t mean_format; /* enable or disable */

  uint8_t stride_x;  // Default to 1
  uint8_t stride_y;  // Default to 1

  uint8_t pad_x_left;
  uint8_t pad_x_right;
  uint8_t pad_y_top;
  uint8_t pad_y_bottom;

  uint8_t dilation_x;  // Default to 1
  uint8_t dilation_y;  // Default to 1

};

struct nna_sdp_surface_desc {
  /* Data cube */
  /* source input cube, available when SDP working on offline mode */
  struct nna_data_cube src_data;

  /* X1 input cube */
  struct nna_data_cube x1_data;

  /* X2 input cube */
  struct nna_data_cube x2_data;

  /* Y input cube */
  struct nna_data_cube y_data;

  /* Output cube */
  struct nna_data_cube dst_data;
};

#define SDP_OP_NONE		0
#define SDP_OP_MUL		1
#define SDP_OP_ADD		2
#define SDP_OP_BOTH		3

#define SDP_ALU_OP_MAX		0
#define SDP_ALU_OP_MIN		1
#define SDP_ALU_OP_SUM		2
#define SDP_ALU_OP_EQL		3

#define SDP_OP_PER_LAYER	0
#define SDP_OP_PER_KERNEL	1
#define SDP_OP_PER_POINT	2

struct nna_sdp_cvt {
  struct nna_cvt_param alu_cvt;
  struct nna_cvt_param mul_cvt;
};

struct nna_sdp_op {
  uint8_t enable; // 0 - disable, 1 - enable
  uint8_t alu_type;
  uint8_t type; // 0 - mul, 1 - alu , 2 - both
  uint8_t mode; // 0 - per kernel, 1 - per element

  uint8_t act;
  uint8_t shift_value;
  uint8_t truncate;
  uint8_t precision; // 0 - 1 byte, 1 - 2 byte

  int32_t alu_operand;
  int32_t mul_operand;

  struct nna_sdp_cvt  cvt;
};

struct nna_sdp_op_desc {

  struct nna_cvt_param out_cvt;

  /* Performance parameters */
  /* nna_conv_mode */
  uint8_t conv_mode;
  uint8_t batch_num;
  uint16_t reserved0;

  uint32_t batch_stride;	/* will be used when batch_num > 1 */

  /* Algorithm parameters */
  struct nna_sdp_op x1_op;
  struct nna_sdp_op x2_op;
  struct nna_sdp_op y_op;
};

struct nna_pdp_surface_desc {

	struct nna_data_cube src_data;
	struct nna_data_cube dst_data;
};

#define POOL_MODE_AVG		0
#define POOL_MODE_MAX		1
#define POOL_MODE_MIN		2

struct nna_pdp_op_desc {

	uint8_t   split_num; // Default to 1

	/* Algorithm parameters */
	uint8_t  pool_mode; /* max,min,average */
	uint8_t  pool_width; /*  width */
	uint8_t  pool_height; /* height */

	uint8_t  stride_x;
	uint8_t  stride_y;

	/**
	 * The left/right/top/bottom padding size,
	 */
	uint8_t  pad_left;
	uint8_t  pad_right;
	uint8_t  pad_top;
	uint8_t  pad_bottom;
};

void nna_conv_set_producer(uint32_t group_id, uint32_t rdma_group_id);
void nna_conv_enable(uint8_t enable_stats, uint8_t is_rdma_needed);
int nna_conv_program(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface);

void nna_sdp_set_producer(uint32_t group_id, uint32_t rdma_group_id);
void nna_sdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed);
int nna_sdp_program(nna_sdp_op_desc* sdp_op, nna_sdp_surface_desc* sdp_surface);

void nna_pdp_set_producer(uint32_t group_id, uint32_t rdma_group_id);
void nna_pdp_enable(uint8_t enable_stats, uint8_t is_rdma_needed);
int nna_pdp_program(nna_pdp_op_desc* pdp_op, nna_pdp_surface_desc* pdp_surface);

uint16_t calculate_eps(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface);
uint32_t calculate_data_bank(nna_conv_op_desc* conv_op, nna_conv_surface_desc* conv_surface);
uint32_t calculate_weight_bank(nna_conv_surface_desc* conv_surface);

#endif // NNA_INTERFACE_H
