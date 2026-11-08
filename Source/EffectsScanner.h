#pragma once
#include "Dsp.h"
#include <juce_audio_utils/juce_audio_utils.h>
inline bool eligibleAU(const juce::String& id) {return bassrack::effectAUIdentifier(id.toStdString());}

class EffectsOnlyAUFormat final : public juce::AudioUnitPluginFormat {
public:
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath& paths,bool recursive,bool async)override {
        auto all=AudioUnitPluginFormat::searchPathsForPlugins(paths,recursive,async);
        for(int i=all.size();--i>=0;)if(!eligibleAU(all[i]))all.remove(i);
        return all;
    }
    bool fileMightContainThisPluginType(const juce::String& id)override {
        return eligibleAU(id)&&AudioUnitPluginFormat::fileMightContainThisPluginType(id);
    }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& out,const juce::String& id)override {
        if(eligibleAU(id))AudioUnitPluginFormat::findAllTypesForFile(out,id);
    }
};
class EffectScanPanel final : public juce::Component,private juce::Timer {
public:
    EffectScanPanel(juce::AudioPluginFormatManager& f,juce::KnownPluginList& k,
        std::function<void(juce::String)> report):formats(f),known(k),notify(std::move(report)),bar(progress) {
        title.setText("AUDIO UNIT EFFECTS",juce::dontSendNotification);
        title.setFont(juce::Font(juce::FontOptions(20)));
        for(auto* label:{&title,&current,&detail}){label->setColour(juce::Label::textColourId,juce::Colours::whitesmoke);addAndMakeVisible(*label);}
        addAndMakeVisible(bar);addAndMakeVisible(scan);addAndMakeVisible(cancel);
        scan.onClick=[this]{start();};cancel.onClick=[this]{cancelled=true;cancel.setEnabled(false);detail.setText("Stopping after the current effect. No further effects will be loaded.",juce::dontSendNotification);notify("AU scan cancellation requested");};
        setSize(640,238);start();
    }
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff171c21));}
    void resized()override {
        title.setBounds(24,18,getWidth()-48,30);current.setBounds(24,58,getWidth()-48,26);
        bar.setBounds(24,101,getWidth()-48,22);detail.setBounds(24,132,getWidth()-48,40);
        scan.setBounds(getWidth()-268,188,116,30);cancel.setBounds(getWidth()-140,188,116,30);
    }
private:
    juce::AudioPluginFormatManager& formats;juce::KnownPluginList& known;
    std::function<void(juce::String)> notify;
    double progress=0;juce::ProgressBar bar;
    juce::Label title,current,detail;
    juce::TextButton scan{"RESCAN"},cancel{"CANCEL"};
    juce::StringArray candidates;
    int index=0,accepted=0,rejected=0;bool waiting=false,cancelled=false;
    juce::Time started;
    void start() {
        if(waiting)return;
        candidates.clear();index=accepted=rejected=0;progress=0;cancelled=false;
        started=juce::Time::getCurrentTime();scan.setEnabled(false);cancel.setEnabled(true);
        if(formats.getNumFormats()>0)candidates=formats.getFormat(0)->searchPathsForPlugins({},false,true);
        for(int i=candidates.size();--i>=0;) {
            if(!eligibleAU(candidates[i]) || candidates[i].endsWith("aufx,BsRk,Crsn"))candidates.remove(i);
        }
        current.setText("Preparing "+juce::String(candidates.size())+" effect candidates...",juce::dontSendNotification);
        detail.setText("Instruments, generators and MIDI-only plugins are excluded before validation.",juce::dontSendNotification);
        notify("Scanning AU effects / 0 of "+juce::String(candidates.size()));
        startTimer(80);
    }
    void timerCallback()override {
        if(waiting)return;
        if(cancelled || index>=candidates.size()) {
            stopTimer();progress=cancelled?progress:1;scan.setEnabled(true);cancel.setEnabled(false);
            const auto seconds=(juce::Time::getCurrentTime()-started).inSeconds();
            auto summary=(cancelled?juce::String("Cancelled"):juce::String("Complete"))+" / "+
                juce::String(accepted)+" effects ready / "+juce::String(rejected)+" unsupported or unavailable / "+juce::String(seconds)+" s";
            current.setText(summary,juce::dontSendNotification);notify("AU scan: "+summary);return;
        }
        const auto id=candidates[index];
        const auto name=formats.getFormat(0)->getNameOfPluginFromIdentifier(id);
        current.setText("Scanning "+juce::String(index+1)+" / "+juce::String(candidates.size())+" : "+name,juce::dontSendNotification);
        notify("Scanning AU effect "+juce::String(index+1)+"/"+juce::String(candidates.size())+" / "+name);
        waiting=true;
        juce::PluginDescription description;description.fileOrIdentifier=id;description.pluginFormatName="AudioUnit";description.name=name;
        juce::Component::SafePointer<EffectScanPanel> safe(this);
        formats.createPluginInstanceAsync(description,48000,512,
            [safe](std::unique_ptr<juce::AudioPluginInstance> plugin,const juce::String&) {
                if(!safe)return;
                safe->waiting=false;
                if(!safe->cancelled && plugin) {
                    plugin->disableNonMainBuses();auto layout=plugin->getBusesLayout();
                    bool stereo=!layout.inputBuses.isEmpty()&&!layout.outputBuses.isEmpty();
                    if(stereo) {
                        layout.inputBuses.getReference(0)=juce::AudioChannelSet::stereo();
                        layout.outputBuses.getReference(0)=juce::AudioChannelSet::stereo();
                        stereo=plugin->setBusesLayout(layout);
                    }
                    auto d=plugin->getPluginDescription();
                    if(stereo&&!d.isInstrument&&eligibleAU(d.fileOrIdentifier)) {
                        safe->known.addType(d);++safe->accepted;
                    }else ++safe->rejected;
                }else ++safe->rejected;
                ++safe->index;
                safe->progress=safe->candidates.isEmpty()?1:static_cast<double>(safe->index)/safe->candidates.size();
                safe->detail.setText(juce::String(safe->accepted)+" stereo effects available. Instruments were not scanned.",juce::dontSendNotification);
            });
    }
};
