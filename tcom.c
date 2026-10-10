#include<string.h>
typedef unsigned U;typedef unsigned char B;typedef signed char S;typedef size_t Z;
#define I __attribute__((always_inline))
static I U G(const B*p,U n){U v=0;while(n)v=v<<8|p[--n];return v;}
static I void W(B*p,U n,U v){while(n--)*p++=v,v>>=8;}
static I U D(U b,U t,U a,U h){U f=t%a+256-a/2;t/=a;
return (b&0xff000000)|((b+f*256)&65280)|(((b&0xff00ff)+(f-h/2)*65537+t%h+t/h*65536)&0xff00ff);}
static I Z R(const B*restrict s,Z l,B*restrict o,Z c,Z r,Z z,U n,int d,U m){
Z i=16;U p=0;
for(Z q=0;o?q<z:l--;){
if(!o)p=q?G(s+q-n,n):0;
U k=1,j=0,t=255,v,u,a=q>=r?G((d?o:s)+q-r,n):p,b=(p|a)-((p^a)&0xfefefefe)/2-((p^a)&m*128&256);
if(d){
if(i==l)return 0;t=s[i++];j=t&3;v=t&128?a:p;
if(j==3&&t<255){k=t/4%32+1;if(k*n>z-q)return 0;}
else{
j=t==255?n:j;if(j>l-i)return 0;v=G(s+i,j);i+=j;
if(t<255){t=t/4+v*64;v=!j?(m&1?D(b,t,16,2):D(b,t,4,4)):j==1?D(b,t,64,16):D(b,t,256,128);}
}
for(U a=k;a--;)W(o+q+a*n,n,v);
}else{
v=u=G(s+q,n);
if(v==p||v==a){t=v!=p;while(o&&k<32-t&&k*n<z-q&&G(s+q+k*n,n)==v)k++;t=k*4-1+128*t;}
else{j=n;
int f=(S)(v/256-b/256),e=(S)(v-b-f),g=(S)(v/65536-b/65536-f);
for(U x=0,a=m&1?16:4,h=m&1?2:4;!((v^b)>>24)&&x<3;a=64<<2*x,h=16<<3*x,x++){
U F=f+a/2,A=e+h/2,B=g+h/2;if(F<a&&(A|B)<h){u=F+(A+B*h)*a;t=u*4+x;u>>=6;j=x;break;}}}
if(o){if(c-i<=j)return 0;o[i]=t;W(o+i+1,j,u);}i+=j+1;
}
p=v;q+=o?k*n:c;
}return d?(i==l?z:0):i;}
I Z tcom(const B*restrict s,Z l,B*restrict o,Z c,U*v,int d){
if(!s||!v)return 0;U m=0;
if(d){if(l<16||memcmp(s,"TCOM",4)||(m=G(s+12,4))>>5!=16)return 0;*v=G(s+4,4);v[1]=G(s+8,4);v[2]=m&7;m=m/8&3;}
U w=*v,h=v[1],n=v[2];
if(!w||!h||n-1>3||w>(Z)-1/h/n)return 0;
Z r=(Z)w*n,z=r*h;if(!o||(d?c<z:l!=z||c<16))return d&&!o?z:0;
if(!d){
Z e=-1,k=(z/n-1)/127*n;for(U h=0;k&&h<4;h++){Z t=R(s,128,0,k,r,z,n,0,h)+3*(h!=0);if(t<e)e=t,m=h;}
memcpy(o,"TCOM",4);W(o+12,4,512+n+m*8);W(o+4,4,w);W(o+8,4,h);}
#define C(n,d)R(s,l,o,c,r,z,n,d,m)
return d?(n==3?C(3,1):n==4?C(4,1):C(n,1)):(n==3?C(3,0):n==4?C(4,0):C(n,0));
#undef C
}
#undef I
