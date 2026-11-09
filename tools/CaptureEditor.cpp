#include "../Source/PluginEditor.h"
#include "../Source/EffectsScanner.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <thread>

static void capture(BassRackProcessor& processor,const juce::File& output,bool master) {
    processor.prepareToPlay(48000,512);
    std::array<float,1024> discarded{};
    while(processor.readAnalyzer(discarded.data(),static_cast<int>(discarded.size()))>0){}
    // A deterministic bass test tone drives the real analyzer for these previews.
    juce::AudioBuffer<float> signal(2,512);juce::MidiBuffer midi;
    for(int block=0;block<64;++block) {
        for(int i=0;i<512;++i) {
            const double t=(block*512+i)/48000.0;
            const float x=static_cast<float>(.20*std::sin(2*bassrack::pi*55*t)
                +.055*std::sin(2*bassrack::pi*110*t)+.025*std::sin(2*bassrack::pi*220*t));
            signal.setSample(0,i,x);signal.setSample(1,i,x);
        }
        processor.processBlock(signal,midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setVisible(true);
    if(master) {
        for(auto* child:editor->getChildren())
            if(auto* button=dynamic_cast<juce::TextButton*>(child))
                if(button->getButtonText()=="MASTER" && button->onClick)button->onClick();
    }
    if(auto* rack=dynamic_cast<BassRackEditor*>(editor.get()))rack->refreshDisplay();
    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.5f);
    output.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(output);
    if(!stream.openedOk())throw std::runtime_error("Cannot create PNG");
    stream.setPosition(0);stream.truncate();
    juce::PNGImageFormat png;
    if(!png.writeImageToStream(image,stream))throw std::runtime_error("Cannot encode PNG");
    std::cout<<output.getFileName()<<": "<<image.getWidth()<<" x "<<image.getHeight()<<"\n";
}

static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void runEngineChecks(BassRackProcessor& processor) {
    const double start=juce::Time::getMillisecondCounterHiRes();
    const auto metadata=registeredEffects();
    for(const auto& d:metadata)require(BassRackProcessor::effectEligible(d),"Metadata discovery included non-effect");
    std::cout<<"Metadata-only discovery: "<<metadata.size()<<" effects / "<<juce::Time::getMillisecondCounterHiRes()-start<<" ms / zero instantiations\n";
    auto* format=processor.formats.getFormat(0);
    const auto candidates=format->searchPathsForPlugins({},false,true);
    require(!candidates.isEmpty(),"No registered effects found");
    for(const auto& id:candidates)require(bassrack::effectAUIdentifier(id.toStdString()),"Scanner enumerated a non-effect AU");
    std::cout<<"Effects-only enumeration: "<<candidates.size()<<" candidates; no instruments/generators\n";
    constexpr int total=32768;
    juce::MidiBuffer midi;
    for(int mode=0;mode<=4;++mode) {
        processor.factoryPreset(0);processor.setParam("oversampling",static_cast<float>(mode));
        processor.setRateAndBufferSizeDetails(48000,512);processor.prepareToPlay(48000,512);
        const auto captureTag=processor.setStereoAnalysis(mode%2==0?0:BassRackProcessor::master,true);
        const int latency=processor.getLatencySamples();
        std::vector<float> input(total);float maximum=0;
        for(int start=0;start<total;start+=512) {
            juce::AudioBuffer<float> block(2,512);
            for(int i=0;i<512;++i){const float x=.17f*std::sin(static_cast<float>((start+i)*2*bassrack::pi*137/48000));input[start+i]=x;block.setSample(0,i,x);block.setSample(1,i,x*.7f);}
            processor.processBlock(block,midi);
            for(int i=0;i<512;++i)if(start+i>=latency) {
                maximum=juce::jmax(maximum,std::abs(block.getSample(0,i)-input[start+i-latency]));
                maximum=juce::jmax(maximum,std::abs(block.getSample(1,i)-input[start+i-latency]*.7f));
            }
        }
        std::array<float,1024> left{},right{};std::array<std::uint32_t,1024> tags{};
        int captured=0;
        for(;;){
            const int n=processor.readStereoAnalysis(left.data(),right.data(),tags.data(),1024);
            if(n==0)break;captured+=n;
            for(int i=0;i<n;++i)require(tags[i]==captureTag&&std::isfinite(left[i])&&std::isfinite(right[i]),"Stereo capture tag/data mismatch");
        }
        require(captured>0,"Stereo capture produced no samples");
        processor.setStereoAnalysis(BassRackProcessor::master,false);
        juce::AudioBuffer<float> silence(2,512);silence.clear();processor.processBlock(silence,midi);
        require(processor.readStereoAnalysis(left.data(),right.data(),tags.data(),1024)==0,"Hidden stereo view still captured audio");
        require(maximum<2.e-5f,"Clean engine failed latency-aligned unity null test");
        std::cout<<"Unity/null OS "<<(1<<mode)<<"x: "<<maximum<<" / latency "<<latency<<"\n";
        processor.releaseResources();
    }
    for(double tone:{55.,1000.}) {
        processor.factoryPreset(0);processor.setParam("oversampling",0);
        processor.setParam("b0_freq",1000);processor.setParam("b0_q",2);processor.setParam("b0_slope",3);processor.setParam("b0_level",6);
        processor.prepareToPlay(48000,512);
        double power=0;
        for(int start=0;start<65536;start+=512) {
            juce::AudioBuffer<float> block(2,512);
            for(int i=0;i<512;++i){const float x=static_cast<float>(.1*std::sin((start+i)*2*bassrack::pi*tone/48000));block.setSample(0,i,x);block.setSample(1,i,x);}
            processor.processBlock(block,midi);
            if(start>=32768)for(int i=0;i<512;++i)power+=block.getSample(0,i)*block.getSample(0,i);
        }
        const double db=10*std::log10(power/32768/.005);
        require(std::abs(db-(tone==1000?6:0))<.16,"Region gain/isolation test failed");
        std::cout<<"Region "<<tone<<" Hz: "<<db<<" dB\n";
        processor.releaseResources();
    }
    processor.factoryPreset(0);processor.setParam("oversampling",0);
    processor.setParam("b0_freq",1000);processor.setParam("b0_q",2);
    processor.setParam("b0_level",6);processor.setParam("autogain",1);
    processor.prepareToPlay(48000,512);
    require(processor.getTailLengthSeconds()>=8192.0/48000,"FIR tail not reported");
    int sample=0;
    auto measure=[&](int blocks) {
        double energy=0;int count=0;
        for(int block=0;block<blocks;++block) {
            juce::AudioBuffer<float> audio(2,512);
            for(int i=0;i<512;++i){float x=static_cast<float>(.1*std::sin((sample++)*2*bassrack::pi*1000/48000));audio.setSample(0,i,x);audio.setSample(1,i,x);}
            processor.processBlock(audio,midi);
            if(block>=blocks-80)for(int i=0;i<512;++i){energy+=audio.getSample(0,i)*audio.getSample(0,i);++count;}
        }
        return 10*std::log10(energy/count/.005);
    };
    const double matched=measure(700);
    require(std::abs(matched)<.12,"Engine Auto Gain did not match input RMS");
    std::cout<<"Native Auto Gain: "<<matched<<" dB / correction "<<processor.autoGainDb()<<" dB\n";
    processor.setParam("output",-3);
    const double trimmed=measure(200);
    require(std::abs(trimmed+3)<.12,"Auto Gain fought manual output trim");
    std::cout<<"Manual trim with Auto Gain: "<<trimmed<<" dB\n";
    processor.releaseResources();processor.factoryPreset(0);
}


static void captureStereoUI(BassRackProcessor& processor,const juce::File& output,bool masterOutput) {
    processor.factoryPreset(1);processor.setParam("stereoview",1);
    processor.setParam("scopesource",masterOutput?1.f:0.f);processor.setParam("b0_width",1.7f);
    processor.setRateAndBufferSizeDetails(48000,512);processor.prepareToPlay(48000,512);
    std::array<float,1024> discarded{};
    while(processor.readAnalyzer(discarded.data(),1024)>0){}
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());editor->setVisible(true);
    auto* rack=dynamic_cast<BassRackEditor*>(editor.get());require(rack!=nullptr,"Stereo editor unavailable");
    juce::AudioBuffer<float> audio(2,512);juce::MidiBuffer midi;
    for(int block=0;block<120;++block) {
        for(int i=0;i<512;++i) {
            const double t=(block*512+i)/48000.;
            const float l=static_cast<float>(.2*std::sin(2*bassrack::pi*55*t)+.055*std::sin(2*bassrack::pi*110*t)+.025*std::sin(2*bassrack::pi*220*t)+.012*std::sin(2*bassrack::pi*2400*t));
            const float r=static_cast<float>(.2*std::sin(2*bassrack::pi*55*t)+.055*std::sin(2*bassrack::pi*110*t+.12)+.025*std::sin(2*bassrack::pi*220*t+.8)+.012*std::sin(2*bassrack::pi*2400*t+1.2));
            audio.setSample(0,i,l);audio.setSample(1,i,r);
        }
        processor.processBlock(audio,midi);rack->refreshDisplay();
    }
    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.5f);
    juce::FileOutputStream stream(output);stream.setPosition(0);stream.truncate();
    juce::PNGImageFormat png;require(png.writeImageToStream(image,stream),"Stereo screenshot failed");
    std::cout<<output.getFileName()<<": stereo test tone / "<<image.getWidth()<<" x "<<image.getHeight()<<"\n";
}


