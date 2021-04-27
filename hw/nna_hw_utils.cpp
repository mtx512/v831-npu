#include <sys/types.h>
#include <stdint.h>

#include "nna_config.h"
#include "nna_interface.h"

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
