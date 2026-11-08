#include "../Source/PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

static void capture(BassRackProcessor& processor,const juce::File& output,bool master) {
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setVisible(true);
    if(master) {
        for(auto* child:editor->getChildren())
            if(auto* button=dynamic_cast<juce::TextButton*>(child))
                if(button->getButtonText()=="MASTER" && button->onClick)button->onClick();
    }
    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.5f);
    output.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(output);
    if(!stream.openedOk())throw std::runtime_error("Cannot create PNG");
    juce::PNGImageFormat png;
    if(!png.writeImageToStream(image,stream))throw std::runtime_error("Cannot encode PNG");
    std::cout<<output.getFileName()<<": "<<image.getWidth()<<" x "<<image.getHeight()<<"\n";
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder=argc>1?juce::File(argv[1]):juce::File::getCurrentWorkingDirectory().getChildFile("previews");
    BassRackProcessor processor;
    processor.factoryPreset(1);
    capture(processor,folder.getChildFile("bass-rack-808.png"),false);
    processor.setParam("bands",5);
    capture(processor,folder.getChildFile("bass-rack-five-bands.png"),false);
    capture(processor,folder.getChildFile("bass-rack-master.png"),true);
    return 0;
}
