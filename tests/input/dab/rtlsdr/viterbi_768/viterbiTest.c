
#include "../dab.h"
#include "tvheadend.h"
#include <malloc.h>
#include "viterbi-768.h"

int main(int argc, char** argv) {
    int count;
    int16_t *input;
    uint8_t *output;
    uint8_t *output2;
    int i;
    clock_t start, end;
    FILE *pFile;
    struct v        vp;
    
    count = 1;
    if (argc == 2) {
        count = atoi(argv[1]);
    }

    input = malloc((3072 + 24) * 2);
    output = malloc(768);
    
    initViterbi768(&vp, 768, 1);
    
    pFile = fopen ("input/dab/rtlsdr/viterbi_768/input" , "rb" );
    fread (input, 2, 3072 + 24, pFile);
    fclose(pFile);

    start = clock();

    for (i = 0; i < count; i++) {
        deconvolve(&vp, input, output);
    }
    end = clock();
    printf("Spiral: Total time taken by CPU: %Lf\n", (long double)(end - start) / CLOCKS_PER_SEC );
    
    destroyViterbi768(&vp);
    
    free(input);

    output2 = malloc(768);
    
    pFile = fopen ("input/dab/rtlsdr/viterbi_768/output", "rb" );
    fread (output2, 1, 768, pFile);
    fclose(pFile);
    
    printf("memcmp: %d\n", memcmp(output, output2, 768));
    
    input = malloc((3072 + 24) * 2);

    initViterbi768(&vp, 768, 0);
    
    pFile = fopen ("input/dab/rtlsdr/viterbi_768/input" , "rb" );
    fread (input, 2, 3072 + 24, pFile);
    fclose(pFile);

    start = clock();

    for (i = 0; i < count; i++) {
        deconvolve(&vp, input, output);
    }
    end = clock();
    printf("Generic: Total time taken by CPU: %Lf\n", (long double)(end - start) / CLOCKS_PER_SEC );
    
    destroyViterbi768(&vp);
    
    free(input);

    free(output);
    free(output2);

    exit(EXIT_SUCCESS);
}
