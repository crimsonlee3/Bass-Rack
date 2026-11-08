#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Dsp.h"

class BassRackProcessor final : public juce::AudioProcessor,private juce::AsyncUpdater,
    private juce::AudioProcessorValueTreeState::Listener {
public:
    static constexpr int bands=5,racks=6,slots=3,master=5,regionDelay=4096,irSize=8193;
    BassRackProcessor();
    ~BassRackProcessor() override;
    juce::AudioProcessorValueTreeState state;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    const juce::String getName()const override{return "Bass Rack";}
    void prepareToPlay(double,int)override;
    void releaseResources()override;
    void reset()override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    bool isBusesLayoutSupported(const BusesLayout&)const override;
    bool acceptsMidi()const override{return false;}
    bool producesMidi()const override{return false;}
    double getTailLengthSeconds()const override;
    bool hasEditor()const override{return true;}
    juce::AudioProcessorEditor* createEditor()override;
    int getNumPrograms()override{return 1;}
    int getCurrentProgram()override{return 0;}
    void setCurrentProgram(int)override{}
    const juce::String getProgramName(int)override{return{};}
    void changeProgramName(int,const juce::String&)override{}
    void getStateInformation(juce::MemoryBlock&)override;
    void setStateInformation(const void*,int)override;
    void loadPlugin(int,int,const juce::PluginDescription&,juce::MemoryBlock={});
    void removePlugin(int,int);
    void editPlugin(int,int);
    juce::String slotName(int,int)const;
    static bool effectEligible(const juce::PluginDescription&);
    void showScanner();
    void factoryPreset(int);
    void setParam(const juce::String&,float);
    float value(const juce::String&)const;
    bool bandActive(int b)const{return value("b"+juce::String(b)+"_active")>.5f;}
    int addRegion(float hz);
    void deleteRegion(int);
    juce::String status()const;
    int readAnalyzer(float*,int);
    std::uint32_t setStereoAnalysis(int source,bool enabled);
    int readStereoAnalysis(float*,float*,std::uint32_t*,int);
    float inputPeak()const{return inputMeter.load();}
    float outputPeak()const{return outputMeter.load();}
    float autoGainDb()const{return autoDb.load();}
    void savePreset(const juce::File&);
    void loadPreset(const juce::File&);
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    struct Slot {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription description;
        juce::MemoryBlock saved;
        bool loading=false;uint64_t generation=0;
        bool occupied()const{return description.name.isNotEmpty();}
    };
    std::array<std::array<Slot,slots>,racks> chains;
    juce::AbstractFifo stereoFifo{8192};
    std::array<std::array<float,8192>,2> stereoSamples{};
    std::array<std::uint32_t,8192> stereoTags{};
    std::atomic<std::uint32_t> stereoRequest{5};
    std::uint32_t lastStereoRequest=0;
    juce::AudioBuffer<float> scopeRegion;
    std::array<juce::dsp::Convolution,bands> scopeFilters;
    void writeStereoAnalysis(const juce::AudioBuffer<float>&,std::uint32_t);
    mutable juce::CriticalSection graphLock;
    juce::OwnedArray<juce::DocumentWindow> windows;
    juce::Component::SafePointer<juce::DocumentWindow> scannerWindow;
    juce::String message="Ready";juce::File knownFile;
    juce::AbstractFifo analyzerFifo{8192};
    std::array<float,8192> analyzerSamples{};
    std::atomic<float> inputMeter{-100},outputMeter{-100},autoDb{0};
    std::array<juce::AudioBuffer<float>,bands> audio,reference;
    juce::AudioBuffer<float> original,matching;
    std::array<juce::dsp::Convolution,bands> pre,post;
    std::array<std::array<std::unique_ptr<juce::dsp::Oversampling<float>>,5>,bands> oversampling,referenceOS;
    std::array<bassrack::StereoDelay,bands> wetAlignment,referenceAlignment;
    bassrack::StereoDelay dryAlignment,matchAlignment;
    std::array<std::array<bassrack::DCBlock,2>,bands> dc;
    std::array<std::atomic<bool>,bands> filterDirty{};
    struct Controls{juce::SmoothedValue<float> drive,mix,level,width,pan;};
    std::array<Controls,bands> smooth;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,juce::dsp::IIR::Coefficients<float>> lowEQ,highEQ;
    juce::dsp::Compressor<float> compressor;
    bassrack::AutoLevel autoLevel;
    juce::SmoothedValue<float> output;
    std::atomic<int> desiredLatency{0};
    juce::ValueTree pendingRestore;
    double sampleRate=48000;int blockSize=512,activeOS=-1,osLatency=0;bool prepared=false;
    void processChunk(juce::AudioBuffer<float>&);
    void processRack(int,juce::AudioBuffer<float>&);
    int rackLatency(int)const;
    void updateLatency();
    void handleAsyncUpdate()override;
    void parameterChanged(const juce::String&,float)override;
    void restoreRack(juce::ValueTree);
    void closePluginWindows();
    void selectOversampling(int);
    juce::AudioBuffer<float> designRegion(int,double)const;
    void loadRegion(int,juce::AudioBuffer<float>,double);
    JUCE_DECLARE_WEAK_REFERENCEABLE(BassRackProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassRackProcessor)
};
