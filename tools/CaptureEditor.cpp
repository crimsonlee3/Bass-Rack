#include "../Source/PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

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

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder=argc>1?juce::File(argv[1]):juce::File::getCurrentWorkingDirectory().getChildFile("previews");
    BassRackProcessor processor;
    runEngineChecks(processor);
    processor.factoryPreset(1);
    processor.setRateAndBufferSizeDetails(48000,512);
    processor.prepareToPlay(48000,512);
    capture(processor,folder.getChildFile("bass-rack-808.png"),false);
    const float centres[]{55,160,800,2500,7000};
    for(int b=0;b<5;++b){processor.setParam("b"+juce::String(b)+"_freq",centres[b]);processor.setParam("b"+juce::String(b)+"_q",1.5f);processor.setParam("b"+juce::String(b)+"_active",1);}

    capture(processor,folder.getChildFile("bass-rack-five-bands.png"),false);
    capture(processor,folder.getChildFile("bass-rack-master.png"),true);
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