static double component(const std::vector<float>& data,double hz) {
    double c=0,s=0;
    for(size_t i=0;i<data.size();++i){c+=data[i]*std::cos(2*bassrack::pi*hz*i/48000);s+=data[i]*std::sin(2*bassrack::pi*hz*i/48000);}
    return 2*std::sqrt(c*c+s*s)/data.size();
}
static std::array<std::vector<float>,2> render(BassRackProcessor& p,double fm,double fs=0) {
    p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);
    std::array<std::vector<float>,2> result;juce::MidiBuffer midi;
    for(int start=0;start<65536;start+=512) {
        const int channels=p.getTotalNumOutputChannels();juce::AudioBuffer<float> audio(channels,512);
        for(int i=0;i<512;++i) {
            const float m=static_cast<float>(.12*std::sin(2*bassrack::pi*fm*(start+i)/48000));
            const float s=static_cast<float>(fs>0?.035*std::sin(2*bassrack::pi*fs*(start+i)/48000):0);
            audio.setSample(0,i,m+s);if(channels==2)audio.setSample(1,i,m-s);
        }
        p.processBlock(audio,midi);
        if(start>=32768)for(int i=0;i<512;++i){result[0].push_back(audio.getSample(0,i));result[1].push_back(audio.getSample(channels==2?1:0,i));}
    }
    p.releaseResources();return result;
}
static void runRevisionChecks(BassRackProcessor& p) {
    p.factoryPreset(0);
    p.setRegionEdges(0,200,2000);
    auto edges=p.regionEdges(0);require(std::abs(edges[0]-200)<.1&&std::abs(edges[1]-2000)<.1,"Independent edge setup failed");
    p.setRegionEdges(0,edges[0],3000);edges=p.regionEdges(0);
    require(std::abs(edges[0]-200)<.1&&std::abs(edges[1]-3000)<.1,"Moving high edge moved low edge");
    p.setRegionEdges(0,350,edges[1]);edges=p.regionEdges(0);
    require(std::abs(edges[0]-350)<.1&&std::abs(edges[1]-3000)<.1,"Moving low edge moved high edge");
    std::cout<<"Independent edges: "<<edges[0]<<" - "<<edges[1]<<" Hz\n";
    for(auto pair:std::array<std::pair<int,int>,3>{{{1,1},{1,2},{2,2}}}) {
        auto layout=p.getBusesLayout();layout.inputBuses.getReference(0)=pair.first==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference(0)=pair.second==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo();
        require(p.setBusesLayout(layout),"Requested Logic channel configuration unsupported");
        p.factoryPreset(0);p.setParam("oversampling",0);p.setParam("b0_freq",330);p.setParam("b0_q",.7f);
        p.setParam("b0_width",2);p.setParam("b0_depth",1);p.setParam("b0_stereoize",1);
        auto output=render(p,330);
        double mid=0,side=0,ll=0,rr=0,lr=0;std::vector<float> folded(output[0].size());
        for(size_t i=0;i<output[0].size();++i){double l=output[0][i],r=output[1][i],m=(l+r)*.5,s=(l-r)*.5;mid+=m*m;side+=s*s;ll+=l*l;rr+=r*r;lr+=l*r;folded[i]=static_cast<float>(m);}
        require(std::abs(20*std::log10(component(folded,330)/.12))<.15,"Stereoizer changed mono fold-down");
        if(pair.second==1)require(side<1.e-20,"True mono output leaked sides");
        else require(side/mid>.1&&bassrack::stereoCorrelation(ll,rr,lr)>-.03,"Mono source did not produce clean stereo spread");
        std::cout<<"Layout "<<pair.first<<"->"<<pair.second<<" / side-mid "<<side/(mid+1.e-20)<<" / correlation "<<bassrack::stereoCorrelation(ll,rr,lr)<<"\n";
    }
    for(int mode=1;mode<=2;++mode) {
        p.factoryPreset(0);p.setParam("oversampling",0);p.setParam("b0_freq",mode==1?330:1000);
        p.setParam("b0_q",.2f);p.setParam("b0_level",6);p.setParam("b0_channel",static_cast<float>(mode));
        auto out=render(p,330,1000);std::vector<float> mid(out[0].size()),side(mid.size());
        for(size_t i=0;i<mid.size();++i){mid[i]=(out[0][i]+out[1][i])*.5f;side[i]=(out[0][i]-out[1][i])*.5f;}
        const double mg=20*std::log10(component(mid,330)/.12),sg=20*std::log10(component(side,1000)/.035);
        require(std::abs(mg-(mode==1?6:0))<.2&&std::abs(sg-(mode==2?6:0))<.2,"Mid/Side isolation failed");
        std::cout<<"Channel mode "<<mode<<" / mid gain "<<mg<<" / side gain "<<sg<<" dB\n";
    }
    std::array<double,2> harmonic{};
    for(int mode=0;mode<2;++mode) {
        p.factoryPreset(0);p.setParam("subprotect",0);p.setParam("oversampling",0);
        p.setParam("b0_freq",330);p.setParam("b0_q",4);p.setParam("b0_slope",2);
        p.setParam("b0_type",3);p.setParam("b0_drive",24);p.setParam("b0_harmonics",static_cast<float>(mode));
        auto out=render(p,330);harmonic[mode]=component(out[0],990);
    }
    require(harmonic[0]>harmonic[1]*10,"Natural harmonics remained boxed into band");
    std::cout<<"Natural vs Contained 3rd harmonic: "<<20*std::log10(harmonic[0]/harmonic[1])<<" dB difference\n";
    std::array<double,2> bass{};
    for(int protect=0;protect<2;++protect) {
        p.factoryPreset(0);p.setParam("oversampling",0);p.setParam("subprotect",static_cast<float>(protect));
        p.setParam("b0_freq",55);p.setParam("b0_type",3);p.setParam("b0_drive",36);
        auto out=render(p,55);std::vector<float> mid(out[0].size());
        for(size_t i=0;i<mid.size();++i)mid[i]=(out[0][i]+out[1][i])*.5f;
        bass[protect]=20*std::log10(component(mid,55)/.12);
    }
    require(std::abs(bass[1])<.35&&std::abs(bass[0])>3,"Sub Protect did not restore original fundamental");
    std::cout<<"Sub Protect heavy-drive fundamental: off "<<bass[0]<<" / on "<<bass[1]<<" dB\n";
    p.factoryPreset(0);
}


