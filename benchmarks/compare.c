#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#ifdef BENCH_LIBPNG
#include <png.h>
#include <zlib.h>
#ifdef BENCH_OXIPNG
#define CODECS 6
typedef struct { unsigned char *data; size_t size; } OxiBuffer;
static OxiBuffer (*oxi_encode)(const unsigned char *,size_t,unsigned,unsigned,unsigned);
static void (*oxi_free)(unsigned char *,size_t);
#else
#define CODECS 5
#endif
static const int levels[] = {1,3,6};
#else
#define CODECS 2
#endif
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../third_party/qoi/qoi.h"
#include "../tcom.c"
typedef struct { unsigned char *p; size_t size, cap, pos; } Buffer;
static unsigned width,height,channels;
static LARGE_INTEGER frequency;
static double now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart/frequency.QuadPart; }
#ifdef BENCH_LIBPNG
static void png_write_bytes(png_structp png, png_bytep data, png_size_t count) {
  Buffer *b=(Buffer *)png_get_io_ptr(png);
  if(count>b->cap-b->size) png_error(png,"output capacity");
  memcpy(b->p+b->size,data,count); b->size+=count;
}
static void png_read_bytes(png_structp png, png_bytep data, png_size_t count) {
  Buffer *b=(Buffer *)png_get_io_ptr(png);
  if(count>b->size-b->pos) png_error(png,"truncated PNG");
  memcpy(data,b->p+b->pos,count); b->pos+=count;
}
#endif
static void free_encoded(int codec,Buffer b) {
#ifdef BENCH_OXIPNG
  if(codec==5) { oxi_free(b.p,b.size); return; }
#endif
  free(b.p);
}
static Buffer encode(int c,const unsigned char *raw,size_t size) {
#ifdef BENCH_OXIPNG
 if(c==5) {
  OxiBuffer o=oxi_encode(raw,size,width,height,channels);
  return (Buffer){.p=o.data,.size=o.size};
 }
#endif
 Buffer b={0};
 if(c==1) { qoi_desc d={width,height,(unsigned char)channels,QOI_SRGB}; int n=0; b.p=qoi_encode(raw,&d,&n); b.size=n; }
 else if(c==0) { unsigned v[3]={width,height,channels}; size_t cap=size*2+4096; b.p=malloc(cap); if(b.p) b.size=tcom(raw,size,b.p,cap,v,0); }
#ifdef BENCH_LIBPNG
 if(c>=2) {
  b.cap=size*2+4096; b.p=malloc(b.cap); b.size=0;
  if(!b.p) return b;
  png_structp p=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
  png_infop i=p?png_create_info_struct(p):NULL;
  if(!p||!i) { free(b.p); b.p=NULL; return b; }
  if(setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p,&i); free(b.p); b.p=NULL; return b; }
  png_set_write_fn(p,&b,png_write_bytes,NULL);
  png_set_IHDR(p,i,width,height,8,channels==4?PNG_COLOR_TYPE_RGBA:PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
  png_set_compression_level(p,levels[c-2]); png_set_filter(p,PNG_FILTER_TYPE_BASE,PNG_ALL_FILTERS);
  png_write_info(p,i);
  for(unsigned y=0;y<height;++y) png_write_row(p,(png_bytep)raw+(size_t)y*width*channels);
  png_write_end(p,i); png_destroy_write_struct(&p,&i); return b;
 }
#endif
 return b;
}
static Buffer decode(int c,Buffer e,size_t size) {
 Buffer b={0};
 if(c==1) { qoi_desc d={0}; b.p=qoi_decode(e.p,(int)e.size,&d,channels); if(b.p && d.width==width && d.height==height && d.channels==channels) b.size=(size_t)d.width*d.height*d.channels; }
 else { b.p=malloc(size); if(c==0 && b.p) { unsigned v[3]; b.size=tcom(e.p,e.size,b.p,size,v,1); } }
#ifdef BENCH_LIBPNG
 if(c>=2) {
  if(!b.p) return b;
  png_structp p=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
  png_infop i=p?png_create_info_struct(p):NULL;
  if(!p||!i) { free(b.p); b.p=NULL; return b; }
  if(setjmp(png_jmpbuf(p))) { png_destroy_read_struct(&p,&i,NULL); free(b.p); b.p=NULL; return b; }
  png_set_read_fn(p,&e,png_read_bytes); png_read_info(p,i);
  if(png_get_image_width(p,i)!=width||png_get_image_height(p,i)!=height||png_get_interlace_type(p,i)!=PNG_INTERLACE_NONE) png_error(p,"shape mismatch");
  int color=png_get_color_type(p,i),depth=png_get_bit_depth(p,i),trns=png_get_valid(p,i,PNG_INFO_tRNS)!=0;
  if(depth>8) png_error(p,"unexpected bit depth");
  if(color==PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(p);
  if(color==PNG_COLOR_TYPE_GRAY&&depth<8) png_set_expand_gray_1_2_4_to_8(p);
  if(trns) png_set_tRNS_to_alpha(p);
  if(color==PNG_COLOR_TYPE_GRAY||color==PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(p);
  if(channels==4&&!(color&PNG_COLOR_MASK_ALPHA)&&!trns) png_set_add_alpha(p,255,PNG_FILLER_AFTER);
  if(channels==3) png_set_strip_alpha(p);
  png_read_update_info(p,i);
  if(png_get_bit_depth(p,i)!=8||png_get_channels(p,i)!=channels||png_get_rowbytes(p,i)!=(size_t)width*channels) png_error(p,"output shape mismatch");
  for(unsigned y=0;y<height;++y) png_read_row(p,b.p+(size_t)y*width*channels,NULL);
  png_read_end(p,i); png_destroy_read_struct(&p,&i,NULL); b.size=size; return b;
 }
#endif
 return b;
}
static const char *const extensions[] = {"tcom","qoi","png1","png3","png6","oxi.png"};
static const char *const names[] = {"tcom","qoi","libpng_level_1","libpng_level_3","libpng_level_6","oxipng_preset_4"};
int main(int argc,char **argv) {
#ifdef BENCH_LIBPNG
  if(argc==2 && !strcmp(argv[1],"versions")) {
    printf("libpng=%s zlib=%s\n",png_get_libpng_ver(NULL),zlibVersion());
    return 0;
  }
#endif
  #ifdef BENCH_OXIPNG
  HMODULE oxi=LoadLibraryW(L"oxi_bench.dll");
  if(!oxi) return 9;
  oxi_encode=(void *)GetProcAddress(oxi,"oxi_encode");
  oxi_free=(void *)GetProcAddress(oxi,"oxi_free");
  if(!oxi_encode||!oxi_free) return 9;
#endif
  if(argc!=8) { fprintf(stderr,"bench raw width height channels reps order output-prefix\n"); return 1; }
  width=(unsigned)strtoul(argv[2],NULL,10); height=(unsigned)strtoul(argv[3],NULL,10);
  channels=(unsigned)strtoul(argv[4],NULL,10); int reps=atoi(argv[5]),order=atoi(argv[6]);
  if(!width||!height||(channels!=3&&channels!=4)||reps<1||reps>20) return 1;
  size_t size=(size_t)width*height*channels; unsigned char *raw=(unsigned char *)malloc(size);
  FILE *f=fopen(argv[1],"rb"); if(!raw||!f||fread(raw,1,size,f)!=size||fgetc(f)!=EOF) return 2; fclose(f);
  QueryPerformanceFrequency(&frequency); DWORD_PTR affinity=2; if(!SetProcessAffinityMask(GetCurrentProcess(),affinity)) return 8;
  Buffer initial[CODECS]; unsigned loops[CODECS]; double enc[CODECS]={0},dec[CODECS]={0}; size_t checks[CODECS]={0};
  double enc_reps[CODECS][20]={{0}},dec_reps[CODECS][20]={{0}};
  for(int step=0;step<CODECS;++step) {
    int c=(order+step)%CODECS; double t=now(); initial[c]=encode(c,raw,size); double elapsed=now()-t;
    if(!initial[c].p||!initial[c].size) return 3;
    loops[c]=reps==1 ? 1 : (unsigned)(0.012/(elapsed>0?elapsed:1)); if(loops[c]<1) loops[c]=1; if(loops[c]>32) loops[c]=32;
    Buffer d=decode(c,initial[c],size); if(!d.p||d.size!=size||memcmp(d.p,raw,size)) return 4; free(d.p); ++checks[c];
    char path[4096]; snprintf(path,sizeof path,"%s.%s",argv[7],extensions[c]);
    f=fopen(path,"wb"); if(!f||fwrite(initial[c].p,1,initial[c].size,f)!=initial[c].size||fclose(f)) return 5;
  }
  for(int r=0;r<reps;++r) for(int step=0;step<CODECS;++step) {
    int c=(order+r+step)%CODECS;
    for(unsigned j=0;j<loops[c];++j) {
      double t=now(); Buffer e=encode(c,raw,size); double elapsed=now()-t; enc[c]+=elapsed; enc_reps[c][r]+=elapsed;
      if(!e.p||e.size!=initial[c].size||memcmp(e.p,initial[c].p,e.size)) return 6; free_encoded(c,e);
      t=now(); Buffer d=decode(c,initial[c],size); elapsed=now()-t; dec[c]+=elapsed; dec_reps[c][r]+=elapsed;
      if(!d.p||d.size!=size||memcmp(d.p,raw,size)) return 7; free(d.p); ++checks[c];
    }
  }
  printf("{\"codecs\":[");
  for(int c=0;c<CODECS;++c) {
    printf("%s{\"name\":\"%s\",\"bytes\":%zu,\"encode_sec\":%.12f,\"decode_sec\":%.12f,\"loops\":%u,\"checks\":%zu,\"encode_reps\":[",
      c?",":"",names[c],initial[c].size,enc[c]/(reps*loops[c]),dec[c]/(reps*loops[c]),loops[c],checks[c]);
    for(int r=0;r<reps;++r) printf("%s%.12f",r?",":"",enc_reps[c][r]/loops[c]);
    printf("],\"decode_reps\":[");
    for(int r=0;r<reps;++r) printf("%s%.12f",r?",":"",dec_reps[c][r]/loops[c]);
    printf("]}");
    free_encoded(c,initial[c]);
  }
  puts("]}"); free(raw); return 0;
}
