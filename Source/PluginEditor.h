#pragma once
#include "PluginProcessor.h"

class BassRackEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BassRackEditor(BassRackProcessor&);
    ~BassRackEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshDisplay(){timerCallback();}
private:
    class RackLook final : public juce::LookAndFeel_V4 {
    public:
        void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
        void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
        void drawToggleButton(juce::Graphics&,juce::ToggleButton&,bool,bool) override;
        void drawComboBox(juce::Graphics&,int,int,bool,int,int,int,int,juce::ComboBox&) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
    };
    class BandMap final : public juce::Component {
    public:
        explicit BandMap(BassRackEditor& e);
        void paint(juce::Graphics&) override;
        void updateSpectrum();
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
        float xFor(float hz) const;
        float hzFor(float x) const;
        juce::Rectangle<float> plot() const;
        float yFor(float db) const;
    private:
        BassRackEditor& editor;
        int dragging=-1,gainBand=-1,filled=0;
        float dragGain=0,dragY=0,fixedEdge=0;
        bool draggingUpper=false;
        juce::dsp::FFT fft{11};
        juce::dsp::WindowingFunction<float> window{2048,juce::dsp::WindowingFunction<float>::hann};
        std::array<float,2048> samples{};
        std::array<float,4096> fftData{};
        std::array<float,256> spectrum{};
        juce::Point<float> node(int band) const;
    };
    class TransferCurve final : public juce::Component {
    public:
        explicit TransferCurve(BassRackEditor& e):editor(e){}
        void paint(juce::Graphics&) override;
    private:
        BassRackEditor& editor;
    };
    class StereoScope final : public juce::Component {
    public:
        explicit StereoScope(BassRackEditor&);
        void paint(juce::Graphics&)override;
        void resized()override;
        void update(int source,bool enabled);
        const std::array<float,256>& midSpectrum()const{return midDb;}
        const std::array<float,256>& sideSpectrum()const{return sideDb;}
        bool active()const{return displaying;}
    private:
        BassRackEditor& editor;
        juce::ComboBox target;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;
        juce::dsp::FFT fft{12};
        juce::dsp::WindowingFunction<float> window{4096,juce::dsp::WindowingFunction<float>::hann};
        std::array<float,4096> midSamples{},sideSamples{};
        std::array<float,8192> midFFT{},sideFFT{};
        std::array<float,256> midDb{},sideDb{};
        std::array<juce::Point<float>,512> trace{};
        std::array<float,512> age{};
        std::array<float,3> sidePower{},totalPower{};
        std::uint32_t request=0;
        int filled=0,tracePosition=0,decimate=0,source=5;
        bool displaying=false;
        double ll=0,rr=0,lr=0;
        float peak=.03f;
        void resetDisplay();
        void transform();
    };
    struct Knob final : juce::Component {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        Knob();
        void bind(BassRackProcessor&,const juce::String&,const juce::String&);
        void resized() override;
    };
    BassRackProcessor& processor;
    RackLook look;
    BandMap map;
    TransferCurve transfer;
    StereoScope stereoScope;
    int selected=0;
    std::array<juce::TextButton,5> bandButtons;
    juce::TextButton masterButton{"MASTER"},addButton{"+"},removeButton{"-"},
        scanButton{"SCAN AUs"},saveButton{"SAVE"},loadButton{"LOAD"};
    juce::ComboBox presets,type,slope,oversamplingChoice,channelMode,harmonicsMode;
    juce::Slider frequencyControl,highControl,qControl,subCutoffControl;
    juce::TextButton deleteButton{"DELETE REGION"};
    juce::ToggleButton autoGain{"AUTO GAIN"},stereoView{"STEREO VIEW"},stereoize{"STEREOIZE"},adaa{"ADAA"},subProtect{"SUB PROTECT"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelAttachment,harmonicsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoizeAttachment,adaaAttachment,subAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> subCutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoViewAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment,qAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAttachment,osAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;
    juce::ToggleButton mute{"MUTE"},solo{"SOLO"},bypass{"BYPASS"},compress{"COMPRESSOR"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment,soloAttachment,bypassAttachment,compressAttachment;
    std::array<Knob,7> knobs;
    std::array<juce::TextButton,3> inserts;
    juce::Label title,subtitle,insertTitle,statusLabel,selectionTitle;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Colour selectedColour() const;
    void select(int);
    void changeCount(int);
    void addAt(float);
    void insertMenu(int);
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassRackEditor)
};
