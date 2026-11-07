#ifndef _WIN32
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <time.h>
#endif
#ifdef BENCH_INTERACTIVE
#define QOIR_IMPLEMENTATION
#include "qoir.h"
#define QOIR_CODEC 2
#define FIRST_PNG 3
#else
#define FIRST_PNG 2
#endif
#ifdef BENCH_LIBPNG
#include <png.h>
#include <zlib.h>
#ifdef BENCH_OXIPNG
#define CODECS 6
typedef struct { unsigned char *data; size_t size; } OxiBuffer;
static OxiBuffer (*oxi_encode)(const unsigned char *,size_t,unsigned,unsigned,unsigned);
static void (*oxi_free)(unsigned char *,size_t);
#elif defined(BENCH_INTERACTIVE)
#define CODECS 12
#else
#define CODECS 5
#endif
#ifdef BENCH_INTERACTIVE
static const int levels[] = {1,2,3,4,5,6,7,8,9};
#else
static const int levels[] = {1,3,6};
#endif
#elif defined(BENCH_INTERACTIVE)
#define CODECS 3
#else
#define CODECS 2
#endif
#ifdef BENCH_FPNG
#include "fpng_adapter.h"
#define FPNG_CODEC 12
#undef CODECS
#define CODECS 13
#endif
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../third_party/qoi/qoi.h"
#include "../tcom.c"
typedef struct { unsigned char *p; size_t size, cap, pos; void *owner; } Buffer;
static unsigned width,height,channels;
#ifdef _WIN32
#define seek_file _fseeki64
#define tell_file _ftelli64
static LARGE_INTEGER frequency;
static double now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart/frequency.QuadPart; }
static int pin_cpu(void) { return SetProcessAffinityMask(GetCurrentProcess(), 2) != 0; }
#else
#define seek_file fseeko
#define tell_file ftello
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static int pin_cpu(void) {
    cpu_set_t allowed, selected;
    if (sched_getaffinity(0, sizeof(allowed), &allowed)) return 0;
    CPU_ZERO(&selected);
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) {
            CPU_SET(cpu, &selected);
            return sched_setaffinity(0, sizeof(selected), &selected) == 0;
        }
    }
    return 0;
}
#endif
#ifdef BENCH_INTERACTIVE
static qoir_pixel_format qoir_format(void) {
    return channels == 3 ? QOIR_PIXEL_FORMAT__RGB : QOIR_PIXEL_FORMAT__RGBA_NONPREMUL;
}

static Buffer encode_qoir(const unsigned char *raw) {
    qoir_pixel_buffer source = {
        .pixcfg = {qoir_format(), width, height},
        .data = (unsigned char *)raw,
        .stride_in_bytes = (size_t)width * channels,
    };
    qoir_encode_result result = qoir_encode(&source, NULL);
    if (result.status_message) {
        fprintf(stderr, "QOIR encode: %s\n", result.status_message);
        free(result.owned_memory);
        return (Buffer){0};
    }
    return (Buffer){.p = result.dst_ptr, .size = result.dst_len, .owner = result.owned_memory};
}

static Buffer decode_qoir(Buffer encoded) {
    qoir_decode_options options = {.pixfmt = qoir_format()};
    qoir_decode_result result = qoir_decode(encoded.p, encoded.size, &options);
    qoir_pixel_buffer pixels = result.dst_pixbuf;
    if (result.status_message || pixels.pixcfg.width_in_pixels != width ||
        pixels.pixcfg.height_in_pixels != height || pixels.pixcfg.pixfmt != options.pixfmt ||
        pixels.stride_in_bytes != (size_t)width * channels) {
        fprintf(stderr, "QOIR decode: %s\n",
                result.status_message ? result.status_message : "output layout mismatch");
        free(result.owned_memory);
        return (Buffer){0};
    }
    return (Buffer){.p = pixels.data, .size = pixels.stride_in_bytes * height,
                    .owner = result.owned_memory};
}
#endif
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
#ifdef BENCH_FPNG
  if (codec == FPNG_CODEC) { fpng_bench_free(b.owner); return; }
#endif
#ifdef BENCH_OXIPNG
  if(codec==5) { oxi_free(b.p,b.size); return; }
