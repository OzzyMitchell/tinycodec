#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../third_party/qoi/qoi.h"
#include "../tcom.c"
typedef struct { unsigned char *p; size_t size; } Buffer;
static unsigned width,height,channels;
static LARGE_INTEGER frequency;
static double now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart/frequency.QuadPart; }
static Buffer encode(int c,const unsigned char *raw,size_t size) {
 Buffer b={0};
 if(c==1) { qoi_desc d={width,height,(unsigned char)channels,QOI_SRGB}; int n=0; b.p=qoi_encode(raw,&d,&n); b.size=n; }
 else { unsigned v[3]={width,height,channels}; size_t cap=size*2+4096; b.p=malloc(cap); if(b.p) b.size=tcom(raw,size,b.p,cap,v,0); }
 return b;
}
static Buffer decode(int c,Buffer e,size_t size) {
 Buffer b={0};
 if(c==1) { qoi_desc d={0}; b.p=qoi_decode(e.p,(int)e.size,&d,channels); if(b.p && d.width==width && d.height==height && d.channels==channels) b.size=(size_t)d.width*d.height*d.channels; }
 else { unsigned v[3]; b.p=malloc(size); if(b.p) b.size=tcom(e.p,e.size,b.p,size,v,1); }
 return b;
}
int main(int argc,char **argv) {
  if(argc!=8) { fprintf(stderr,"bench raw width height channels reps order output-prefix\n"); return 1; }
  width=(unsigned)strtoul(argv[2],NULL,10); height=(unsigned)strtoul(argv[3],NULL,10);
  channels=(unsigned)strtoul(argv[4],NULL,10); int reps=atoi(argv[5]),order=atoi(argv[6]);
  if(!width||!height||(channels!=3&&channels!=4)||reps<1||reps>20) return 1;
  size_t size=(size_t)width*height*channels; unsigned char *raw=(unsigned char *)malloc(size);
  FILE *f=fopen(argv[1],"rb"); if(!raw||!f||fread(raw,1,size,f)!=size||fgetc(f)!=EOF) return 2; fclose(f);
  QueryPerformanceFrequency(&frequency); DWORD_PTR affinity=2; if(!SetProcessAffinityMask(GetCurrentProcess(),affinity)) return 8;
  Buffer initial[2]; unsigned loops[2]; double enc[2]={0},dec[2]={0}; size_t checks[2]={0};
  double enc_reps[2][20]={{0}},dec_reps[2][20]={{0}};
  for(int step=0;step<2;++step) {
    int c=(order+step)%2; double t=now(); initial[c]=encode(c,raw,size); double elapsed=now()-t;
    if(!initial[c].p||!initial[c].size) return 3;
    loops[c]=(unsigned)(0.012/(elapsed>0?elapsed:1)); if(loops[c]<1) loops[c]=1; if(loops[c]>32) loops[c]=32;
    Buffer d=decode(c,initial[c],size); if(!d.p||d.size!=size||memcmp(d.p,raw,size)) return 4; free(d.p); ++checks[c];
    char path[4096]; snprintf(path,sizeof path,"%s.%s",argv[7],c==0?"tcom":c==1?"qoi":"png");
    f=fopen(path,"wb"); if(!f||fwrite(initial[c].p,1,initial[c].size,f)!=initial[c].size||fclose(f)) return 5;
  }
  for(int r=0;r<reps;++r) for(int step=0;step<2;++step) {
    int c=(order+r+step)%2;
    for(unsigned j=0;j<loops[c];++j) {
      double t=now(); Buffer e=encode(c,raw,size); double elapsed=now()-t; enc[c]+=elapsed; enc_reps[c][r]+=elapsed;
      if(!e.p||e.size!=initial[c].size||memcmp(e.p,initial[c].p,e.size)) return 6; free(e.p);
      t=now(); Buffer d=decode(c,initial[c],size); elapsed=now()-t; dec[c]+=elapsed; dec_reps[c][r]+=elapsed;
      if(!d.p||d.size!=size||memcmp(d.p,raw,size)) return 7; free(d.p); ++checks[c];
    }
  }
  printf("{\"codecs\":[");
  for(int c=0;c<2;++c) {
    printf("%s{\"name\":\"%s\",\"bytes\":%zu,\"encode_sec\":%.12f,\"decode_sec\":%.12f,\"loops\":%u,\"checks\":%zu,\"encode_reps\":[",
      c?",":"",c==0?"tiny":c==1?"qoi":"libpng",initial[c].size,enc[c]/(reps*loops[c]),dec[c]/(reps*loops[c]),loops[c],checks[c]);
    for(int r=0;r<reps;++r) printf("%s%.12f",r?",":"",enc_reps[c][r]/loops[c]);
    printf("],\"decode_reps\":[");
    for(int r=0;r<reps;++r) printf("%s%.12f",r?",":"",dec_reps[c][r]/loops[c]);
    printf("]}");
    free(initial[c].p);
  }
  puts("]}"); free(raw); return 0;
}
