function backlightLevel(index) {
  index = clampIndex(index, backlightLevels.length);
  return backlightLevels[index];
}

function nearestBacklightIndex(level) {
  var best = 0;
  var bestDelta = 1000;
  var i;
  var delta;
  for (i = 0; i < backlightLevels.length; i++) {
    delta = Math.abs(backlightLevels[i] - level);
    if (delta < bestDelta) {
      best = i;
      bestDelta = delta;
    }
  }
  return best;
}

function writeSettings() {
  var text = "";
  if (!JS2300.fs.writeText) return;
  text += "brightness=" + String(config.brightness) + "\n";
  text += "audio=" + String(config.audio) + "\n";
  text += "gain=" + String(config.gain) + "\n";
  text += "cpu=" + String(config.cpu) + "\n";
  text += "ge=" + String(config.ge) + "\n";
  text += "frameskip=" + String(config.frameskip) + "\n";
  text += "display=" + String(config.display) + "\n";
  text += "av=" + String(config.av) + "\n";
  text += "auto_index=" + String(config.autoIndex) + "\n";
  text += "rom_roots=" + config.romRoots + "\n";
  text += "language=" + config.language + "\n";
  text += "font=" + config.font + "\n";
  text += "font_size=" + String(config.fontSize) + "\n";
  text += "fast_sd=" + config.fastSd + "\n";
  text += "last_path=" + config.lastPath + "\n";
  text += "last_core=" + config.lastCore + "\n";
  JS2300.fs.writeText(SETTINGS_PATH, text);
  writeUserOptions();
}

function writeUserOptions() {
  var text = "";
  if (!JS2300.fs.writeText) return;
  text += "### [brightness] :[" + String(config.brightness) + "] :[1|5|10|20|30|40|50|60|70|80|90|100]\n";
  text += "brightness=" + String(config.brightness) + "\n";
  text += "### [audio] :[" + String(config.audio) + "] :[0|1]\n";
  text += "audio=" + String(config.audio) + "\n";
  text += "### [gain] :[" + String(config.gain) + "] :[0|2|4|8]\n";
  text += "gain=" + String(config.gain) + "\n";
  text += "### [cpu] :[" + String(config.cpu) + "] :[198|297|396|594|702|756|810|864|918]\n";
  text += "cpu=" + String(config.cpu) + "\n";
  text += "### [ge] :[" + String(config.ge) + "] :[0|1|2|3]\n";
  text += "ge=" + String(config.ge) + "\n";
  text += "### [frameskip] :[" + String(config.frameskip) + "] :[0|1|2|3]\n";
  text += "frameskip=" + String(config.frameskip) + "\n";
  text += "### [display] :[" + String(config.display) + "] :[0|1|2]\n";
  text += "display=" + String(config.display) + "\n";
  text += "### [av] :[" + String(config.av) + "] :[0|1|2]\n";
  text += "av=" + String(config.av) + "\n";
  text += "### [rom_roots] :[" + config.romRoots + "] :[/ROMS|/|/ROMS]\n";
  text += "rom_roots=" + config.romRoots + "\n";
  text += "### [auto_index] :[" + String(config.autoIndex) + "] :[0|1]\n";
  text += "auto_index=" + String(config.autoIndex) + "\n";
  text += "### [language] :[" + config.language + "] :[en|zh-Hans|hi|es|fr|ar|bn|pt|ru|ur|id|de|ja|sw|mr|te|tr|ta|vi|ko]\n";
  text += "language=" + config.language + "\n";
  text += "### [font] :[" + config.font + "] :[NotoSans-Regular|NotoSansArabic|NotoSansDevanagari|NotoSansBengali|NotoSansTamil|NotoSansTelugu|NotoSansCJKsc]\n";
  text += "font=" + config.font + "\n";
  text += "### [font_size] :[" + String(config.fontSize) + "] :[10|11|12|13|14|15]\n";
  text += "font_size=" + String(config.fontSize) + "\n";
  text += "### [fast_sd] :[" + config.fastSd + "] :[boot|hs1|wide50|wide|uhs12|uhs25|uhs]\n";
  text += "fast_sd=" + config.fastSd + "\n";
  JS2300.fs.writeText(USER_OPTIONS_PATH, text);
}

