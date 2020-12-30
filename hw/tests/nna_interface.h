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

#ifndef NNA_INTERFACE_H
#define NA_INTERFACE_H

#define FORMAT_T_R8			             0
#define FORMAT_FEATURE			        36

#define MEAN_FORMAT_DISABLE     0
#define MEAN_FORMAT_ENABLE      1

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

#endif // NNA_INTERFACE_H
