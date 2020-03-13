#include "rtlsdr_private.h"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

FILE *pFile;

int readFromDevice(rtlsdr_frontend_t *lfe) {
    uint8_t *input = calloc(1024, sizeof(uint8_t));
    int res = fread (input, 1, 1024, pFile);
    if (res < 1024) {
        free(input);
        return 0;
    }
    cbWrite(&(lfe->sdr.fifo), input, 1024);
    free(input);
    return 1;
}

int main(int argc, char** argv) {
    pFile = fopen("input/dab/rtlsdr/input_sdrTest/rtlsdr_raw", "rb");

    rtlsdr_frontend_t *lfe = calloc(1, sizeof(rtlsdr_frontend_t));
    
    lfe->sdr.mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    sdr_init(&lfe->sdr);
    
    float _Complex v[512];
    
    uint32_t result = getSamples(lfe, v, 512, 0);
    
    printf("result: %d\n", result);
    
    float _Complex orig[512];
    FILE *pOut = fopen("input/dab/rtlsdr/input_sdrTest/rtlsdr_samples", "rb");
    fread(orig, 512, sizeof(float _Complex), pOut);
    fclose(pOut);
    
    printf("memcmp: %d\n", memcmp(v, orig, 512 * sizeof(float _Complex)));
    
    sdr_destroy(&lfe->sdr);
    
    free(lfe->sdr.mmi);
    free(lfe);
    fclose(pFile);

    exit(EXIT_SUCCESS);
}