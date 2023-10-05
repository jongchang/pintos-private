#include <string.h>
#include <stdio.h>

#define SPACE " "

int main(int argc, char **argv)
{
    char inp[1024] = "args-single onearg";
    char *nxt_ptr;
    char *srt_ptr = strtok_r(inp, SPACE, &nxt_ptr);

    while(srt_ptr){
        printf("argument = [%s]\n", srt_ptr);

        srt_ptr = strtok_r(NULL, SPACE, &nxt_ptr);
    }

    return 0;
}