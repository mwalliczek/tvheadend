#include "tvheadend.h"
#include "sdr_fifo.h"


/* http://en.wikipedia.org/wiki/Circular_buffer */



void cbInit(CircularBuffer *cb, uint32_t size) {
    cb->size  = size;
    cb->start = 0;
    cb->end   = 0;
    cb->count = 0;
    cb->elems = calloc(cb->size, sizeof(uint8_t));
}

void cbFree(CircularBuffer *cb) {
    free(cb->elems);
    cb->elems = NULL;
}

int cbIsFull(CircularBuffer *cb) {
    return cbCount(cb) == cb->size;
}
 
int cbIsEmpty(CircularBuffer *cb) {
    return cbCount(cb) == 0;
}

/* producer: append data, what does not fit any more is dropped */
void cbWrite(CircularBuffer *cb, uint8_t *elem, uint32_t size) {
    uint32_t space = cb->size - cbCount(cb);
    uint32_t len = size;

    if (len > space) {
        len = space & ~1u;  /* keep I/Q pairs together */
        tvherror(LS_RTLSDR, "fifo overflow, %u bytes lost!", size - len);
    }
    if (len == 0)
        return;
    if ((cb->size - cb->end) >= len) {
        memcpy(&cb->elems[cb->end], elem, len);
    } else {
        memcpy(&cb->elems[cb->end], elem, cb->size - cb->end);
        memcpy(&cb->elems[0], &elem[cb->size - cb->end], len - (cb->size - cb->end));
    }
    cb->end = (cb->end + len) % cb->size;
    __atomic_add_fetch(&cb->count, len, __ATOMIC_RELEASE);
}

/* consumer: caller has to check that at least two bytes are available */
uint8_t * cbReadDouble(CircularBuffer *cb) {
    uint8_t *result = &cb->elems[cb->start];
    cb->start = (cb->start + 2) % cb->size;
    __atomic_sub_fetch(&cb->count, 2, __ATOMIC_RELEASE);
    return result;
}
