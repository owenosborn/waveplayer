#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    struct timeval start, end;
    uint8_t thing1[2048];
    uint8_t thing2[4096];
    uint8_t thing3[6];
    
    gettimeofday(&start, NULL);


    memset(thing1, 0, sizeof(thing1));
    memset(thing2, 0, sizeof(thing2));
    memset(thing3, 0, sizeof(thing3));

    gettimeofday(&end, NULL);
    
    long seconds = (end.tv_sec - start.tv_sec);
    long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);
    printf("zeroed in %d microseconds \n", micros);

}
