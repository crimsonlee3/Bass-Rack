#include "PluginEditor.h"
using namespace juce;
namespace {
Colour background(0xff0d1119),panel(0xff171e29),text(0xffedf2fa),muted(0xff8f9db3),accent(0xffb1f26b);
const std::array<Colour,5> colours{Colour(0xffb1f26b),Colour(0xff56d8cf),Colour(0xff69a6ff),Colour(0xffbb8cff),Colour(0xffffad70)};
String bandKey(int b,const char* suffix){return "b"+String(b)+"_"+suffix;}
void labelStyle(Label& l,float size,Colour colour){l.setFont(Font(size));l.setColour(Label::textColourId,colour);}
}
BassRackEditor::Knob::Knob() {
    slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(Slider::TextBoxBelow,false,100,22);
    slider.setColour(Slider::textBoxOutlineColourId,Colours::transparentBlack);
    label.setJustificationType(Justification::centred);
    labelStyle(label,13,muted);addAndMakeVisible(slider);addAndMakeVisible(label);
}
void BassRackEditor::Knob::bind(BassRackProcessor& p,const String& id,const String& name) {
    attachment.reset();label.setText(name,dontSendNotification);
    slider.setNumDecimalPlacesToDisplay(1);
    slider.setTextValueSuffix(id.endsWith("drive")||id.endsWith("level")||id=="output"
        ||id=="bass"||id=="air"||id=="threshold"?" dB":id=="attack"||id=="release"?" ms":"");
    attachment=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.state,id,slider);
}
void BassRackEditor::Knob::resized() {
    auto r=getLocalBounds();label.setBounds(r.removeFromTop(22));slider.setBounds(r);
}
BassRackEditor::BassRackEditor(BassRackProcessor& p):AudioProcessorEditor(p),processor(p),map(*this) {
    look.setColour(ResizableWindow::backgroundColourId,background);
    look.setColour(TextButton::buttonColourId,panel);
    look.setColour(TextButton::buttonOnColourId,Colour(0xff344832));
    look.setColour(TextButton::textColourOffId,text);
    look.setColour(TextButton::textColourOnId,accent);
    look.setColour(ComboBox::backgroundColourId,panel);
    look.setColour(ComboBox::textColourId,text);
    look.setColour(ComboBox::outlineColourId,Colour(0xff303b4b));
    look.setColour(Slider::rotarySliderFillColourId,accent);
    look.setColour(Slider::rotarySliderOutlineColourId,Colour(0xff303b4b));
    look.setColour(Slider::textBoxTextColourId,text);
    look.setColour(ToggleButton::textColourId,text);
    look.setColour(ToggleButton::tickColourId,accent);
    setLookAndFeel(&look);
    title.setText("BASS RACK",dontSendNotification);labelStyle(title,25,text);
    subtitle.setText("Shape the weight. Build the harmonics.",dontSendNotification);labelStyle(subtitle,13,muted);
    for(auto* l:{&title,&subtitle,&insertTitle,&statusLabel})addAndMakeVisible(*l);
    labelStyle(insertTitle,14,text);labelStyle(statusLabel,12,muted);
    addAndMakeVisible(map);
    for(int b=0;b<5;++b) {
        auto& button=bandButtons[b];button.setButtonText("BAND "+String(b+1));
        button.onClick=[this,b]{if(b<static_cast<int>(processor.value("bands")))select(b);};
        addAndMakeVisible(button);
    }
    for(auto* button:{&masterButton,&addButton,&removeButton,&scanButton,&saveButton,&loadButton})addAndMakeVisible(*button);
    masterButton.onClick=[this]{select(BassRackProcessor::master);};
    addButton.onClick=[this]{changeCount(1);};removeButton.onClick=[this]{changeCount(-1);};
    scanButton.onClick=[this]{processor.showScanner();};
    presets.addItem("Init - clean rack",1);presets.addItem("808 - clean sub",2);presets.addItem("Bass - bite",3);
    presets.setText("Factory settings",dontSendNotification);
    presets.setTooltip("Stock settings only. Your AU insert chains stay loaded.");
    presets.onChange=[this]{processor.factoryPreset(presets.getSelectedId()-1);select(0);};
    addAndMakeVisible(presets);
    for(int i=0;i<6;++i)type.addItem(StringArray{"Clean","Soft","Tube","Clip","Fold","Sine"}[i],i+1);
    addAndMakeVisible(type);
    for(auto* toggle:{&mute,&solo,&bypass,&compress})addAndMakeVisible(*toggle);
    for(auto& knob:knobs)addAndMakeVisible(knob);
    for(int i=0;i<3;++i) {addAndMakeVisible(inserts[i]);inserts[i].onClick=[this,i]{insertMenu(i);};}
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
    setSize(1100,720);setResizable(true,true);setResizeLimits(960,700,1500,950);
    select(0);startTimerHz(12);
}
BassRackEditor::~BassRackEditor(){stopTimer();setLookAndFeel(nullptr);}
void BassRackEditor::select(int band) {
    selected=band;const bool master=selected==BassRackProcessor::master;
    typeAttachment.reset();muteAttachment.reset();soloAttachment.reset();bypassAttachment.reset();compressAttachment.reset();
    type.setVisible(!master);mute.setVisible(!master);solo.setVisible(!master);bypass.setVisible(!master);compress.setVisible(master);
    for(auto& knob:knobs)knob.setVisible(true);
    if(master) {
        const char* ids[]{"bass","air","threshold","ratio","attack","release","output"};
        const char* names[]{"Bass / 120 Hz","Treble / 4 kHz","Threshold","Ratio","Attack","Release","Output"};
        for(int i=0;i<7;++i)knobs[i].bind(processor,ids[i],names[i]);
        compressAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"compress",compress);
        insertTitle.setText("MASTER INSERTS   /   after EQ + compression",dontSendNotification);
    } else {
        const char* ids[]{"drive","mix","level","width","pan"};
        const char* names[]{"Drive","Distortion mix","Band level","Width  /  0 = mono","Pan  /  -1 L, +1 R"};
        for(int i=0;i<5;++i)knobs[i].bind(processor,bandKey(selected,ids[i]),names[i]);
        knobs[5].setVisible(false);knobs[6].setVisible(false);
        typeAttachment=std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,bandKey(band,"type"),type);
        muteAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,bandKey(band,"mute"),mute);
        soloAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,bandKey(band,"solo"),solo);
        bypassAttachment=std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(processor.state,bandKey(band,"bypass"),bypass);
        insertTitle.setText("BAND "+String(band+1)+" INSERTS   /   distortion → AU chain → stereo → level",dontSendNotification);
    }
    for(int b=0;b<5;++b)bandButtons[b].setToggleState(selected==b,dontSendNotification);
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
    if(selected>=count && selected!=BassRackProcessor::master) {select(count-1);return;}
    for(int b=0;b<5;++b)bandButtons[b].setEnabled(b<count);
    addButton.setEnabled(count<5);removeButton.setEnabled(count>1);
    for(int i=0;i<3;++i)inserts[i].setButtonText(String(i+1)+"   "+processor.slotName(selected,i));
    statusLabel.setText(processor.status()+"   ·   "+String(processor.getLatencySamples())+" samples latency",dontSendNotification);
    map.repaint();
}
void BassRackEditor::paint(Graphics& g) {
    g.fillAll(background);
    g.setColour(panel);g.fillRoundedRectangle(24,340,static_cast<float>(getWidth()-48),static_cast<float>(getHeight()-400),14);
    g.setColour(muted);g.setFont(12);
    g.drawText("Drag crossover handles  ·  Double-click the map to add a band  ·  Up to 5 bands",28,310,getWidth()-56,20,Justification::centredLeft);
}
void BassRackEditor::resized() {
    const int width=getWidth();
    title.setBounds(26,19,240,32);subtitle.setBounds(26,52,320,22);
    presets.setBounds(width-634,28,188,32);scanButton.setBounds(width-430,28,112,32);
    saveButton.setBounds(width-306,28,130,32);loadButton.setBounds(width-164,28,138,32);
    const int bw=(width-250)/5;
    for(int b=0;b<5;++b)bandButtons[b].setBounds(26+b*bw,94,bw-8,32);
    addButton.setBounds(width-210,94,34,32);removeButton.setBounds(width-170,94,34,32);
    masterButton.setBounds(width-124,94,98,32);
    map.setBounds(26,144,width-52,158);
    const bool master=selected==BassRackProcessor::master;
    type.setBounds(44,356,170,32);
    mute.setBounds(236,356,80,32);solo.setBounds(322,356,80,32);bypass.setBounds(408,356,160,32);
    compress.setBounds(44,356,200,32);
    const int n=master?7:5;
    const int kw=(width-88)/n;
    for(int i=0;i<n;++i)knobs[i].setBounds(44+i*kw,402,kw-10,138);
    insertTitle.setBounds(44,554,width-88,24);
    const int iw=(width-100)/3;
    for(int i=0;i<3;++i)inserts[i].setBounds(44+i*(iw+6),590,iw,38);
    statusLabel.setBounds(26,getHeight()-46,width-52,26);
}
float BassRackEditor::BandMap::xFor(float hz) const {
    return 12+(getWidth()-24)*std::log(hz/20.f)/std::log(1000.f);
}
float BassRackEditor::BandMap::hzFor(float x) const {
    return 20*std::pow(1000.f,jlimit(0.f,1.f,(x-12)/static_cast<float>(getWidth()-24)));
}
void BassRackEditor::BandMap::paint(Graphics& g) {
    const int count=static_cast<int>(editor.processor.value("bands"));
    const auto xs=editor.processor.crossovers();
    auto r=getLocalBounds().toFloat();
    g.setColour(panel);g.fillRoundedRectangle(r,12);
    for(int b=0;b<count;++b) {
        const float left=b==0?12:xFor(xs[b-1]),right=b==count-1?getWidth()-12.f:xFor(xs[b]);
        g.setColour(colours[b].withAlpha(editor.selected==b?.20f:.07f));
        g.fillRect(left,12.f,right-left,getHeight()-42.f);
        g.setColour(colours[b]);g.fillRect(left,getHeight()-32.f,right-left,3.f);
        g.setFont(14);g.drawText("B"+String(b+1),static_cast<int>(left+10),20,48,22,Justification::centredLeft);
    }
    for(float hz:{20.f,50.f,100.f,250.f,1000.f,5000.f,20000.f}) {
        const float x=xFor(hz);g.setColour(muted.withAlpha(.15f));g.drawVerticalLine(static_cast<int>(x),45,getHeight()-40.f);
        g.setColour(muted);g.setFont(11);
        g.drawText(hz>=1000?String(hz/1000,0)+"k":String(hz,0),static_cast<int>(x-20),getHeight()-24,40,18,Justification::centred);
    }
    for(int i=0;i<count-1;++i) {
        const float x=xFor(xs[i]);g.setColour(colours[i]);g.drawLine(x,10,x,getHeight()-34.f,2);
        g.fillRoundedRectangle(x-6,56,12,34,4);
        g.setColour(text);g.setFont(12);
        g.drawText(String(xs[i],0)+" Hz",static_cast<int>(x-40),getHeight()-63,80,20,Justification::centred);
    }
}
void BassRackEditor::BandMap::mouseDown(const MouseEvent& e) {
    const int count=static_cast<int>(editor.processor.value("bands"));
    const auto xs=editor.processor.crossovers();
    for(int i=0;i<count-1;++i)if(std::abs(e.position.x-xFor(xs[i]))<12) {
        dragging=i;editor.processor.state.getParameter("x"+String(i))->beginChangeGesture();return;
    }
    const float hz=hzFor(e.position.x);int band=0;
    while(band<count-1 && hz>xs[band])++band;
    editor.select(band);
}
void BassRackEditor::BandMap::mouseDrag(const MouseEvent& e) {
    if(dragging<0)return;
    const int count=static_cast<int>(editor.processor.value("bands"));
    const auto xs=editor.processor.crossovers();
    const float min=dragging==0?25.f:xs[dragging-1]*1.08f;
    const float max=dragging==count-2?19000.f:xs[dragging+1]/1.08f;
    if(min>max)return;
    const float hz=jlimit(min,max,hzFor(e.position.x));
    auto* p=editor.processor.state.getParameter("x"+String(dragging));
    p->setValueNotifyingHost(p->convertTo0to1(hz));repaint();
}
void BassRackEditor::BandMap::mouseUp(const MouseEvent&) {
    if(dragging>=0)editor.processor.state.getParameter("x"+String(dragging))->endChangeGesture();
    dragging=-1;
}
void BassRackEditor::BandMap::mouseDoubleClick(const MouseEvent& e) {editor.addAt(hzFor(e.position.x));}
