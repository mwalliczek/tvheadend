#include <stdlib.h>
#include <stdio.h>

#include "phasereference.h"

int main(int argc, char** argv) {
    int count;
    struct sdr_state_t sdr;
    int i;
    clock_t start, end;
    FILE *pFile;
    float _Complex* input;
    int32_t startIndex;

    count = 1;
    if (argc == 2) {
        count = atoi(argv[1]);
    }

    memset(&sdr, 0, sizeof(sdr));
    
    initPhaseReference(&sdr);
    
    input = malloc(2552 * sizeof(float _Complex));
    
    pFile = fopen ("input/mpegts/rtlsdr/phasereferenceInput" , "rb" );
    fread (input, sizeof(float _Complex), 2552, pFile);
    fclose(pFile);
    
    start = clock();
    startIndex = phaseReferenceFindIndex(&sdr, input);

    for (i = 1; i < count; i++) {
        phaseReferenceFindIndex(&sdr, input);
    }
    end = clock();
    printf("Total time taken by CPU: %Lf\n", (long double)(end - start) / CLOCKS_PER_SEC );
    
    printf("startIndex: %d\n", startIndex);
    
    destroyPhaseReference(&sdr);
    
    free(input);

    exit(EXIT_SUCCESS);
}

