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

namespace bassrack {
inline std::array<float,2> midSide(float l,float r) {
    return {(l+r)*.7071067811865475f,(l-r)*.7071067811865475f};
}
inline float stereoCorrelation(double ll,double rr,double lr) {
    if(ll<=1.e-20||rr<=1.e-20)return 0;
    return static_cast<float>(std::clamp(lr/std::sqrt(ll*rr),-1.,1.));
}
inline float sideEnergyShare(double mid,double side) {
    return mid+side>1.e-20?static_cast<float>(std::clamp(side/(mid+side),0.,1.)):0.f;
}
}

namespace bassrack {
struct ShapeADAA {
    double previous=0,integral=0;int shape=-1;bool ready=false;
    static double logCosh(double x){const double a=std::abs(x);return a+std::log1p(std::exp(-2*a))-std::log(2.);}
    static double diode(double x) {
        constexpr double k=.015,vt=.24;
        double y=vt*std::asinh(x/k);
        for(int i=0;i<8;++i) {
            const double u=std::clamp(y/vt,-20.,20.);
            y-=(y+k*std::sinh(u)-x)/(1+k/vt*std::cosh(u));
        }
        return y;
    }
    static double value(double x,int type) {
        switch(type) {
            case 1:return std::tanh(x);
            case 2:return std::tanh(x+.18)-std::tanh(.18);
            case 3:return std::clamp(x,-1.,1.);
            case 4:{double t=std::fmod(x+1,4.);if(t<0)t+=4;return 1-std::abs(t-2);}
            case 5:return std::sin(x);
            case 6:return x/(1+std::abs(x));
            case 7:return diode(x);
            case 8:{const double y=1.5*std::tanh(x/1.5);return y-.18*y*y;}
            default:return x;
        }
    }
    static double primitive(double x,int type) {
        switch(type) {
            case 1:return logCosh(x);
            case 2:return logCosh(x+.18)-std::tanh(.18)*x;
            case 3:return std::abs(x)<=1?.5*x*x:std::abs(x)-.5;
            case 4:{double t=std::fmod(x+1,4.);if(t<0)t+=4;return (t<=2?.5*t*t-t:3*t-.5*t*t-4)+.5;}
            case 5:return 1-std::cos(x);
            case 6:{const double a=std::abs(x);return a-std::log1p(a);}
            case 7:{const double y=diode(x),u=std::clamp(y/.24,-20.,20.);return .5*y*y+.015*(y*std::sinh(u)-.24*(std::cosh(u)-1));}
            case 8:return 2.25*logCosh(x/1.5)-.405*(x-1.5*std::tanh(x/1.5));
            default:return .5*x*x;
        }
    }
    void reset(){ready=false;shape=-1;previous=integral=0;}
    float process(double x,int type,bool enabled) {
        if(type!=shape){ready=false;shape=type;}
        double y=value(x,type);
        if(!enabled){previous=x;ready=false;return static_cast<float>(y);}
        const double p=type==7?.5*y*y+.015*(y*std::sinh(y/.24)-.24*(std::cosh(y/.24)-1)):primitive(x,type);
        const double difference=x-previous;
        if(enabled&&ready)y=std::abs(difference)>1.e-5?(p-integral)/difference:value((x+previous)*.5,type);
        previous=x;integral=p;ready=true;
        return static_cast<float>(y);
    }
};
struct AnalogStage {
    ShapeADAA first,second;
    double sr=48000,envelope=0,low=0,plate=0,flux=0,envRate=0,capRate=0,lowRate=0,fluxRate=0,softRate=0,hardRate=0;
    void prepare(double rate) {
        sr=rate;envRate=1-std::exp(-1/(sr*.035));
        capRate=1-std::exp(-2*pi*18000/sr);lowRate=1-std::exp(-2*pi*500/sr);
        fluxRate=1-std::exp(-1/(sr*.0004));
        softRate=1-std::exp(-2*pi*7000/sr);hardRate=1-std::exp(-2*pi*6000/sr);reset();
    }
    void reset(){first.reset();second.reset();envelope=low=plate=flux=0;}
    float process(float x,float drive,int type,bool adaa) {
        if(type==0)return x;
        const double v=std::clamp(static_cast<double>(x)*gain(drive),-2048.,2048.);
        if(type<=5)return first.process(v,type,adaa);
        envelope+=envRate*(std::min(8.,std::abs(v))-envelope);
        low+=lowRate*(v-low);
        double out=0;
        if(type==6) { // two biased valve stages and finite plate capacitance
            const double bias=.12-.08*envelope;
            const double a=first.process(1.6*v+bias,2,adaa)-ShapeADAA::value(bias,2);
            out=second.process(1.45*a+.06,2,adaa)-ShapeADAA::value(.06,2);
        } else if(type==7) { // harder odd-harmonic stages with supply sag
            const double a=first.process(v*2.8/(1+.18*envelope),6,adaa);
            out=second.process(a*2.3,6,adaa)*1.6;
        } else if(type==8) { // Shockley antiparallel diode pair + load resistor
            out=first.process(v,7,adaa);
        } else if(type==9) { // pre-emphasis, magnetic memory and HF loss
            const double a=first.process(1.4*(v+.5*(v-low))+.15*flux,1,adaa);
            flux+=fluxRate*(a-flux);out=a/(1+.12*envelope);
        } else if(type==10) { // low-frequency magnetic core with remanent feedback
            const double a=first.process(2.2*low+.2*flux,1,adaa);
            flux+=fluxRate*(a-flux);out=.65*a+second.process(.85*(v-low),1,adaa);
        } else if(type==11) {
            const double a=first.process(1.4*v+.04,2,adaa)-ShapeADAA::value(.04,2);
            out=second.process(1.2*a,1,adaa);
        } else if(type==12) {
            out=first.process(1.8*v/(1+.04*envelope),8,adaa);
        } else if(type==13) {
            out=first.process(v+3*(v-low),7,adaa);
        } else if(type==14) {
            const double a=first.process(6*v,3,adaa);
            out=second.process(1.8*a,7,adaa);
        } else if(type==15) {
            const double a=first.process(2*v/(1+.12*envelope)+.08,2,adaa)-ShapeADAA::value(.08,2);
            out=second.process(1.5*a,1,adaa);
        }
        const double rate=type==13?softRate:type==14?hardRate:capRate;
        plate+=rate*(out-plate);return static_cast<float>(plate);
    }
};
}

