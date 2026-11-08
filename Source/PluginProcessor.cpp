#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EffectsScanner.h"

using namespace juce;
namespace {
String key(int b,const char* suffix){return "b"+String(b)+"_"+suffix;}
class PluginWindow final : public DocumentWindow {
public:
    PluginWindow(String name,Component* component)
    :DocumentWindow(name,Colour(0xff171c25),DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true); setContentOwned(component,true);
        centreWithSize(getWidth(),getHeight()); setVisible(true);
    }
    void closeButtonPressed() override {setVisible(false);}
};
}

AudioProcessorValueTreeState::ParameterLayout BassRackProcessor::parameters() {
    AudioProcessorValueTreeState::ParameterLayout p;
    auto floating=[&](String id,String name,float lo,float hi,float def,float skew=1.f){
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{id,1},name,NormalisableRange<float>(lo,hi,.001f,skew),def));
    };
    // Retained for reading older projects; independent active flags now define regions.
    p.add(std::make_unique<AudioParameterInt>(ParameterID{"bands",1},"Legacy band count",0,5,1));
    for(int i=0;i<4;++i)floating("x"+String(i),"Legacy crossover "+String(i+1),20,20000,90.f*(i+1),.25f);
    const float defaults[]{55,160,800,2500,7000};
    for(int b=0;b<bands;++b) {
        String prefix="Region "+String(b+1)+" ";
        p.add(std::make_unique<AudioParameterBool>(ParameterID{key(b,"active"),1},prefix+"active",b==0));
        floating(key(b,"freq"),prefix+"frequency",20,20000,defaults[b],.25f);
        floating(key(b,"q"),prefix+"Q",.2f,16,1,.4f);
        p.add(std::make_unique<AudioParameterChoice>(ParameterID{key(b,"slope"),1},prefix+"slope",StringArray{"12 dB/oct","24 dB/oct","48 dB/oct","96 dB/oct"},1));
        p.add(std::make_unique<AudioParameterChoice>(ParameterID{key(b,"type"),1},prefix+"distortion",StringArray{"Clean","Soft","Tube","Clip","Fold","Sine"},0));
        floating(key(b,"drive"),prefix+"drive",0,36,0);floating(key(b,"mix"),prefix+"distortion mix",0,1,1);
        floating(key(b,"level"),prefix+"level",-36,12,0);floating(key(b,"width"),prefix+"width",0,2,1);
        floating(key(b,"pan"),prefix+"pan",-1,1,0);
        for(auto* id:{"mute","solo","bypass"})p.add(std::make_unique<AudioParameterBool>(ParameterID{key(b,id),1},prefix+id,false));
    }
    p.add(std::make_unique<AudioParameterChoice>(ParameterID{"oversampling",1},"Oversampling",StringArray{"Off / 1x","2x","4x","8x","16x"},1));
    p.add(std::make_unique<AudioParameterBool>(ParameterID{"autogain",1},"Auto Gain",false));
    floating("bass","Master bass",-12,12,0);floating("air","Master treble",-12,12,0);
    floating("threshold","Compressor threshold",-48,0,-12);floating("ratio","Compressor ratio",1,10,2);
    floating("attack","Compressor attack",.1f,100,15,.4f);floating("release","Compressor release",10,600,120,.5f);
    p.add(std::make_unique<AudioParameterBool>(ParameterID{"compress",1},"Master compressor",false));
    p.add(std::make_unique<AudioParameterBool>(ParameterID{"stereoview",1},"Stereo analysis view",false));
    p.add(std::make_unique<AudioParameterChoice>(ParameterID{"scopesource",1},"Stereo analysis source",StringArray{"Selected region","Master output"},0));
    floating("output","Output trim",-36,12,0);
    return p;
}
BassRackProcessor::BassRackProcessor()
:AudioProcessor(BusesProperties().withInput("Input",AudioChannelSet::stereo(),true).withOutput("Output",AudioChannelSet::stereo(),true)),
 state(*this,nullptr,"BASSRACK",parameters()) {
    formats.addFormat(new EffectsOnlyAUFormat);
    knownFile=File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("BassRack").getChildFile("known-plugins.xml");
    knownFile.getParentDirectory().createDirectory();
    if(auto xml=XmlDocument::parse(knownFile))known.recreateFromXml(*xml);
    for(const auto& description:known.getTypes())if(!effectEligible(description))known.removeType(description);
    for(int b=0;b<bands;++b) {
        filterDirty[b].store(true);
        for(auto* suffix:{"freq","q","slope"})state.addParameterListener(key(b,suffix),this);
    }
    state.addParameterListener("oversampling",this);
    for(int b=0;b<bands;++b)state.addParameterListener(key(b,"active"),this);
}
BassRackProcessor::~BassRackProcessor() {
    cancelPendingUpdate();
    state.removeParameterListener("oversampling",this);
    for(int b=0;b<bands;++b)for(auto* suffix:{"freq","q","slope","active"})state.removeParameterListener(key(b,suffix),this);
    closePluginWindows();
    if(auto xml=known.createXml())xml->writeTo(knownFile);
}
bool BassRackProcessor::effectEligible(const PluginDescription& d) {
    return !d.isInstrument&&eligibleAU(d.fileOrIdentifier)&&d.name!="Bass Rack"&&!d.fileOrIdentifier.endsWith("aufx,BsRk,Crsn");
}
bool BassRackProcessor::isBusesLayoutSupported(const BusesLayout& l)const {
    return (l.getMainInputChannelSet()==AudioChannelSet::mono()||l.getMainInputChannelSet()==AudioChannelSet::stereo())
        &&l.getMainOutputChannelSet()==AudioChannelSet::stereo();
}
float BassRackProcessor::value(const String& id)const{return state.getRawParameterValue(id)->load();}
void BassRackProcessor::setParam(const String& id,float v) {
    if(auto* p=state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}
}
int BassRackProcessor::addRegion(float hz) {
    for(int b=0;b<bands;++b)if(!bandActive(b)) {
        setParam(key(b,"freq"),hz);setParam(key(b,"q"),1);setParam(key(b,"slope"),1);
        setParam(key(b,"active"),1);return b;
    }
    return -1;
}
void BassRackProcessor::deleteRegion(int b) {
    if(b<0||b>=bands)return;
    setParam(key(b,"active"),0);setParam(key(b,"solo"),0);
    for(int i=0;i<slots;++i)removePlugin(b,i);
    for(auto* id:{"type","drive","level","pan","mute","bypass"})setParam(key(b,id),0);
    setParam(key(b,"mix"),1);setParam(key(b,"width"),1);
}
void BassRackProcessor::parameterChanged(const String& id,float) {
    for(int b=0;b<bands;++b)
        if(id==key(b,"freq")||id==key(b,"q")||id==key(b,"slope"))filterDirty[b].store(true);
    triggerAsyncUpdate();
}
AudioBuffer<float> BassRackProcessor::designRegion(int b,double sr)const {
    constexpr int n=16384;
    dsp::FFT transform(14);
    std::vector<std::complex<float>> frequency(n),time(n);
    const double centre=jlimit(20.,sr*.43,static_cast<double>(value(key(b,"freq"))));
    const double q=value(key(b,"q"));const int order=2<<static_cast<int>(value(key(b,"slope")));
    for(int i=0;i<n;++i) {
        const double hz=jmin(i,n-i)*sr/n;
        frequency[static_cast<size_t>(i)]={static_cast<float>(std::sqrt(bassrack::regionMagnitude(hz,centre,q,order))),0};
    }
    transform.perform(frequency.data(),time.data(),true);
    AudioBuffer<float> impulse(2,irSize);
    double centreResponse=0;
    for(int i=0;i<irSize;++i) {
        const int relative=i-regionDelay;
        const float window=.5f+.5f*static_cast<float>(std::cos(bassrack::pi*relative/regionDelay));
        const float x=time[static_cast<size_t>((relative+n)%n)].real()*window;
        impulse.setSample(0,i,x);impulse.setSample(1,i,x);
        centreResponse+=x*std::cos(2*bassrack::pi*centre*relative/sr);
    }
    if(centreResponse>1.e-6)impulse.applyGain(static_cast<float>(1/centreResponse));
    return impulse;
}
void BassRackProcessor::loadRegion(int b,AudioBuffer<float> impulse,double sr) {
    AudioBuffer<float> copy;copy.makeCopyOf(impulse);
    AudioBuffer<float> scopeCopy;scopeCopy.makeCopyOf(impulse);
    pre[b].loadImpulseResponse(std::move(impulse),sr,dsp::Convolution::Stereo::yes,dsp::Convolution::Trim::no,dsp::Convolution::Normalise::no);
    post[b].loadImpulseResponse(std::move(copy),sr,dsp::Convolution::Stereo::yes,dsp::Convolution::Trim::no,dsp::Convolution::Normalise::no);
    scopeFilters[b].loadImpulseResponse(std::move(scopeCopy),sr,dsp::Convolution::Stereo::yes,dsp::Convolution::Trim::no,dsp::Convolution::Normalise::no);
}
void BassRackProcessor::prepareToPlay(double sr,int block) {
    const ScopedLock lock(graphLock);
    prepared=false;sampleRate=sr;blockSize=jmax(32,block);activeOS=-1;
    dsp::ProcessSpec spec{sr,static_cast<uint32>(blockSize),2};
    original.setSize(2,blockSize);matching.setSize(2,blockSize);scopeRegion.setSize(2,blockSize);lastStereoRequest=0;
    dryAlignment.prepare();matchAlignment.prepare();autoLevel.prepare(sr);autoDb.store(0);
    for(int b=0;b<bands;++b) {
        audio[b].setSize(2,blockSize);reference[b].setSize(2,blockSize);
        wetAlignment[b].prepare();referenceAlignment[b].prepare();
        loadRegion(b,designRegion(b,sr),sr);filterDirty[b].store(false);
        pre[b].prepare(spec);post[b].prepare(spec);scopeFilters[b].prepare(spec);
        for(int mode=1;mode<=4;++mode) {
            for(auto* bank:{&oversampling,&referenceOS}) {
                (*bank)[b][mode]=std::make_unique<dsp::Oversampling<float>>(2,static_cast<size_t>(mode),
                    dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,true,true);
                (*bank)[b][mode]->initProcessing(static_cast<size_t>(blockSize));
            }
        }
        auto& c=smooth[b];
        c.level.reset(sr,.02);c.width.reset(sr,.02);c.pan.reset(sr,.02);
        c.level.setCurrentAndTargetValue(bassrack::gain(value(key(b,"level"))));
        c.width.setCurrentAndTargetValue(value(key(b,"width")));c.pan.setCurrentAndTargetValue(value(key(b,"pan")));
    }
    selectOversampling(static_cast<int>(value("oversampling")));
    *lowEQ.state=dsp::IIR::ArrayCoefficients<float>::makeLowShelf(sr,120,.707f,bassrack::gain(value("bass")));
    *highEQ.state=dsp::IIR::ArrayCoefficients<float>::makeHighShelf(sr,jmin(4000.,sr*.4),.707f,bassrack::gain(value("air")));
    lowEQ.prepare(spec);highEQ.prepare(spec);compressor.prepare(spec);
    lowEQ.reset();highEQ.reset();compressor.reset();
    output.reset(sr,.02);output.setCurrentAndTargetValue(bassrack::gain(value("output")));
    for(auto& rack:chains)for(auto& s:rack)if(s.plugin){s.plugin->setRateAndBufferSizeDetails(sr,blockSize);s.plugin->prepareToPlay(sr,blockSize);}
    prepared=true;updateLatency();setLatencySamples(desiredLatency.load());
}
void BassRackProcessor::selectOversampling(int mode) {
    mode=jlimit(0,4,mode);if(activeOS==mode)return;activeOS=mode;
    osLatency=mode>0?static_cast<int>(std::round(oversampling[0][mode]->getLatencyInSamples())):0;
    for(int b=0;b<bands;++b) {
        if(mode>0){oversampling[b][mode]->reset();referenceOS[b][mode]->reset();}
        const double rate=sampleRate*(1<<mode);
        smooth[b].drive.reset(rate,.02);smooth[b].mix.reset(rate,.02);
        smooth[b].drive.setCurrentAndTargetValue(value(key(b,"drive")));
        smooth[b].mix.setCurrentAndTargetValue(value(key(b,"mix")));
        for(auto& d:dc[b])d.prepare(rate);
    }
}
void BassRackProcessor::releaseResources() {
    const ScopedLock lock(graphLock);prepared=false;
    for(auto& rack:chains)for(auto& s:rack)if(s.plugin)s.plugin->releaseResources();
}
void BassRackProcessor::reset() {
    const ScopedLock lock(graphLock);
    for(int b=0;b<bands;++b) {
        pre[b].reset();post[b].reset();scopeFilters[b].reset();wetAlignment[b].reset();referenceAlignment[b].reset();
        for(int m=1;m<=4;++m){if(oversampling[b][m])oversampling[b][m]->reset();if(referenceOS[b][m])referenceOS[b][m]->reset();}
        for(auto& d:dc[b])d.reset();
    }
    dryAlignment.reset();matchAlignment.reset();autoLevel.reset();autoDb.store(0);
    lowEQ.reset();highEQ.reset();compressor.reset();
    for(auto& rack:chains)for(auto& s:rack)if(s.plugin)s.plugin->reset();
}
int BassRackProcessor::rackLatency(int rack)const {
    int sum=0;for(const auto& s:chains[rack])if(s.plugin)sum+=jmax(0,s.plugin->getLatencySamples());return sum;
}
void BassRackProcessor::updateLatency() {
    int maximum=0;for(int b=0;b<bands;++b)if(bandActive(b))maximum=jmax(maximum,rackLatency(b));
    int latency=2*regionDelay+osLatency+maximum+rackLatency(master);
    if(desiredLatency.exchange(latency)!=latency)triggerAsyncUpdate();
}
void BassRackProcessor::processRack(int rack,AudioBuffer<float>& buffer) {
    MidiBuffer midi;
    for(auto& s:chains[rack])if(s.plugin){s.plugin->setPlayHead(getPlayHead());s.plugin->processBlock(buffer,midi);midi.clear();}
}
void BassRackProcessor::processBlock(AudioBuffer<float>& buffer,MidiBuffer& midi) {
    const ScopedTryLock lock(graphLock);
    ScopedNoDenormals noDenormals;
    midi.clear();
    if(!lock.isLocked() || !prepared || buffer.getNumChannels()!=2) {buffer.clear();return;}
    if(getTotalNumInputChannels()==1)
        buffer.copyFrom(1,0,buffer,0,0,buffer.getNumSamples());
    const float meterFall=buffer.getNumSamples()*36.f/static_cast<float>(sampleRate);
    float inputMaximum=0;
    for(int i=0;i<buffer.getNumSamples();++i)
        inputMaximum=jmax(inputMaximum,std::abs(buffer.getSample(0,i)),std::abs(buffer.getSample(1,i)));
    inputMeter.store(jmax(Decibels::gainToDecibels(inputMaximum,-100.f),inputMeter.load()-meterFall));
    int start1,size1,start2,size2;
    analyzerFifo.prepareToWrite(buffer.getNumSamples(),start1,size1,start2,size2);
    for(int i=0;i<size1;++i)analyzerSamples[static_cast<size_t>(start1+i)]=
        (buffer.getSample(0,i)+buffer.getSample(1,i))*.5f;
    for(int i=0;i<size2;++i)analyzerSamples[static_cast<size_t>(start2+i)]=
        (buffer.getSample(0,size1+i)+buffer.getSample(1,size1+i))*.5f;
    analyzerFifo.finishedWrite(size1+size2);
    // Hosts can supply a block larger than the advertised maximum.
    for(int start=0;start<buffer.getNumSamples();start+=blockSize) {
        const int size=jmin(blockSize,buffer.getNumSamples()-start);
        float* channels[]{buffer.getWritePointer(0,start),buffer.getWritePointer(1,start)};
        AudioBuffer<float> chunk(channels,2,size); processChunk(chunk);
    }
    float outputMaximum=0;
    for(int i=0;i<buffer.getNumSamples();++i)
        outputMaximum=jmax(outputMaximum,std::abs(buffer.getSample(0,i)),std::abs(buffer.getSample(1,i)));
    outputMeter.store(jmax(Decibels::gainToDecibels(outputMaximum,-100.f),outputMeter.load()-meterFall));
}
int BassRackProcessor::readAnalyzer(float* destination,int maximum) {
    int start1,size1,start2,size2;
    analyzerFifo.prepareToRead(maximum,start1,size1,start2,size2);
    std::copy_n(analyzerSamples.data()+start1,size1,destination);
    std::copy_n(analyzerSamples.data()+start2,size2,destination+size1);
    analyzerFifo.finishedRead(size1+size2);
    return size1+size2;
}
std::uint32_t BassRackProcessor::setStereoAnalysis(int source,bool enabled) {
    const auto old=stereoRequest.load();
    const auto mode=static_cast<std::uint32_t>(jlimit(0,5,source))|(enabled?8u:0u);
    const auto next=(old&15u)==mode?old:((old&~15u)+16u)|mode;
    stereoRequest.store(next);return next;
}
int BassRackProcessor::readStereoAnalysis(float* l,float* r,std::uint32_t* tags,int maximum) {
    int a,n,b,m;stereoFifo.prepareToRead(maximum,a,n,b,m);
    std::copy_n(stereoSamples[0].data()+a,n,l);std::copy_n(stereoSamples[1].data()+a,n,r);
    std::copy_n(stereoTags.data()+a,n,tags);
    std::copy_n(stereoSamples[0].data()+b,m,l+n);std::copy_n(stereoSamples[1].data()+b,m,r+n);
    std::copy_n(stereoTags.data()+b,m,tags+n);stereoFifo.finishedRead(n+m);return n+m;
}
void BassRackProcessor::writeStereoAnalysis(const AudioBuffer<float>& buffer,std::uint32_t tag) {
    int a,n,b,m;stereoFifo.prepareToWrite(buffer.getNumSamples(),a,n,b,m);
    for(int channel=0;channel<2;++channel) {
        std::copy_n(buffer.getReadPointer(channel),n,stereoSamples[channel].data()+a);
        std::copy_n(buffer.getReadPointer(channel)+n,m,stereoSamples[channel].data()+b);
    }
    std::fill_n(stereoTags.data()+a,n,tag);std::fill_n(stereoTags.data()+b,m,tag);
    stereoFifo.finishedWrite(n+m);
}
void BassRackProcessor::processChunk(AudioBuffer<float>& buffer) {
    selectOversampling(static_cast<int>(value("oversampling")));
    const int n=buffer.getNumSamples();bool solo=false;int maximum=0;
    const auto scopeRequest=stereoRequest.load();
    const bool showScope=(scopeRequest&8u)!=0;const int scopeSource=static_cast<int>(scopeRequest&7u);
    if(scopeRequest!=lastStereoRequest) {
        if(showScope&&scopeSource<bands)scopeFilters[scopeSource].reset();
        lastStereoRequest=scopeRequest;
    }
    for(int b=0;b<bands;++b)if(bandActive(b)){maximum=jmax(maximum,rackLatency(b));solo=solo||value(key(b,"solo"))>.5f;}
    const int baseDelay=2*regionDelay+osLatency+maximum;
    if(baseDelay+rackLatency(master)>=bassrack::StereoDelay::capacity){buffer.clear();return;}
    original.copyFrom(0,0,buffer,0,0,n);original.copyFrom(1,0,buffer,1,0,n);
    matching.copyFrom(0,0,buffer,0,0,n);matching.copyFrom(1,0,buffer,1,0,n);
    dryAlignment.process(buffer.getWritePointer(0),buffer.getWritePointer(1),n,baseDelay);
    matchAlignment.process(matching.getWritePointer(0),matching.getWritePointer(1),n,baseDelay+rackLatency(master));
    if(solo)buffer.clear();
    for(int b=0;b<bands;++b) {
        if(!bandActive(b))continue;
        const bool bypass=value(key(b,"bypass"))>.5f,mute=value(key(b,"mute"))>.5f;
        auto& c=smooth[b];const int type=bypass?0:static_cast<int>(value(key(b,"type")));
        c.drive.setTargetValue(value(key(b,"drive")));c.mix.setTargetValue(value(key(b,"mix")));
        c.level.setTargetValue(bassrack::gain(value(key(b,"level"))));
        c.width.setTargetValue(value(key(b,"width")));c.pan.setTargetValue(value(key(b,"pan")));
        audio[b].copyFrom(0,0,original,0,0,n);audio[b].copyFrom(1,0,original,1,0,n);
        float* wetChannels[]{audio[b].getWritePointer(0),audio[b].getWritePointer(1)};
        float* refChannels[]{reference[b].getWritePointer(0),reference[b].getWritePointer(1)};
        AudioBuffer<float> wet(wetChannels,2,n),ref(refChannels,2,n);
        dsp::AudioBlock<float> wetBlock(wet);
        dsp::ProcessContextReplacing<float> preContext(wetBlock);pre[b].process(preContext);
        ref.copyFrom(0,0,wet,0,0,n);ref.copyFrom(1,0,wet,1,0,n);
        dsp::AudioBlock<float> refBlock(ref);
        auto up=activeOS>0?oversampling[b][activeOS]->processSamplesUp(wetBlock):wetBlock;
        if(activeOS>0)referenceOS[b][activeOS]->processSamplesUp(refBlock);
        for(size_t i=0;i<up.getNumSamples();++i) {
            const float drive=c.drive.getNextValue(),mix=c.mix.getNextValue();
            for(int ch=0;ch<2;++ch) {
                float& x=up.getChannelPointer(static_cast<size_t>(ch))[i];
                const float shaped=dc[b][ch].process(bassrack::distort(x,drive,type));
                if(type!=0)x+=(shaped-x)*mix;
            }
        }
        if(activeOS>0){oversampling[b][activeOS]->processSamplesDown(wetBlock);referenceOS[b][activeOS]->processSamplesDown(refBlock);}
        processRack(b,wet);
        wetAlignment[b].process(wetChannels[0],wetChannels[1],n,maximum-rackLatency(b));
        referenceAlignment[b].process(refChannels[0],refChannels[1],n,maximum);
        const bool capture=showScope&&scopeSource==b;
        for(int i=0;i<n;++i) {
            float l=wetChannels[0][i],r=wetChannels[1][i];
            bassrack::image(l,r,c.width.getNextValue(),c.pan.getNextValue());
            const float gain=c.level.getNextValue()*(mute?0.f:1.f);
            const bool selected=value(key(b,"solo"))>.5f;
            for(int ch=0;ch<2;++ch) {
                const float reference=refChannels[ch][i],processed=(ch==0?l:r)*gain;
                if(capture)scopeRegion.setSample(ch,i,(solo&&!selected)?0.f:(bypass?reference:processed));
                float result=bypass?reference:processed;
                if(solo)result=selected?result:0;
                else result=bypass?0:result-reference;
                wetChannels[ch][i]=result;
            }
        }
        if(capture) {
            float* channels[]{scopeRegion.getWritePointer(0),scopeRegion.getWritePointer(1)};
            AudioBuffer<float> scoped(channels,2,n);
            dsp::AudioBlock<float> block(scoped);dsp::ProcessContextReplacing<float> scopeContext(block);
            scopeFilters[b].process(scopeContext);writeStereoAnalysis(scoped,scopeRequest);
        }
        // Re-filter only the change, so generated harmonics remain local to the region.
        dsp::ProcessContextReplacing<float> postContext(wetBlock);post[b].process(postContext);
        buffer.addFrom(0,0,wet,0,0,n);buffer.addFrom(1,0,wet,1,0,n);
    }
    *lowEQ.state=dsp::IIR::ArrayCoefficients<float>::makeLowShelf(sampleRate,120,.707f,bassrack::gain(value("bass")));
    *highEQ.state=dsp::IIR::ArrayCoefficients<float>::makeHighShelf(sampleRate,jmin(4000.,sampleRate*.4),.707f,bassrack::gain(value("air")));
    dsp::AudioBlock<float> finalBlock(buffer);dsp::ProcessContextReplacing<float> context(finalBlock);
    lowEQ.process(context);highEQ.process(context);
    compressor.setThreshold(value("threshold"));compressor.setRatio(value("ratio"));
    compressor.setAttack(value("attack"));compressor.setRelease(value("release"));
    if(value("compress")>.5f)compressor.process(context);
    processRack(master,buffer);output.setTargetValue(bassrack::gain(value("output")));
    for(int i=0;i<n;++i) {
        const float l=buffer.getSample(0,i),r=buffer.getSample(1,i);
        const float il=matching.getSample(0,i),ir=matching.getSample(1,i);
        const float compensation=autoLevel.process(.5f*(il*il+ir*ir),.5f*(l*l+r*r),value("autogain")>.5f&&!solo);
        const float gain=compensation*output.getNextValue();
        for(int ch=0;ch<2;++ch){const float v=buffer.getSample(ch,i)*gain;buffer.setSample(ch,i,std::isfinite(v)?v:0);}
    }
    if(showScope&&scopeSource==master)writeStereoAnalysis(buffer,scopeRequest);
    autoDb.store(autoLevel.db());updateLatency();
}
double BassRackProcessor::getTailLengthSeconds() const {
    const ScopedLock lock(graphLock);
    double maximum=0;
    for(int b=0;b<bands;++b) {
        double tail=0; for(const auto& s:chains[b])if(s.plugin)tail+=s.plugin->getTailLengthSeconds();
        maximum=jmax(maximum,tail);
    }
    for(const auto& s:chains[master])if(s.plugin)maximum+=s.plugin->getTailLengthSeconds();
    return maximum+2.0*regionDelay/sampleRate;
}
void BassRackProcessor::closePluginWindows() {windows.clear();}
void BassRackProcessor::loadPlugin(int rack,int slot,const PluginDescription& description,MemoryBlock initial) {
    if(rack<0||rack>=racks||slot<0||slot>=slots)return;
    if(!effectEligible(description)) {
        const ScopedLock lock(graphLock); message="Choose an audio effect other than Bass Rack."; return;
    }
    uint64_t generation;
    {
        const ScopedLock lock(graphLock); closePluginWindows();
        auto& s=chains[rack][slot]; generation=++s.generation;
        s.plugin.reset(); s.description=description; s.saved=initial; s.loading=true;
        message="Loading "+description.name+"...";
    }
    WeakReference<BassRackProcessor> weak(this);
    formats.createPluginInstanceAsync(description,sampleRate,blockSize,
        [weak,rack,slot,generation,initial](std::unique_ptr<AudioPluginInstance> plugin,const String& error) {
            if(weak==nullptr)return;
            const ScopedLock lock(weak->graphLock);
            auto& s=weak->chains[rack][slot];
            if(s.generation!=generation)return;
            s.loading=false;
            if(!plugin) {weak->message="Unavailable: "+s.description.name+" - "+error;return;}
            plugin->disableNonMainBuses();
            auto layout=plugin->getBusesLayout();
            if(layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty()) {
                weak->message="This effect has no stereo main input/output.";return;
            }
            layout.inputBuses.getReference(0)=AudioChannelSet::stereo();
            layout.outputBuses.getReference(0)=AudioChannelSet::stereo();
            if(!plugin->setBusesLayout(layout)) {weak->message="This effect does not support stereo.";return;}
            if(initial.getSize()>0)plugin->setStateInformation(initial.getData(),static_cast<int>(initial.getSize()));
            plugin->setRateAndBufferSizeDetails(weak->sampleRate,weak->blockSize);
            plugin->prepareToPlay(weak->sampleRate,weak->blockSize);
            s.plugin=std::move(plugin); weak->message="Loaded "+s.description.name;
            weak->updateLatency();
        });
}
void BassRackProcessor::removePlugin(int rack,int slot) {
    const ScopedLock lock(graphLock); closePluginWindows();
    auto& s=chains[rack][slot]; ++s.generation; s.plugin.reset();
    s.description={}; s.saved.reset(); s.loading=false; message="Insert removed"; updateLatency();
}
String BassRackProcessor::slotName(int rack,int slot) const {
    const ScopedLock lock(graphLock); const auto& s=chains[rack][slot];
    if(s.loading)return "Loading: "+s.description.name;
    if(!s.occupied())return "+ Add AU effect";
    return s.plugin?s.description.name:"Unavailable: "+s.description.name;
}
String BassRackProcessor::status() const {const ScopedLock lock(graphLock);return message;}
void BassRackProcessor::editPlugin(int rack,int slot) {
    const ScopedLock lock(graphLock);
    if(auto* plugin=chains[rack][slot].plugin.get()) {
        auto* editor=plugin->createEditorIfNeeded();
        if(!editor)editor=new GenericAudioProcessorEditor(*plugin);
        // An existing editor may already belong to an owned window.
        for(auto* w:windows)if(w->getContentComponent()==editor) {w->setVisible(true);w->toFront(true);return;}
        windows.add(new PluginWindow(plugin->getName(),editor));
    }
}
void BassRackProcessor::showScanner() {
    if(scannerWindow!=nullptr){scannerWindow->setVisible(true);scannerWindow->toFront(true);return;}
    WeakReference<BassRackProcessor> weak(this);
    auto* panel=new EffectScanPanel(formats,known,[weak](String text){
        if(weak!=nullptr){const ScopedLock lock(weak->graphLock);weak->message=text;}
    });
    auto* window=new PluginWindow("Bass Rack / Effects-only AU scan",panel);
    windows.add(window);scannerWindow=window;window->setAlwaysOnTop(true);window->toFront(true);
}
void BassRackProcessor::getStateInformation(MemoryBlock& dest) {
    const ScopedLock lock(graphLock);
    auto root=state.copyState();
    root.setProperty("engineVersion",3,nullptr);
    if(auto old=root.getChildWithName("RACKS");old.isValid())root.removeChild(old,nullptr);
    ValueTree graph("RACKS");
    for(int r=0;r<racks;++r) for(int i=0;i<slots;++i) {
        auto& s=chains[r][i]; if(!s.occupied())continue;
        ValueTree node("INSERT"); node.setProperty("rack",r,nullptr); node.setProperty("slot",i,nullptr);
        if(auto xml=s.description.createXml())node.setProperty("description",xml->toString(),nullptr);
        if(s.plugin)s.plugin->getStateInformation(s.saved);
        node.setProperty("state",s.saved.toBase64Encoding(),nullptr); graph.appendChild(node,nullptr);
    }
    root.appendChild(pendingRestore.isValid()?pendingRestore.createCopy():graph,nullptr);
    if(auto xml=root.createXml())copyXmlToBinary(*xml,dest);
}
void BassRackProcessor::setStateInformation(const void* data,int size) {
    if(auto xml=getXmlFromBinary(data,size)) {
        auto root=ValueTree::fromXml(*xml); if(!root.hasType("BASSRACK"))return;
        const ScopedLock lock(graphLock);
        if(static_cast<int>(root.getProperty("engineVersion",0))<3) {
            // Old linked-crossovers cannot map exactly; preserve controls/inserts and
            // convert their frequency spans into independent center/Q regions.
            const int count=jlimit(1,5,static_cast<int>(root.getChildWithProperty("id","bands")["value"]));
            auto set=[&](String id,float v){auto child=root.getChildWithProperty("id",id);if(child.isValid())child.setProperty("value",v,nullptr);
                else {ValueTree node("PARAM");node.setProperty("id",id,nullptr);node.setProperty("value",v,nullptr);root.appendChild(node,nullptr);}};
            float low=20;
            for(int b=0;b<bands;++b) {
                float high=b==count-1?20000:static_cast<float>(root.getChildWithProperty("id","x"+String(b))["value"]);
                high=jmax(low+1,high);set(key(b,"active"),b<count?1.f:0.f);
                set(key(b,"freq"),std::sqrt(low*high));set(key(b,"q"),jlimit(.2f,16.f,std::sqrt(low*high)/(high-low)));
                low=high;
            }
            set("output",0);set("oversampling",1);set("autogain",0);
            message="Older project converted to independent regions. Check its settings.";
        }
        pendingRestore=root.getChildWithName("RACKS").createCopy();
        // Even a preset with no inserts must clear the existing graph.
        if(!pendingRestore.isValid())pendingRestore=ValueTree("RACKS");
        root.removeChild(root.getChildWithName("RACKS"),nullptr);
        state.replaceState(root); triggerAsyncUpdate();
    }
}
void BassRackProcessor::restoreRack(ValueTree graph) {
    closePluginWindows();
    for(auto& rack:chains)for(auto& s:rack) {
        ++s.generation; s.plugin.reset(); s.description={};s.saved.reset();s.loading=false;
    }
    for(auto node:graph) {
        const int r=node["rack"],i=node["slot"];
        if(r<0||r>=racks||i<0||i>=slots)continue;
        PluginDescription d;
        if(auto xml=XmlDocument::parse(node["description"].toString()))
            if(d.loadFromXml(*xml)) {
                MemoryBlock saved;saved.fromBase64Encoding(node["state"].toString());
                loadPlugin(r,i,d,saved);
            }
    }
}
void BassRackProcessor::handleAsyncUpdate() {
    {
        const ScopedLock lock(graphLock);
        if(pendingRestore.isValid()){auto graph=pendingRestore;pendingRestore={};restoreRack(graph);}
        if(prepared){updateLatency();setLatencySamples(desiredLatency.load());}
    }
    for(int b=0;b<bands;++b)if(filterDirty[b].exchange(false)) {
        double sr;{const ScopedLock lock(graphLock);if(!prepared)continue;sr=sampleRate;}
        auto impulse=designRegion(b,sr); // FFT/filter construction happens on the message thread.
        const ScopedLock lock(graphLock);
        if(prepared&&sampleRate==sr)loadRegion(b,std::move(impulse),sr);
    }
}
void BassRackProcessor::factoryPreset(int preset) {
    for(int b=0;b<bands;++b) {
        for(auto* id:{"type","drive","level","pan","mute","solo","bypass","active"})setParam(key(b,id),0);
        setParam(key(b,"mix"),1);setParam(key(b,"width"),1);setParam(key(b,"q"),1);setParam(key(b,"slope"),1);
    }
    setParam("output",0);setParam("autogain",0);setParam("oversampling",1);
    setParam("bass",0);setParam("air",0);setParam("compress",0);
    setParam("threshold",-12);setParam("ratio",2);setParam("attack",15);setParam("release",120);
    if(preset==0) {setParam(key(0,"active"),1);setParam(key(0,"freq"),55);}
    if(preset==1) {
        setParam(key(0,"active"),1);setParam(key(0,"freq"),150);setParam(key(0,"q"),.65f);
        setParam(key(0,"type"),2);setParam(key(0,"drive"),9);setParam(key(0,"mix"),.4f);
        setParam(key(1,"active"),1);setParam(key(1,"freq"),800);setParam(key(1,"q"),.75f);
        setParam(key(1,"type"),1);setParam(key(1,"drive"),12);setParam(key(1,"mix"),.3f);
        setParam("autogain",1);
    }
    if(preset==2) {
        setParam(key(0,"active"),1);setParam(key(0,"freq"),180);setParam(key(0,"type"),3);
        setParam(key(0,"drive"),12);setParam(key(0,"mix"),.5f);
        setParam(key(1,"active"),1);setParam(key(1,"freq"),1700);setParam(key(1,"type"),2);
        setParam(key(1,"drive"),18);setParam(key(1,"mix"),.4f);setParam(key(1,"width"),1.3f);setParam("autogain",1);
    }
}
void BassRackProcessor::savePreset(const File& file) {
    MemoryBlock block;getStateInformation(block);
    const bool ok=file.replaceWithData(block.getData(),block.getSize());
    const ScopedLock lock(graphLock); message=ok?"Preset saved":"Could not write preset";
}
void BassRackProcessor::loadPreset(const File& file) {
    MemoryBlock block;
    if(file.loadFileAsData(block))setStateInformation(block.getData(),static_cast<int>(block.getSize()));
}
AudioProcessorEditor* BassRackProcessor::createEditor(){return new BassRackEditor(*this);}
AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new BassRackProcessor();}
