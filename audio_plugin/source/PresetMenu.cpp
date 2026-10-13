namespace audio_plugin {
namespace {
constexpr float kWinPad = 4.0f;  // inner padding of the teal window
constexpr float kGapPx = 40.0f;  // blank gap between marquee loop copies
}  // namespace

PresetMenu::PresetMenu(PluginProcessor& p) : proc_(p) {
  nameBtn_.setAlpha(0.0f);  // invisible hit zone over the teal window
  nameBtn_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  nameBtn_.setTooltip(
      "Preset menu, click to interact.\n"
      "Arrows step through factory+user list.");
  nameBtn_.onClick = [this] { showMenu(); };
  addAndMakeVisible(nameBtn_);
  prev_.onClick = [this] { step(-1); };
  next_.onClick = [this] { step(+1); };
  addAndMakeVisible(prev_);
  addAndMakeVisible(next_);
  startTimerHz(30);
  sync();
}

void PresetMenu::resized() {
  nameBtn_.setBounds(1, 1, 93, 20);
  prev_.setBounds(17, 28, 30, 20);
  next_.setBounds(48, 28, 30, 20);
  updateScrolling();
}

void PresetMenu::setNameFont(const juce::Font& f) {
  nameFont_ = f;
  textW_ = static_cast<float>(
      juce::GlyphArrangement::getStringWidthInt(nameFont_, dispName_));
  tick_ = 0;
  updateScrolling();
}

void PresetMenu::paint(juce::Graphics& g) {
  // Preset name inside the printed teal window; one line, condenses
  // horizontally instead of wrapping when the name is long.
  if (!dispName_.isEmpty()) {
    g.setFont(nameFont_);
    g.setColour(ui::black);
    // Preset name in the printed teal window: centred when it fits,
    // car-radio marquee (hold, then seamless left loop) when it does not.
    if (!dispName_.isEmpty()) {
      const auto win = nameBtn_.getBounds().toFloat();
      g.saveState();
      g.reduceClipRegion(nameBtn_.getBounds());
      if (!scrolling_) {
        g.drawText(dispName_, win.toNearestInt(), juce::Justification::centred,
                   false);
      } else {
        const float off = marqueeOffset();
        const float x1 = win.getX() + kWinPad - off;
        const float x2 = x1 + textW_ + kGapPx;  // second copy, seamless loop
        auto line = [&](float x) {
          return juce::Rectangle<int>(
              juce::roundToInt(x), juce::roundToInt(win.getTopLeft().getY()),
              juce::roundToInt(textW_) + 2, juce::roundToInt(win.getHeight()));
        };
        g.drawText(dispName_, line(x1), juce::Justification::centredLeft,
                   false);
        g.drawText(dispName_, line(x2), juce::Justification::centredLeft,
                   false);
      }
      g.restoreState();
    }
  }
  // Hover/press dimming over the printed arrow pills: a translucent
  // black path darkens exactly the printed pixels underneath. Only the
  // outer corners are rounded, matching the two halves of the pill.
  auto dim = [&g](const juce::Rectangle<int>& b, bool leftHalf, float a) {
    if (a <= 0.0f)
      return;
    juce::Path p;
    const auto r = b.toFloat();
    p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                          10.0f, 10.0f, leftHalf, !leftHalf, leftHalf,
                          !leftHalf);
    g.setColour(ui::black.withAlpha(a));
    g.fillPath(p);
  };
  for (const auto& z :
       {std::make_pair(&prev_, true), std::make_pair(&next_, false)}) {
    const float a = z.first->isMouseButtonDown()
                        ? 0.25f
                        : (z.first->isMouseOver() ? 0.12f : 0.0f);
    dim(z.first->getBounds(), z.second, a);
  }
}

void PresetMenu::timerCallback() {
  sync();
  if (scrolling_) {
    ++tick_;
    repaint(nameBtn_.getBounds());  // only the display window moves
  }
}

void PresetMenu::sync() {
  const juce::String t =
      proc_.presetName() +
      (proc_.presetDirty() ? juce::String(" *") : juce::String());
  const bool changed = (t != dispName_);
  if (changed) {
    dispName_ = t;
    textW_ = static_cast<float>(
        juce::GlyphArrangement::getStringWidthInt(nameFont_, t));
    tick_ = 0;  // new name: hold at the head before scrolling
  }
  updateScrolling();
  if (changed)
    repaint(nameBtn_.getBounds());
}

