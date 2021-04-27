/*
 * Copyright (C) 2021  Jasbir Matharu, <jasknuj@gmail.com>
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
 *
 * The Allwinner kernel implements a simple ion interface to allocate
 * memory, however the physical addresses can't be retrieved using the
 * the ion inteface. Therefore need to use the cedar ioctl to get the
 * physical address which is required by the V831 NNA.
 *
 * In this simple implementation you can only reverse a single buffer hence
 * mutliple calls to sunxi_ion_alloc_palloc() without a subsequent
 * sunxi_ion_alloc_free() with result in memory leaks!
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "ion_uapi.h"

#define DEV_ION   "/dev/ion"
#define DEV_CEDAR "/dev/cedar_dev"

#define SZ_4k 0x00001000

#define ION_ALLOC_ALIGN  SZ_4k

#define AW_MEM_ENGINE_REQ 0x206
#define AW_MEM_ENGINE_REL 0x207
#define AW_MEM_GET_IOMMU_ADDR	0x502
#define AW_MEM_FREE_IOMMU_ADDR	0x503

#define ION_IOC_SUNXI_FLUSH_RANGE           5
#define ION_IOC_SUNXI_PHYS_ADDR             7

typedef struct ION_ALLOC_CONTEXT {
    int fd_ion;         // Handle to ion driver
    int fd_cedar;       // Handle to cedar driver
    int ref_cnt;        // reference count
    unsigned int  phyOffset;
} ion_alloc_context;

typedef struct sunix_phys_data {
    ion_user_handle_t handle;
    unsigned int  phys_addr;
    unsigned int  size;
} sunxi_phys_data;

typedef struct ion_buffer {
    unsigned long addr_phy; // phisical address
    unsigned long addr_vir; // virtual address
    unsigned int size;      // buffer size
    ion_fd_data fd_data;
} ion_buffer;

typedef struct {
    long    start;
    long    end;
} sunxi_cache_range;

struct sunxi_iommu_param {
    int	fd;
    unsigned int iommu_addr;
};

static ion_alloc_context *g_ion_alloc_context = NULL;
static pthread_mutex_t g_ion_mutex_alloc = PTHREAD_MUTEX_INITIALIZER;

static struct ion_buffer g_ion_buffer;

signed int sunxi_ion_alloc_open() {

  pthread_mutex_lock((pthread_mutex_t *)&g_ion_mutex_alloc);
  if (g_ion_alloc_context==NULL) {
    g_ion_alloc_context = (ion_alloc_context*)malloc(sizeof(ion_alloc_context));
    if (g_ion_alloc_context) {
      memset((void*)g_ion_alloc_context, 0, sizeof(ion_alloc_context));
      g_ion_alloc_context->fd_ion = open(DEV_ION, O_RDONLY, 0);
      if (g_ion_alloc_context->fd_ion > 0) {
        g_ion_alloc_context->fd_cedar = open(DEV_CEDAR, O_RDONLY, 0);
        if (g_ion_alloc_context->fd_cedar >0) {
          g_ion_alloc_context->ref_cnt++;
          pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);
          return 0;
        } else {
          printf("Failed to open %s error %s\n",DEV_CEDAR,strerror(errno));
          close(g_ion_alloc_context->fd_ion);
          free(g_ion_alloc_context);
          g_ion_alloc_context = NULL;
        }
      } else {
        printf("Failed to open %s error %s\n",DEV_ION,strerror(errno));
        free(g_ion_alloc_context);
        g_ion_alloc_context = NULL;
      }
    }
    else {
      printf("Failed to create ion allocator out of memory\n");
    }

  } else {
    g_ion_alloc_context->ref_cnt++;
    pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);
    return 0;
  }

  pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);
  return -1;
}


int sunxi_ion_alloc_close() {

  pthread_mutex_lock((pthread_mutex_t *)&g_ion_mutex_alloc);
  if (--g_ion_alloc_context->ref_cnt <= 0) {
    close(g_ion_alloc_context->fd_cedar);
    close(g_ion_alloc_context->fd_ion);
    free(g_ion_alloc_context);
    g_ion_alloc_context = NULL;
  } else {
    printf("Ref count %d > 0 can't free\n",g_ion_alloc_context->ref_cnt);
  }
  return pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);
}


void *sunxi_ion_alloc_palloc(unsigned int size, void *vaddr, void *paddr ) {

  ion_allocation_data ion_alloc_data;
  ion_fd_data fd_data;

  sunxi_iommu_param iommu_param;
  unsigned long addr_vir = 0;
  unsigned long addr_phy = 0;

  int ret = 0;

  pthread_mutex_lock((pthread_mutex_t *)&g_ion_mutex_alloc);
  if (g_ion_alloc_context) {
    if (size > 0) {
      memset((void*)&g_ion_buffer,0,sizeof(g_ion_buffer));
      ion_alloc_data.len = (size_t)size;
      ion_alloc_data.align = ION_ALLOC_ALIGN;
      ion_alloc_data.heap_id_mask = ION_HEAP_SYSTEM_MASK;
      ion_alloc_data.flags = ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC;

      ret = ioctl(g_ion_alloc_context->fd_ion, ION_IOC_ALLOC, &ion_alloc_data);
      if (ret) {
        printf("ION_IOC_ALLOC failed to allocate %d bytes with error %s\n",size,strerror(errno));
      } else
      {
        fd_data.handle = ion_alloc_data.handle;
        ret = ioctl(g_ion_alloc_context->fd_ion, ION_IOC_MAP,&fd_data );
        if (ret) {
          printf("ION_IOC_MAP failed with error %s\n",strerror(errno));
          ioctl(g_ion_alloc_context->fd_ion, ION_IOC_FREE,&ion_alloc_data.handle);
        } else {
          addr_vir = (unsigned long)mmap(NULL, ion_alloc_data.len, \
            PROT_READ|PROT_WRITE, MAP_SHARED, fd_data.fd, 0);

          if ((unsigned long)MAP_FAILED == addr_vir) {
            addr_vir = 0;
            printf("Failed to map allocated memory with error %s\n",strerror(errno));
            ioctl(g_ion_alloc_context->fd_ion, ION_IOC_FREE,&ion_alloc_data.handle);
          } else {
            memset(&iommu_param, 0, sizeof(iommu_param));
            iommu_param.fd = fd_data.fd;
            ret = ioctl(g_ion_alloc_context->fd_cedar, AW_MEM_ENGINE_REQ, 0);
            if (ret) {
              printf("ENGINE_REQ failed with error ret %s\n",strerror(errno));
              munmap((void *)(addr_vir), size);
              ioctl(g_ion_alloc_context->fd_ion, ION_IOC_FREE,&ion_alloc_data.handle);
              addr_phy = 0;
              addr_vir = 0;
            } else {
              ret = ioctl(g_ion_alloc_context->fd_cedar, AW_MEM_GET_IOMMU_ADDR, &iommu_param);
              if (ret) {
                printf("GET_IOMMU_ADDR failed with error %s\n", strerror(errno));
                munmap((void *)(addr_vir), size);
                ioctl(g_ion_alloc_context->fd_ion, ION_IOC_FREE,&ion_alloc_data.handle);
                addr_phy = 0;
                addr_vir = 0;
              } else {
                addr_phy = iommu_param.iommu_addr;
                g_ion_buffer.addr_vir = addr_vir;
                g_ion_buffer.addr_phy = addr_phy;
                g_ion_buffer.size = size;
                g_ion_buffer.fd_data.handle = ion_alloc_data.handle;
                g_ion_buffer.fd_data.fd = fd_data.fd;
                *(unsigned long *)vaddr = addr_vir;
                *(unsigned long *)paddr = addr_phy;
              }
            }
          }
        }
      }

    } else {
      printf("Allocation size %d must be greater than zero\n",size);
    }
  } else {
    printf("Need to ion_alloc_open before %s\n", __func__);
  }
  pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);
  return (void*)addr_vir;
}

void sunxi_ion_alloc_free() {

  int ret;

  pthread_mutex_lock((pthread_mutex_t *)&g_ion_mutex_alloc);
  if (g_ion_alloc_context) {
    if (g_ion_buffer.addr_vir) {
      if (munmap((void *)(g_ion_buffer.addr_vir), g_ion_buffer.size) < 0) {
        printf("munmap 0x%p, size: %d failed\n", (void*)g_ion_buffer.addr_vir, g_ion_buffer.size);
      }
      close(g_ion_buffer.fd_data.fd);
      ret = ioctl(g_ion_alloc_context->fd_ion, ION_IOC_FREE, &g_ion_buffer.fd_data.handle);
			if (ret) {
        printf("ION_IOC_FREE failed with error %s\n",strerror(errno));
			}
    }
  } else {
    printf("Need to ion_alloc_open before %s\n", __func__);
  }
  pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);

}

void* sunxi_ion_alloc_phy2vir_cpu(void * pbuf) {

    unsigned long addr_vir = 0;
    unsigned long addr_phy = (unsigned long)pbuf;

    if (pbuf == 0)
    {
        printf("can not phy2vir NULL buffer \n");
        return (void *)0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&g_ion_mutex_alloc);

    if (addr_phy >= g_ion_buffer.addr_phy
            && addr_phy < g_ion_buffer.addr_phy+ g_ion_buffer.size) {
            addr_vir = g_ion_buffer.addr_vir + addr_phy - g_ion_buffer.addr_phy;
    } else {
        printf("ion_alloc_phy2vir failed, do not find physical address: 0x%lx \n", addr_phy);
    }

    pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);

    return (void*)addr_vir;
}

int sunxi_ion_alloc_flush_cache(void *startAddr, int size) {
	sunxi_cache_range range;
	int ret;

  pthread_mutex_lock((pthread_mutex_t *)&g_ion_mutex_alloc);

	/* clean and invalid user cache */
	range.start = (unsigned long)startAddr;
	range.end = (unsigned long)startAddr + size;

	ret = ioctl(g_ion_alloc_context->fd_ion, ION_IOC_SUNXI_FLUSH_RANGE, &range);
	if (ret)
    printf("ION_IOC_SUNXI_FLUSH_RANGE failed with error %s\n",strerror(errno));

  pthread_mutex_unlock((pthread_mutex_t *)&g_ion_mutex_alloc);
  return ret;
}

int sunxi_ion_loadin(void *saddr, size_t size, int paddr) {
  void *vaddr;
  void *daddr;

  vaddr = (void *)sunxi_ion_alloc_phy2vir_cpu((void*)paddr);
  daddr = memcpy(vaddr, saddr, size);
  return sunxi_ion_alloc_flush_cache(daddr, size);
}

void * sunxi_ion_loadout(int paddr, size_t size, void *daddr) {
  void *vaddr;

  vaddr = (void *)sunxi_ion_alloc_phy2vir_cpu((void*)paddr);
  sunxi_ion_alloc_flush_cache(vaddr, size);
  return memcpy(daddr, vaddr, size);
}