function loadSettings() {
  var text = JS2300.fs.readText ? JS2300.fs.readText(SETTINGS_PATH) : null;
  var defaultOptions = JS2300.fs.readText ? JS2300.fs.readText(DEFAULT_OPTIONS_PATH) : null;
  var userOptions = JS2300.fs.readText ? JS2300.fs.readText(USER_OPTIONS_PATH) : null;
  var firstBoot = !text;
  applyFrontendOptions(defaultOptions);
  applyFrontendOptions(userOptions);
  if (text) {
    config.brightness = toInt(readKey(text, "brightness"), config.brightness);
    config.audio = toInt(readKey(text, "audio"), config.audio) ? 1 : 0;
    config.gain = toInt(readKey(text, "gain"), config.gain);
    config.cpu = toInt(readKey(text, "cpu"), config.cpu);
    config.ge = toInt(readKey(text, "ge"), config.ge);
    config.frameskip = toInt(readKey(text, "frameskip"), config.frameskip);
    config.display = toInt(readKey(text, "display"), config.display);
    config.av = toInt(readKey(text, "av"), config.av);
    config.autoIndex = toInt(readKey(text, "auto_index"), config.autoIndex) ? 1 : 0;
    config.romRoots = readKey(text, "rom_roots") || config.romRoots;
    config.language = readKey(text, "language") || config.language;
    config.font = readKey(text, "font") || config.font;
    config.fontSize = toInt(readKey(text, "font_size"), config.fontSize);
    config.fastSd = readKey(text, "fast_sd") || config.fastSd;
    config.lastPath = readKey(text, "last_path");
    config.lastCore = readKey(text, "last_core");
  }
  config.brightness = backlightLevel(nearestBacklightIndex(config.brightness));
  config.gain = launchGainOptions[optionIndex(launchGainOptions, config.gain, 1)].value;
  config.cpu = launchCpuOptions[optionIndex(launchCpuOptions, config.cpu, 8)].value;
  config.ge = launchGeOptions[optionIndex(launchGeOptions, config.ge, 0)].value;
  config.frameskip =
    launchFrameskipOptions[optionIndex(launchFrameskipOptions, config.frameskip, 1)].value;
  config.display =
    launchDisplayOptions[optionIndex(launchDisplayOptions, config.display, 0)].value;
  config.av = avOutputOptions[optionIndex(avOutputOptions, config.av, 0)].value;
  config.language =
    languageOptions[optionIndex(languageOptions, config.language, 0)].value;
  config.font = fontOptions[optionIndex(fontOptions, config.font, 0)].value;
  config.fontSize =
    fontSizeOptions[optionIndex(fontSizeOptions, config.fontSize, 2)].value;
  config.fastSd = fastSdOptions[optionIndex(fastSdOptions, config.fastSd, 0)].value;
  if (JS2300.system.backlight)
    JS2300.system.backlight(config.brightness);
  if (JS2300.system.avOutput)
    JS2300.system.avOutput(config.av);
  if (firstBoot) {
    if (JS2300.system.action &&
        JS2300.system.action("storage:fast-read-active") > 0)
      settingsWritePending = true;
    else
      writeSettings();
  }
}

function applyFrontendOptions(text) {
  var value;
  if (!text) return;
  value = readKey(text, "brightness");
  if (value) config.brightness = toInt(value, config.brightness);
  value = readKey(text, "audio");
  if (value) config.audio = toInt(value, config.audio) ? 1 : 0;
  value = readKey(text, "gain");
  if (value) config.gain = toInt(value, config.gain);
  value = readKey(text, "cpu");
  if (value) config.cpu = toInt(value, config.cpu);
  value = readKey(text, "ge");
  if (value) config.ge = toInt(value, config.ge);
  value = readKey(text, "frameskip");
  if (value) config.frameskip = toInt(value, config.frameskip);
  value = readKey(text, "display");
  if (value) config.display = toInt(value, config.display);
  value = readKey(text, "av");
  if (value) config.av = toInt(value, config.av);
  value = readKey(text, "rom_roots");
  if (value) config.romRoots = value;
  value = readKey(text, "auto_index");
  if (value) config.autoIndex = toInt(value, config.autoIndex) ? 1 : 0;
  value = readKey(text, "language");
  if (value) config.language = value;
  value = readKey(text, "font");
  if (value) config.font = value;
  value = readKey(text, "font_size");
  if (value) config.fontSize = toInt(value, config.fontSize);
  value = readKey(text, "fast_sd");
  if (value) config.fastSd = value;
}