float PresetMenu::marqueeOffset() const {
  constexpr float kHoldTicks = 45.0f;  // 1.5 s hold at the head (30 Hz)
  constexpr float kPxPerTick = 1.0f;   // = 30 px/s, car-radio calm
  const float cycle = textW_ + kGapPx;
  const float t = static_cast<float>(tick_);
  if (t < kHoldTicks)
    return 0.0f;
  return std::fmod(t - kHoldTicks, cycle) * kPxPerTick;
}

void PresetMenu::updateScrolling() {
  const bool was = scrolling_;
  // Before layout the window width is zero; never marquee against an
  // unmeasured window, or every fitting name starts life as a scroll.
  scrolling_ =
      nameBtn_.getWidth() > 0 &&
      textW_ > static_cast<float>(nameBtn_.getWidth()) - 2.0f * kWinPad;
  if (was != scrolling_)
    repaint(nameBtn_.getBounds());  // mode flip is itself a visual change
}

void PresetMenu::markModified() {
  if (!applying_)
    proc_.setPresetDirty(true);
}

std::vector<PresetMenu::PresetRef> PresetMenu::collect() const {
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

void PresetMenu::apply(const juce::String& xml,
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

void PresetMenu::step(int dir) {
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

void PresetMenu::writePreset(const juce::String& name) {
  const auto file =
      PluginProcessor::userPresetDirectory().getChildFile(name + ".tspreset");
  if (!file.replaceWithText(proc_.copyStateAsXml())) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, "Save preset",
        "Could not write \"" + name + ".tspreset\".");
    return;  // identity stays untouched: nothing was actually saved
  }
  proc_.setPresetIdentity(name, "USER");
  proc_.setPresetDirty(false);
  sync();
}

void PresetMenu::confirmOverwrite(const juce::String& name) {
  auto* box = new juce::AlertWindow("Save preset",
                                    "Preset \"" + name +
                                        "\" already exists.\n"
                                        "Replace it with the current state?",
                                    juce::AlertWindow::QuestionIcon, nullptr);
  box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  box->addButton("Replace", 1, juce::KeyPress(juce::KeyPress::returnKey));
  box->enterModalState(
      true,
      juce::ModalCallbackFunction::create(
          [this, name,
           safe = juce::Component::SafePointer<juce::AlertWindow>(box)](
              int result) {
            if (safe.getComponent() == nullptr || result != 1)
              return;  // cancelled: the existing file stays untouched
            writePreset(name);
          }),
      true);
}

void PresetMenu::saveAs() {
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
            juce::String name = w->getTextEditor("name")
                                    ->getText()
                                    .trim()
                                    .removeCharacters("/\\:*?\"<>|")
                                    .substring(0, maxNameLength_);
            if (name.isEmpty())
              name = "Preset";
            const auto file =
                PluginProcessor::userPresetDirectory().getChildFile(
                    name + ".tspreset");
            if (file.existsAsFile())
              confirmOverwrite(name);  // second modal, sequential: the first
            else                       // one is already dismissed by now
              writePreset(name);
          }),
      true);
}

void PresetMenu::deleteCurrent() {
  if (proc_.presetSource() != "USER")
    return;
  const auto name = proc_.presetName();
  const auto file =
      PluginProcessor::userPresetDirectory().getChildFile(name + ".tspreset");
  if (!file.existsAsFile())
    return;

  auto* box =
      new juce::AlertWindow("Delete preset",
                            "Delete user preset \"" + name +
                                "\"?\nThe .tspreset file will be removed.",
                            juce::AlertWindow::QuestionIcon, nullptr);
  box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  box->addButton("Delete", 1, juce::KeyPress(juce::KeyPress::returnKey));

  box->enterModalState(
      true,
      juce::ModalCallbackFunction::create(
          [this, name,
           safe = juce::Component::SafePointer<juce::AlertWindow>(box)](
              int result) {
            auto* w = safe.getComponent();
            if (w == nullptr || result != 1)
              return;

            const auto file =
                PluginProcessor::userPresetDirectory().getChildFile(
                    name + ".tspreset");
            if (file.deleteFile()) {
              proc_.setPresetDirty(true);
              sync();
            }
          }),
      true);
}

