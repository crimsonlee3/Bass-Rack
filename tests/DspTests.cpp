#include "Dsp.h"
#include <cstdlib>
#include <iostream>
#include <random>
using namespace bassrack;
void require(bool condition, const char* text) {
    if(!condition) {std::cerr<<text<<"\n";std::exit(1);}
}
int main() {
    // LR4 phase compensation must preserve full-band magnitude.
    for(double sr:{44100.,48000.,96000.})
    for(int bands=1;bands<=5;++bands)
    for(double hz:{30.,70.,120.,500.,2000.,7000.,15000.}) {
        Splitter s; s.configure(sr,bands,{90,350,2500,8000});
        double input=0,output=0;
        for(int i=0;i<static_cast<int>(sr);++i) {
            const float x=static_cast<float>(0.2*std::sin(2*pi*hz*i/sr));
            const auto b=s.process(x); float y=0; for(float v:b)y+=v;
            require(std::isfinite(y),"non-finite crossover output");
            if(i>sr/2) {input+=x*x;output+=y*y;}
        }
        require(std::abs(10*std::log10(output/input))<0.06,"crossover reconstruction magnitude");
    }
    float l=.7f,r=-.2f; image(l,r,1,0);
    require(std::abs(l-.7f)<1e-6 && std::abs(r+.2f)<1e-6,"stereo identity");
    image(l,r,0,0); require(l==r,"mono width");
    for(int type=0;type<=5;++type)
    for(int i=-10000;i<=10000;++i) {
        const float x=i*0.001f, v=distort(x,36,type);
        require(std::isfinite(v),"non-finite distortion");
        if(type==0) require(v==x,"clean distortion changed input");
        if(type==1||type==3||type==4||type==5) require(std::abs(v)<=1.00001,"distortion bounds");
    }
    StereoDelay delay; delay.prepare();
    float dl[8]{1},dr[8]{1}; delay.process(dl,dr,8,3);
    require(dl[0]==0 && dl[3]==1 && dr[3]==1,"latency alignment impulse");
    delay.reset(); float zl[8]{},zr[8]{}; delay.process(zl,zr,8,0);
    require(zl[0]==0,"delay reset");
    std::cout<<"DSP tests passed\n";
}
