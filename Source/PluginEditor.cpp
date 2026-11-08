#include "PluginEditor.h"
using namespace juce;
namespace {
const Colour background(0xff111315),panel(0xff1b1e21),text(0xffeeefeb),muted(0xff8b9294),accent(0xffffad55),edge(0xff34393c);
const std::array<Colour,5> colours{Colour(0xffe4c369),Colour(0xff77cfc0),Colour(0xff79aff2),Colour(0xffb19cfa),Colour(0xffed8b89)};
String bandKey(int b,const char* suffix){return "b"+String(b)+"_"+suffix;}
Font font(float size,bool bold=false){return Font(FontOptions(size,bold?Font::bold:Font::plain));}
void labelStyle(Label& l,float size,Colour colour){l.setFont(font(size));l.setColour(Label::textColourId,colour);}
String frequency(float hz){return hz>=1000?String(hz/1000,hz<10000?1:0)+"k":String(hz,0);}
}
void BassRackEditor::RackLook::drawButtonBackground(Graphics& g,Button& button,const Colour&,bool hover,bool down) {
    auto r=button.getLocalBounds().toFloat().reduced(.5f);
    const bool active=button.getToggleState();
    const auto colour=button.findColour(TextButton::textColourOnId);
    g.setColour(active?colour.withAlpha(.13f):Colour(hover?0xff2a2e31:0xff202427));
    g.fillRoundedRectangle(r,5);
    g.setColour(active?colour.withAlpha(.7f):Colour(down?0xff60696e:0xff343a3e));
    g.drawRoundedRectangle(r,5,.8f);
}
void BassRackEditor::RackLook::drawButtonText(Graphics& g,TextButton& button,bool,bool) {
    g.setFont(font(11.5f,true));
    g.setColour((button.getToggleState()?button.findColour(TextButton::textColourOnId):text).withAlpha(button.isEnabled()?1.f:.26f));
    g.drawFittedText(button.getButtonText(),button.getLocalBounds().reduced(8,2),Justification::centred,1);
}
void BassRackEditor::RackLook::drawToggleButton(Graphics& g,ToggleButton& button,bool hover,bool) {
    auto r=button.getLocalBounds().toFloat().reduced(.5f);
    const bool active=button.getToggleState();
    const auto colour=button.findColour(ToggleButton::tickColourId);
    g.setColour(active?colour.withAlpha(.17f):Colour(hover?0xff303538:0xff24282b));
    g.fillRoundedRectangle(r,4);
    g.setColour(active?colour:edge);g.drawRoundedRectangle(r,4,.8f);
    g.setFont(font(10.5f,true));g.setColour(active?colour:muted);
    g.drawText(button.getButtonText(),button.getLocalBounds(),Justification::centred);
}
void BassRackEditor::RackLook::drawComboBox(Graphics& g,int w,int h,bool,int,int,int,int,ComboBox&) {
    auto r=Rectangle<float>(0,0,static_cast<float>(w),static_cast<float>(h)).reduced(.5f);
    g.setColour(Colour(0xff24282b));g.fillRoundedRectangle(r,5);
    g.setColour(edge);g.drawRoundedRectangle(r,5,.8f);
    Path arrow;arrow.startNewSubPath(w-19.f,h*.44f);arrow.lineTo(w-15.f,h*.57f);arrow.lineTo(w-11.f,h*.44f);
    g.setColour(muted);g.strokePath(arrow,PathStrokeType(1.2f));
}
Font BassRackEditor::RackLook::getComboBoxFont(ComboBox&){return font(12,true);}
void BassRackEditor::RackLook::drawRotarySlider(Graphics& g,int x,int y,int w,int h,float position,float start,float end,Slider& slider) {
    const float radius=jmin(static_cast<float>(w),static_cast<float>(h))*.5f-9;
    const float cx=x+w*.5f,cy=y+h*.5f;
    auto colour=slider.findColour(Slider::rotarySliderFillColourId);
    Path track;track.addCentredArc(cx,cy,radius,radius,0,start,end,true);
    g.setColour(Colour(0xff363c40));g.strokePath(track,PathStrokeType(3.2f,PathStrokeType::curved,PathStrokeType::rounded));
    Path fill;fill.addCentredArc(cx,cy,radius,radius,0,start,start+position*(end-start),true);
    g.setColour(colour);g.strokePath(fill,PathStrokeType(3.2f,PathStrokeType::curved,PathStrokeType::rounded));
    const float inner=radius-7;
    g.setGradientFill(ColourGradient(Colour(0xff3b4246),cx,cy-inner,Colour(0xff1c2023),cx,cy+inner,false));
    g.fillEllipse(cx-inner,cy-inner,inner*2,inner*2);
    g.setColour(Colour(0xff50575a));g.drawEllipse(cx-inner,cy-inner,inner*2,inner*2,.7f);
    const float angle=start+position*(end-start);
    g.setColour(text);
    g.drawLine(cx+std::sin(angle)*inner*.57f,cy-std::cos(angle)*inner*.57f,
        cx+std::sin(angle)*inner*.86f,cy-std::cos(angle)*inner*.86f,2.1f);
    g.setColour(Colour(0xff131719));g.fillEllipse(cx-2,cy-2,4,4);
}
BassRackEditor::Knob::Knob() {
    slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(MathConstants<float>::pi*1.22f,MathConstants<float>::pi*2.78f,true);
    slider.setTextBoxStyle(Slider::TextBoxBelow,false,116,22);
    slider.setColour(Slider::textBoxOutlineColourId,Colours::transparentBlack);
    slider.setColour(Slider::textBoxBackgroundColourId,Colours::transparentBlack);
    label.setJustificationType(Justification::centred);
    labelStyle(label,10.5f,muted);addAndMakeVisible(slider);addAndMakeVisible(label);
}
void BassRackEditor::Knob::bind(BassRackProcessor& p,const String& id,const String& name) {
    attachment.reset();label.setText(name,dontSendNotification);
    attachment=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.state,id,slider);
    slider.setTextValueSuffix("");
    const bool percent=id.endsWith("mix")||id.endsWith("width"),pan=id.endsWith("pan");
    const bool time=id=="attack"||id=="release",ratio=id=="ratio";
    slider.textFromValueFunction=[percent,pan,time,ratio](double v) {
        if(percent)return String(roundToInt(v*100))+" %";
        if(pan)return std::abs(v)<.005?"CENTER":String(roundToInt(std::abs(v)*100))+(v<0?" L":" R");
        if(time)return String(v,v<10?1:0)+" ms";
        if(ratio)return String(v,1)+" : 1";
        return String(v,1)+" dB";
    };
    slider.valueFromTextFunction=[percent,pan](const String& input) {
        const double v=input.retainCharacters("0123456789.-").getDoubleValue();
        return percent?v/100.:pan?(input.containsIgnoreCase("L")?-std::abs(v)/100.:v/100.):v;
    };
    slider.setNumDecimalPlacesToDisplay(1);
}
void BassRackEditor::Knob::resized() {
    auto r=getLocalBounds();label.setBounds(r.removeFromTop(24));slider.setBounds(r);
}
Colour BassRackEditor::selectedColour() const {return selected==BassRackProcessor::master?accent:colours[static_cast<size_t>(selected)];}
BassRackEditor::BassRackEditor(BassRackProcessor& p):AudioProcessorEditor(p),processor(p),map(*this),transfer(*this) {
    look.setColour(ResizableWindow::backgroundColourId,background);
    look.setColour(TextButton::textColourOnId,accent);
    look.setColour(ComboBox::textColourId,text);
    look.setColour(Slider::textBoxTextColourId,text);
    look.setColour(ToggleButton::tickColourId,accent);
    setLookAndFeel(&look);
    title.setText("BASS RACK",dontSendNotification);title.setFont(font(25,true));title.setColour(Label::textColourId,text);
    subtitle.setText("MULTIBAND DISTORTION  /  AU RACK",dontSendNotification);labelStyle(subtitle,9.5f,muted);
    for(auto* label:{&title,&subtitle,&insertTitle,&statusLabel,&selectionTitle})addAndMakeVisible(*label);
    labelStyle(insertTitle,10,muted);labelStyle(statusLabel,10,muted);labelStyle(selectionTitle,12,text);
    addAndMakeVisible(map);addAndMakeVisible(transfer);
    for(int b=0;b<5;++b) {
        auto& button=bandButtons[static_cast<size_t>(b)];
        button.setButtonText("0"+String(b+1));
        button.setColour(TextButton::textColourOnId,colours[static_cast<size_t>(b)]);
        button.onClick=[this,b]{if(b<static_cast<int>(processor.value("bands")))select(b);};
        addAndMakeVisible(button);
    }
    for(auto* button:{&masterButton,&addButton,&removeButton,&scanButton,&saveButton,&loadButton})addAndMakeVisible(*button);
    masterButton.onClick=[this]{select(BassRackProcessor::master);};
    addButton.onClick=[this]{changeCount(1);};removeButton.onClick=[this]{changeCount(-1);};
    scanButton.onClick=[this]{processor.showScanner();};
    presets.addItem("INIT / CLEAN RACK",1);presets.addItem("808 / CLEAN SUB",2);presets.addItem("BASS / BITE",3);
    presets.setText("PRESETS",dontSendNotification);
    presets.setTooltip("Factory settings adjust stock controls and keep your AU inserts.");
    presets.onChange=[this]{if(presets.getSelectedId()>0){processor.factoryPreset(presets.getSelectedId()-1);select(0);}};
    addAndMakeVisible(presets);
    for(int i=0;i<6;++i)type.addItem(StringArray{"Clean","Soft","Tube","Clip","Fold","Sine"}[i],i+1);
    addAndMakeVisible(type);
    for(auto* toggle:{&mute,&solo,&bypass,&compress})addAndMakeVisible(*toggle);
    for(auto& knob:knobs)addAndMakeVisible(knob);
    for(int i=0;i<3;++i){addAndMakeVisible(inserts[static_cast<size_t>(i)]);inserts[static_cast<size_t>(i)].onClick=[this,i]{insertMenu(i);};}
    saveButton.onClick=[this] {
        chooser=std::make_unique<FileChooser>("Save full rack preset",File{},"*.bassrack");
        Component::SafePointer<BassRackEditor> safe(this);
        chooser->launchAsync(FileBrowserComponent::saveMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::warnAboutOverwriting,
            [safe](const FileChooser& c){if(safe && c.getResult()!=File{})safe->processor.savePreset(c.getResult().withFileExtension("bassrack"));});
    };
    loadButton.onClick=[this] {
        chooser=std::make_unique<FileChooser>("Load full rack preset",File{},"*.bassrack");
        Component::SafePointer<BassRackEditor> safe(this);
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles,
            [safe](const FileChooser& c){if(safe && c.getResult()!=File{})safe->processor.loadPreset(c.getResult());});
    };
    setSize(1180,820);setResizable(true,true);setResizeLimits(1000,780,1600,1100);
    select(0);startTimerHz(30);
}
BassRackEditor::~BassRackEditor(){stopTimer();setLookAndFeel(nullptr);}
void BassRackEditor::select(int band) {
    selected=band;const bool master=selected==BassRackProcessor::master;
    typeAttachment.reset();muteAttachment.reset();soloAttachment.reset();bypassAttachment.reset();compressAttachment.reset();
    type.setVisible(!master);transfer.setVisible(!master);
    mute.setVisible(!master);solo.setVisible(!master);bypass.setVisible(!master);compress.setVisible(master);
    for(auto& knob:knobs){knob.setVisible(true);knob.slider.setColour(Slider::rotarySliderFillColourId,selectedColour());}
    for(auto* toggle:{&mute,&solo,&bypass,&compress})toggle->setColour(ToggleButton::tickColourId,selectedColour());
    if(master) {
        const char* ids[]{"bass","air","threshold","ratio","attack","release","output"};
        const char* names[]{"BASS / 120 Hz","TREBLE / 4 kHz","THRESHOLD","RATIO","ATTACK","RELEASE","OUTPUT"};
        for(int i=0;i<7;++i)knobs[static_cast<size_t>(i)].bind(processor,ids[i],names[i]);
        compressAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"compress",compress);
        insertTitle.setText("MASTER FX CHAIN  /  AFTER EQ + COMPRESSION",dontSendNotification);
    } else {
        const char* ids[]{"drive","mix","level","width","pan"};
        const char* names[]{"DRIVE","DISTORTION MIX","BAND LEVEL","STEREO WIDTH","PAN"};
        for(int i=0;i<5;++i)knobs[static_cast<size_t>(i)].bind(processor,bandKey(selected,ids[i]),names[i]);
        knobs[5].setVisible(false);knobs[6].setVisible(false);
        typeAttachment=std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,bandKey(band,"type"),type);
        muteAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,bandKey(band,"mute"),mute);
        soloAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,bandKey(band,"solo"),solo);
        bypassAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,bandKey(band,"bypass"),bypass);
        insertTitle.setText("BAND 0"+String(band+1)+" FX CHAIN  /  SHAPER > AU INSERTS > STEREO > LEVEL",dontSendNotification);
    }
    for(int b=0;b<5;++b)bandButtons[static_cast<size_t>(b)].setToggleState(selected==b,dontSendNotification);
    masterButton.setToggleState(master,dontSendNotification);
    timerCallback();resized();repaint();
}
void BassRackEditor::changeCount(int delta) {
    const int count=jlimit(1,5,static_cast<int>(processor.value("bands"))+delta);
    processor.setParam("bands",static_cast<float>(count));
    if(selected<count || selected==BassRackProcessor::master)timerCallback();else select(count-1);
}
void BassRackEditor::addAt(float hz) {
    const int count=static_cast<int>(processor.value("bands"));
    if(count>=5)return;
    auto xs=processor.crossovers();std::vector<float> edges;
    for(int i=0;i<count-1;++i)edges.push_back(xs[i]);
    edges.push_back(jlimit(25.f,19000.f,hz));std::sort(edges.begin(),edges.end());
    for(int i=0;i<count;++i)processor.setParam("x"+String(i),edges[static_cast<size_t>(i)]);
    processor.setParam("bands",static_cast<float>(count+1));timerCallback();
}
void BassRackEditor::insertMenu(int slot) {
    PopupMenu menu;
    menu.addItem(1,"Open editor");menu.addItem(2,"Remove insert");menu.addSeparator();
    const auto types=processor.known.getTypes();
    PopupMenu effects;
    for(int i=0;i<types.size();++i)
        if(!types[i].isInstrument && types[i].name!="Bass Rack")
            effects.addItem(100+i,types[i].manufacturerName+" / "+types[i].name);
    if(types.isEmpty())menu.addItem(3,"Scan Audio Units...");
    else menu.addSubMenu("Load / replace AU effect",effects);
    menu.addItem(3,"Manage / scan Audio Units...");
    Component::SafePointer<BassRackEditor> safe(this);
    const int rack=selected;
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&inserts[slot]),
        [safe,slot,rack,types](int id) {
            if(!safe)return;
            if(id==1)safe->processor.editPlugin(rack,slot);
            else if(id==2)safe->processor.removePlugin(rack,slot);
            else if(id==3)safe->processor.showScanner();
            else if(id>=100 && id-100<types.size())safe->processor.loadPlugin(rack,slot,types[id-100]);
        });
}
void BassRackEditor::timerCallback() {
    const int count=static_cast<int>(processor.value("bands"));
    if(selected>=count && selected!=BassRackProcessor::master){select(count-1);return;}
    const auto edges=processor.crossovers();
    if(selected==BassRackProcessor::master)selectionTitle.setText("MASTER / FINISH",dontSendNotification);
    else {
        const float low=selected==0?20:edges[static_cast<size_t>(selected-1)];
        const float high=selected==count-1?20000:edges[static_cast<size_t>(selected)];
        selectionTitle.setText("BAND 0"+String(selected+1)+"   /   "+frequency(low)+" - "+frequency(high)+" Hz",dontSendNotification);
    }
    selectionTitle.setColour(Label::textColourId,selectedColour());
    for(int b=0;b<5;++b)bandButtons[static_cast<size_t>(b)].setEnabled(b<count);
    addButton.setEnabled(count<5);removeButton.setEnabled(count>1);
    for(int i=0;i<3;++i)inserts[static_cast<size_t>(i)].setButtonText("0"+String(i+1)+"   /   "+processor.slotName(selected,i));
    statusLabel.setText(processor.status()+"   /   "+String(processor.getLatencySamples())+" samples latency",dontSendNotification);
    map.updateSpectrum();map.repaint();transfer.repaint();
}
void BassRackEditor::paint(Graphics& g) {
    g.fillAll(background);
    g.setColour(Colour(0xff292e31));g.drawHorizontalLine(77,0,static_cast<float>(getWidth()));
    const float controls=static_cast<float>(getHeight()-372);
    g.setGradientFill(ColourGradient(Colour(0xff202427),0,controls,panel,0,controls+218,false));
    g.fillRoundedRectangle(24,controls,getWidth()-48.f,218,8);
    g.setColour(edge);g.drawRoundedRectangle(24.5f,controls+.5f,getWidth()-49.f,217,8,.8f);
    if(selected!=BassRackProcessor::master) {
        g.setColour(edge);g.drawVerticalLine(244,controls+18,controls+200);
        g.setColour(muted);g.setFont(font(10,true));g.drawText("SHAPER",42,static_cast<int>(controls+12),160,20,Justification::centredLeft);
    }
    g.setColour(panel);g.fillRoundedRectangle(24,controls+232,getWidth()-48.f,88,8);
    g.setColour(edge);g.drawRoundedRectangle(24.5f,controls+232.5f,getWidth()-49.f,87,8,.8f);
    g.setColour(muted);g.setFont(font(10.5f));
    g.drawText("Drag a crossover to split. Drag a numbered node to set band level. Double-click to add a band.",
        28,static_cast<int>(controls-28),getWidth()-56,20,Justification::centredLeft);
    g.setColour(Colour(0xff687276));g.setFont(font(9.5f,true));
    g.drawText("2x OVERSAMPLING    /    AU HOST    /    0.2.0",getWidth()-390,getHeight()-35,362,18,Justification::centredRight);
}
void BassRackEditor::resized() {
    const int w=getWidth(),c=getHeight()-372;
    title.setBounds(26,15,240,30);subtitle.setBounds(28,45,280,18);
    presets.setBounds(w-630,22,242,32);scanButton.setBounds(w-372,22,124,32);
    saveButton.setBounds(w-236,22,96,32);loadButton.setBounds(w-128,22,100,32);
    for(int b=0;b<5;++b)bandButtons[static_cast<size_t>(b)].setBounds(26+b*58,90,50,28);
    addButton.setBounds(328,90,30,28);removeButton.setBounds(366,90,30,28);
    masterButton.setBounds(w-136,90,108,28);
    map.setBounds(24,130,w-48,c-164);
    const bool master=selected==BassRackProcessor::master;
    type.setBounds(42,c+43,182,31);transfer.setBounds(42,c+89,182,109);
    selectionTitle.setBounds(master?42:260,c+10,master?420:340,24);
    mute.setBounds(w-296,c+12,66,25);solo.setBounds(w-222,c+12,66,25);bypass.setBounds(w-148,c+12,104,25);
    compress.setBounds(w-200,c+12,156,25);
    const int n=master?7:5,left=master?40:258,kw=(w-left-40)/n;
    for(int i=0;i<n;++i)knobs[static_cast<size_t>(i)].setBounds(left+i*kw,c+49,kw-4,151);
    insertTitle.setBounds(42,c+239,w-84,20);
    const int iw=(w-104)/3;
    for(int i=0;i<3;++i)inserts[static_cast<size_t>(i)].setBounds(42+i*(iw+10),c+267,iw,35);
    statusLabel.setBounds(28,getHeight()-37,w-450,22);
}
void BassRackEditor::TransferCurve::paint(Graphics& g) {
    if(editor.selected==BassRackProcessor::master)return;
    const auto colour=editor.selectedColour();
    auto r=getLocalBounds().toFloat().reduced(.5f);
    g.setColour(Colour(0xff15191b));g.fillRoundedRectangle(r,4);
    g.setColour(edge.withAlpha(.7f));g.drawRoundedRectangle(r,4,.7f);
    const auto plot=r.reduced(10,17);
    const float cx=plot.getCentreX(),cy=plot.getCentreY();
    g.setColour(Colour(0xff2f3639));g.drawHorizontalLine(static_cast<int>(cy),plot.getX(),plot.getRight());
    g.drawVerticalLine(static_cast<int>(cx),plot.getY(),plot.getBottom());
    const int type=static_cast<int>(editor.processor.value(bandKey(editor.selected,"type")));
    const float drive=editor.processor.value(bandKey(editor.selected,"drive"));
    Path line;
    for(int i=0;i<=160;++i) {
        const float x=-1+i/80.f,y=jlimit(-1.2f,1.2f,bassrack::distort(x,drive,type));
        const float px=plot.getX()+plot.getWidth()*i/160.f,py=cy-y*plot.getHeight()*.4f;
        if(i==0)line.startNewSubPath(px,py);else line.lineTo(px,py);
    }
    g.setColour(colour.withAlpha(.1f));g.strokePath(line,PathStrokeType(6));
    g.setColour(colour);g.strokePath(line,PathStrokeType(1.6f,PathStrokeType::curved));
    g.setFont(font(8.5f));g.setColour(muted);g.drawText("TRANSFER CURVE",10,getHeight()-16,getWidth()-20,12,Justification::centredLeft);
}
BassRackEditor::BandMap::BandMap(BassRackEditor& e):editor(e){spectrum.fill(-100);setMouseCursor(MouseCursor::PointingHandCursor);}
Rectangle<float> BassRackEditor::BandMap::plot() const {return {42,36,getWidth()-68.f,getHeight()-68.f};}
float BassRackEditor::BandMap::xFor(float hz) const {
    const auto r=plot();return r.getX()+r.getWidth()*std::log(hz/20.f)/std::log(1000.f);
}
float BassRackEditor::BandMap::hzFor(float x) const {
    const auto r=plot();return 20*std::pow(1000.f,jlimit(0.f,1.f,(x-r.getX())/r.getWidth()));
}
float BassRackEditor::BandMap::yFor(float db) const {
    const auto r=plot();return r.getBottom()-(jlimit(-72.f,12.f,db)+72)/84*r.getHeight();
}
Point<float> BassRackEditor::BandMap::node(int band) const {
    const auto edges=editor.processor.crossovers();
    const int count=static_cast<int>(editor.processor.value("bands"));
    const float lo=band==0?20:edges[static_cast<size_t>(band-1)],hi=band==count-1?20000:edges[static_cast<size_t>(band)];
    return {xFor(std::sqrt(lo*hi)),yFor(editor.processor.value(bandKey(band,"level")))};
}
void BassRackEditor::BandMap::updateSpectrum() {
    std::array<float,1024> scratch{};
    for(int block=0;block<8;++block) {
        const int n=editor.processor.readAnalyzer(scratch.data(),static_cast<int>(scratch.size()));
        if(n==0)break;
        for(int i=0;i<n;++i) {
            samples[static_cast<size_t>(filled++)]=scratch[static_cast<size_t>(i)];
            if(filled==2048) {
                std::copy(samples.begin(),samples.end(),fftData.begin());
                std::fill(fftData.begin()+2048,fftData.end(),0.f);
                window.multiplyWithWindowingTable(fftData.data(),2048);
                fft.performFrequencyOnlyForwardTransform(fftData.data());
                const double sr=editor.processor.getSampleRate()>0?editor.processor.getSampleRate():48000;
                for(size_t bin=0;bin<spectrum.size();++bin) {
                    const double hz=20*std::pow(1000.,static_cast<double>(bin)/(spectrum.size()-1));
                    const int index=jlimit(1,1023,static_cast<int>(hz*2048/sr));
                    const float magnitude=jmax(fftData[static_cast<size_t>(index-1)],fftData[static_cast<size_t>(index)],fftData[static_cast<size_t>(index+1)])/512.f;
                    const float db=Decibels::gainToDecibels(magnitude,-100.f);
                    spectrum[bin]=jmax(db,spectrum[bin]-2.0f);
                }
                filled=0;
            }
        }
    }
    for(auto& db:spectrum)db=jmax(-100.f,db-.35f);
}
void BassRackEditor::BandMap::paint(Graphics& g) {
    const int count=static_cast<int>(editor.processor.value("bands"));
    const auto edges=editor.processor.crossovers();const auto r=plot();
    const double sr=editor.processor.getSampleRate()>0?editor.processor.getSampleRate():48000;
    g.setColour(Colour(0xff14181a));g.fillRoundedRectangle(getLocalBounds().toFloat(),7);
    g.setColour(edge);g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),7,.8f);
    g.setFont(font(9.5f,true));g.setColour(muted);
    g.drawText("INPUT SPECTRUM  /  CROSSOVER RESPONSE",16,9,350,16,Justification::centredLeft);
    auto meter=[](float db){return db<=-99?String("-inf"):String(db,1);};
    g.drawText("IN "+meter(editor.processor.inputPeak())+"    /    OUT "+meter(editor.processor.outputPeak())+" dB",
        getWidth()-350,9,330,16,Justification::centredRight);
    for(float hz:{20.f,50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f,20000.f}) {
        const float x=xFor(hz);
        g.setColour(Colour(0xff282e31));g.drawVerticalLine(static_cast<int>(x),r.getY(),r.getBottom());
        g.setColour(muted);g.setFont(font(9.5f));
        g.drawText(frequency(hz),static_cast<int>(x-18),static_cast<int>(r.getBottom()+10),36,15,Justification::centred);
    }
    for(float db:{0.f,-12.f,-24.f,-36.f,-48.f,-60.f}) {
        g.setColour(db==0?Colour(0xff4a5154):Colour(0xff272d30));
        g.drawHorizontalLine(static_cast<int>(yFor(db)),r.getX(),r.getRight());
        g.setColour(muted);g.setFont(font(9));
        g.drawText(String(db,0),3,static_cast<int>(yFor(db)-7),30,14,Justification::centredRight);
    }
    g.saveState();g.reduceClipRegion(r.toNearestInt());
    Path spectrumLine;
    for(size_t i=0;i<spectrum.size();++i) {
        const float x=r.getX()+r.getWidth()*static_cast<float>(i)/(spectrum.size()-1),y=yFor(spectrum[i]);
        if(i==0)spectrumLine.startNewSubPath(x,y);else spectrumLine.lineTo(x,y);
    }
    Path spectrumFill=spectrumLine;spectrumFill.lineTo(r.getBottomRight());spectrumFill.lineTo(r.getBottomLeft());spectrumFill.closeSubPath();
    g.setGradientFill(ColourGradient(Colour(0xffc2c9cc).withAlpha(.15f),0,r.getY(),Colours::transparentBlack,0,r.getBottom(),false));
    g.fillPath(spectrumFill);g.setColour(Colour(0xffc4cbce).withAlpha(.45f));g.strokePath(spectrumLine,PathStrokeType(1.1f));
    std::array<double,4> cutoff{};
    for(int i=0;i<4;++i)cutoff[static_cast<size_t>(i)]=std::tan(bassrack::pi*jmin(static_cast<double>(edges[static_cast<size_t>(i)]),sr*.44)/sr);
    for(int band=0;band<count;++band) {
        const auto colour=colours[static_cast<size_t>(band)];
        const bool selected=editor.selected==band;
        const float level=editor.processor.value(bandKey(band,"level"));
        const bool muted=editor.processor.value(bandKey(band,"mute"))>.5f;
        Path response;
        for(int point=0;point<=400;++point) {
            const double hz=20*std::pow(1000.,point/400.);
            const double f=std::tan(bassrack::pi*jmin(hz,sr*.45)/sr);
            double magnitude=1;
            for(int split=0;split<band;++split) {
                const double q=std::pow(f/cutoff[static_cast<size_t>(split)],4);
                magnitude*=q/(1+q);
            }
            if(band<count-1)magnitude/=1+std::pow(f/cutoff[static_cast<size_t>(band)],4);
            const float db=static_cast<float>(20*std::log10(jmax(1.e-9,magnitude)))+level;
            const float x=r.getX()+r.getWidth()*point/400.f,y=yFor(db);
            if(point==0)response.startNewSubPath(x,y);else response.lineTo(x,y);
        }
        Path fill=response;fill.lineTo(r.getBottomRight());fill.lineTo(r.getBottomLeft());fill.closeSubPath();
        g.setGradientFill(ColourGradient(colour.withAlpha(muted?.015f:selected?.19f:.075f),0,r.getY(),colour.withAlpha(.008f),0,r.getBottom(),false));
        g.fillPath(fill);g.setColour(colour.withAlpha(muted?.25f:selected?1.f:.65f));
        g.strokePath(response,PathStrokeType(selected?2.2f:1.35f,PathStrokeType::curved));
    }
    g.restoreState();
    for(int i=0;i<count-1;++i) {
        const float x=xFor(edges[static_cast<size_t>(i)]);
        g.setColour(colours[static_cast<size_t>(i)].withAlpha(.25f));
        g.drawVerticalLine(static_cast<int>(x),r.getY()+8,r.getBottom());
        g.setColour(colours[static_cast<size_t>(i)]);
        Path handle;handle.addTriangle(x-4,r.getY(),x+4,r.getY(),x,r.getY()+6);g.fillPath(handle);
        g.setFont(font(9));g.setColour(text.withAlpha(.8f));
        g.drawText(frequency(edges[static_cast<size_t>(i)]),static_cast<int>(x-24),static_cast<int>(r.getBottom()-21),48,15,Justification::centred);
    }
    for(int band=0;band<count;++band) {
        const auto position=node(band);const bool selected=editor.selected==band;
        const auto colour=colours[static_cast<size_t>(band)];
        if(selected){g.setColour(colour.withAlpha(.13f));g.fillEllipse(position.x-16,position.y-16,32,32);}
        g.setColour(Colour(0xff171c1f));g.fillEllipse(position.x-9,position.y-9,18,18);
        g.setColour(colour);g.drawEllipse(position.x-9,position.y-9,18,18,selected?2.f:1.f);
        g.setFont(font(10,true));g.drawText(String(band+1),static_cast<int>(position.x-9),static_cast<int>(position.y-8),18,16,Justification::centred);
    }
}
void BassRackEditor::BandMap::mouseDown(const MouseEvent& e) {
    const int count=static_cast<int>(editor.processor.value("bands"));
    const auto xs=editor.processor.crossovers();
    for(int band=0;band<count;++band)if(e.position.getDistanceFrom(node(band))<13) {
        gainBand=band;editor.select(band);
        editor.processor.state.getParameter(bandKey(band,"level"))->beginChangeGesture();return;
    }
    for(int i=0;i<count-1;++i)if(std::abs(e.position.x-xFor(xs[static_cast<size_t>(i)]))<9) {
        dragging=i;editor.processor.state.getParameter("x"+String(i))->beginChangeGesture();return;
    }
    const float hz=hzFor(e.position.x);int band=0;
    while(band<count-1 && hz>xs[static_cast<size_t>(band)])++band;
    editor.select(band);
}
void BassRackEditor::BandMap::mouseDrag(const MouseEvent& e) {
    if(gainBand>=0) {
        const auto r=plot();const float db=jlimit(-36.f,12.f,(r.getBottom()-e.position.y)/r.getHeight()*84-72);
        auto* parameter=editor.processor.state.getParameter(bandKey(gainBand,"level"));
        parameter->setValueNotifyingHost(parameter->convertTo0to1(db));repaint();return;
    }
    if(dragging<0)return;
    const int count=static_cast<int>(editor.processor.value("bands"));const auto xs=editor.processor.crossovers();
    const float minimum=dragging==0?25.f:xs[static_cast<size_t>(dragging-1)]*1.08f;
    const float maximum=dragging==count-2?19000.f:xs[static_cast<size_t>(dragging+1)]/1.08f;
    if(minimum>maximum)return;
    auto* parameter=editor.processor.state.getParameter("x"+String(dragging));
    parameter->setValueNotifyingHost(parameter->convertTo0to1(jlimit(minimum,maximum,hzFor(e.position.x))));repaint();
}
void BassRackEditor::BandMap::mouseUp(const MouseEvent&) {
    if(dragging>=0)editor.processor.state.getParameter("x"+String(dragging))->endChangeGesture();
    if(gainBand>=0)editor.processor.state.getParameter(bandKey(gainBand,"level"))->endChangeGesture();
    dragging=gainBand=-1;
}
void BassRackEditor::BandMap::mouseDoubleClick(const MouseEvent& e){editor.addAt(hzFor(e.position.x));}