void PresetMenu::renameCurrent() {
  if (proc_.presetSource() != "USER")
    return;
  const auto oldName = proc_.presetName();
  const auto oldFile = PluginProcessor::userPresetDirectory().getChildFile(
      oldName + ".tspreset");
  if (!oldFile.existsAsFile())
    return;
  auto* box = new juce::AlertWindow("Rename preset", {}, {}, nullptr);
  box->addTextEditor("name", oldName, "New name:");
  if (auto* ed = box->getTextEditor("name"))
    ed->setSelectAllWhenFocused(true);
  box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  box->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
  box->enterModalState(
      true,
      juce::ModalCallbackFunction::create(
          [this, oldName, oldFile,
           safe = juce::Component::SafePointer<juce::AlertWindow>(box)](
              int result) {
            auto* w = safe.getComponent();
            if (w == nullptr || result != 1)
              return;
            juce::String newName = w->getTextEditor("name")
                                       ->getText()
                                       .trim()
                                       .removeCharacters("/\\:*?\"<>|")
                                       .substring(0, maxNameLength_);
            if (newName.isEmpty() || newName == oldName)
              return;  // empty or unchanged: nothing to do
            const auto newFile =
                PluginProcessor::userPresetDirectory().getChildFile(
                    newName + ".tspreset");
            // A case-only rename hits the same file on case-insensitive
            // filesystems: that is legal. Any other existing file means
            // the name is occupied - rename refuses, never overwrites.
            const bool sameFile = oldFile.getFullPathName().compareIgnoreCase(
                                      newFile.getFullPathName()) == 0;
            if (!sameFile && newFile.existsAsFile()) {
              juce::AlertWindow::showMessageBoxAsync(
                  juce::AlertWindow::WarningIcon, "Rename preset",
                  "Preset \"" + newName + "\" already exists.");
              return;
            }
            if (!oldFile.moveFileTo(newFile)) {
              juce::AlertWindow::showMessageBoxAsync(
                  juce::AlertWindow::WarningIcon, "Rename preset",
                  "Could not rename the preset file.");
              return;
            }
            // Pure file move: the sound is untouched, so the dirty flag
            // stays exactly as it was; only the identity follows the file.
            proc_.setPresetIdentity(newName, "USER");
            sync();
          }),
      true);
}

void PresetMenu::revealFolder() {
  PluginProcessor::userPresetDirectory().startAsProcess();
}

void PresetMenu::showMenu() {
  juce::PopupMenu m;

  // File operations group
  m.addItem("Save as...",
            [this] { juce::MessageManager::callAsync([this] { saveAs(); }); });
  const auto userFile = PluginProcessor::userPresetDirectory().getChildFile(
      proc_.presetName() + ".tspreset");
  const bool userFileLive =
      proc_.presetSource() == "USER" && userFile.existsAsFile();
  m.addItem("Rename current preset...", userFileLive, false, [this] {
    juce::MessageManager::callAsync([this] { renameCurrent(); });
  });
  m.addItem("Delete current preset...", userFileLive, false, [this] {
    juce::MessageManager::callAsync([this] { deleteCurrent(); });
  });
  m.addItem("Show presets folder", [this] { revealFolder(); });

  m.addSeparator();

  // Clipboard group
  m.addItem("Copy to clipboard", [this] {
    juce::SystemClipboard::copyTextToClipboard(proc_.copyStateAsXml());
  });
  m.addItem("Paste from clipboard", [this] {
    apply(juce::SystemClipboard::getTextFromClipboard(), "Clipboard", "clip");
  });

  m.addSeparator();

  // Preset selection group
  const auto list = collect();
  juce::PopupMenu user;
  std::vector<std::pair<juce::String, juce::PopupMenu>> cats;
  int itemId = 1000;
  for (const auto& pr : list) {
    const bool tick = pr.name == proc_.presetName() &&
                      pr.source == proc_.presetSource() && !proc_.presetDirty();
    auto act = [this, pr] { apply(pr.xml, pr.name, pr.source); };
    juce::PopupMenu::Item item;
    item.itemID = ++itemId;
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