function loadThemeFile() {
  var text = JS2300.fs.readText ? JS2300.fs.readText(THEME_PATH) : null;
  var fontPath;
  if (!text && JS2300.fs.readText) text = JS2300.fs.readText(DEFAULT_THEME_PATH);
  if (!text) return;
  theme.colors.background = parseColor(readKey(text, "background"), theme.colors.background);
  theme.colors.panel = parseColor(readKey(text, "panel"), theme.colors.panel);
  theme.colors.row = parseColor(readKey(text, "row"), theme.colors.row);
  theme.colors.edge = parseColor(readKey(text, "edge"), theme.colors.edge);
  theme.colors.accent = parseColor(readKey(text, "accent"), theme.colors.accent);
  theme.colors.accent2 = parseColor(readKey(text, "accent2"), theme.colors.accent2);
  theme.colors.dark = parseColor(readKey(text, "dark"), theme.colors.dark);
  theme.text.title = parseColor(readKey(text, "text_title"), theme.text.title);
  theme.text.primary = parseColor(readKey(text, "text_primary"), theme.text.primary);
  theme.text.selected = parseColor(readKey(text, "text_selected"), theme.text.selected);
  theme.text.muted = parseColor(readKey(text, "text_muted"), theme.text.muted);
  theme.text.selectedMuted = parseColor(readKey(text, "text_selected_muted"),
    theme.text.selectedMuted);
  theme.text.footer = parseColor(readKey(text, "text_footer"), theme.text.footer);
  theme.text.warning = parseColor(readKey(text, "text_warning"), theme.text.warning);
  theme.homeLayout = readKey(text, "home_layout") || theme.homeLayout;
  theme.iconRoot = readKey(text, "icon_root") || theme.iconRoot;
  theme.icons.gba = readKey(text, "icon_gba") || theme.icons.gba;
  theme.icons.gb = readKey(text, "icon_gb") || theme.icons.gb;
  theme.icons.nes = readKey(text, "icon_nes") || theme.icons.nes;
  theme.icons.snes = readKey(text, "icon_snes") || theme.icons.snes;
  theme.icons.genesis = readKey(text, "icon_genesis") || theme.icons.genesis;
  theme.icons.pcengine = readKey(text, "icon_pcengine") || theme.icons.pcengine;
  theme.icons.psx = readKey(text, "icon_psx") || theme.icons.psx;
  theme.icons.media = readKey(text, "icon_media") || theme.icons.media;
  theme.icons.games = readKey(text, "icon_games") || theme.icons.games;
  theme.icons.firmware = readKey(text, "icon_firmware") || theme.icons.firmware;
  theme.icons.last = readKey(text, "icon_last") || theme.icons.last;
  theme.icons.settings = readKey(text, "icon_settings") || theme.icons.settings;
  theme.icons.files = readKey(text, "icon_files") || theme.icons.files;
  theme.icons.index = readKey(text, "icon_index") || theme.icons.index;
  theme.icons.developer = readKey(text, "icon_developer") || theme.icons.developer;
  fontPath = readKey(text, "font");
  if (!fontPath) fontPath = config.font;
  if (config.fontSize)
    fontPath += ";size=" + String(config.fontSize);
  if (fontPath && JS2300.video.font)
    JS2300.video.font(fontPath);
}

function applyConfiguredFont() {
  if (JS2300.video.font)
    JS2300.video.font(config.font + ";size=" + String(config.fontSize));
}

function updateBattery(now) {
  var battery;
  if (now < nextBatteryMs) return;
  battery = JS2300.system.battery();
  batteryPercent = typeof battery.percent === "number" ? String(battery.percent) + "%" : "--";
  batteryLow = !!battery.low;
  nextBatteryMs = now + 2000;
  dirty = true;
}

function noteFrontendReady(now) {
  if (!frontendReadyLogged) {
    JS2300.log("frontend ready input_ms=" + String(now) + " view=" + String(view));
    frontendReadyLogged = true;
  }
}

function showToast(text, now) {
  toast = text;
  toastUntilMs = now + 1400;
  startMotion(now);
}
