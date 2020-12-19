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

#ifndef NNA_COMMON_H

enum nna_cmds { nna_cmd_on, nna_cmd_off, nna_cmd_reset, nna_cmd_clk };

void* xreg_open(void);
int xreg_close(void);
int xregr(int reg);
int xregw(int reg, unsigned int value);
int nna_clean_interrupt();
int nna_configure(nna_cmds cmd, unsigned int value);
int nna_on();
int nna_off();
void nna_reset();
int nna_wait_done(int event_mask, int event_value);

#endif // NNA_COMMON_H
