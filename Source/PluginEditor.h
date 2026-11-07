#pragma once
#include "PluginProcessor.h"

class BassRackEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BassRackEditor(BassRackProcessor&);
    ~BassRackEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    class BandMap final : public juce::Component {
    public:
        explicit BandMap(BassRackEditor& e):editor(e){}
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        float xFor(float hz) const;
        float hzFor(float x) const;
    private:
        BassRackEditor& editor;
        int dragging=-1;
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
    juce::LookAndFeel_V4 look;
    BandMap map;
    int selected=0;
    std::array<juce::TextButton,5> bandButtons;
    juce::TextButton masterButton{"MASTER"},addButton{"+"},removeButton{"-"},
        scanButton{"Scan AUs"},saveButton{"Save preset"},loadButton{"Load preset"};
    juce::ComboBox presets,type;
    juce::ToggleButton mute{"Mute"},solo{"Solo"},bypass{"Bypass band"},compress{"Compression"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment,soloAttachment,bypassAttachment,compressAttachment;
    std::array<Knob,7> knobs;
    std::array<juce::TextButton,3> inserts;
    juce::Label title,subtitle,insertTitle,statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    void select(int);
    void changeCount(int);
    void addAt(float);
    void insertMenu(int);
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassRackEditor)
};
