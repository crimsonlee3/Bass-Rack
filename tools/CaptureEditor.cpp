#include "../Source/PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

static void capture(BassRackProcessor& processor,const juce::File& output,bool master) {
    // A deterministic bass test tone drives the real analyzer for these previews.
    juce::AudioBuffer<float> signal(2,512);juce::MidiBuffer midi;
    for(int block=0;block<16;++block) {
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
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder=argc>1?juce::File(argv[1]):juce::File::getCurrentWorkingDirectory().getChildFile("previews");
    BassRackProcessor processor;
    processor.factoryPreset(1);
    processor.setRateAndBufferSizeDetails(48000,512);
    processor.prepareToPlay(48000,512);
    capture(processor,folder.getChildFile("bass-rack-808.png"),false);
    processor.setParam("bands",5);
    capture(processor,folder.getChildFile("bass-rack-five-bands.png"),false);
    capture(processor,folder.getChildFile("bass-rack-master.png"),true);
    return 0;
}
