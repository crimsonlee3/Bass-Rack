#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <string>

namespace bassrack {
constexpr int maxBands = 5;
constexpr double pi = 3.14159265358979323846;
inline float gain(float db) { return std::pow(10.0f, db / 20.0f); }

struct Biquad {
    double b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
    void reset() { z1=z2=0; }
    float process(float x) {
        const double y=b0*x+z1;
        z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
        return static_cast<float>(y);
    }
    void configure(double sr, double hz, bool high) {
        const double w=2*pi*std::clamp(hz, 10.0, sr*0.45)/sr;
        const double c=std::cos(w), s=std::sin(w), alpha=s/std::sqrt(2.0);
        const double a0=1+alpha;
        b0=(high ? (1+c) : (1-c))/(2*a0);
        b1=(high ? -(1+c) : (1-c))/a0; b2=b0;
        a1=-2*c/a0; a2=(1-alpha)/a0;
    }
};
// Fourth-order Linkwitz-Riley split. LP + HP is an all-pass response.
struct LR4 {
    std::array<Biquad,2> low, high;
    void set(double sr, double hz) {
        for(auto& q:low) q.configure(sr,hz,false);
        for(auto& q:high) q.configure(sr,hz,true);
    }
    void reset() { for(auto& q:low) q.reset(); for(auto& q:high) q.reset(); }
    std::array<float,2> split(float x) {
        float l=x,h=x;
        for(auto& q:low) l=q.process(l);
        for(auto& q:high) h=q.process(h);
        return {l,h};
    }
    float allpass(float x) { const auto v=split(x); return v[0]+v[1]; }
};
class Splitter {
    std::array<LR4,4> splits;
    std::array<std::array<LR4,4>,4> compensation;
    int count=3;
public:
    void configure(double sr, int n, std::array<float,4> hz) {
        count=std::clamp(n,1,5);
        float last=20;
        for(int i=0;i<4;++i) {
            hz[i]=std::clamp(hz[i],last,static_cast<float>(sr*0.44));
            last=hz[i]; splits[i].set(sr,hz[i]);
            for(auto& stage:compensation[i]) stage.set(sr,hz[i]);
        }
    }
    void reset() {
        for(auto& s:splits) s.reset();
        for(auto& row:compensation) for(auto& s:row) s.reset();
    }
    std::array<float,5> process(float x) {
        std::array<float,5> out{};
        float remainder=x;
        for(int i=0;i<count-1;++i) {
            const auto pair=splits[i].split(remainder);
            out[i]=pair[0]; remainder=pair[1];
            // Match the phase of later splits on all earlier bands.
            for(int b=0;b<i;++b) out[b]=compensation[i][b].allpass(out[b]);
        }
        out[count-1]=remainder;
        return out;
    }
};
inline float distort(float x, float driveDb, int type) {
    if(type==0) return x;
    const float v=x*gain(driveDb);
    switch(type) {
      case 1: return std::tanh(v); // soft
      case 2: return std::tanh(v+0.18f)-std::tanh(0.18f); // tube / asymmetric
      case 3: return std::clamp(v,-1.0f,1.0f); // clip
      case 4: { // triangle folding; finite for large positive and negative input
        float t=std::fmod(v+1.0f,4.0f); if(t<0) t+=4;
        return 1.0f-std::abs(t-2.0f);
      }
      case 5: return std::sin(v); // sine
      default: return x;
    }
}
inline void image(float& l, float& r, float width, float pan) {
    const float mid=(l+r)*0.5f, side=(l-r)*0.5f*width;
    l=mid+side; r=mid-side;
    // Balance pan: centre is unity; full left/right silences the other side.
    l*=std::min(1.0f,1.0f-pan); r*=std::min(1.0f,1.0f+pan);
}
struct DCBlock {
    float x=0,y=0,coefficient=0.9974f;
    void prepare(double sr) { coefficient=static_cast<float>(std::exp(-2*pi*20/sr)); reset(); }
    void reset(){x=y=0;}
    float process(float v) { const float out=v-x+coefficient*y; x=v; y=out; return out; }
};
class StereoDelay {
    std::array<std::vector<float>,2> data;
    int pos=0;
public:
    static constexpr int capacity=262144;
    void prepare() { for(auto& c:data) c.assign(capacity,0); pos=0; }
    void reset() { for(auto& c:data) std::fill(c.begin(),c.end(),0); pos=0; }
    void process(float* l,float* r,int n,int delay) {
        delay=std::clamp(delay,0,capacity-1);
        for(int i=0;i<n;++i) {
            const int read=(pos+capacity-delay)%capacity;
            const float a=l[i],b=r[i];
            data[0][pos]=a; data[1][pos]=b;
            l[i]=data[0][read]; r[i]=data[1][read];
            pos=(pos+1)%capacity;
        }
    }
};
}

namespace bassrack {
inline double regionMagnitude(double hz,double centre,double q,int order) {
    if(hz<=0)return 0;
    const double shape=std::abs((hz/centre-centre/hz)*q);
    return 1/std::sqrt(1+std::pow(shape,2*order));
}
inline std::array<double,2> regionEdges(double centre,double q) {
    const double width=1/q,k=std::sqrt(4+width*width);
    return {centre*(k-width)*.5,centre*(k+width)*.5};
}
class AutoLevel {
    double input=0,output=0,correction=1,energyRate=.00005,gainRate=.0002;
public:
    void prepare(double sr) {
        energyRate=1-std::exp(-1/(sr*.4));gainRate=1-std::exp(-1/(sr*.1));reset();
    }
    void reset(){input=output=0;correction=1;}
    float process(float inputPower,float outputPower,bool enabled) {
        input+=energyRate*(inputPower-input);output+=energyRate*(outputPower-output);
        double target=1;
        if(enabled) {
            target=correction;
            if(input>1.e-8 && output>1.e-12)target=std::clamp(std::sqrt(input/output),.25,4.);
        }
        correction+=gainRate*(target-correction);
        return static_cast<float>(correction);
    }
    float db()const{return static_cast<float>(20*std::log10(correction));}
};
inline bool effectAUIdentifier(const std::string& identifier) {
    if(identifier.rfind("AudioUnit:",0)!=0)return false;
    const auto pos=identifier.find_last_of(":/");
    const auto code=identifier.substr(pos+1,4);
    return (code=="aufx"||code=="aumf") && identifier.size()>pos+5 && identifier[pos+5]==',';
}
}
