#include "ficHandler.h"

void process_ficInput(struct sdr_state_t *sdr, int16_t ficno);

int main(int argc, char** argv) {
    struct sdr_state_t sdr;
    FILE *pFile;
    uint8_t *output;
    
    memset(&sdr, 0, sizeof(sdr));
    
    initFicHandler(&sdr);
    
    pFile = fopen ("input/dab/rtlsdr/ficInput" , "rb" );
    fread (sdr.ofdm_input, 2, 2304, pFile);
    fclose(pFile);
    
    process_ficInput(&sdr, 0);
    
    printf("sdr.fibCRCsuccess %d\n", sdr.fibCRCsuccess);
    
    output = malloc(256);
    
    pFile = fopen ("input/dab/rtlsdr/ficOutput" , "rb" );
    fread (output, 1, 256, pFile);
    fclose(pFile);
    
    printf("memcmp: %d\n", memcmp(output, &sdr.bitBuffer_out[512], 256));
    
    exit(EXIT_SUCCESS);
}