namespace bassrack {
inline float staticCharacter(float x,float drive,int type) {
    if(type<=5)return distort(x,drive,type);
    const double v=x*gain(drive);
    switch(type) {
        case 6:{const double a=std::tanh(1.6*v+.30)-std::tanh(.30);return static_cast<float>(std::tanh(1.45*a+.24)-std::tanh(.24));}
        case 7:return static_cast<float>(1.6*ShapeADAA::value(2.3*ShapeADAA::value(2.8*v,6),6));
        case 8:return static_cast<float>(ShapeADAA::diode(v));
        case 9:return static_cast<float>(std::tanh(1.4*v));
        case 10:return static_cast<float>(.65*std::tanh(2.2*v));
        case 11:return static_cast<float>(std::tanh(1.2*(std::tanh(1.4*v+.22)-std::tanh(.22))));
        case 12:return static_cast<float>(ShapeADAA::value(1.8*v,8));
        case 13:return static_cast<float>(ShapeADAA::diode(v));
        case 14:return static_cast<float>(ShapeADAA::diode(1.8*std::clamp(6*v,-1.,1.)));
        case 15:return static_cast<float>(std::tanh(1.5*(std::tanh(2*v+.26)-std::tanh(.26))));
        default:return x;
    }
}
}

namespace bassrack {
class ShortStereoDelay {
    std::array<std::vector<float>,2> history;size_t position=0;
public:
    void prepare(int delay){for(auto& c:history)c.assign(static_cast<size_t>(std::max(1,delay)),0);position=0;}
    void reset(){for(auto& c:history)std::fill(c.begin(),c.end(),0);position=0;}
    void process(float* l,float* r,int count) {
        for(int i=0;i<count;++i){
            const float a=history[0][position],b=history[1][position];
            history[0][position]=l[i];history[1][position]=r[i];l[i]=a;r[i]=b;
            position=(position+1)%history[0].size();
        }
    }
};
}
