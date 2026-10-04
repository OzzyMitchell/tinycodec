#include<string.h>
typedef unsigned U;
typedef unsigned char B;typedef signed char S;
typedef size_t Z;
#define I __attribute__((always_inline))
#define M 0xffffffu
static I U G(const B*p,U n){U v=n<4?~M:0;for(U j=0;j<n;j++)v|=(U)p[j]<<(j*8);return v;}
static I void W(B*p,U n,U v){for(U j=0;j<n;j++)p[j]=(v>>(j*8));}
static U P(U l,U u){return(l&u&M)+((l^u)&0xfefefe)/2|(l&~M);}
static I Z R(const B*s,Z l,B*o,Z c,U w,U h,U n,int d){
U a[32]={0},p=~M;Z i=16,z=(Z)w*h*n;
if(d?c<z:l!=z||c<16)return 0;
if(!d){memcpy(o,"TCOM",4);W(o+4,4,w);W(o+8,4,h);W(o+12,4,n);}
const B*m=d?o:s;
for(U y=0;y<h;y++)for(U x=0;x<w;){
Z q=((Z)y*w+x)*n;U k=0,v,t;
if(d){
if(i>=l)return 0;t=s[i++];
if(t/16==14||t/2==121){
if(t<240)k=t-223;else{if(i==l)return 0;k=s[i++]+1;}
if(k>w-x||(t==243&&!y))return 0;
}else if(t/2==120){U j=t-237;if(j>l-i)return 0;
v=G(s+i,j);if(j==3)v=(v&M)|(p&~M);i+=j;}
else if(t/32==6)v=a[t-192];
else{U u=y?G(m+q-w*n,n):~M,b=x?P(p,u):u;int e,f,g;
if(t<64){e=(int)(t&3)-2;f=(int)(t>>2&3)-2;g=(int)(t>>4)-2;}
else if(t<192){if(i==l)return 0;int j=(t-64)<<8|s[i++];
f=(j&31)-16;e=f+(j>>5&31)-16;g=f+(j>>10)-16;}
else{if(t/4!=61||l-i<2)return 0;int j=(t-244)<<16|s[i]|s[i+1]<<8;i+=2;
f=(j&63)-32;e=f+(j>>6&63)-32;g=f+(j>>12)-32;}
v=((b+e)&255)|((b>>8)+f&255)<<8|((b>>16)+g&255)<<16|(b&~M);}
}else{
v=G(s+q,n);U j=v*0x9e3779b1>>27,u=y?G(m+q-w*n,n):~M;
if(v==p||(y&&v==u)){
t=v==p?242:243;k=1;
#define F(C)while(k<256&&k<w-x&&G(s+q+k*n,n)==(C))k++
if(t==242)F(p);else F(G(s+q-w*n+k*n,n));
#undef F
if(c-i<2)return 0;
if(t==242&&k<=16)o[i++]=223+k;else{o[i++]=t;o[i++]=k-1;}
}else{
if(c-i<5)return 0;
if(a[j]==v)o[i++]=192+j;
else{U b=x?P(p,u):u;int e=(S)(v-b),f=(S)((v>>8)-(b>>8)),g=(S)((v>>16)-(b>>16));
if((v^b)>M)goto L;
if(((U)((e+2)|(f+2)|(g+2)))<4)o[i++]=e+2|(f+2)<<2|(g+2)<<4;
#define H(b,j,t)else if((U)((f+b)|(e-f+b)|(g-f+b))<b*2){U z=f+b|(e-f+b)<<j|(g-f+b)<<(j*2);o[i++]=t+(z>>(j==5?8:16));o[i++]=z;if(j==6)o[i++]=z>>8;}
H(16,5,64)H(32,6,244)
#undef H
else L:{U j=(v^p)<=M?3:4;o[i++]=j+237;W(o+i,j,v);i+=j;}}
}}
if(k){
if(d){if(t==243)memcpy(o+q,o+q-w*n,k*n);
else{W(o+q,n,p);for(U j=1;j<k;){U b=j<k-j?j:k-j;memcpy(o+q+j*n,o+q,b*n);j+=b;}}}
p=G(m+q+(k-1)*n,n);
}else{if(d)W(o+q,n,v);a[v*0x9e3779b1>>27]=p=v;k=1;}
x+=k;}
return d?(i==l?z:0):i;}
I Z tcom(const B*s,Z l,B*o,Z c,U*v,int d){
if(!s||!v)return 0;
if(d){if(l<16||memcmp(s,"TCOM",4))return 0;
v[0]=G(s+4,4);v[1]=G(s+8,4);v[2]=G(s+12,4);}
U w=*v,h=v[1],n=v[2];
if(!w||w>1000000||!h||n-1>3||w>(Z)-1/h/n)return 0;
if(!o)return d?(Z)w*h*n:0;
#define C(n)R(s,l,o,c,w,h,n,d)
return n==3?C(3):n==4?C(4):C(n);
#undef C
}
#undef I
#undef M
