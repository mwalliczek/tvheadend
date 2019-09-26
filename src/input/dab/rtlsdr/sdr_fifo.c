#include "tvheadend.h"
#include "sdr_fifo.h"


/* http://en.wikipedia.org/wiki/Circular_buffer */



void cbInit(CircularBuffer *cb, uint32_t size) {
    cb->size  = size;
    cb->start = 0;
    cb->count = 0;
    cb->elems = (uint8_t *)calloc(cb->size, sizeof(uint8_t));
}
void cbFree(CircularBuffer *cb) {
    free(cb->elems);
	}

int cbIsFull(CircularBuffer *cb) {
    return cb->count == cb->size; 
	}
 
int cbIsEmpty(CircularBuffer *cb) {
    return cb->count == 0;
	 }
 
void cbWrite(CircularBuffer *cb, uint8_t *elem, uint32_t size) {
    uint32_t end = (cb->start + cb->count) % cb->size;
	if ((cb->size - end) >= size) {
		memcpy(&cb->elems[end], elem, size);
	}
	else
	{
		memcpy(&cb->elems[end], elem, cb->size - end);
		memcpy(&cb->elems[0], &elem[cb->size - end], size - (cb->size - end));
	}

	cb->count += size;
	if (cb->count >= cb->size) {
        cb->start = (cb->start + size) % cb->size; 
		cb->count = cb->size;
		tvherror(LS_RTLSDR, "fifo overflow!");
	}
}
 
uint8_t * cbReadDouble(CircularBuffer *cb) {
	uint8_t *result;
	result = &cb->elems[cb->start];
    cb->start = (cb->start + 2) % cb->size;
	cb->count -= 2;
	return result;
}

