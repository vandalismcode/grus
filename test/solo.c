/* Renders one pattern with only one track playing, for comparing the float and double builds track by track. */
#include "../src/grus.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t x; static uint32_t xr(void){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
int main(int c,char**v){int k=atoi(v[1]),only=atoi(v[2]);x=0x9E3779B9u;grus_random=xr;grus_init(44100);grus_load_init(&grus_factory[k]);
  grus_pattern*W=grus_pattern_edit();for(int i=0;i<GRUS_TRACKS;i++)W->tr[i].mute=i!=only;grus_play(1);
  float L[1024],R[1024];FILE*f=fopen(v[3],"wb");for(long s=0;s<44100*3;s+=1024){grus_render(L,R,1024);for(int i=0;i<1024;i++){fwrite(&L[i],4,1,f);fwrite(&R[i],4,1,f);}}fclose(f);return 0;}
