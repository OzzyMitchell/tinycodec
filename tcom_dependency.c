#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <errno.h>
#include <locale.h>
#include <ctype.h>
#include <limits.h>
#include <png.h>
#include <webp/decode.h>
#include <webp/encode.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 1000000
#include <stb/stb_image.h>
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include <qoi/qoi.h>
#include "tcom.c"
static void usage(void) { puts("tcom pack input [output.tcom]\ntcom unpack input.tcom [output]"); }
static wchar_t *output_name(const wchar_t *input, int decode) { const wchar_t *base = input, *dot = NULL; for (const wchar_t *p = input; *p; ++p) {
if (*p == L'/' || *p == L'\\') { base = p + 1; dot = NULL; } else if (*p == L'.' && p != base) dot = p; }
size_t stem = dot ? (size_t)(dot - input) : wcslen(input); const wchar_t *suffix = decode ? L".unpacked.png" : L".tcom"; size_t extra = wcslen(suffix) + 1;
if (stem > SIZE_MAX / sizeof(wchar_t) - extra) return NULL; wchar_t *name = malloc((stem + extra) * sizeof *name);
if (name) { wmemcpy(name, input, stem); wcscpy(name + stem, suffix); } return name; }
static unsigned char *read_file(const wchar_t *path, size_t *size) { FILE *f = _wfopen(path, L"rb"); if (!f) { _wperror(path); return NULL; }
if (_fseeki64(f, 0, SEEK_END)) { fclose(f); return NULL; } __int64 length = _ftelli64(f);
if (length < 0 || (uint64_t)length > SIZE_MAX || _fseeki64(f, 0, SEEK_SET)) { fclose(f); return NULL; } *size = (size_t)length;
unsigned char *data = malloc(*size ? *size : 1); int ok = data && fread(data, 1, *size, f) == *size && !ferror(f); if (fclose(f)) ok = 0;
if (!ok) { free(data); return NULL; } return data; }
static int reject_animation(png_structp png, png_unknown_chunkp chunk) { if (!memcmp(chunk->name, "acTL", 4)) png_error(png, "Animated PNGs are not supported");
return 0; }
static int read_png(const wchar_t *path, unsigned char **pixels, unsigned v[3], size_t *size) { FILE *f = _wfopen(path, L"rb");
if (!f) { _wperror(path); return 0; } png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
png_infop i = p ? png_create_info_struct(p) : NULL; if (!p || !i) { png_destroy_read_struct(&p, &i, NULL); fclose(f); return 0; }
if (setjmp(png_jmpbuf(p))) { png_destroy_read_struct(&p, &i, NULL); fclose(f); return 0; } png_init_io(p, f);
png_set_keep_unknown_chunks(p, PNG_HANDLE_CHUNK_ALWAYS, (png_const_bytep)"acTL", 1); png_set_read_user_chunk_fn(p, NULL, reject_animation); png_read_info(p, i);
int color = png_get_color_type(p, i), depth = png_get_bit_depth(p, i); if (depth > 8) png_error(p, "16-bit PNG is not supported");
if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(p); if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(p);
if (png_get_valid(p, i, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(p);
int passes = png_set_interlace_handling(p); png_read_update_info(p, i); v[0] = png_get_image_width(p, i); v[1] = png_get_image_height(p, i);
v[2] = png_get_channels(p, i); if (!v[0] || v[0] > 1000000 || !v[1] || v[2] < 1 || v[2] > 4 || (size_t)v[0] > SIZE_MAX / v[1] / v[2]) png_error(p, "Unsupported image dimensions"); size_t stride = (size_t)v[0] * v[2];
if (png_get_rowbytes(p, i) != stride) png_error(p, "Unexpected pixel layout"); *size = stride * v[1]; *pixels = calloc(*size, 1);
if (!*pixels) png_error(p, "Not enough memory"); for (int pass = 0; pass < passes; ++pass) for (unsigned y = 0; y < v[1]; ++y) png_read_row(p, *pixels + y * stride, NULL); png_read_end(p, i); png_destroy_read_struct(&p, &i, NULL);
return fclose(f) == 0; }
static int write_png(FILE *f, const unsigned char *pixels, const unsigned v[3]) {
const int colors[] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_RGBA};
png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); png_infop i = p ? png_create_info_struct(p) : NULL;
if (!p || !i) { png_destroy_write_struct(&p, &i); return 0; } if (setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p, &i); return 0; } png_init_io(p, f);
png_set_IHDR(p, i, v[0], v[1], 8, colors[v[2] - 1], PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT); png_write_info(p, i);
for (unsigned y = 0; y < v[1]; ++y) png_write_row(p, (png_bytep)pixels + (size_t)y * v[0] * v[2]); png_write_end(p, i); png_destroy_write_struct(&p, &i);
return 1; }
static size_t image_size(const unsigned v[3]) { return !v[0] || v[0]>1000000 || !v[1] || v[2]<1 || v[2]>4 || (size_t)v[0]>SIZE_MAX/v[1]/v[2] ? 0 : (size_t)v[0]*v[1]*v[2]; }
static void pnm_space(const unsigned char **p,const unsigned char *end) { for (;;) { while (*p<end && isspace(**p)) ++*p;
if (*p==end || **p!='#') return; while (*p<end && **p!='\r' && **p!='\n') ++*p; } }
static int pnm_number(const unsigned char **p,const unsigned char *end,unsigned *value) { pnm_space(p,end); if (*p==end || !isdigit(**p)) return 0;
unsigned n=0; do { unsigned d=*(*p)++-'0'; if (n>(UINT_MAX-d)/10) return 0; n=n*10+d; } while (*p<end && isdigit(**p));
if (*p<end && !isspace(**p) && **p!='#') return 0; *value=n; return 1; }
static int read_pnm(const unsigned char *data,size_t len,unsigned char **pixels,unsigned v[3],size_t *size) {
const unsigned char *p=data+2,*end=data+len; unsigned max=1; int kind=data[1]-'0';
if (kind==7) { unsigned seen=0; char tuple[64]=""; if (p==end || (*p!='\n' && *p!='\r')) return 0;
while (p<end) { const unsigned char *next=memchr(p,'\n',(size_t)(end-p)); if (!next || next-p>=255) return 0;
char line[256],key[32],value[128],extra; size_t n=(size_t)(next-p); memcpy(line,p,n); line[n]=0; p=next+1;
char *q=line; while (isspace((unsigned char)*q)) ++q; if (!*q || *q=='#') continue;
int fields=sscanf(q,"%31s %127s %c",key,value,&extra); if (!strcmp(key,"ENDHDR")) { if (fields!=1 || seen!=15) return 0; break; }
if (fields!=2) return 0; if (!strcmp(key,"TUPLTYPE")) { if (*tuple || strlen(value)>=sizeof tuple) return 0; strcpy(tuple,value); continue; }
unsigned bit=0,*dst=NULL; if (!strcmp(key,"WIDTH")) {bit=1;dst=v;} else if (!strcmp(key,"HEIGHT")) {bit=2;dst=v+1;}
else if (!strcmp(key,"DEPTH")) {bit=4;dst=v+2;} else if (!strcmp(key,"MAXVAL")) {bit=8;dst=&max;} else return 0;
const unsigned char *a=(const unsigned char *)value,*b=a+strlen(value); if ((seen&bit) || !pnm_number(&a,b,dst) || a!=b) return 0; seen|=bit; }
const char *types[]={"GRAYSCALE","GRAYSCALE_ALPHA","RGB","RGB_ALPHA"}; if (seen!=15 || !image_size(v) || (max!=255 && max!=1)) return 0;
if (*tuple && strcmp(tuple,types[v[2]-1]) && !(max==1 && v[2]<=2 && !strcmp(tuple,v[2]==1?"BLACKANDWHITE":"BLACKANDWHITE_ALPHA"))) return 0;
} else { if (!pnm_number(&p,end,v) || !pnm_number(&p,end,v+1)) return 0; v[2]=(kind==3 || kind==6)?3:1;
if (kind!=1 && kind!=4 && (!pnm_number(&p,end,&max) || max!=255)) return 0; }
*size=image_size(v); if (!*size) return 0;
if (kind>=4 && kind!=7) { if (p<end && *p=='#') while (p<end && *p!='\r' && *p!='\n') ++p;
if (p==end || !isspace(*p)) return 0; unsigned delimiter=*p++; size_t raster=kind==4?((size_t)v[0]+7)/8*v[1]:*size;
if (delimiter=='\r' && p<end && *p=='\n' && (size_t)(end-p)==raster+1) ++p; }
if (kind>=4 && (size_t)(end-p)!=(kind==4?((size_t)v[0]+7)/8*v[1]:*size)) return 0;
*pixels=malloc(*size); if (!*pixels) return 0;
for (size_t j=0;j<*size;++j) { unsigned a; if (kind==1) { pnm_space(&p,end); if (p==end || (*p!='0' && *p!='1')) return 0; a=*p++-'0'; }
else if (kind<4) { if (!pnm_number(&p,end,&a) || a>max) return 0; }
else if (kind==4) a=(p[(j/v[0])*(((size_t)v[0]+7)/8)+(j%v[0])/8]>>(7-j%v[0]%8))&1;
else { a=*p++; if (a>max) return 0; } (*pixels)[j]=(unsigned char)((kind==1 || kind==4)?255-a*255:max==1?a*255:a); }
if (kind<4) { pnm_space(&p,end); if (p!=end) return 0; } return 1; }
static unsigned read_be32(const unsigned char *p) { return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3]; }
static int valid_qoi(const unsigned char *s,size_t len,unsigned v[3]) { if (len<22 || memcmp(s,"qoif",4) || s[12]<3 || s[12]>4 || s[13]>1) return 0;
v[0]=read_be32(s+4);v[1]=read_be32(s+8);v[2]=s[12]; if (!image_size(v) || memcmp(s+len-8,"\0\0\0\0\0\0\0\1",8)) return 0;
size_t i=14,pixels=(size_t)v[0]*v[1]; while (pixels) { if (i>=len-8) return 0; unsigned tag=s[i++],skip=0,run=1;
if (tag==254) skip=3; else if (tag==255) skip=4; else if ((tag&192)==128) skip=1; else if ((tag&192)==192) run=(tag&63)+1;
if (skip>len-8-i || run>pixels) return 0; i+=skip;pixels-=run; } return i==len-8; }
static int read_image(const wchar_t *path,unsigned char **pixels,unsigned v[3],size_t *size) { size_t len=0; unsigned char *data=read_file(path,&len); if (!data) return 0;
int ok=0; if (len>=8 && !memcmp(data,"\211PNG\r\n\032\n",8)) { free(data);return read_png(path,pixels,v,size); }
if (len>=3 && data[0]=='P' && data[1]>='1' && data[1]<='7' && isspace(data[2])) ok=read_pnm(data,len,pixels,v,size);
else if (len>=22 && !memcmp(data,"qoif",4) && len<=INT_MAX && valid_qoi(data,len,v)) { qoi_desc d; *pixels=qoi_decode(data,(int)len,&d,0);ok=*pixels!=NULL; }
else if (len>=12 && !memcmp(data,"RIFF",4) && !memcmp(data+8,"WEBP",4) && (size_t)G(data+4,4)+8==len) { WebPBitstreamFeatures f;
if (WebPGetFeatures(data,len,&f)==VP8_STATUS_OK && !f.has_animation) { v[0]=f.width;v[1]=f.height;v[2]=f.has_alpha?4:3; *size=image_size(v);
if (*size && (*pixels=malloc(*size))) ok=(v[2]==4?WebPDecodeRGBAInto(data,len,*pixels,*size,(int)(v[0]*v[2])):WebPDecodeRGBInto(data,len,*pixels,*size,(int)(v[0]*v[2])))!=NULL; } }
else if (len>=54 && data[0]=='B' && data[1]=='M' && len<=INT_MAX) { unsigned header=G(data+14,4),offset=G(data+10,4),bits=data[28]|data[29]<<8,compression=G(data+30,4);
if ((header!=40 && header!=108 && header!=124) || (bits!=1 && bits!=4 && bits!=8 && bits!=16 && bits!=24 && bits!=32) || (compression!=0 && (compression!=3 || (bits!=16 && bits!=32)))) {free(data);return 0;}
int32_t height=(int32_t)G(data+22,4);size_t w=G(data+18,4),h=height<0?(size_t)(-(int64_t)height):(size_t)height,stride=(w*bits+31)/32*4;
unsigned start=14+header+(header==40 && compression==3?12:0);
if (w && w<=1000000 && h && h<=1000000 && offset>=start && offset<=len && h<=(len-offset)/stride) {
if (bits<16) { unsigned count=(offset-start)/4; if ((offset-start)%4 || !count || count>(1u<<bits)) {free(data);return 0;}
for(size_t y=0;y<h;++y)for(size_t x=0;x<w;++x)if(((data[offset+y*stride+x*bits/8]>>(8-bits-x*bits%8))&((1u<<bits)-1))>=count){free(data);return 0;}
} else if (offset!=start) {memmove(data+start,data+offset,len-offset);len-=offset-start;W(data+10,4,start);}
int x,y,n; *pixels=stbi_load_from_memory(data,(int)len,&x,&y,&n,0);
if (*pixels) {v[0]=x;v[1]=y;v[2]=n;ok=1;} } }
free(data); if (ok) { *size=image_size(v);ok=*size!=0; } return ok; }
static int output_format(const wchar_t *path) { const wchar_t *dot=wcsrchr(path,L'.'); if (!dot) return 0;
const wchar_t *ext[]={L".png",L".bmp",L".pnm",L".pgm",L".ppm",L".pbm",L".pam",L".qoi",L".webp"};
for (int i=0;i<9;++i) if (!_wcsicmp(dot,ext[i])) return i+1; return 0; }
static int write_pnm(FILE *f,int kind,const unsigned char *p,const unsigned v[3]) { size_t size=image_size(v); unsigned n=v[2];
if (kind==7) { const char *types[]={"GRAYSCALE","GRAYSCALE_ALPHA","RGB","RGB_ALPHA"};
fprintf(f,"P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL 255\nTUPLTYPE %s\nENDHDR\n",v[0],v[1],n,types[n-1]); }
else if (kind==6) { if (n!=1) return 0; for (size_t j=0;j<size;++j) if (p[j]!=0 && p[j]!=255) return 0;
fprintf(f,"P4\n%u %u\n",v[0],v[1]); for (unsigned y=0;y<v[1];++y) for (unsigned x=0;x<v[0];x+=8) { unsigned b=0;
for (unsigned k=0;k<8 && x+k<v[0];++k) b|=(p[(size_t)y*v[0]+x+k]==0)<<(7-k); if (fputc((int)b,f)==EOF) return 0; }
return !ferror(f); }
else { if ((n!=1 && n!=3) || (kind==4 && n!=1) || (kind==5 && n!=3)) return 0; fprintf(f,"P%u\n%u %u\n255\n",n==1?5:6,v[0],v[1]); }
return fwrite(p,1,size,f)==size && !ferror(f); }
static int write_bmp(FILE *f,const unsigned char *p,const unsigned v[3]) { unsigned n=v[2],head=n==4?122:54; size_t stride=((size_t)v[0]*n+3)&~(size_t)3;
if (v[1]>INT_MAX || stride>(UINT32_MAX-head)/v[1]) return 0; unsigned char header[122]={0}; memcpy(header,"BM",2);
W(header+2,4,(unsigned)(head+stride*v[1]));W(header+10,4,head);W(header+14,4,head-14);W(header+18,4,v[0]);W(header+22,4,v[1]);W(header+26,2,1);W(header+28,2,n*8);
if (n==4) {W(header+30,4,3);W(header+54,4,0xff0000);W(header+58,4,0xff00);W(header+62,4,0xff);W(header+66,4,0xff000000);W(header+70,4,0x73524742);}
unsigned char *row=calloc(stride,1); if (!row) return 0; int ok=fwrite(header,1,head,f)==head;
for (unsigned y=v[1];ok && y-->0;) { const unsigned char *s=p+(size_t)y*v[0]*n; for (unsigned x=0;x<v[0];++x) {size_t j=(size_t)x*n;row[j]=s[j+2];row[j+1]=s[j+1];row[j+2]=s[j];if(n==4)row[j+3]=s[j+3];} ok=fwrite(row,1,stride,f)==stride; }
free(row);return ok; }
static int webp_write(const uint8_t *data,size_t size,const WebPPicture *p) { return fwrite(data,1,size,(FILE *)p->custom_ptr)==size; }
static int write_webp(FILE *f,const unsigned char *p,const unsigned v[3]) { WebPConfig c;WebPPicture image;
if (!WebPConfigInit(&c) || !WebPPictureInit(&image) || v[0]>WEBP_MAX_DIMENSION || v[1]>WEBP_MAX_DIMENSION) return 0;
c.lossless=1;c.exact=1;image.width=(int)v[0];image.height=(int)v[1];image.use_argb=1;image.writer=webp_write;image.custom_ptr=f;
int ok=v[2]==4?WebPPictureImportRGBA(&image,p,(int)v[0]*4):WebPPictureImportRGB(&image,p,(int)v[0]*3);
if (ok) ok=WebPEncode(&c,&image);WebPPictureFree(&image);return ok; }
static int write_image(FILE *f,int kind,const unsigned char *p,const unsigned original[3]) { unsigned v[3]={original[0],original[1],original[2]};
if (kind==1) return write_png(f,p,v); if (kind>=3 && kind<=7) return write_pnm(f,kind,p,v);
unsigned char *rgb=NULL; if (v[2]<3) {size_t count=(size_t)v[0]*v[1];unsigned n=v[2]+2;if(count>SIZE_MAX/n)return 0;rgb=malloc(count*n);if(!rgb)return 0;
for(size_t j=0;j<count;++j){rgb[j*n]=rgb[j*n+1]=rgb[j*n+2]=p[j*v[2]];if(n==4)rgb[j*n+3]=p[j*v[2]+1];}p=rgb;v[2]=n;}
int ok=0;if(kind==2)ok=write_bmp(f,p,v);else if(kind==9)ok=write_webp(f,p,v);else if(kind==8){qoi_desc d={v[0],v[1],(unsigned char)v[2],QOI_SRGB};int len=0;void *s=qoi_encode(p,&d,&len);if(s){ok=fwrite(s,1,(size_t)len,f)==(size_t)len;free(s);}}
free(rgb);return ok && !ferror(f); }
int wmain(int argc, wchar_t **argv) { setlocale(LC_ALL, ".UTF8");
if (argc == 1 || (argc == 2 && (!wcscmp(argv[1], L"--help") || !wcscmp(argv[1], L"-h") || !wcscmp(argv[1], L"help")))) { usage(); return 0; }
int decode = !wcscmp(argv[1], L"unpack"); if ((argc != 3 && argc != 4) || (!decode && wcscmp(argv[1], L"pack"))) { usage(); return 1; } unsigned v[3] = {0};
unsigned char *src = NULL, *dst = NULL; wchar_t *automatic = argc == 3 ? output_name(argv[2], decode) : NULL;
const wchar_t *output = argc == 4 ? argv[3] : automatic; const char *error = "Not enough memory"; size_t length = 0, capacity = 0, bytes = 0; int result = 1;
if (!output) goto done; if (decode) { error = "Could not read the TCOM file"; src = read_file(argv[2], &length); if (!src) goto done;
error = "Invalid TCOM header"; capacity = tcom(src, length, NULL, 0, v, 1); if (!capacity) goto done; } else { error = "Unsupported or invalid image";
if (!read_image(argv[2], &src, v, &length)) goto done; size_t pixels = length / v[2], maximum = v[2] == 4 ? 5 : 4; error = "Image is too large";
if (pixels > (SIZE_MAX - 32) / maximum) goto done; capacity = pixels * maximum + 32; } error = "Not enough memory"; dst = malloc(capacity); if (!dst) goto done;
error = "Invalid or truncated TCOM data"; bytes = tcom(src, length, dst, capacity, v, decode);
if (!bytes) { if (!decode) error = "Could not encode the image"; goto done; } free(src); src = NULL; int kind=decode?output_format(output):0;
error="Unsupported output extension";if(decode && !kind)goto done;FILE *f = _wfopen(output, L"wbx"); if (!f) {
if (errno == EEXIST) fprintf(stderr, "Output already exists.\n"); else _wperror(output); error = NULL; goto done; }
int ok = decode ? write_image(f,kind,dst,v) : fwrite(dst, 1, bytes, f) == bytes; if (fclose(f)) ok = 0; error = "Cannot write this image in the requested format";
if (!ok) { _wremove(output); goto done; } printf("%ls\n", output); result = 0; done: if (result && error) fprintf(stderr, "%s.\n", error);
free(src); free(dst); free(automatic); return result; }