#endif
  free(b.owner ? b.owner : b.p);
}
static void free_decoded(int codec,Buffer b) {
#ifdef BENCH_FPNG
  if (codec == FPNG_CODEC) { fpng_bench_free(b.owner); return; }
#endif
  free(b.owner ? b.owner : b.p);
}
static Buffer encode(int c,const unsigned char *raw,size_t size) {
#ifdef BENCH_FPNG
    if (c == FPNG_CODEC) {
        FpngResult result = fpng_bench_encode(raw, width, height, channels);
        return (Buffer){.p = result.data, .size = result.size, .owner = result.owner};
    }
#endif
#ifdef BENCH_INTERACTIVE
    if (c == QOIR_CODEC) {
        return encode_qoir(raw);
    }
#endif
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
 if(c>=FIRST_PNG) {
  b.cap=size*2+4096; b.p=malloc(b.cap); b.size=0;
  if(!b.p) return b;
  png_structp p=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
  png_infop i=p?png_create_info_struct(p):NULL;
  if(!p||!i) { free(b.p); b.p=NULL; return b; }
  if(setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p,&i); free(b.p); b.p=NULL; return b; }
  png_set_write_fn(p,&b,png_write_bytes,NULL);
  png_set_IHDR(p,i,width,height,8,channels==4?PNG_COLOR_TYPE_RGBA:PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
  png_set_compression_level(p,levels[c-FIRST_PNG]); png_set_filter(p,PNG_FILTER_TYPE_BASE,PNG_ALL_FILTERS);
  png_write_info(p,i);
  for(unsigned y=0;y<height;++y) png_write_row(p,(png_bytep)raw+(size_t)y*width*channels);
  png_write_end(p,i); png_destroy_write_struct(&p,&i); return b;
 }
#endif
 return b;
}
static Buffer decode(int c,Buffer e,size_t size) {
#ifdef BENCH_FPNG
    if (c == FPNG_CODEC) {
        FpngResult result = fpng_bench_decode(e.p, e.size, width, height, channels);
        return (Buffer){.p = result.data, .size = result.size, .owner = result.owner};
    }
#endif
#ifdef BENCH_INTERACTIVE
    if (c == QOIR_CODEC) {
        return decode_qoir(e);
    }
#endif
 Buffer b={0};
 if(c==1) { qoi_desc d={0}; b.p=qoi_decode(e.p,(int)e.size,&d,channels); if(b.p && d.width==width && d.height==height && d.channels==channels) b.size=(size_t)d.width*d.height*d.channels; }
 else { b.p=malloc(size); if(c==0 && b.p) { unsigned v[3]; b.size=tcom(e.p,e.size,b.p,size,v,1); } }
#ifdef BENCH_LIBPNG
 if(c>=FIRST_PNG) {
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
#ifdef BENCH_INTERACTIVE
static const char *const names[] = {
    "tcom", "qoi_reference", "qoir", "libpng_level_1", "libpng_level_2", "libpng_level_3",
    "libpng_level_4", "libpng_level_5", "libpng_level_6", "libpng_level_7", "libpng_level_8",
    "libpng_level_9", "fpng_single_pass",
};

static int file_operation(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr, "bench encode|decode codec input output width height channels\n");
        return 1;
    }
    char *end;
    unsigned long codec = strtoul(argv[2], &end, 10);
    if (!*argv[2] || *end || codec >= CODECS) return 1;
#ifdef BENCH_FPNG
    if (codec == FPNG_CODEC && !fpng_bench_init()) {
        fprintf(stderr, "fpng requires SSE4.1 support\n");
        return 1;
    }
#endif
    width = (unsigned)strtoul(argv[5], NULL, 10);
    height = (unsigned)strtoul(argv[6], NULL, 10);
    channels = (unsigned)strtoul(argv[7], NULL, 10);
    if (!width || !height || (channels != 3 && channels != 4) ||
        (uint64_t)width * height > (SIZE_MAX - 4096) / 2 / channels) return 1;
    if (!pin_cpu()) return 8;
    size_t size = (size_t)width * height * channels;
    int encoding = !strcmp(argv[1], "encode");
    FILE *input_file = fopen(argv[3], "rb");
    if (!input_file) return 2;
    if (seek_file(input_file, 0, SEEK_END)) {
        fclose(input_file);
        return 2;
    }
    int64_t length = tell_file(input_file);
    if (length <= 0 || (uint64_t)length > SIZE_MAX ||
        (encoding && (uint64_t)length != size) || seek_file(input_file, 0, SEEK_SET)) {
        fclose(input_file);
        return 2;
    }
    Buffer input = {.size = (size_t)length};
    input.p = malloc(input.size);
    if (!input.p) {
        fclose(input_file);
        return 2;
    }
    int read_failed = fread(input.p, 1, input.size, input_file) != input.size;
    if (fclose(input_file)) read_failed = 1;
    if (read_failed) {
        free(input.p);
        return 2;
    }
    Buffer output = encoding ? encode((int)codec, input.p, size) : decode((int)codec, input, size);
    free(input.p);
    if (!output.p || !output.size || (!encoding && output.size != size)) {
        free_encoded((int)codec, output);
        fprintf(stderr, "%s %s failed\n", names[codec], argv[1]);
        return 3;
    }
    FILE *output_file = fopen(argv[4], "wb");
    int write_failed = output_file == NULL;
    if (output_file) {
        if (fwrite(output.p, 1, output.size, output_file) != output.size) write_failed = 1;
        if (fclose(output_file)) write_failed = 1;
    }
    free_encoded((int)codec, output);
    return write_failed ? 5 : 0;
}
#else
static const char *const extensions[] = {"tcom","qoi","png1","png3","png6","oxi.png"};
static const char *const names[] = {"tcom","qoi","libpng_level_1","libpng_level_3","libpng_level_6","oxipng_preset_4"};
#endif
int main(int argc,char **argv) {
#ifdef BENCH_LIBPNG
  if(argc==2 && !strcmp(argv[1],"versions")) {
    printf("libpng=%s zlib=%s\n",png_get_libpng_ver(NULL),zlibVersion());
    return 0;
  }
#endif
#ifdef BENCH_INTERACTIVE
  if (argc > 1 && (!strcmp(argv[1], "encode") || !strcmp(argv[1], "decode"))) {
      return file_operation(argc, argv);
  }
#endif
  #ifdef BENCH_OXIPNG
  HMODULE oxi=LoadLibraryW(L"oxi_bench.dll");
  if(!oxi) return 9;
  oxi_encode=(void *)GetProcAddress(oxi,"oxi_encode");
  oxi_free=(void *)GetProcAddress(oxi,"oxi_free");
  if(!oxi_encode||!oxi_free) return 9;
#endif
  int selected[CODECS];
  int selected_count = 0;
  int no_warmup = 0;
#ifdef BENCH_INTERACTIVE
  no_warmup = argc == 9 && !strcmp(argv[8], "--no-warmup");
  if (argc != 8 && !no_warmup) {
      fprintf(stderr, "bench raw width height channels reps order codec-mask [--no-warmup]\n");
      return 1;
  }
  char *end;
  unsigned long mask = strtoul(argv[7], &end, 0);
  if (!mask || *end || mask >= (1u << CODECS)) {
      fprintf(stderr, "Invalid codec selection\n");
      return 1;
  }
  for (int codec = 0; codec < CODECS; ++codec) {
      if (mask & (1u << codec)) {
          selected[selected_count++] = codec;
      }
  }
#ifdef BENCH_FPNG
  if ((mask & (1u << FPNG_CODEC)) && !fpng_bench_init()) {
      fprintf(stderr, "fpng requires SSE4.1 support\n");
      return 1;
  }
#endif
#else
  if(argc!=8) { fprintf(stderr,"bench raw width height channels reps order output-prefix\n"); return 1; }
  for (int codec = 0; codec < CODECS; ++codec) {
      selected[selected_count++] = codec;
  }
#endif
  width=(unsigned)strtoul(argv[2],NULL,10); height=(unsigned)strtoul(argv[3],NULL,10);
  channels=(unsigned)strtoul(argv[4],NULL,10); int reps=atoi(argv[5]),order=atoi(argv[6]);
  if(!width||!height||(channels!=3&&channels!=4)||reps<1||reps>20 ||
     order<0 || order>=selected_count || (uint64_t)width*height>(SIZE_MAX-4096)/2/channels) return 1;
  size_t size=(size_t)width*height*channels; unsigned char *raw=(unsigned char *)malloc(size);
  FILE *f=fopen(argv[1],"rb"); if(!raw||!f||fread(raw,1,size,f)!=size||fgetc(f)!=EOF) return 2; fclose(f);
#ifdef _WIN32
  QueryPerformanceFrequency(&frequency);
#endif
  if (!pin_cpu()) return 8;
  Buffer initial[CODECS]; unsigned loops[CODECS]; double enc[CODECS]={0},dec[CODECS]={0}; size_t checks[CODECS]={0};
  double enc_reps[CODECS][20]={{0}},dec_reps[CODECS][20]={{0}};
  for(int step=0;step<selected_count;++step) {
    int c = selected[(order + step) % selected_count];
    double start = now();
    initial[c] = encode(c, raw, size);
    double elapsed = now() - start;
    if (!initial[c].p || !initial[c].size) {
        fprintf(stderr, "%s encode failed\n", names[c]);
        return 3;
    }
    loops[c]=(reps==1 || no_warmup) ? 1 : (unsigned)(0.012/(elapsed>0?elapsed:1)); if(loops[c]<1) loops[c]=1; if(loops[c]>32) loops[c]=32;
    start = now();
    Buffer d = decode(c, initial[c], size);
    double decode_elapsed = now() - start;
    if (!d.p || d.size != size || memcmp(d.p, raw, size)) {
        fprintf(stderr, "%s pixel check failed\n", names[c]);
        return 4;
    }
    free_decoded(c, d);
    ++checks[c];
    if (no_warmup) {
        enc[c] = enc_reps[c][0] = elapsed;
        dec[c] = dec_reps[c][0] = decode_elapsed;
    }
#ifndef BENCH_INTERACTIVE
    char path[4096]; snprintf(path,sizeof path,"%s.%s",argv[7],extensions[c]);
    f=fopen(path,"wb"); if(!f||fwrite(initial[c].p,1,initial[c].size,f)!=initial[c].size||fclose(f)) return 5;
#endif
  }
  for(int r=no_warmup ? 1 : 0;r<reps;++r) for(int step=0;step<selected_count;++step) {
    int c=selected[(order+r+step)%selected_count];
    for(unsigned j=0;j<loops[c];++j) {
      double t=now(); Buffer e=encode(c,raw,size); double elapsed=now()-t; enc[c]+=elapsed; enc_reps[c][r]+=elapsed;
      if (!e.p || e.size != initial[c].size || memcmp(e.p, initial[c].p, e.size)) {
          fprintf(stderr, "%s encoded bytes changed\n", names[c]);
          return 6;
      }
      free_encoded(c, e);
      t=now(); Buffer d=decode(c,initial[c],size); elapsed=now()-t; dec[c]+=elapsed; dec_reps[c][r]+=elapsed;
      if (!d.p || d.size != size || memcmp(d.p, raw, size)) {
          fprintf(stderr, "%s timed pixel check failed\n", names[c]);
          return 7;
      }
      free_decoded(c, d);
      ++checks[c];
    }
  }
  printf("{\"codecs\":[");
  for(int slot=0;slot<selected_count;++slot) {
    int c=selected[slot];
    printf("%s{\"name\":\"%s\",\"bytes\":%zu,\"encode_sec\":%.12f,\"decode_sec\":%.12f,\"loops\":%u,\"checks\":%zu,\"encode_reps\":[",
      slot?",":"",names[c],initial[c].size,enc[c]/(reps*loops[c]),dec[c]/(reps*loops[c]),loops[c],checks[c]);
    for(int r=0;r<reps;++r) printf("%s%.12f",r?",":"",enc_reps[c][r]/loops[c]);
    printf("],\"decode_reps\":[");
    for(int r=0;r<reps;++r) printf("%s%.12f",r?",":"",dec_reps[c][r]/loops[c]);
    printf("]}");
    free_encoded(c,initial[c]);
  }
  puts("]}"); free(raw); return 0;
}
