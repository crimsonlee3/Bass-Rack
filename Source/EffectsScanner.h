#pragma once
#include "Dsp.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <AudioToolbox/AudioToolbox.h>
#include <unordered_map>
inline bool eligibleAU(const juce::String& id){return bassrack::effectAUIdentifier(id.toStdString());}
class EffectsOnlyAUFormat final : public juce::AudioUnitPluginFormat {
public:
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath& paths,bool recursive,bool async)override {
        auto all=AudioUnitPluginFormat::searchPathsForPlugins(paths,recursive,async);
        for(int i=all.size();--i>=0;)if(!eligibleAU(all[i]))all.remove(i);
        return all;
    }
    bool fileMightContainThisPluginType(const juce::String& id)override{return eligibleAU(id)&&AudioUnitPluginFormat::fileMightContainThisPluginType(id);}
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& out,const juce::String& id)override {
        if(eligibleAU(id))AudioUnitPluginFormat::findAllTypesForFile(out,id);
    }
};
inline juce::String auFourCC(OSType code) {
    char value[5]{static_cast<char>(code>>24),static_cast<char>(code>>16),static_cast<char>(code>>8),static_cast<char>(code),0};
    return juce::String::fromUTF8(value,4);
}
// Registration metadata only. Never creates an AudioUnit or opens an audio processor.
inline std::vector<juce::PluginDescription> registeredEffects() {
    std::vector<juce::PluginDescription> out;
    for(OSType type:{static_cast<OSType>(kAudioUnitType_Effect),static_cast<OSType>(kAudioUnitType_MusicEffect)}) {
        AudioComponentDescription query{};query.componentType=type;AudioComponent component=nullptr;
        while((component=AudioComponentFindNext(component,&query))!=nullptr) {
            AudioComponentDescription d{};if(AudioComponentGetDescription(component,&d)!=noErr)continue;
            juce::PluginDescription p;
            p.fileOrIdentifier="AudioUnit:Effects/"+auFourCC(d.componentType)+","+auFourCC(d.componentSubType)+","+auFourCC(d.componentManufacturer);
            if(!eligibleAU(p.fileOrIdentifier)||p.fileOrIdentifier.endsWith("aufx,BsRk,Crsn"))continue;
            CFStringRef label=nullptr;
            if(AudioComponentCopyName(component,&label)==noErr&&label!=nullptr){p.name=juce::String::fromCFString(label);CFRelease(label);}
            const int colon=p.name.indexOfChar(':');
            if(colon>=0){p.manufacturerName=p.name.substring(0,colon).trim();p.name=p.name.substring(colon+1).trim();}
            else p.manufacturerName=auFourCC(d.componentManufacturer);
            if(p.name.isEmpty())p.name=p.fileOrIdentifier;
            UInt32 version=0;if(AudioComponentGetVersion(component,&version)==noErr)
                p.version=juce::String(static_cast<int>(version>>16))+"."+juce::String(static_cast<int>((version>>8)&255))+"."+juce::String(static_cast<int>(version&255));
            p.pluginFormatName="AudioUnit";p.category="Effect";p.isInstrument=false;
            p.uniqueId=p.deprecatedUid=p.fileOrIdentifier.hashCode();p.numInputChannels=p.numOutputChannels=0;
            out.push_back(p);
        }
    }
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.name.compareIgnoreCase(b.name)<0;});
    return out;
}
class EffectScanPanel final : public juce::Component,private juce::Timer {
public:
    EffectScanPanel(juce::AudioPluginFormatManager&,juce::KnownPluginList& list,
        std::function<void(juce::String)> report,juce::File cache={})
        :known(list),notify(std::move(report)),cacheFile(cache),bar(progress) {
        title.setText("FAST AUDIO UNIT EFFECT DISCOVERY",juce::dontSendNotification);
        title.setFont(juce::Font(juce::FontOptions(19)));
        for(auto* label:{&title,&current,&detail}){label->setColour(juce::Label::textColourId,juce::Colours::whitesmoke);addAndMakeVisible(*label);}
        addAndMakeVisible(bar);addAndMakeVisible(scan);addAndMakeVisible(cancel);
        scan.onClick=[this]{start();};cancel.onClick=[this]{stopTimer();current.setText("Discovery cancelled",juce::dontSendNotification);scan.setEnabled(true);cancel.setEnabled(false);notify("AU discovery cancelled");};
        setSize(640,238);start();
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff171c21));}
    void resized()override {
        title.setBounds(24,18,getWidth()-48,30);current.setBounds(24,58,getWidth()-48,26);
        bar.setBounds(24,101,getWidth()-48,22);detail.setBounds(24,132,getWidth()-48,42);
        scan.setBounds(getWidth()-290,188,150,30);cancel.setBounds(getWidth()-128,188,104,30);
    }
private:
    juce::KnownPluginList& known;std::function<void(juce::String)> notify;juce::File cacheFile;
    double progress=0,started=0;juce::ProgressBar bar;juce::Label title,current,detail;
    juce::TextButton scan{"REFRESH EFFECTS"},cancel{"CANCEL"};
    std::vector<juce::PluginDescription> entries;
    std::unordered_map<std::string,juce::PluginDescription> cached;
    int index=0,reused=0;bool enumerated=false;
    void start() {
        stopTimer();index=reused=0;progress=0;enumerated=false;entries.clear();cached.clear();
        for(const auto& d:known.getTypes())cached.emplace(d.fileOrIdentifier.toStdString(),d);
        scan.setEnabled(false);cancel.setEnabled(true);started=juce::Time::getMillisecondCounterHiRes();
        current.setText("Reading macOS effect registration metadata...",juce::dontSendNotification);
        detail.setText("No effects are instantiated during discovery. Stereo compatibility is checked when loading an insert.",juce::dontSendNotification);
        notify("Discovering AU effects / metadata only");startTimer(15);
    }
    void timerCallback()override {
        if(!enumerated){entries=registeredEffects();enumerated=true;}
        const int end=juce::jmin(index+32,static_cast<int>(entries.size()));
        for(;index<end;++index) {
            const auto& d=entries[static_cast<size_t>(index)];const auto old=cached.find(d.fileOrIdentifier.toStdString());
            if(old!=cached.end()&&old->second.version==d.version)++reused;
            else {if(old!=cached.end())known.removeType(old->second);known.addType(d);}
        }
        progress=entries.empty()?1:static_cast<double>(index)/entries.size();
        current.setText("Discovered "+juce::String(index)+" / "+juce::String(entries.size())+" effects",juce::dontSendNotification);
        if(index==static_cast<int>(entries.size())) {
            stopTimer();scan.setEnabled(true);cancel.setEnabled(false);
            std::unordered_map<std::string,bool> present;for(const auto& d:entries)present[d.fileOrIdentifier.toStdString()]=true;
            for(const auto& d:known.getTypes())if(!eligibleAU(d.fileOrIdentifier)||present.find(d.fileOrIdentifier.toStdString())==present.end())known.removeType(d);
            if(cacheFile!=juce::File{})if(auto xml=known.createXml())xml->writeTo(cacheFile);
            const auto ms=juce::Time::getMillisecondCounterHiRes()-started;
            const auto summary=juce::String(entries.size())+" effects / "+juce::String(reused)+" cached / "+juce::String(ms/1000.,2)+" s";
            detail.setText("Ready. Metadata-only discovery; instruments excluded. Effects validate on load.",juce::dontSendNotification);
            current.setText(summary,juce::dontSendNotification);notify("AU discovery complete: "+summary);
        }
    }
};
