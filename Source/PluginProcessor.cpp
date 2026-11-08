#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    auto floating=[&](String id,String name,float min,float max,float def,float skew=1.f){
        NormalisableRange<float> range(min,max,0.001f,skew);
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{id,1},name,range,def));
    };
    p.add(std::make_unique<AudioParameterInt>(ParameterID{"bands",1},"Band count",1,5,3));
    const float defaults[]{90,350,2500,8000};
    for(int i=0;i<4;++i) floating("x"+String(i),"Crossover "+String(i+1),20,20000,defaults[i],0.25f);
    for(int b=0;b<bands;++b) {
        const String prefix="Band "+String(b+1)+" ";
        p.add(std::make_unique<AudioParameterChoice>(ParameterID{key(b,"type"),1},prefix+"distortion",
            StringArray{"Clean","Soft","Tube","Clip","Fold","Sine"},0));
        floating(key(b,"drive"),prefix+"drive",0,36,0);
        floating(key(b,"mix"),prefix+"distortion mix",0,1,1);
        floating(key(b,"level"),prefix+"level",-36,12,0);
        floating(key(b,"width"),prefix+"width",0,2,1);
        floating(key(b,"pan"),prefix+"pan",-1,1,0);
        p.add(std::make_unique<AudioParameterBool>(ParameterID{key(b,"mute"),1},prefix+"mute",false));
        p.add(std::make_unique<AudioParameterBool>(ParameterID{key(b,"solo"),1},prefix+"solo",false));
        p.add(std::make_unique<AudioParameterBool>(ParameterID{key(b,"bypass"),1},prefix+"bypass",false));
    }
    floating("bass","Master bass",-12,12,0);
    floating("air","Master treble",-12,12,0);
    floating("threshold","Compressor threshold",-48,0,-12);
    floating("ratio","Compressor ratio",1,10,2);
    floating("attack","Compressor attack",0.1f,100,15,0.4f);
    floating("release","Compressor release",10,600,120,0.5f);
    p.add(std::make_unique<AudioParameterBool>(ParameterID{"compress",1},"Master compressor",false));
    floating("output","Output trim",-36,12,-6);
    return p;
}
BassRackProcessor::BassRackProcessor()
:AudioProcessor(BusesProperties().withInput("Input",AudioChannelSet::stereo(),true)
    .withOutput("Output",AudioChannelSet::stereo(),true)),
 state(*this,nullptr,"BASSRACK",parameters()) {
    formats.addDefaultFormats();
    knownFile=File::getSpecialLocation(File::userApplicationDataDirectory)
        .getChildFile("BassRack").getChildFile("known-plugins.xml");
    if(auto xml=XmlDocument::parse(knownFile)) known.recreateFromXml(*xml);
}
BassRackProcessor::~BassRackProcessor() {
    cancelPendingUpdate();
    closePluginWindows();
    if(auto xml=known.createXml()) {knownFile.getParentDirectory().createDirectory(); xml->writeTo(knownFile);}
}
bool BassRackProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainInputChannelSet()==AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet()==AudioChannelSet::stereo();
}
float BassRackProcessor::value(const String& id) const {return state.getRawParameterValue(id)->load();}
void BassRackProcessor::setParam(const String& id,float v) {
    if(auto* p=state.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    }
}
std::array<float,4> BassRackProcessor::crossovers() const {
    std::array<float,4> out{};
    float last=20;
    for(int i=0;i<4;++i) {out[i]=jlimit(last,20000.f,value("x"+String(i))); last=out[i];}
    return out;
}
void BassRackProcessor::prepareToPlay(double sr,int block) {
    const ScopedLock lock(graphLock);
    sampleRate=sr; blockSize=jmax(32,block); prepared=true; lastBandCount=0;
    const dsp::ProcessSpec spec{sr,static_cast<uint32>(blockSize),2};
    for(int b=0;b<bands;++b) {
        audio[b].setSize(2,blockSize);
        oversampling[b]=std::make_unique<dsp::Oversampling<float>>(2,1,
            dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,true,true);
        oversampling[b]->initProcessing(static_cast<size_t>(blockSize));
        alignment[b].prepare();
        for(auto& d:dc[b]) d.prepare(sr*2);
        auto& c=smooth[b];
        c.drive.reset(sr*2,.02); c.mix.reset(sr*2,.02);
        c.level.reset(sr,.02); c.width.reset(sr,.02); c.pan.reset(sr,.02);
        c.drive.setCurrentAndTargetValue(value(key(b,"drive")));
        c.mix.setCurrentAndTargetValue(value(key(b,"mix")));
        c.level.setCurrentAndTargetValue(bassrack::gain(value(key(b,"level"))));
        c.width.setCurrentAndTargetValue(value(key(b,"width")));
        c.pan.setCurrentAndTargetValue(value(key(b,"pan")));
    }
    osLatency=static_cast<int>(std::round(oversampling[0]->getLatencyInSamples()));
    output.reset(sr,.02); output.setCurrentAndTargetValue(bassrack::gain(value("output")));
    for(auto& s:splitter) s.reset();
    lowEQ.prepare(spec); highEQ.prepare(spec); compressor.prepare(spec);
    lowEQ.reset(); highEQ.reset(); compressor.reset();
    for(auto& rack:chains) for(auto& s:rack)
        if(s.plugin) {s.plugin->setRateAndBufferSizeDetails(sr,blockSize); s.plugin->prepareToPlay(sr,blockSize);}
    updateLatency();
}
void BassRackProcessor::releaseResources() {
    const ScopedLock lock(graphLock); prepared=false;
    for(auto& rack:chains) for(auto& s:rack) if(s.plugin) s.plugin->releaseResources();
}
void BassRackProcessor::reset() {
    const ScopedLock lock(graphLock);
    for(auto& s:splitter) s.reset();
    for(auto& d:alignment) d.reset();
    for(auto& os:oversampling) if(os)os->reset();
    for(auto& pair:dc) for(auto& d:pair)d.reset();
    lowEQ.reset(); highEQ.reset(); compressor.reset();
    for(auto& rack:chains) for(auto& s:rack) if(s.plugin)s.plugin->reset();
}
int BassRackProcessor::rackLatency(int rack) const {
    int sum=0;
    for(const auto& s:chains[rack]) if(s.plugin) sum+=jmax(0,s.plugin->getLatencySamples());
    return sum;
}
void BassRackProcessor::updateLatency() {
    const int n=jlimit(1,5,static_cast<int>(value("bands")));
    int maximum=0;
    for(int b=0;b<n;++b) maximum=jmax(maximum,rackLatency(b));
    const int latency=osLatency+maximum+rackLatency(master);
    if(desiredLatency.exchange(latency)!=latency) triggerAsyncUpdate();
}
void BassRackProcessor::processRack(int rack,AudioBuffer<float>& buffer) {
    MidiBuffer midi;
    for(auto& s:chains[rack]) if(s.plugin) {
        s.plugin->setPlayHead(getPlayHead());
        s.plugin->processBlock(buffer,midi); midi.clear();
    }
}
void BassRackProcessor::processBlock(AudioBuffer<float>& buffer,MidiBuffer& midi) {
    const ScopedTryLock lock(graphLock);
    ScopedNoDenormals noDenormals;
    midi.clear();
    if(!lock.isLocked() || !prepared || buffer.getNumChannels()!=2) {buffer.clear();return;}
    // Hosts can supply a block larger than the advertised maximum.
    for(int start=0;start<buffer.getNumSamples();start+=blockSize) {
        const int size=jmin(blockSize,buffer.getNumSamples()-start);
        float* channels[]{buffer.getWritePointer(0,start),buffer.getWritePointer(1,start)};
        AudioBuffer<float> chunk(channels,2,size); processChunk(chunk);
    }
}
void BassRackProcessor::processChunk(AudioBuffer<float>& buffer) {
    const int count=jlimit(1,5,static_cast<int>(value("bands")));
    const int size=buffer.getNumSamples();
    const bool topologyChanged=count!=lastBandCount;
    if(topologyChanged) {for(auto& s:splitter)s.reset(); lastBandCount=count;}
    const auto xs=crossovers();
    for(auto& s:splitter)s.configure(sampleRate,count,xs);
    for(int i=0;i<size;++i)
        for(int ch=0;ch<2;++ch) {
            const auto split=splitter[ch].process(buffer.getSample(ch,i));
            for(int b=0;b<count;++b)audio[b].setSample(ch,i,split[b]);
        }
    int maxLatency=0; bool anySolo=false;
    for(int b=0;b<count;++b) {
        maxLatency=jmax(maxLatency,rackLatency(b));
        anySolo=anySolo || value(key(b,"solo"))>.5f;
    }
    // Fail closed instead of silently misaligning extremely high-latency plugins.
    if(maxLatency>=bassrack::StereoDelay::capacity) {
        message="A band chain exceeds the 262143-sample latency limit."; buffer.clear();return;
    }
    buffer.clear();
    for(int b=0;b<count;++b) {
        auto& c=smooth[b]; const bool bypass=value(key(b,"bypass"))>.5f;
        const int type=bypass?0:static_cast<int>(value(key(b,"type")));
        c.drive.setTargetValue(value(key(b,"drive")));
        c.mix.setTargetValue(value(key(b,"mix")));
        c.level.setTargetValue(bassrack::gain(value(key(b,"level"))));
        c.width.setTargetValue(bypass?1.f:value(key(b,"width")));
        c.pan.setTargetValue(bypass?0.f:value(key(b,"pan")));
        float* channels[]{audio[b].getWritePointer(0),audio[b].getWritePointer(1)};
        AudioBuffer<float> band(channels,2,size);
        dsp::AudioBlock<float> block(band);
        auto up=oversampling[b]->processSamplesUp(block);
        for(size_t i=0;i<up.getNumSamples();++i) {
            const float drive=c.drive.getNextValue(),mix=c.mix.getNextValue();
            for(int ch=0;ch<2;++ch) {
                float& x=up.getChannelPointer(static_cast<size_t>(ch))[i];
                const float wet=dc[b][ch].process(bassrack::distort(x,drive,type));
                if(type!=0)x+=(wet-x)*mix;
            }
        }
        oversampling[b]->processSamplesDown(block);
        // Bypass retains inserts and their delay but asks each insert to bypass.
        if(bypass) {
            MidiBuffer empty;
            for(auto& s:chains[b]) if(s.plugin)s.plugin->processBlockBypassed(band,empty);
        } else processRack(b,band);
        alignment[b].process(channels[0],channels[1],size,maxLatency-rackLatency(b));
        const bool audible=value(key(b,"mute"))<.5f && (!anySolo || value(key(b,"solo"))>.5f);
        for(int i=0;i<size;++i) {
            float l=channels[0][i],r=channels[1][i];
            bassrack::image(l,r,c.width.getNextValue(),c.pan.getNextValue());
            const float level=c.level.getNextValue()*(audible?1.f:0.f);
            buffer.addSample(0,i,l*level); buffer.addSample(1,i,r*level);
        }
    }
    *lowEQ.state=dsp::IIR::ArrayCoefficients<float>::makeLowShelf(sampleRate,120,.707f,bassrack::gain(value("bass")));
    *highEQ.state=dsp::IIR::ArrayCoefficients<float>::makeHighShelf(sampleRate,jmin(4000.,sampleRate*.4),.707f,bassrack::gain(value("air")));
    dsp::AudioBlock<float> finalBlock(buffer); dsp::ProcessContextReplacing<float> context(finalBlock);
    lowEQ.process(context); highEQ.process(context);
    compressor.setThreshold(value("threshold")); compressor.setRatio(value("ratio"));
    compressor.setAttack(value("attack")); compressor.setRelease(value("release"));
    if(value("compress")>.5f) compressor.process(context);
    processRack(master,buffer);
    output.setTargetValue(bassrack::gain(value("output")));
    for(int i=0;i<size;++i) {
        const float gain=output.getNextValue();
        for(int ch=0;ch<2;++ch) {
            const float x=buffer.getSample(ch,i)*gain;
            buffer.setSample(ch,i,std::isfinite(x)?x:0.f);
        }
    }
    updateLatency();
}
double BassRackProcessor::getTailLengthSeconds() const {
    const ScopedLock lock(graphLock);
    double maximum=0;
    for(int b=0;b<bands;++b) {
        double tail=0; for(const auto& s:chains[b])if(s.plugin)tail+=s.plugin->getTailLengthSeconds();
        maximum=jmax(maximum,tail);
    }
    for(const auto& s:chains[master])if(s.plugin)maximum+=s.plugin->getTailLengthSeconds();
    return maximum;
}
void BassRackProcessor::closePluginWindows() {windows.clear();}
void BassRackProcessor::loadPlugin(int rack,int slot,const PluginDescription& description,MemoryBlock initial) {
    if(rack<0||rack>=racks||slot<0||slot>=slots)return;
    if(description.isInstrument || description.name=="Bass Rack"
        || description.fileOrIdentifier.containsIgnoreCase("audio.crimson.bassrack")) {
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
    auto* list=new PluginListComponent(formats,known,
        knownFile.getSiblingFile("scan-in-progress.txt"),nullptr,true);
    list->setSize(660,420);
    windows.add(new PluginWindow("AU effects - scan before adding inserts",list));
}
void BassRackProcessor::getStateInformation(MemoryBlock& dest) {
    const ScopedLock lock(graphLock);
    auto root=state.copyState();
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
    const ScopedLock lock(graphLock);
    if(pendingRestore.isValid()) {
        auto graph=pendingRestore; pendingRestore={};restoreRack(graph);
    }
    setLatencySamples(desiredLatency.load());
}
void BassRackProcessor::factoryPreset(int index) {
    // Factory settings alter stock controls, keeping user insert chains in place.
    for(int b=0;b<bands;++b) {
        setParam(key(b,"type"),0);setParam(key(b,"drive"),0);setParam(key(b,"mix"),1);
        setParam(key(b,"level"),0);setParam(key(b,"width"),1);setParam(key(b,"pan"),0);
        setParam(key(b,"solo"),0);setParam(key(b,"mute"),0);setParam(key(b,"bypass"),0);
    }
    setParam("bands",3);setParam("x0",90);setParam("x1",350);setParam("x2",2500);setParam("x3",8000);
    setParam("bass",0);setParam("air",0);setParam("compress",0);setParam("output",-6);
    setParam("threshold",-12);setParam("ratio",2);setParam("attack",15);setParam("release",120);
    if(index==1) { // Clean sub, harmonics on the body and upper band
        setParam(key(0,"width"),0);
        setParam(key(1,"type"),2);setParam(key(1,"drive"),9);setParam(key(1,"mix"),.45f);
        setParam(key(2,"type"),1);setParam(key(2,"drive"),12);setParam(key(2,"mix"),.35f);
    } else if(index==2) {
        setParam("bands",4);setParam("x0",80);setParam("x1",250);setParam("x2",1800);
        setParam(key(0,"width"),0);setParam(key(1,"type"),3);setParam(key(1,"drive"),12);
        setParam(key(1,"mix"),.5f);setParam(key(2,"type"),2);setParam(key(2,"drive"),18);
        setParam(key(2,"mix"),.4f);setParam(key(3,"width"),1.3f);
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
