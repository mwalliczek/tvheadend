#include "mp4processor.h"

int main(int argc, char** argv) {
    mp4processor_t* mp4 = init_mp4processor(72);
    destroy_mp4processor(mp4);
    exit(EXIT_SUCCESS);
}
