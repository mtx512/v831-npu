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
 */

int sunxi_ion_alloc_open();
int sunxi_ion_alloc_close();
void* sunxi_ion_alloc_palloc(unsigned int size, void *vaddr, void *paddr );
void sunxi_ion_alloc_free();
int sunxi_ion_loadin(void *saddr, size_t size, int paddr);
void * sunxi_ion_loadout(int a1, size_t a2, void *a3);
void * sunxi_ion_loadout(int paddr, size_t size, void *daddr);
