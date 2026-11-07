#include<string.h>
#define T return
typedef unsigned U;typedef unsigned char B;typedef signed char S;typedef size_t Z;
#define I __attribute__((always_inline))
static I U G(const B*p,U n){U v=0;memcpy(&v,p,n);if(!*(B*)&(U){1})v=__builtin_bswap32(v);T v;}static I void W(B*p,U n,U v){if(!*(B*)&(U){1})v=__builtin_bswap32(v);memcpy(p,&v,n);}static __attribute__((noinline,cold)) __attribute__((preserve_most)) U H(const B*p,U n){U v=0;while(n)v=v<<8|p[--n];T v;}static I U L(const B*p,U n,Z left,U mask){if(left>=4){U v;memcpy(&v,p,4);if(!*(B*)&(U){1})v=__builtin_bswap32(v);T v&mask;}T H(p,n);}static __attribute__((noinline,cold)) __attribute__((preserve_most)) void V(B*p,U v,U n){while(n--)*p++=v,v>>=8;}
static I void O(B*p,U n,U v,Z left){if(left>=4){if(!*(B*)&(U){1})v=__builtin_bswap32(v);memcpy(p,&v,4);}else V(p,v,n);}
static I Z R(const B*s,Z limit,B*o,Z r,Z z,U n,int d,U m,U prev){
    const B *ie=s+(d?limit:0); B *oe=o+(d?z:limit),*ob=o;
    Z left=z,first=r?z-r:0;
    U mask=~0u>>((4-n)*8);
    if(d)s+=16;else o+=16;const B*p=d?o:s;
    while(left){
        U k=1,j=0,t=255,v,u;
        U above=left<=first?L(p-r,n,left+r,mask):prev;
        U b=(prev|above)-((prev^above)&0xfefefefe)/2-((prev^above)&m*128&256);
        if(d){
            if(s==ie)return 0;
            t=*s++;j=t&3;v=t&128?above:prev;
            if(j==3&&t<255){k=t/4%32+1;if(k*n>left)return 0;}
            else{
                j=t==255?n:j;if(j>(Z)(ie-s))return 0;
                if(t==255)v=L(s,n,ie-s,mask);
                else{U bits=0x78461422u>>(8*(j?j+1:m&1)),gb=bits&15,cb=bits>>4&15;
                    U packed=t/4+(L(s,j,ie-s,(1u<<(8*j))-1)<<6);
                    U a=1u<<gb,h=1u<<cb,f=(packed&(a-1))+256-a/2;
                    packed>>=gb;U e=(packed&(h-1))-h/2,g=(packed>>cb)-h/2;
                    v=(b&0xff000000u)|((b+f*256)&0xff00)|(((b&0xff00ff)+(f+e)+(f+g)*65536)&0xff00ff);}
                s+=j;
            }
            U repeat=k;do{O(o,n,v,oe-o);o+=n;}while(--repeat);
        }else{
            v=u=L(s,n,left,mask);
            if(v==prev||v==above){
                t=v!=prev;
                while(k<32-t&&k*n<left&&L(s+k*n,n,left-k*n,mask)==v)k++;
                t=k*4-1+128*t;
            }else{
                j=n;
                int f=(S)((v>>(n==1?0:8))-(b>>(n==1?0:8)));if(n==1&&f<-8)f=-8;
                int e=(S)(v-b-f),g=(S)(n<3?-f*(m>>2):v/65536-b/65536-f);
                if(!((v^b)>>24)){U a=m&1?16:4,h=m&1?2:4,F=f+a/2,A=e+h/2,B=g+h/2;if((F|((A|B)<<(m&1?3:0)))<a){t=(F+(A+B*h)*a)*4;j=0;}else if(n>1){F=f+32;A=e+8;B=g+8;if((F|((A|B)<<2))<64){t=F*4+1;u=A|B<<4;j=1;}else if(n>2){F=f+128;A=e+64;B=g+64;if((A|B)<128){t=F*4+2;u=F>>6|A<<2|B<<9;j=2;}}}}
            }
            if((Z)(oe-o)<=j)return 0;
            *o++=t;O(o,j,u,oe-o);o+=j;s+=k*n;
        }
        prev=v;left-=k*n;p+=k*n;
    }
    return (!d||s==ie)?(Z)(o-ob):0;
}
I Z tcom(const B*s,Z l,B*o,Z c,U*v,int d){if(!s||!v)T 0;U m=0;if(d){if(l<16||G(s,4)!=0x4d4f4354||(m=G(s+12,4))>>5!=16)T 0;*v=G(s+4,4);v[1]=G(s+8,4);v[2]=m&7;m=m/8&3;}U w=*v,h=v[1],n=v[2];Z r,z;if(n-1>3||__builtin_mul_overflow((Z)w,(Z)n,&r)||__builtin_mul_overflow(r,(Z)h,&z)||!z)T 0;if(!o||(d?c<z:l!=z||c<16))T (d&&!o)*z;if(!d){m=n==1;W(o,4,0x4d4f4354);W(o+4,4,w);W(o+8,4,h);W(o+12,4,512+n+m*8);Z step=m?0:(z/n-1)/127*n;U best=(U)-1;
for(U model=0;step&&model<4;model++){
    U score=3*!!model;
    Z q=0;for(U k=0;k<128;k++,q+=step)score+=(U)R(s+q,c,o,q>=r?r:0,n,n,0,model|4,q?H(s+q-n,n):0);
    if(score<best)best=score,m=model;
}
o[12]=n+m*8;}
#define C(n,d)R(s,(d)?l:c,o,r,z,n,d,m,0)
T d?n==1?C(1,1):n==3?C(3,1):n==4?C(4,1):C(2,1):n==1?C(1,0):n==3?C(3,0):n==4?C(4,0):C(2,0);
#undef C
}
#undef I
#undef T
