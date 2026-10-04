/*
This file is part of rtl-dab
trl-dab is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Foobar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rtl-dab.  If not, see <http://www.gnu.org/licenses/>.


david may 2012
david.may.muc@googlemail.com

*/

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>


/*
 * Single producer (rtlsdr read thread) / single consumer (demodulator)
 * ring buffer: start is only changed by the consumer, end only by the
 * producer and count is updated atomically by both.
 */
typedef struct 
{
  uint32_t size;
  uint32_t start;
  uint32_t end;
  uint32_t count;
  uint8_t *elems;
} CircularBuffer;


void cbInit(CircularBuffer *cb, uint32_t size);
void cbFree(CircularBuffer *cb);
int cbIsFull(CircularBuffer *cb);
int cbIsEmpty(CircularBuffer *cb);
void cbWrite(CircularBuffer *cb, uint8_t *elem, uint32_t size);
uint8_t * cbReadDouble(CircularBuffer *cb);

static inline uint32_t cbCount(CircularBuffer *cb) {
  return __atomic_load_n(&cb->count, __ATOMIC_ACQUIRE);
}
