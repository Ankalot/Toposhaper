namespace audio_plugin {

PresetBar::PresetBar(PluginProcessor& p) : proc_(p) {
  label_.setJustificationType(juce::Justification::centredLeft);
  label_.setFont(juce::Font(
      juce::FontOptions().withHeight(ui::fontChip + 2).withStyle("Bold")));
  label_.setColour(juce::Label::textColourId, ui::textHi);
  addAndMakeVisible(label_);

  nameBtn_.setLookAndFeel(&pillLnF_);
  nameBtn_.setColour(juce::TextButton::buttonColourId,
                     ui::accentL.withAlpha(0.35f));
  nameBtn_.setColour(juce::TextButton::textColourOnId, ui::textHi);
  nameBtn_.setTooltip(
      "Preset menu: factory categories, user presets, clipboard, save.\n"
      "Arrows step through factory+user list. '*' = edited.");
  nameBtn_.onClick = [this] { showMenu(); };
  addAndMakeVisible(nameBtn_);

  prev_.onClick = [this] { step(-1); };
  next_.onClick = [this] { step(+1); };
  addAndMakeVisible(prev_);
  addAndMakeVisible(next_);

  startTimerHz(10);
  sync();
}

PresetBar::~PresetBar() {
  nameBtn_.setLookAndFeel(nullptr);
}

void PresetBar::resized() {
  auto r = getLocalBounds();
  label_.setBounds(r.removeFromLeft(64));  // "PRESET:"
  auto arrows = r.removeFromRight(52);     // ◀ ▶
  next_.setBounds(arrows.removeFromRight(24));
  prev_.setBounds(arrows.removeFromLeft(24));
  r.removeFromRight(8);   // gap pill <-> arrows
  nameBtn_.setBounds(r);  // pill takes the rest
}

void PresetBar::markModified() {
  if (!applying_)
    proc_.setPresetDirty(true);
}

void PresetBar::timerCallback() {
  sync();
}

void PresetBar::sync() {
  const juce::String t =
      proc_.presetName() +
      (proc_.presetDirty() ? juce::String(" *") : juce::String());
  if (nameBtn_.getButtonText() != t)
    nameBtn_.setButtonText(t);
}

std::vector<PresetBar::PresetRef> PresetBar::collect() const {
  std::vector<PresetRef> out;
  for (int i = 0; i < kNumFactoryPresets; ++i)
    out.push_back(PresetRef{juce::String(kFactoryPresets[i].name),
                            juce::String(kFactoryPresets[i].category),
                            juce::String(kFactoryPresets[i].xml)});
  juce::StringArray names;
  for (const auto& f : PluginProcessor::userPresetDirectory().findChildFiles(
           juce::File::findFiles, false, "*.tspreset"))
    names.add(f.getFileNameWithoutExtension());
  names.sort(true);
  for (const auto& n : names)
    out.push_back(PresetRef{n, "USER",
                            PluginProcessor::userPresetDirectory()
                                .getChildFile(n + ".tspreset")
                                .loadFileAsString()});
  return out;
}

void PresetBar::apply(const juce::String& xml,
                      const juce::String& name,
                      const juce::String& source) {
  applying_ = true;
  const bool ok = proc_.applyStateXml(xml);
  if (ok) {
    proc_.setPresetIdentity(name, source);
    proc_.setPresetDirty(false);
  }
  applying_ = false;
  sync();
}

void PresetBar::step(int dir) {
  const auto list = collect();
  if (list.empty())
    return;
  int idx = -1;
  for (int i = 0; i < (int)list.size(); ++i)
    if (list[i].name == proc_.presetName() &&
        list[i].source == proc_.presetSource())
      idx = i;
  idx = idx < 0 ? (dir > 0 ? 0 : (int)list.size() - 1)
                : (idx + dir + (int)list.size()) % (int)list.size();
  apply(list[idx].xml, list[idx].name, list[idx].source);
}

void PresetBar::saveAs() {
  auto* box = new juce::AlertWindow("Save preset", {}, {}, nullptr);
  box->addTextEditor("name", proc_.presetName(), "Preset name:");
  if (auto* ed = box->getTextEditor("name"))
    ed->setSelectAllWhenFocused(true);
  box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  box->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  box->enterModalState(
      true,  // take keyboard focus
      juce::ModalCallbackFunction::create(
          [this, safe = juce::Component::SafePointer<juce::AlertWindow>(box)](
              int result) {
            auto* w = safe.getComponent();
            if (w == nullptr || result != 1)
              return;
            juce::String name =
                w->getTextEditor("name")->getText().trim().removeCharacters(
                    "/\\:*?\"<>|");
            if (name.isEmpty())
              name = "Preset";
            const auto file =
                PluginProcessor::userPresetDirectory().getChildFile(
                    name + ".tspreset");
            file.replaceWithText(proc_.copyStateAsXml());
            proc_.setPresetIdentity(name, "USER");
            proc_.setPresetDirty(false);
            sync();
          }),
      true);
}

void PresetBar::showMenu() {
  juce::PopupMenu m;
  m.addItem("Copy to clipboard", [this] {
    juce::SystemClipboard::copyTextToClipboard(proc_.copyStateAsXml());
  });
  m.addItem("Paste from clipboard", [this] {
    apply(juce::SystemClipboard::getTextFromClipboard(), "Clipboard", "clip");
  });
  m.addItem("Save as...",
            [this] { juce::MessageManager::callAsync([this] { saveAs(); }); });
  m.addSeparator();

  const auto list = collect();
  juce::PopupMenu user;
  std::vector<std::pair<juce::String, juce::PopupMenu>> cats;
  int itemId = 1000;
  for (const auto& pr : list) {
    const bool tick = pr.name == proc_.presetName() &&
                      pr.source == proc_.presetSource() && !proc_.presetDirty();
    auto act = [this, pr] { apply(pr.xml, pr.name, pr.source); };
    juce::PopupMenu::Item item;
    item.itemID = ++itemId;  // <- без этой строки action не вызывается
    item.text = pr.name;
    item.isEnabled = true;
    item.isTicked = tick;
    item.action = act;
    if (pr.source == "USER") {
      user.addItem(item);
    } else {
      auto it = std::find_if(cats.begin(), cats.end(), [&](const auto& c) {
        return c.first == pr.source;
      });
      if (it == cats.end()) {
        cats.emplace_back(pr.source, juce::PopupMenu());
        it = cats.end() - 1;
      }
      it->second.addItem(item);
    }
  }
  if (user.getNumItems() > 0)
    m.addSubMenu("USER PRESETS:", user);
  else {
    juce::PopupMenu::Item item;
    item.text = "(no user presets yet - Save as...)";
    item.isEnabled = false;
    m.addItem(item);
  }
  if (!cats.empty()) {
    juce::PopupMenu fac;
    for (auto& c : cats)
      fac.addSubMenu(c.first, c.second);
    m.addSubMenu("FACTORY PRESETS:", fac);
  }
  m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nameBtn_));
}

}  // namespace audio_plugin