struct BassRackTestAccess {
    static uint64_t begin(BassRackProcessor& p,int slot) {
        auto& s=p.chains[0][slot];s.loading=true;s.pendingName="Test replacement";return ++s.generation;
    }
    static void finish(BassRackProcessor& p,int slot,uint64_t gen,std::unique_ptr<juce::AudioPluginInstance> instance,
        juce::String error={},uint64_t configOffset=0) {
        juce::PluginDescription d;d.name="Lifecycle probe";d.fileOrIdentifier="AudioUnit:aufx,Test,CrSn";
        p.finishPluginLoad(0,slot,gen,p.configurationGeneration+configOffset,48000,512,d,{},std::move(instance),error);
    }
    static void changeConfiguration(BassRackProcessor& p){const juce::ScopedLock lock(p.graphLock);++p.configurationGeneration;}
    static bool unlocked(BassRackProcessor& p) {
        bool ok=false;
        std::thread worker([&]{ok=p.graphLock.tryEnter();if(ok)p.graphLock.exit();});worker.join();return ok;
    }
    static juce::AudioPluginInstance* instance(BassRackProcessor& p,int slot){return p.chains[0][slot].plugin.get();}
};
struct LifecycleProbe {
    int prepares=0,releases=0,destroyed=0,editorsDestroyed=0;
    bool lockFree=true,editorOrder=true,oldEditorAlive=true;
    std::function<void()> onPrepare;
};
class LifecycleEffect final : public juce::AudioPluginInstance {
public:
    LifecycleEffect(BassRackProcessor& p,LifecycleProbe& s):AudioPluginInstance(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),host(p),stats(s){}
    ~LifecycleEffect()override{stats.editorOrder&=getActiveEditor()==nullptr;++stats.destroyed;}
    void fillInPluginDescription(juce::PluginDescription& d)const override {
        d.name="Lifecycle probe";d.fileOrIdentifier="AudioUnit:aufx,Test,CrSn";d.pluginFormatName="AudioUnit";
    }
    const juce::String getName()const override{return "Lifecycle probe";}
    void prepareToPlay(double,int)override {
        ++stats.prepares;stats.lockFree&=BassRackTestAccess::unlocked(host);if(stats.onPrepare)stats.onPrepare();
    }
    void releaseResources()override {
        ++stats.releases;stats.lockFree&=BassRackTestAccess::unlocked(host);
        stats.editorOrder&=getActiveEditor()==nullptr;
    }
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override{}
    bool isBusesLayoutSupported(const BusesLayout& l)const override {
        return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            &&l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    bool acceptsMidi()const override{return false;}
    bool producesMidi()const override{return false;}
    double getTailLengthSeconds()const override{return 0;}
    bool hasEditor()const override{return true;}
    struct Editor final : juce::AudioProcessorEditor {
        LifecycleProbe& stats;
        Editor(LifecycleEffect& p):AudioProcessorEditor(p),stats(p.stats){setSize(240,100);}
        ~Editor()override{++stats.editorsDestroyed;}
    };
    juce::AudioProcessorEditor* createEditor()override{return new Editor(*this);}
    int getNumPrograms()override{return 1;}
    int getCurrentProgram()override{return 0;}
    void setCurrentProgram(int)override{}
    const juce::String getProgramName(int)override{return {};}
    void changeProgramName(int,const juce::String&)override{}
    void getStateInformation(juce::MemoryBlock&)override{}
    void setStateInformation(const void*,int)override{}
private:
    BassRackProcessor& host;LifecycleProbe& stats;
};
static void runHostingChecks() {
    LifecycleProbe old,other,replacement,canceled,config,changedDuringPrepare;
    BassRackProcessor p;
    auto load=[&](int slot,LifecycleProbe& stats) {
        auto gen=BassRackTestAccess::begin(p,slot);
        BassRackTestAccess::finish(p,slot,gen,std::make_unique<LifecycleEffect>(p,stats));
    };
    load(0,old);load(1,other);
    p.editPlugin(0,0);p.editPlugin(0,1);
    juce::Component::SafePointer<juce::AudioProcessorEditor> oldEditor(BassRackTestAccess::instance(p,0)->getActiveEditor());
    juce::Component::SafePointer<juce::AudioProcessorEditor> otherEditor(BassRackTestAccess::instance(p,1)->getActiveEditor());
    auto* original=BassRackTestAccess::instance(p,0);
    auto gen=BassRackTestAccess::begin(p,0);
    BassRackTestAccess::finish(p,0,gen,nullptr,"Expected test failure");
    require(BassRackTestAccess::instance(p,0)==original&&oldEditor!=nullptr&&otherEditor!=nullptr,
        "Failed replacement removed current effect or an editor");
    replacement.onPrepare=[&]{replacement.oldEditorAlive=oldEditor!=nullptr&&BassRackTestAccess::instance(p,0)==original;};
    load(0,replacement);
    require(replacement.oldEditorAlive&&oldEditor==nullptr&&otherEditor!=nullptr,
        "Replacement closed unrelated editor or retired old effect before new effect prepared");
    require(old.releases==1&&old.destroyed==1&&old.editorsDestroyed==1&&old.editorOrder&&old.lockFree,
        "Old effect cleanup ran under graph lock or before editor cleanup");
    gen=BassRackTestAccess::begin(p,0);p.removePlugin(0,0);
    BassRackTestAccess::finish(p,0,gen,std::make_unique<LifecycleEffect>(p,canceled));
    require(!BassRackTestAccess::instance(p,0)&&canceled.prepares==0&&canceled.destroyed==1,
        "Canceled async completion resurrected removed effect");
    gen=BassRackTestAccess::begin(p,0);
    BassRackTestAccess::finish(p,0,gen,std::make_unique<LifecycleEffect>(p,config),{},1);
    require(!BassRackTestAccess::instance(p,0)&&config.prepares==0&&config.destroyed==1,
        "Obsolete audio configuration was accepted");
    p.removePlugin(0,1);
    require(otherEditor==nullptr&&other.editorOrder&&other.lockFree&&other.releases==1,
        "Removal did not close its editor before releasing the AU");
    require(replacement.prepares==1&&replacement.releases==1&&replacement.lockFree,
        "Replacement was prepared more than once or held the graph lock");
    changedDuringPrepare.onPrepare=[&]{BassRackTestAccess::changeConfiguration(p);};
    load(0,changedDuringPrepare);
    require(!BassRackTestAccess::instance(p,0)&&changedDuringPrepare.prepares==1
        &&changedDuringPrepare.releases==1&&changedDuringPrepare.destroyed==1&&changedDuringPrepare.lockFree,
        "Configuration change during prepare installed a stale effect or skipped cleanup");
    for(const auto& d:p.known.getTypes())if(d.name=="Lifecycle probe")p.known.removeType(d);
    std::cout<<"Hosting lifecycle: failure rollback, replacement/editor order, cancellation and configuration guards passed\n";
}


static void runControlAndMeterChecks() {
    BassRackProcessor p;
    p.factoryPreset(0);p.state.copyState();p.controlUndo.clearUndoHistory();
    p.setParam("bass",6);
    require(p.undoControls()&&std::abs(p.value("bass"))<.001f,"Control undo failed");
    require(p.redoControls()&&std::abs(p.value("bass")-6)<.001f,"Control redo failed");
    const auto before=p.regionEdges(0);
    p.setRegionEdges(0,200,2000);
    require(p.undoControls(),"Region edge undo unavailable");
    const auto restored=p.regionEdges(0);
    require(std::abs(restored[0]-before[0])<.1f&&std::abs(restored[1]-before[1])<.1f,
        "Undo separated a grouped low/high edit");
    require(p.redoControls(),"Region edge redo unavailable");
    auto edges=p.regionEdges(0);
    require(std::abs(edges[0]-200)<.1f&&std::abs(edges[1]-2000)<.1f,"Grouped edge redo failed");
    p.setParam("output",-3);
    require(!p.controlUndo.canRedo(),"New edit did not invalidate redo");
    p.factoryPreset(0);p.setParam("oversampling",0);p.setParam("subprotect",0);
    p.setParam("b0_active",0);p.setParam("threshold",-24);p.setParam("ratio",4);
    p.setParam("attack",1);p.setParam("release",50);p.setParam("compress",1);
    p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);
    juce::AudioBuffer<float> block(2,512);juce::MidiBuffer midi;
    for(int count=0;count<200;++count) {
        for(int i=0;i<512;++i){block.setSample(0,i,.5f);block.setSample(1,i,.25f);}
        p.processBlock(block,midi);
    }
    const float expected=(-6.0206f+24)*(1-1.f/4);
    const float measured=-juce::Decibels::gainToDecibels(std::abs(block.getSample(0,511))/.5f);
    require(std::abs(p.compressionReductionDb()-expected)<.06f
        &&std::abs(p.compressionReductionDb()-measured)<.06f,
        "Compressor meter does not match actual applied gain");
    std::cout<<"Controls: undo/redo, grouped edges, redo invalidation passed; compressor GR "
        <<p.compressionReductionDb()<<" dB / measured "<<measured<<" dB\n";
    p.setParam("compress",0);block.clear();p.processBlock(block,midi);
    require(p.compressionReductionDb()==0,"Bypassed compressor retained GR");
    p.releaseResources();
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder=argc>1?juce::File(argv[1]):juce::File::getCurrentWorkingDirectory().getChildFile("previews");
    BassRackProcessor processor;
    runHostingChecks();
    runControlAndMeterChecks();
    runEngineChecks(processor);
    runRevisionChecks(processor);
    processor.factoryPreset(1);
    processor.setRateAndBufferSizeDetails(48000,512);
    processor.prepareToPlay(48000,512);
    capture(processor,folder.getChildFile("bass-rack-808.png"),false);
    const float centres[]{55,160,800,2500,7000};
    for(int b=0;b<5;++b){processor.setParam("b"+juce::String(b)+"_freq",centres[b]);processor.setParam("b"+juce::String(b)+"_q",1.5f);processor.setParam("b"+juce::String(b)+"_active",1);}

    capture(processor,folder.getChildFile("bass-rack-five-bands.png"),false);
    capture(processor,folder.getChildFile("bass-rack-master.png"),true);
    processor.setParam("compress",1);processor.setParam("threshold",-24);processor.setParam("ratio",4);
    capture(processor,folder.getChildFile("bass-rack-master-gr.png"),true);
    processor.setParam("compress",0);
    captureStereoUI(processor,folder.getChildFile("bass-rack-stereo-region.png"),false);
    captureStereoUI(processor,folder.getChildFile("bass-rack-stereo-master.png"),true);
    processor.showScanner();
    for(int i=0;i<juce::Desktop::getInstance().getNumComponents();++i) {
        auto* window=dynamic_cast<juce::DocumentWindow*>(juce::Desktop::getInstance().getComponent(i));
        if(window&&window->getName().contains("Effects-only")) {
            auto image=window->createComponentSnapshot(window->getLocalBounds(),true,1.5f);
            juce::FileOutputStream stream(folder.getChildFile("bass-rack-effects-scan.png"));stream.setPosition(0);stream.truncate();
            juce::PNGImageFormat png;require(png.writeImageToStream(image,stream),"Scanner PNG capture failed");
        }
    }
    return 0;
}
