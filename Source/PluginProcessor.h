#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Dsp.h"

class BassRackProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
    static constexpr int bands=5, racks=6, slots=3, master=5;
    BassRackProcessor();
    ~BassRackProcessor() override;
    juce::AudioProcessorValueTreeState state;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    const juce::String getName() const override {return "Bass Rack";}
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override;
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    void loadPlugin(int rack,int slot,const juce::PluginDescription&,juce::MemoryBlock initial={});
    void removePlugin(int rack,int slot);
    void editPlugin(int rack,int slot);
    juce::String slotName(int rack,int slot) const;
    void showScanner();
    void factoryPreset(int index);
    void setParam(const juce::String&,float);
    float value(const juce::String& id) const;
    std::array<float,4> crossovers() const;
    juce::String status() const;
    void savePreset(const juce::File&);
    void loadPreset(const juce::File&);
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    struct Slot {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription description;
        juce::MemoryBlock saved;
        bool loading=false;
        uint64_t generation=0;
        bool occupied() const {return description.name.isNotEmpty();}
    };
    std::array<std::array<Slot,slots>,racks> chains;
    mutable juce::CriticalSection graphLock;
    juce::OwnedArray<juce::DocumentWindow> windows;
    juce::String message="Ready";
    juce::File knownFile;
    bassrack::Splitter splitter[2];
    std::array<juce::AudioBuffer<float>,bands> audio;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>,bands> oversampling;
    std::array<std::array<bassrack::DCBlock,2>,bands> dc;
    std::array<bassrack::StereoDelay,bands> alignment;
    struct Controls {
        juce::SmoothedValue<float> drive,mix,level,width,pan;
    };
    std::array<Controls,bands> smooth;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>> lowEQ,highEQ;
    juce::dsp::Compressor<float> compressor;
    juce::SmoothedValue<float> output;
    std::atomic<int> desiredLatency{0};
    juce::ValueTree pendingRestore;
    double sampleRate=48000;
    int blockSize=512, lastBandCount=0;
    bool prepared=false;
    int osLatency=0;
    void processChunk(juce::AudioBuffer<float>&);
    void processRack(int,juce::AudioBuffer<float>&);
    int rackLatency(int) const;
    void handleAsyncUpdate() override;
    void restoreRack(juce::ValueTree);
    void updateLatency();
    void closePluginWindows();
    JUCE_DECLARE_WEAK_REFERENCEABLE(BassRackProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassRackProcessor)
};
