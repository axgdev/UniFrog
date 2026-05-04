var theme = {
  colors: {
    background: 0x0841,
    panel: 0x1084,
    row: 0x18c6,
    edge: 0x39c7,
    accent: 0xfda0,
    accent2: 0x56b5,
    dark: 0x0020
  },
  text: {
    title: 0xffff,
    primary: 0xffff,
    selected: 0x0841,
    muted: 0xad55,
    selectedMuted: 0x2945,
    footer: 0xbdf7,
    warning: 0xff60
  },
  homeLayout: "systems",
  iconRoot: "/media/mmcblk0/unifrog/themes/system-icons",
  icons: {
    gba: "icons/gba.png",
    gb: "icons/gb.png",
    nes: "icons/nes.png",
    snes: "icons/snes.png",
    genesis: "icons/genesis.png",
    pcengine: "icons/pcengine.png",
    psx: "icons/psx.png",
    media: "icons/media.png",
    firmware: "icons/firmware.png",
    last: "icons/continue.png",
    settings: "icons/settings.png",
    files: "icons/files.png",
    index: "icons/index.png",
    developer: "icons/developer.png"
  }
};

var HOME = 0;
var BROWSER = 1;
var INPUT = 2;
var ABOUT = 3;
var VIDEO_MODE = 4;
var LAUNCH = 5;
var SYSTEMS = 6;
var INDEX_LIST = 7;
var SETTINGS = 8;
var DEVELOPER = 9;
var SCRIPT_LIST = 10;
var SYSTEM_CHECK = 11;

var BTN_UP = 1 << 0;
var BTN_DOWN = 1 << 1;
var BTN_LEFT = 1 << 2;
var BTN_RIGHT = 1 << 3;
var BTN_A = 1 << 4;
var BTN_B = 1 << 5;
var BTN_X = 1 << 6;
var BTN_Y = 1 << 7;
var BTN_L = 1 << 8;
var BTN_R = 1 << 9;
var BTN_SELECT = 1 << 10;
var BTN_START = 1 << 11;

var buttons = [
  [BTN_UP, "UP"], [BTN_DOWN, "DOWN"], [BTN_LEFT, "LEFT"],
  [BTN_RIGHT, "RIGHT"], [BTN_A, "A"], [BTN_B, "B"],
  [BTN_X, "X"], [BTN_Y, "Y"], [BTN_L, "L"], [BTN_R, "R"],
  [BTN_SELECT, "SELECT"], [BTN_START, "START"]
];

var SETTINGS_PATH = "/media/mmcblk0/unifrog/settings.ini";
var THEME_PATH = "/media/mmcblk0/unifrog/theme.ini";
var DEFAULT_THEME_PATH = "/media/mmcblk0/unifrog/themes/default.ini";
var INDEX_PATH = "/media/mmcblk0/unifrog/game-index.txt";
var MEDIA_INDEX_PATH = "/media/mmcblk0/unifrog/media-index.txt";
var SCRIPT_DIR = "/media/mmcblk0/unifrog/scripts";
var SYSTEM_CHECK_REPORT_PATH = "/media/mmcblk0/unifrog/system-check.txt";

var backlightLevels = [1, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
var launchGainOptions = [
  { label: "0", value: 0 },
  { label: "2", value: 2 },
  { label: "4", value: 4 },
  { label: "8", value: 8 }
];
var launchCpuOptions = [
  { label: "198", value: 198 },
  { label: "297", value: 297 },
  { label: "396", value: 396 },
  { label: "594", value: 594 },
  { label: "702", value: 702 },
  { label: "756", value: 756 },
  { label: "810", value: 810 },
  { label: "864", value: 864 },
  { label: "918", value: 918 }
];
var launchGeOptions = [
  { label: "198", value: 0 },
  { label: "148", value: 1 },
  { label: "225", value: 2 },
  { label: "238", value: 3 }
];
var launchFrameskipOptions = [
  { label: "Off", value: 0 },
  { label: "Auto", value: 1 },
  { label: "1", value: 2 },
  { label: "2", value: 3 }
];
var launchDisplayOptions = [
  { label: "Fit", value: 0 },
  { label: "Stretch", value: 1 },
  { label: "Original", value: 2 }
];
var avOutputOptions = [
  { label: "Off", value: 0 },
  { label: "NTSC", value: 1 },
  { label: "PAL", value: 2 }
];
var mediaSuffixes = [
  ".mp4", ".mov", ".mkv", ".avi", ".ts", ".m2ts", ".mpg", ".mpeg",
  ".h264", ".264", ".mp3", ".wav", ".flac", ".ogg", ".opus", ".aac",
  ".m4a", ".jpg", ".jpeg", ".png", ".gif", ".bmp"
];
var videoModes = [
  { id: 0, label: "Audio loose", detail: "Default smooth playback" },
  { id: 0, label: "Video only", detail: "Disable audio output", noAudio: 1 },
  { id: 1, label: "STC sync", detail: "Stricter clock sync" },
  { id: 2, label: "Freerun", detail: "No A/V master clock" },
  { id: 3, label: "Audio quick", detail: "Drop delayed frames" },
  { id: 4, label: "Video master", detail: "Video controls timing" },
  { id: 5, label: "STC buffered", detail: "STC with player buffer" },
  { id: 6, label: "Audio buffered", detail: "Audio with player buffer" }
];
var launchRows = [
  "Audio", "Gain", "CPU", "GE", "Backlight", "Frameskip", "Display", "Core", "Start"
];
var settingRows = [
  { id: "brightness", label: "Backlight" },
  { id: "audio", label: "Audio" },
  { id: "gain", label: "Gain" },
  { id: "cpu", label: "CPU" },
  { id: "ge", label: "GE" },
  { id: "av", label: "AV output" },
  { id: "frameskip", label: "Frameskip" },
  { id: "auto_index", label: "Auto index boot" },
  { id: "index", label: "Index library" },
  { id: "system_check", label: "System check" },
  { id: "input", label: "Input monitor" },
  { id: "developer", label: "Developer tools" },
  { id: "about", label: "About" }
];
var homeItems = [
  { id: "firmware", label: "Firmware", detail: "Boot firmware" },
  { id: "system:Game Boy Advance", label: "Game Boy Advance", detail: "GBA", icon: "gba" },
  { id: "system:Game Boy / Color", label: "Game Boy", detail: "GB GBC", icon: "gb" },
  { id: "system:Nintendo (NES)", label: "Nintendo", detail: "NES FDS", icon: "nes" },
  { id: "system:Super Nintendo", label: "Super Nintendo", detail: "SNES", icon: "snes" },
  { id: "system:Sega Genesis", label: "Sega Genesis", detail: "MD SMS GG", icon: "genesis" },
  { id: "system:PC Engine", label: "PC Engine", detail: "PCE SGX", icon: "pcengine" },
  { id: "system:PlayStation", label: "PlayStation", detail: "PS1", icon: "psx" },
  { id: "media", label: "Media", detail: "Videos music images", icon: "media" },
  { id: "last", label: "Continue", detail: "Last game" },
  { id: "index", label: "Index Library", detail: "Scan SD card" },
  { id: "settings", label: "Settings", detail: "Display audio input" },
  { id: "developer", label: "Developer", detail: "Checks scripts tools" },
  { id: "files", label: "File Browser", detail: "Open any file" }
];
var developerItems = [
  { id: "system_check", label: "System check", detail: "Detect stale SD files" },
  { id: "smoke", label: "Smoke test", detail: "Run packaged diagnostics" },
  { id: "exception", label: "Test exception", detail: "Open crash screen" },
  { id: "cpu_exception", label: "CPU exception", detail: "Execute BREAK" },
  { id: "run_core", label: "Run core file", detail: "/unifrog/cores" },
  { id: "scripts", label: "Scripts", detail: "/unifrog/scripts" }
];
var psxFolders = ["/psx/", "/ps1/", "/playstation/"];
var coreCatalog = [
  {
    label: "gpSP",
    value: "gpsp",
    system: "Game Boy Advance",
    suffixes: [".gba"],
    folders: ["/gba/", "/game boy advance/"]
  },
  {
    label: "Gambatte",
    value: "gambatte",
    system: "Game Boy / Color",
    suffixes: [".gb", ".gbc"],
    folders: ["/gb/", "/gbc/", "/gbb/", "/game boy/"]
  },
  {
    label: "Gearboy",
    value: "gearboy",
    system: "Game Boy / Color",
    suffixes: [".gb", ".gbc"],
    folders: ["/gb/", "/gbc/", "/gbb/", "/game boy/"]
  },
  {
    label: "QuickNES",
    value: "quicknes",
    system: "Nintendo (NES)",
    suffixes: [".nes"],
    folders: ["/nes/", "/famicom/"]
  },
  {
    label: "FCEUmm",
    value: "fceumm",
    system: "Nintendo (NES)",
    suffixes: [".nes", ".fds"],
    folders: ["/nes/", "/famicom/"]
  },
  {
    label: "Snes9x 2005",
    value: "snes9x2005",
    system: "Super Nintendo",
    suffixes: [".sfc", ".smc"],
    folders: ["/snes/", "/super nintendo/"]
  },
  {
    label: "Snes9x 2002",
    value: "snes9x2002",
    system: "Super Nintendo",
    suffixes: [".sfc", ".smc"],
    folders: ["/snes/", "/super nintendo/"]
  },
  {
    label: "PicoDrive",
    value: "picodrive",
    system: "Sega Genesis",
    suffixes: [".md", ".gen", ".smd", ".32x", ".sms", ".gg", ".sg"],
    folders: ["/genesis/", "/megadrive/", "/mega drive/", "/sms/", "/gg/"]
  },
  {
    label: "PCE Fast",
    value: "pce-fast",
    system: "PC Engine",
    suffixes: [".pce", ".sgx"],
    folders: ["/pce/", "/pc engine/", "/turbografx/"]
  },
  {
    label: "QPSX",
    value: "qpsx",
    system: "PlayStation",
    suffixes: [".cue", ".iso", ".img", ".pbp"],
    folders: psxFolders
  },
  {
    label: "PMP Video",
    value: "pmp-video",
    system: "Media",
    suffixes: [".avi"],
    folders: ["/video/", "/videos/", "/media/"]
  }
];

var config = {
  brightness: 50,
  audio: 1,
  gain: 2,
  cpu: 918,
  ge: 0,
  frameskip: 1,
  display: 0,
  av: 0,
  autoIndex: 0,
  lastPath: "",
  lastCore: ""
};

var view = HOME;
var selected = 0;
var scroll = 0;
var path = "/media/mmcblk0";
var filter = "all";
var title = "UniFrog";
var entries = [];
var indexItems = [];
var mediaItems = [];
var systems = [];
var systemCounts = [];
var currentItems = [];
var currentSystem = "";
var currentListTitle = "";
var running = true;
var dirty = true;
var prevInput = 0;
var startupInputGate = true;
var startupInputGateMask = -1;
var repeatMask = 0;
var nextRepeatMs = 0;
var toast = "Ready";
var toastUntilMs = 0;
var inputMask = 0;
var batteryPercent = "--";
var batteryLow = false;
var nextBatteryMs = 0;
var pendingVideoPath = "";
var pendingGamePath = "";
var launchAudioIndex = 0;
var launchGainIndex = 1;
var launchCpuIndex = 8;
var launchGeIndex = 0;
var launchBacklightIndex = 6;
var launchFrameskipIndex = 1;
var launchDisplayIndex = 0;
var launchCoreIndex = 0;
var launchCoreOptions = [];
var launchBackView = HOME;
var scriptItems = [];
var systemCheckRows = [];
var systemCheckTitle = "System Check";
var systemCheckDetail = "";
var pendingDeveloperCore = "";
var pendingDeveloperCorePath = "";
var frontendReadyLogged = false;
var motionStartMs = 0;
var motionUntilMs = 0;
var nextMarqueeMs = 0;

function startMotion(now) {
  motionStartMs = now;
  motionUntilMs = now + 190;
  nextMarqueeMs = now;
  dirty = true;
}

function motionStep(now) {
  var span = 190;
  var elapsed = now - motionStartMs;
  if (elapsed <= 0) return 0;
  if (elapsed >= span) return 8;
  return Math.floor((elapsed * 8) / span);
}

function shortText(text, limit) {
  var out = "";
  var i;
  if (!text) return "";
  if (text.length <= limit) return text;
  for (i = 0; i < limit - 1 && i < text.length; i++)
    out += String.fromCharCode(text.charCodeAt(i));
  return out + "~";
}

function textWindow(text, start, count) {
  var out = "";
  var i;
  if (!text) return "";
  for (i = 0; i < count && start + i < text.length; i++)
    out += String.fromCharCode(text.charCodeAt(start + i));
  return out;
}

function marqueeText(text, limit, active, now) {
  var padded;
  var span;
  var phase;
  var offset;
  if (!text) return "";
  if (text.length <= limit) return text;
  if (!active) return shortText(text, limit);
  padded = text + "   ";
  span = padded.length;
  phase = Math.floor((now - motionStartMs) / 170) % (span + 6);
  if (phase < 3) offset = 0;
  else if (phase >= span + 3) offset = 0;
  else offset = phase - 3;
  if (offset + limit <= padded.length)
    return textWindow(padded, offset, limit);
  return textWindow(padded, offset, padded.length - offset) +
    textWindow(padded, 0, limit - (padded.length - offset));
}

function lowerAscii(text) {
  var out = "";
  var i;
  var c;
  for (i = 0; i < text.length; i++) {
    c = text.charCodeAt(i);
    if (c >= 65 && c <= 90) c += 32;
    out += String.fromCharCode(c);
  }
  return out;
}

function startsWithText(text, prefix) {
  var i;
  if (text.length < prefix.length) return false;
  for (i = 0; i < prefix.length; i++) {
    if (text.charCodeAt(i) !== prefix.charCodeAt(i)) return false;
  }
  return true;
}

function endsWithCI(text, suffix) {
  var offset;
  var i;
  var a;
  var b;
  if (text.length < suffix.length) return false;
  offset = text.length - suffix.length;
  for (i = 0; i < suffix.length; i++) {
    a = text.charCodeAt(offset + i);
    b = suffix.charCodeAt(i);
    if (a >= 65 && a <= 90) a += 32;
    if (b >= 65 && b <= 90) b += 32;
    if (a !== b) return false;
  }
  return true;
}

function hasAnySuffix(text, suffixes) {
  var i;
  for (i = 0; i < suffixes.length; i++) {
    if (endsWithCI(text, suffixes[i])) return true;
  }
  return false;
}

function isCompressedWrapper(name) {
  return endsWithCI(name, ".zip") || endsWithCI(name, ".lz4") ||
    endsWithCI(name, ".zst") || endsWithCI(name, ".zstd");
}

function isLegacyStubName(name) {
  var semi = name.indexOf(";");
  return semi > 0;
}

function stripOneExtension(name) {
  var dot = name.lastIndexOf(".");
  var out = "";
  var i;
  if (dot <= 0) return name;
  for (i = 0; i < dot; i++) out += String.fromCharCode(name.charCodeAt(i));
  return out;
}

function catalogName(name) {
  if (endsWithCI(name, ".lz4") || endsWithCI(name, ".zst") ||
      endsWithCI(name, ".zstd"))
    return stripOneExtension(name);
  if (endsWithCI(name, ".zip"))
    return stripOneExtension(name);
  return name;
}

function isCoreFile(name) {
  return endsWithCI(name, ".bin");
}

function isPsxTrackBin(name, dir) {
  if (!endsWithCI(catalogName(name), ".bin")) return false;
  return hasAnyFolderHint(dir, psxFolders);
}

function containsCI(text, part) {
  return lowerAscii(text).indexOf(lowerAscii(part)) >= 0;
}

function hasAnyFolderHint(pathText, folders) {
  var p = pathText;
  var i;
  if (!endsWithCI(p, "/")) p += "/";
  for (i = 0; i < folders.length; i++) {
    if (containsCI(p, folders[i])) return true;
  }
  return false;
}

function readKey(text, key) {
  var i = 0;
  var line = "";
  var prefix = key + "=";
  var c;
  if (!text) return "";
  while (i <= text.length) {
    if (i === text.length) c = 10;
    else c = text.charCodeAt(i);
    if (c === 10 || c === 13) {
      if (startsWithText(line, prefix))
        return lineValue(line, prefix.length);
      line = "";
    } else {
      line += String.fromCharCode(c);
    }
    i++;
  }
  return "";
}

function lineValue(line, offset) {
  var out = "";
  var i;
  for (i = offset; i < line.length; i++)
    out += String.fromCharCode(line.charCodeAt(i));
  return out;
}

function toInt(text, fallback) {
  var i;
  var value = 0;
  var sign = 1;
  var seen = 0;
  if (!text) return fallback;
  if (text.charCodeAt(0) === 45) {
    sign = -1;
    i = 1;
  } else {
    i = 0;
  }
  for (; i < text.length; i++) {
    var c = text.charCodeAt(i);
    if (c < 48 || c > 57) break;
    value = value * 10 + c - 48;
    seen = 1;
  }
  return seen ? value * sign : fallback;
}

function hexValue(c) {
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 65 && c <= 70) return c - 55;
  if (c >= 97 && c <= 102) return c - 87;
  return -1;
}

function parseColor(text, fallback) {
  var i = 0;
  var value = 0;
  var digits = 0;
  var h;
  if (!text) return fallback;
  if (text.charCodeAt(0) === 35) i = 1;
  else if (text.length > 2 && text.charCodeAt(0) === 48 &&
      (text.charCodeAt(1) === 120 || text.charCodeAt(1) === 88)) i = 2;
  for (; i < text.length; i++) {
    h = hexValue(text.charCodeAt(i));
    if (h < 0) break;
    value = value * 16 + h;
    digits++;
  }
  return digits ? value : fallback;
}

function clampIndex(value, length) {
  if (value < 0) return 0;
  if (value >= length) return length - 1;
  return value;
}

function optionIndex(options, value, fallback) {
  var i;
  for (i = 0; i < options.length; i++) {
    if (options[i].value === value) return i;
  }
  return fallback;
}

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
  text += "last_path=" + config.lastPath + "\n";
  text += "last_core=" + config.lastCore + "\n";
  JS2300.fs.writeText(SETTINGS_PATH, text);
}

function loadSettings() {
  var text = JS2300.fs.readText ? JS2300.fs.readText(SETTINGS_PATH) : null;
  var firstBoot = !text;
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
  if (JS2300.system.backlight)
    JS2300.system.backlight(config.brightness);
  if (JS2300.system.avOutput)
    JS2300.system.avOutput(config.av);
  if (firstBoot)
    writeSettings();
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
  theme.icons.firmware = readKey(text, "icon_firmware") || theme.icons.firmware;
  theme.icons.last = readKey(text, "icon_last") || theme.icons.last;
  theme.icons.settings = readKey(text, "icon_settings") || theme.icons.settings;
  theme.icons.files = readKey(text, "icon_files") || theme.icons.files;
  theme.icons.index = readKey(text, "icon_index") || theme.icons.index;
  theme.icons.developer = readKey(text, "icon_developer") || theme.icons.developer;
  fontPath = readKey(text, "font");
  if (fontPath && JS2300.video.font)
    JS2300.video.font(fontPath);
}

function pushCoreOption(out, core) {
  var i;
  for (i = 0; i < out.length; i++) {
    if (out[i].value === core.value) return;
  }
  out.push({ label: core.label, value: core.value });
}

function pushAllCoreOptions(out) {
  var i;
  for (i = 0; i < coreCatalog.length; i++) pushCoreOption(out, coreCatalog[i]);
}

function isGame(name) {
  var i;
  var detect = catalogName(name);
  if (isLegacyStubName(name)) return false;
  if (isCompressedWrapper(name)) return true;
  for (i = 0; i < coreCatalog.length; i++) {
    if (hasAnySuffix(detect, coreCatalog[i].suffixes)) return true;
  }
  return false;
}

function isVideo(name) {
  return hasAnySuffix(name, mediaSuffixes);
}

function isAsd(name) {
  return endsWithCI(name, ".asd");
}

function isScript(name) {
  return endsWithCI(name, ".js");
}

function shouldHideFile(name, dir) {
  if (isLegacyStubName(name)) return true;
  if (isPsxTrackBin(name, dir)) return true;
  return false;
}

function catalogForPath(pathText) {
  var i;
  var detect = catalogName(pathText);
  for (i = 0; i < coreCatalog.length; i++) {
    if (hasAnySuffix(detect, coreCatalog[i].suffixes))
      return coreCatalog[i];
  }
  for (i = 0; i < coreCatalog.length; i++) {
    if (hasAnyFolderHint(pathText, coreCatalog[i].folders))
      return coreCatalog[i];
  }
  return null;
}

function gameCoreOptions(pathText) {
  var out = [];
  var i;
  var detect = catalogName(pathText);
  for (i = 0; i < coreCatalog.length; i++) {
    if (hasAnySuffix(detect, coreCatalog[i].suffixes))
      pushCoreOption(out, coreCatalog[i]);
  }
  if (out.length === 0) {
    for (i = 0; i < coreCatalog.length; i++) {
      if (hasAnyFolderHint(pathText, coreCatalog[i].folders))
        pushCoreOption(out, coreCatalog[i]);
    }
  }
  pushAllCoreOptions(out);
  return out;
}

function joinPath(base, name) {
  if (base === "/") return "/" + name;
  return base + "/" + name;
}

function parentPath(p) {
  var i;
  var out = "";
  var j;
  if (p === "/" || p === "/media/mmcblk0") return "/media/mmcblk0";
  i = p.lastIndexOf("/");
  if (i <= 0) return "/";
  for (j = 0; j < i; j++) out += String.fromCharCode(p.charCodeAt(j));
  return out;
}

function basename(p) {
  var i = p.lastIndexOf("/");
  var out = "";
  var j;
  if (i < 0) i = -1;
  for (j = i + 1; j < p.length; j++) out += String.fromCharCode(p.charCodeAt(j));
  return out;
}

function stripExtension(name) {
  var dot = name.lastIndexOf(".");
  var out = "";
  var i;
  if (dot <= 0) return name;
  for (i = 0; i < dot; i++) out += String.fromCharCode(name.charCodeAt(i));
  return out;
}

function accepted(entry) {
  if (entry.dir) return true;
  if (filter !== "core" && shouldHideFile(entry.name, path)) return false;
  if (filter === "games") return isGame(entry.name);
  if (filter === "media") return isVideo(entry.name);
  if (filter === "firmware") return isAsd(entry.name);
  if (filter === "core") return entry.dir || isCoreFile(entry.name);
  return true;
}

function sortEntries(list) {
  var out = [];
  var i;
  var j;
  var tmp;
  for (i = 0; i < list.length; i++) {
    if (accepted(list[i])) out.push(list[i]);
  }
  for (i = 0; i < out.length; i++) {
    for (j = i + 1; j < out.length; j++) {
      if ((!out[i].dir && out[j].dir) ||
          (out[i].dir === out[j].dir &&
           lowerAscii(out[i].name) > lowerAscii(out[j].name))) {
        tmp = out[i];
        out[i] = out[j];
        out[j] = tmp;
      }
    }
  }
  return out;
}

function refreshBrowser() {
  var list = JS2300.fs.list(path);
  entries = sortEntries(list);
  selected = 0;
  scroll = 0;
  showToast(String(entries.length) + " items", JS2300.now());
  JS2300.log("frontend list " + path + " count=" + String(entries.length));
}

function openBrowser(newTitle, newPath, newFilter) {
  title = newTitle;
  path = newPath;
  filter = newFilter;
  view = BROWSER;
  refreshBrowser();
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

function showToast(text, now) {
  toast = text;
  toastUntilMs = now + 1400;
  startMotion(now);
}

function field(text, index) {
  var out = "";
  var current = 0;
  var i;
  var c;
  for (i = 0; i < text.length; i++) {
    c = text.charCodeAt(i);
    if (c === 124) {
      if (current === index) return out;
      current++;
      out = "";
    } else if (current === index) {
      out += String.fromCharCode(c);
    }
  }
  return current === index ? out : "";
}

function clearSystemCheckShow(text) {
  var out = "";
  var line = "";
  var i;
  var c;
  if (!text) return "";
  for (i = 0; i <= text.length; i++) {
    if (i === text.length) c = 10;
    else c = text.charCodeAt(i);
    if (c === 10 || c === 13) {
      if (startsWithText(line, "show=")) out += "show=0\n";
      else if (line.length > 0) out += line + "\n";
      line = "";
    } else {
      line += String.fromCharCode(c);
    }
  }
  return out;
}

function addSystemCheckLine(line) {
  if (startsWithText(line, "title=")) {
    systemCheckTitle = lineValue(line, 6);
  } else if (startsWithText(line, "detail=")) {
    systemCheckDetail = lineValue(line, 7);
  } else if (field(line, 0) === "item") {
    systemCheckRows.push({
      kind: field(line, 1),
      label: field(line, 2),
      expected: field(line, 3),
      actual: field(line, 4)
    });
  }
}

function parseSystemCheckReport(text) {
  var show = 0;
  var line = "";
  var i;
  var c;
  systemCheckRows = [];
  systemCheckTitle = "System Check";
  systemCheckDetail = "";
  if (!text) return 0;
  for (i = 0; i <= text.length; i++) {
    if (i === text.length) c = 10;
    else c = text.charCodeAt(i);
    if (c === 10 || c === 13) {
      if (line.length > 0) {
        if (startsWithText(line, "show="))
          show = toInt(lineValue(line, 5), 0);
        addSystemCheckLine(line);
      }
      line = "";
    } else {
      line += String.fromCharCode(c);
    }
  }
  return show;
}

function loadSystemCheckReport(now) {
  var text = JS2300.fs.readText ? JS2300.fs.readText(SYSTEM_CHECK_REPORT_PATH) : null;
  var show;
  if (!text) return;
  show = parseSystemCheckReport(text);
  if (show) {
    selected = 0;
    scroll = 0;
    view = SYSTEM_CHECK;
    showToast("System check", now);
    if (JS2300.fs.writeText)
      JS2300.fs.writeText(SYSTEM_CHECK_REPORT_PATH, clearSystemCheckShow(text));
  }
}

function pushSystem(system) {
  var i;
  for (i = 0; i < systems.length; i++) {
    if (systems[i] === system) {
      systemCounts[i]++;
      return;
    }
  }
  systems.push(system);
  systemCounts.push(1);
}

function clearIndexData() {
  indexItems = [];
  mediaItems = [];
  systems = [];
  systemCounts = [];
}

function addIndexLine(line) {
  var kind = field(line, 0);
  var item;
  if (kind === "game") {
    item = {
      system: field(line, 1),
      core: field(line, 2),
      path: field(line, 3),
      label: field(line, 4)
    };
    if (item.path) {
      if (shouldHideFile(basename(item.path), parentPath(item.path))) return;
      if (!item.label) item.label = basename(item.path);
      if (!item.system) item.system = "Other";
      indexItems.push(item);
      pushSystem(item.system);
    }
  } else if (kind === "media") {
    item = {
      system: "Media",
      core: "video",
      path: field(line, 3),
      label: field(line, 4)
    };
    if (item.path) {
      if (shouldHideFile(basename(item.path), parentPath(item.path))) return;
      if (!item.label) item.label = basename(item.path);
      mediaItems.push(item);
    }
  }
}

function parseIndex(text) {
  var line = "";
  var i;
  var c;
  clearIndexData();
  if (!text) return;
  for (i = 0; i <= text.length; i++) {
    if (i === text.length) c = 10;
    else c = text.charCodeAt(i);
    if (c === 10 || c === 13) {
      if (line.length > 0) addIndexLine(line);
      line = "";
    } else {
      line += String.fromCharCode(c);
    }
  }
}

function loadIndex() {
  var gameText = JS2300.fs.readText ? JS2300.fs.readText(INDEX_PATH) : null;
  var mediaText = JS2300.fs.readText ? JS2300.fs.readText(MEDIA_INDEX_PATH) : null;
  parseIndex((gameText ? gameText : "") + (mediaText ? mediaText : ""));
}

function skipDir(name, full) {
  if (!name || name.charCodeAt(0) === 46) return true;
  if (full === "/media/mmcblk0/unifrog") return true;
  if (full === "/media/mmcblk0/bios") return true;
  if (full === "/media/mmcblk0/firmware") return true;
  if (full === "/media/mmcblk0/System Volume Information") return true;
  return false;
}

function drawScan(pathText, games, media) {
  JS2300.video.clear(theme.colors.background);
  topBar("Index games");
  JS2300.video.text(18, 58, "Scanning SD card", theme.text.primary);
  JS2300.video.text(18, 84, String(games) + " games", theme.text.muted);
  JS2300.video.text(18, 102, String(media) + " media", theme.text.muted);
  JS2300.video.text(18, 128, pathText, theme.text.muted);
  footer("Please wait");
  JS2300.video.present();
}

function scanDirectory(dir, depth, scan) {
  var list;
  var i;
  var full;
  var cat;
  var label;
  if (depth > 10 || scan.files > 4096 || scan.dirs > 512) return;
  scan.dirs++;
  drawScan(dir, scan.games, scan.media);
  list = JS2300.fs.list(dir);
  for (i = 0; i < list.length; i++) {
    full = joinPath(dir, list[i].name);
    if (list[i].dir) {
      if (!skipDir(list[i].name, full))
        scanDirectory(full, depth + 1, scan);
    } else {
      scan.files++;
      if (shouldHideFile(list[i].name, dir))
        continue;
      label = list[i].name;
      if (isGame(list[i].name)) {
        cat = catalogForPath(full);
        if (cat) {
          scan.gameText += "game|" + cat.system + "|" + cat.value + "|" +
            full + "|" + label + "\n";
          scan.games++;
        }
      } else if (isVideo(list[i].name)) {
        scan.mediaText += "media|Media|video|" + full + "|" + label + "\n";
        scan.media++;
      }
      if ((scan.files % 24) === 0)
        drawScan(dir, scan.games, scan.media);
    }
  }
}

function indexGames(now) {
  var scan = { gameText: "", mediaText: "", games: 0, media: 0, files: 0, dirs: 0 };
  scanDirectory("/media/mmcblk0", 0, scan);
  if (JS2300.fs.writeText) {
    JS2300.fs.writeText(INDEX_PATH, scan.gameText);
    JS2300.fs.writeText(MEDIA_INDEX_PATH, scan.mediaText);
  }
  parseIndex(scan.gameText + scan.mediaText);
  showToast(String(scan.games) + " games indexed", now);
  view = HOME;
}

function homeDetail(item) {
  if (startsWithText(item.id, "system:"))
    return String(countForSystem(lineValue(item.id, 7))) + " games";
  if (item.id === "last")
    return config.lastPath ? basename(config.lastPath) : "None";
  if (item.id === "games")
    return String(indexItems.length) + " games";
  if (item.id === "media")
    return String(mediaItems.length) + " files";
  if (item.id === "settings")
    return String(config.brightness) + "%";
  return item.detail;
}

function countForSystem(name) {
  var i;
  for (i = 0; i < systems.length; i++) {
    if (systems[i] === name) return systemCounts[i];
  }
  return 0;
}

function openSystemByName(name, now) {
  var i;

  for (i = 0; i < systems.length; i++) {
    if (systems[i] === name) {
      selected = i;
      scroll = 0;
      openSystemList(now);
      return;
    }
  }
  showToast("Index library first", now);
}

function iconFile(name) {
  if (!name) return "";
  if (name.charCodeAt(0) === 47) return name;
  return joinPath(theme.iconRoot, name);
}

function iconForItem(item) {
  var key = item.icon ? item.icon : item.id;

  if (key === "firmware") return iconFile(theme.icons.firmware);
  if (key === "last") return iconFile(theme.icons.last);
  if (key === "settings") return iconFile(theme.icons.settings);
  if (key === "files") return iconFile(theme.icons.files);
  if (key === "index") return iconFile(theme.icons.index);
  if (key === "developer") return iconFile(theme.icons.developer);
  if (key === "media") return iconFile(theme.icons.media);
  if (key === "gba") return iconFile(theme.icons.gba);
  if (key === "gb") return iconFile(theme.icons.gb);
  if (key === "nes") return iconFile(theme.icons.nes);
  if (key === "snes") return iconFile(theme.icons.snes);
  if (key === "genesis") return iconFile(theme.icons.genesis);
  if (key === "pcengine") return iconFile(theme.icons.pcengine);
  if (key === "psx") return iconFile(theme.icons.psx);
  return "";
}

function drawIconFallback(x, y, active) {
  var c = active ? theme.colors.dark : theme.colors.edge;

  JS2300.video.rects([
    [x + 4, y + 4, 32, 24, c],
    [x + 10, y + 10, 20, 4, active ? theme.text.selectedMuted : theme.text.muted],
    [x + 10, y + 18, 14, 4, active ? theme.text.selectedMuted : theme.text.muted]
  ]);
}

function drawItemIcon(item, x, y, active) {
  var path = iconForItem(item);
  var ret = -1;

  if (path && JS2300.video.image)
    ret = JS2300.video.image(path, x, y, 36, 36);
  if (ret !== 0)
    drawIconFallback(x, y, active);
}

function topBar(text) {
  var batteryColor = batteryLow ? theme.text.warning : theme.text.muted;
  JS2300.video.rects([
    [0, 0, 320, 25, theme.colors.panel],
    [0, 0, 320, 2, theme.colors.accent],
    [0, 25, 320, 1, theme.colors.edge],
    [262, 5, 46, 14, theme.colors.dark]
  ]);
  JS2300.video.text(10, 7, text, theme.text.title);
  JS2300.video.text(270, 8, batteryPercent, batteryColor);
}

function footer(text) {
  JS2300.video.rects([
    [0, 218, 320, 22, theme.colors.dark],
    [0, 218, 320, 1, theme.colors.edge]
  ]);
  JS2300.video.text(10, 226, text, theme.text.footer);
}

function drawToast(now) {
  if (toast && now < toastUntilMs) {
    JS2300.video.rects([[8, 198, 304, 16, theme.colors.dark]]);
    JS2300.video.text(14, 202, toast, theme.text.footer);
  }
}

function drawRows(count, rowTop, rowStep, rowHeight) {
  var rows = [];
  var i;
  var idx;
  var y;
  var visible = visibleRowsForView();
  for (i = 0; i < count && i < visible; i++) {
    idx = scroll + i;
    y = rowTop + i * rowStep;
    rows.push([8, y, 304, rowHeight, idx === selected ? theme.colors.accent : theme.colors.row]);
    if (idx === selected)
      rows.push([8, y, 4, rowHeight, theme.colors.accent2]);
  }
  JS2300.video.rects(rows);
}

function drawHome(now) {
  var i;
  var idx;
  var col;
  var rowIndex;
  var x;
  var y;
  var w = 146;
  var h = 40;
  var focus;
  var detail;
  var label;
  var step = motionStep(now);
  var active;
  JS2300.video.clear(theme.colors.background);
  topBar("UniFrog");
  JS2300.video.rects([
    [10, 31, 300, 11, theme.colors.edge],
    [10, 31, 78 + step * 4, 11, theme.colors.accent2]
  ]);
  JS2300.video.text(16, 33, "D-pad moves cards  A open  Y log", theme.text.selected);
  for (i = 0; i < 8; i++) {
    idx = scroll + i;
    if (idx >= homeItems.length) break;
    col = i % 2;
    rowIndex = Math.floor(i / 2);
    x = 10 + col * 154;
    y = 47 + rowIndex * 42;
    active = idx === selected;
    focus = active ? theme.colors.accent : theme.colors.row;
    label = shortText(homeItems[idx].label, 14);
    detail = shortText(homeDetail(homeItems[idx]), 12);
    JS2300.video.rects([
      [x, y, w, h, focus],
      [x, y, 4 + (active ? step : 0), h, theme.colors.accent2],
      [x + 4, y + h - 2, w - 8, 1, active ? theme.colors.dark : theme.colors.edge]
    ]);
    drawItemIcon(homeItems[idx], x + 8, y + 2, active);
    JS2300.video.text(x + 50, y + 9, label,
      active ? theme.text.selected : theme.text.primary);
    JS2300.video.text(x + 50, y + 23, detail,
      active ? theme.text.selectedMuted : theme.text.muted);
  }
  drawToast(now);
  footer("A open   Y log   " + String(selected + 1) + "/" + String(homeItems.length));
  JS2300.video.present();
}

function drawBrowser(now) {
  var i;
  var idx;
  var y;
  var entry;
  var label;
  JS2300.video.clear(theme.colors.background);
  topBar(title);
  JS2300.video.text(10, 30, path, theme.text.muted);
  drawRows(8, 50, 20, 18);
  for (i = 0; i < 8; i++) {
    idx = scroll + i;
    y = 55 + i * 20;
    if (idx < entries.length) {
      entry = entries[idx];
      label = entry.dir ? "[" + entry.name + "]" : entry.name;
      label = marqueeText(label, 46, idx === selected, now);
      JS2300.video.text(16, y, label, idx === selected ? theme.text.selected : theme.text.primary);
    }
  }
  if (entries.length === 0) JS2300.video.text(16, 96, "No matching files", theme.text.muted);
  drawToast(now);
  footer("A open   B back   Y log");
  JS2300.video.present();
}

function drawSystems(now) {
  var i;
  var idx;
  var y;
  var active;
  JS2300.video.clear(theme.colors.background);
  topBar("Games");
  drawRows(systems.length, 38, 22, 19);
  for (i = 0; i < 8; i++) {
    idx = scroll + i;
    y = 43 + i * 22;
    if (idx < systems.length) {
      active = idx === selected;
      JS2300.video.text(16, y, systems[idx], active ? theme.text.selected : theme.text.primary);
      JS2300.video.text(248, y, String(systemCounts[idx]),
        active ? theme.text.selectedMuted : theme.text.muted);
    }
  }
  if (systems.length === 0) JS2300.video.text(16, 96, "Run Index games", theme.text.muted);
  drawToast(now);
  footer("A open   B back   Y log");
  JS2300.video.present();
}

function drawIndexList(now) {
  var i;
  var idx;
  var y;
  var active;
  JS2300.video.clear(theme.colors.background);
  topBar(currentListTitle);
  drawRows(currentItems.length, 36, 22, 19);
  for (i = 0; i < 8; i++) {
    idx = scroll + i;
    y = 41 + i * 22;
    if (idx < currentItems.length) {
      active = idx === selected;
      JS2300.video.text(16, y, marqueeText(currentItems[idx].label, 46, active, now),
        active ? theme.text.selected : theme.text.primary);
    }
  }
  if (currentItems.length === 0) JS2300.video.text(16, 96, "No indexed files", theme.text.muted);
  drawToast(now);
  footer("A open   B back   Y log");
  JS2300.video.present();
}

function drawDeveloper(now) {
  var i;
  var y;
  var active;
  JS2300.video.clear(theme.colors.background);
  topBar("Developer");
  drawRows(developerItems.length, 44, 28, 22);
  for (i = 0; i < developerItems.length; i++) {
    y = 50 + i * 28;
    active = i === selected;
    JS2300.video.text(16, y, developerItems[i].label,
      active ? theme.text.selected : theme.text.primary);
    JS2300.video.text(154, y, developerItems[i].detail,
      active ? theme.text.selectedMuted : theme.text.muted);
  }
  drawToast(now);
  footer("A open   B back   Y log");
  JS2300.video.present();
}

function drawScriptList(now) {
  var i;
  var idx;
  var y;
  JS2300.video.clear(theme.colors.background);
  topBar("Scripts");
  drawRows(scriptItems.length, 38, 22, 19);
  for (i = 0; i < 8; i++) {
    idx = scroll + i;
    y = 43 + i * 22;
    if (idx < scriptItems.length) {
      JS2300.video.text(16, y, scriptItems[idx].label,
        idx === selected ? theme.text.selected : theme.text.primary);
    }
  }
  if (scriptItems.length === 0)
    JS2300.video.text(16, 96, "No scripts in /unifrog/scripts", theme.text.muted);
  drawToast(now);
  footer("A run   B back   Y log");
  JS2300.video.present();
}

function drawSystemCheck(now) {
  var rows = [];
  var i;
  var idx;
  var y;
  var active;
  var row;
  var detail;
  JS2300.video.clear(theme.colors.background);
  topBar(systemCheckTitle);
  JS2300.video.text(12, 32, systemCheckDetail, theme.text.muted);
  for (i = 0; i < 5; i++) {
    idx = scroll + i;
    if (idx >= systemCheckRows.length) break;
    y = 52 + i * 32;
    active = idx === selected;
    rows.push([8, y, 304, 28, active ? theme.colors.accent : theme.colors.row]);
    if (active)
      rows.push([8, y, 4, 28, theme.colors.accent2]);
  }
  JS2300.video.rects(rows);
  for (i = 0; i < 5; i++) {
    idx = scroll + i;
    if (idx >= systemCheckRows.length) break;
    y = 58 + i * 32;
    active = idx === selected;
    row = systemCheckRows[idx];
    detail = row.expected + "  " + row.actual;
    JS2300.video.text(16, y, row.kind + "  " + shortText(row.label, 29),
      active ? theme.text.selected : theme.text.primary);
    JS2300.video.text(16, y + 13, marqueeText(detail, 46, active, now),
      active ? theme.text.selectedMuted : theme.text.muted);
  }
  if (systemCheckRows.length === 0)
    JS2300.video.text(16, 96, "No stale or missing files", theme.text.muted);
  drawToast(now);
  footer("B back   Y log   " +
    String(systemCheckRows.length ? selected + 1 : 0) + "/" +
    String(systemCheckRows.length));
  JS2300.video.present();
}

function drawSlider(x, y, w, value, min, max, active) {
  var fill;
  var knob;
  var color = active ? theme.colors.accent2 : theme.colors.edge;
  if (max <= min) max = min + 1;
  if (value < min) value = min;
  if (value > max) value = max;
  fill = Math.floor(((value - min) * w) / (max - min));
  if (fill < 1) fill = 1;
  if (fill > w) fill = w;
  knob = x + fill - 2;
  if (knob < x) knob = x;
  if (knob > x + w - 4) knob = x + w - 4;
  JS2300.video.rects([
    [x, y + 3, w, 4, theme.colors.edge],
    [x, y + 3, fill, 4, color],
    [knob, y, 4, 10, active ? theme.colors.accent : theme.colors.accent2]
  ]);
}

function settingValue(id) {
  if (id === "brightness") return String(config.brightness);
  if (id === "audio") return config.audio ? "On" : "Off";
  if (id === "gain") return String(config.gain);
  if (id === "cpu")
    return launchCpuOptions[optionIndex(launchCpuOptions, config.cpu, 8)].label;
  if (id === "ge") return launchGeOptions[optionIndex(launchGeOptions, config.ge, 0)].label;
  if (id === "av") return avOutputOptions[optionIndex(avOutputOptions, config.av, 0)].label;
  if (id === "frameskip")
    return launchFrameskipOptions[optionIndex(launchFrameskipOptions, config.frameskip, 1)].label;
  if (id === "auto_index") return config.autoIndex ? "On" : "Off";
  if (id === "index") return "Scan";
  if (id === "system_check") return "Verify";
  if (id === "developer") return "Tools";
  return "";
}

function drawSettings(now) {
  var i;
  var idx;
  var y;
  var active;
  var row;
  JS2300.video.clear(theme.colors.background);
  topBar("Settings");
  drawRows(settingRows.length, 31, 21, 18);
  for (i = 0; i < 9; i++) {
    idx = scroll + i;
    if (idx >= settingRows.length) break;
    y = 36 + i * 21;
    active = idx === selected;
    row = settingRows[idx];
    JS2300.video.text(16, y, row.label, active ? theme.text.selected : theme.text.primary);
    if (row.id === "brightness") {
      drawSlider(154, y - 1, 82, config.brightness, 1, 100, active);
      JS2300.video.text(250, y, settingValue(row.id),
        active ? theme.text.selectedMuted : theme.text.muted);
    } else {
      JS2300.video.text(176, y, settingValue(row.id),
        active ? theme.text.selectedMuted : theme.text.muted);
    }
  }
  drawToast(now);
  footer("< > adjust   A open   B back");
  JS2300.video.present();
}

function pressedNames(mask) {
  var names = "";
  var i;
  for (i = 0; i < buttons.length; i++) {
    if (mask & buttons[i][0]) {
      if (names.length > 0) names += " ";
      names += buttons[i][1];
    }
  }
  return names.length ? names : "none";
}

function hex8(value) {
  var out = "";
  var digits = "0123456789ABCDEF";
  var i;
  for (i = 7; i >= 0; i--) out += digits[(value >> (i * 4)) & 15];
  return out;
}

function drawInput(now) {
  var rows = [];
  var i;
  var x;
  var y;
  var down;
  JS2300.video.clear(theme.colors.background);
  topBar("Input");
  JS2300.video.text(12, 34, "Mask 0x" + hex8(inputMask), theme.text.primary);
  JS2300.video.text(12, 52, pressedNames(inputMask), theme.text.muted);
  for (i = 0; i < buttons.length; i++) {
    x = 12 + (i % 3) * 100;
    y = 78 + Math.floor(i / 3) * 28;
    down = (inputMask & buttons[i][0]) !== 0;
    rows.push([x, y, 88, 22, down ? theme.colors.accent : theme.colors.row]);
  }
  JS2300.video.rects(rows);
  for (i = 0; i < buttons.length; i++) {
    x = 22 + (i % 3) * 100;
    y = 84 + Math.floor(i / 3) * 28;
    down = (inputMask & buttons[i][0]) !== 0;
    JS2300.video.text(x, y, buttons[i][1], down ? theme.text.selected : theme.text.primary);
  }
  drawToast(now);
  footer("B back   Y log");
  JS2300.video.present();
}

function drawAbout(now) {
  JS2300.video.clear(theme.colors.background);
  topBar("About");
  JS2300.video.text(16, 44, "UniFrog JS frontend", theme.text.primary);
  JS2300.video.text(16, 68, "Settings: /unifrog/settings.ini", theme.text.muted);
  JS2300.video.text(16, 86, "Theme: /unifrog/theme.ini", theme.text.muted);
  JS2300.video.text(16, 110, "Games: indexed by system", theme.text.muted);
  JS2300.video.text(16, 128, "Storage: /media/mmcblk0", theme.text.muted);
  drawToast(now);
  footer("B back   Y log");
  JS2300.video.present();
}

function drawVideoMode(now) {
  var i;
  var y;
  var active;
  JS2300.video.clear(theme.colors.background);
  topBar("Video Mode");
  JS2300.video.text(10, 30, pendingVideoPath, theme.text.muted);
  drawRows(videoModes.length, 50, 22, 19);
  for (i = 0; i < videoModes.length; i++) {
    y = 55 + i * 22;
    active = i === selected;
    JS2300.video.text(16, y, videoModes[i].label, active ? theme.text.selected : theme.text.primary);
    JS2300.video.text(156, y, videoModes[i].detail, active ? theme.text.selectedMuted : theme.text.muted);
  }
  drawToast(now);
  footer("A play   B back   Y log");
  JS2300.video.present();
}

function launchValue(row) {
  if (row === 0) return launchAudioIndex === 0 ? "On" : "Off";
  if (row === 1) return launchGainOptions[launchGainIndex].label;
  if (row === 2) return launchCpuOptions[launchCpuIndex].label;
  if (row === 3) return launchGeOptions[launchGeIndex].label;
  if (row === 4) return String(backlightLevel(launchBacklightIndex));
  if (row === 5) return launchFrameskipOptions[launchFrameskipIndex].label;
  if (row === 6) return launchDisplayOptions[launchDisplayIndex].label;
  if (row === 7) return launchCoreOptions[launchCoreIndex].label;
  return "Run game";
}

function drawLaunch(now) {
  var i;
  var y;
  var active;
  var rowTop = 42;
  var rowStep = 19;
  JS2300.video.clear(theme.colors.background);
  topBar("Launch Options");
  JS2300.video.text(10, 30, pendingGamePath, theme.text.muted);
  drawRows(launchRows.length, rowTop, rowStep, 18);
  for (i = 0; i < launchRows.length; i++) {
    y = rowTop + 5 + i * rowStep;
    active = i === selected;
    JS2300.video.text(16, y, launchRows[i], active ? theme.text.selected : theme.text.primary);
    if (i === 4) {
      drawSlider(148, y - 1, 106, backlightLevel(launchBacklightIndex), 1, 100, active);
      JS2300.video.text(270, y, launchValue(i), active ? theme.text.selectedMuted : theme.text.muted);
    } else {
      JS2300.video.text(150, y, launchValue(i), active ? theme.text.selectedMuted : theme.text.muted);
    }
  }
  drawToast(now);
  footer("A change/start   B back   <> change");
  JS2300.video.present();
}

function draw(now) {
  if (view === BROWSER) drawBrowser(now);
  else if (view === INPUT) drawInput(now);
  else if (view === ABOUT) drawAbout(now);
  else if (view === VIDEO_MODE) drawVideoMode(now);
  else if (view === LAUNCH) drawLaunch(now);
  else if (view === SYSTEMS) drawSystems(now);
  else if (view === INDEX_LIST) drawIndexList(now);
  else if (view === SETTINGS) drawSettings(now);
  else if (view === DEVELOPER) drawDeveloper(now);
  else if (view === SCRIPT_LIST) drawScriptList(now);
  else if (view === SYSTEM_CHECK) drawSystemCheck(now);
  else drawHome(now);
}

function selectedMarqueeText() {
  var entry;
  if (view === BROWSER && selected < entries.length) {
    entry = entries[selected];
    return entry.dir ? "[" + entry.name + "]" : entry.name;
  }
  if (view === INDEX_LIST && selected < currentItems.length)
    return currentItems[selected].label;
  if (view === SYSTEM_CHECK && selected < systemCheckRows.length)
    return systemCheckRows[selected].expected + " " +
      systemCheckRows[selected].actual;
  return "";
}

function marqueeActive() {
  return selectedMarqueeText().length > 46;
}

function visibleRowsForView() {
  if (view === SETTINGS) return 9;
  if (view === LAUNCH) return 9;
  if (view === SYSTEM_CHECK) return 5;
  return 8;
}

function move(delta, count) {
  var rows = visibleRowsForView();
  if (count <= 0) return;
  selected = (selected + count + delta) % count;
  if (selected < scroll) scroll = selected;
  if (selected >= scroll + rows) scroll = selected - rows + 1;
  startMotion(JS2300.now());
}

function moveHomeGrid(delta) {
  var count = homeItems.length;

  if (count <= 0) return;
  selected = (selected + count + delta) % count;
  if (selected < scroll)
    scroll = selected - (selected % 2);
  if (selected >= scroll + 8)
    scroll = selected - 6 - ((selected - 6) % 2);
  if (scroll < 0)
    scroll = 0;
  if (scroll > count - 1)
    scroll = count - 1;
  if (scroll % 2)
    scroll--;
  startMotion(JS2300.now());
}

function repeated(mask, button, now, delay, interval) {
  if ((mask & button) === 0) {
    if (repeatMask === button) {
      repeatMask = 0;
      nextRepeatMs = 0;
    }
    return false;
  }
  if ((prevInput & button) === 0) {
    repeatMask = button;
    nextRepeatMs = now + delay;
    return true;
  }
  if (repeatMask === button && now >= nextRepeatMs) {
    nextRepeatMs = now + interval;
    return true;
  }
  return false;
}

function openSystems(now) {
  selected = 0;
  scroll = 0;
  view = SYSTEMS;
  showToast(String(indexItems.length) + " games", now);
}

function openSystemList(now) {
  var i;
  currentSystem = systems[selected];
  currentListTitle = currentSystem;
  currentItems = [];
  for (i = 0; i < indexItems.length; i++) {
    if (indexItems[i].system === currentSystem)
      currentItems.push(indexItems[i]);
  }
  selected = 0;
  scroll = 0;
  view = INDEX_LIST;
  showToast(String(currentItems.length) + " games", now);
}

function openMediaIndex(now) {
  if (mediaItems.length === 0) {
    openBrowser("Media", "/media/mmcblk0", "media");
    return;
  }
  currentSystem = "Media";
  currentListTitle = "Media";
  currentItems = mediaItems;
  selected = 0;
  scroll = 0;
  view = INDEX_LIST;
  showToast(String(currentItems.length) + " files", now);
}

function refreshScripts(now) {
  var list = JS2300.fs.list(SCRIPT_DIR);
  var i;
  scriptItems = [];
  for (i = 0; i < list.length; i++) {
    if (!list[i].dir && isScript(list[i].name)) {
      scriptItems.push({
        label: list[i].name,
        path: joinPath(SCRIPT_DIR, list[i].name)
      });
    }
  }
  selected = 0;
  scroll = 0;
  view = SCRIPT_LIST;
  showToast(String(scriptItems.length) + " scripts", now);
}

function openDeveloperItem(now) {
  var id = developerItems[selected].id;
  if (id === "system_check") runAction("developer:system_check", now);
  else if (id === "smoke") runAction("script:" + SCRIPT_DIR + "/smoke-test.js", now);
  else if (id === "exception") runAction("developer:exception", now);
  else if (id === "cpu_exception") runAction("developer:cpu_exception", now);
  else if (id === "run_core") {
    pendingDeveloperCore = "";
    pendingDeveloperCorePath = "";
    openBrowser("Core File", "/media/mmcblk0/unifrog/cores", "core");
  }
  else if (id === "scripts") refreshScripts(now);
}

function runSelectedScript(now) {
  if (scriptItems.length === 0) return;
  runAction("script:" + scriptItems[selected].path, now);
}

function coreIdFromFile(name) {
  var id = stripOneExtension(name);
  if (id === "pce_fast") return "pce-fast";
  return id;
}

function runDeveloperCoreWithPath(full, now) {
  var options;
  if (!pendingDeveloperCore) return;
  options = "audio=" + String(config.audio ? 1 : 0) +
    ",gain=" + String(config.gain) +
    ",cpu=" + String(config.cpu) +
    ",ge=" + String(config.ge) +
    ",backlight=" + String(config.brightness) +
    ",fs=" + String(config.frameskip) +
    ",display=" + String(config.display) +
    ",core=" + pendingDeveloperCore +
    ",corefile=" + pendingDeveloperCorePath;
  runAction("run+" + options + ":" + full, now);
}

function applyLaunchDefaults(full, preferredCore) {
  launchAudioIndex = config.audio ? 0 : 1;
  launchGainIndex = optionIndex(launchGainOptions, config.gain, 1);
  launchCpuIndex = optionIndex(launchCpuOptions, config.cpu, 8);
  launchGeIndex = optionIndex(launchGeOptions, config.ge, 0);
  launchBacklightIndex = nearestBacklightIndex(config.brightness);
  launchFrameskipIndex = optionIndex(launchFrameskipOptions, config.frameskip, 1);
  launchDisplayIndex = optionIndex(launchDisplayOptions, config.display, 0);
  launchCoreOptions = gameCoreOptions(full);
  launchCoreIndex = 0;
  if (preferredCore) {
    var i;
    for (i = 0; i < launchCoreOptions.length; i++) {
      if (launchCoreOptions[i].value === preferredCore) launchCoreIndex = i;
    }
  }
  selected = 8;
}

function openGame(full, preferredCore, now) {
  pendingGamePath = full;
  launchBackView = view;
  applyLaunchDefaults(full, preferredCore);
  view = LAUNCH;
  showToast("Launch options", now);
}

function openIndexedItem(now) {
  var item;
  if (currentItems.length === 0) return;
  item = currentItems[selected];
  if (item.core === "video") {
    pendingVideoPath = item.path;
    selected = 0;
    view = VIDEO_MODE;
  } else {
    openGame(item.path, item.core, now);
  }
}

function openHomeItem(now) {
  var id = homeItems[selected].id;
  if (startsWithText(id, "system:")) openSystemByName(lineValue(id, 7), now);
  else if (id === "firmware") openBrowser("Firmware", "/media/mmcblk0/firmware", "firmware");
  else if (id === "last") {
    if (config.lastPath) openGame(config.lastPath, config.lastCore, now);
    else showToast("No last game", now);
  } else if (id === "games") openSystems(now);
  else if (id === "index") indexGames(now);
  else if (id === "media") openMediaIndex(now);
  else if (id === "settings") {
    selected = 0;
    scroll = 0;
    view = SETTINGS;
    showToast("Settings", now);
  } else if (id === "developer") {
    selected = 0;
    scroll = 0;
    view = DEVELOPER;
    showToast("Developer", now);
  } else if (id === "files") openBrowser("Files", "/media/mmcblk0", "all");
}

function runAction(id, now) {
  var ret = JS2300.system.action(id);
  if (ret !== 0) showToast("Action failed " + String(ret), now);
  else running = false;
}

function runLaunchGame(now) {
  var audio = launchAudioIndex === 0 ? 1 : 0;
  var gain = launchGainOptions[launchGainIndex].value;
  var cpu = launchCpuOptions[launchCpuIndex].value;
  var ge = launchGeOptions[launchGeIndex].value;
  var backlight = backlightLevel(launchBacklightIndex);
  var fs = launchFrameskipOptions[launchFrameskipIndex].value;
  var display = launchDisplayOptions[launchDisplayIndex].value;
  var core = launchCoreOptions[launchCoreIndex].value;
  var options = "audio=" + String(audio) + ",gain=" + String(gain) +
    ",cpu=" + String(cpu) + ",ge=" + String(ge) +
    ",backlight=" + String(backlight) + ",fs=" + String(fs) +
    ",display=" + String(display) + ",core=" + core;
  config.audio = audio;
  config.gain = gain;
  config.cpu = cpu;
  config.ge = ge;
  config.brightness = backlight;
  config.frameskip = fs;
  config.display = display;
  config.lastPath = pendingGamePath;
  config.lastCore = core;
  writeSettings();
  runAction("run+" + options + ":" + pendingGamePath, now);
}

function openEntry(now) {
  var entry;
  var full;
  if (entries.length === 0) return;
  entry = entries[selected];
  full = joinPath(path, entry.name);
  if (entry.dir) {
    path = full;
    refreshBrowser();
  } else if (filter === "core" && isCoreFile(entry.name)) {
    pendingDeveloperCore = coreIdFromFile(entry.name);
    pendingDeveloperCorePath = full;
    openBrowser("Core Path", "/media/mmcblk0", "core_path");
    showToast("Pick content path", now);
  } else if (filter === "core_path") {
    runDeveloperCoreWithPath(full, now);
  } else if (isVideo(entry.name)) {
    pendingVideoPath = full;
    selected = 0;
    view = VIDEO_MODE;
    showToast("Choose playback mode", now);
  } else if (isAsd(entry.name)) {
    runAction("firmware:" + entry.name, now);
  } else if (isScript(entry.name)) {
    runAction("script:" + full, now);
  } else {
    openGame(full, "", now);
  }
}

function back(now) {
  var i;
  if (view === HOME) {
    showToast("Home", now);
  } else if (view === LAUNCH) {
    view = launchBackView;
    showToast("Back", now);
  } else if (view === VIDEO_MODE) {
    if (currentListTitle === "Media") view = INDEX_LIST;
    else view = BROWSER;
    showToast("Back", now);
  } else if (view === INDEX_LIST) {
    if (currentListTitle === "Media") view = HOME;
    else view = SYSTEMS;
    showToast("Back", now);
  } else if (view === SCRIPT_LIST) {
    view = DEVELOPER;
    selected = 0;
    for (i = 0; i < developerItems.length; i++) {
      if (developerItems[i].id === "scripts")
        selected = i;
    }
    scroll = 0;
    showToast("Developer", now);
  } else if (view === SYSTEM_CHECK) {
    view = HOME;
    selected = 0;
    scroll = 0;
    showToast("Home", now);
  } else if (view === SYSTEMS || view === SETTINGS || view === INPUT ||
      view === ABOUT || view === DEVELOPER) {
    view = HOME;
    selected = 0;
    scroll = 0;
    showToast("Home", now);
  } else if (view === BROWSER && path !== "/media/mmcblk0" && path !== "/media/mmcblk0/firmware") {
    path = parentPath(path);
    refreshBrowser();
  } else {
    view = HOME;
    selected = 0;
    scroll = 0;
    showToast("Home", now);
  }
}

function cycleLaunch(delta) {
  if (selected === 0) {
    launchAudioIndex = (launchAudioIndex + 2 + delta) % 2;
  } else if (selected === 1) {
    launchGainIndex =
      (launchGainIndex + launchGainOptions.length + delta) % launchGainOptions.length;
  } else if (selected === 2) {
    launchCpuIndex =
      (launchCpuIndex + launchCpuOptions.length + delta) % launchCpuOptions.length;
  } else if (selected === 3) {
    launchGeIndex =
      (launchGeIndex + launchGeOptions.length + delta) % launchGeOptions.length;
  } else if (selected === 4) {
    launchBacklightIndex = clampIndex(launchBacklightIndex + delta, backlightLevels.length);
  } else if (selected === 5) {
    launchFrameskipIndex =
      (launchFrameskipIndex + launchFrameskipOptions.length + delta) %
      launchFrameskipOptions.length;
  } else if (selected === 6) {
    launchDisplayIndex =
      (launchDisplayIndex + launchDisplayOptions.length + delta) %
      launchDisplayOptions.length;
  } else if (selected === 7) {
    launchCoreIndex =
      (launchCoreIndex + launchCoreOptions.length + delta) % launchCoreOptions.length;
  }
  dirty = true;
}

function changeSetting(delta, now) {
  var id = settingRows[selected].id;
  if (id === "index" || id === "system_check" || id === "input" ||
      id === "developer" || id === "about") {
    return;
  } else if (id === "brightness") {
    config.brightness =
      backlightLevel(clampIndex(nearestBacklightIndex(config.brightness) + delta,
      backlightLevels.length));
    if (JS2300.system.backlight)
      JS2300.system.backlight(config.brightness);
  } else if (id === "audio") {
    config.audio = config.audio ? 0 : 1;
  } else if (id === "gain") {
    config.gain =
      launchGainOptions[(optionIndex(launchGainOptions, config.gain, 1) +
      launchGainOptions.length + delta) % launchGainOptions.length].value;
  } else if (id === "cpu") {
    config.cpu =
      launchCpuOptions[(optionIndex(launchCpuOptions, config.cpu, 8) +
      launchCpuOptions.length + delta) % launchCpuOptions.length].value;
  } else if (id === "ge") {
    config.ge =
      launchGeOptions[(optionIndex(launchGeOptions, config.ge, 0) +
      launchGeOptions.length + delta) % launchGeOptions.length].value;
  } else if (id === "av") {
    config.av =
      avOutputOptions[(optionIndex(avOutputOptions, config.av, 0) +
      avOutputOptions.length + delta) % avOutputOptions.length].value;
    if (JS2300.system.avOutput)
      JS2300.system.avOutput(config.av);
  } else if (id === "frameskip") {
    config.frameskip =
      launchFrameskipOptions[(optionIndex(launchFrameskipOptions, config.frameskip, 1) +
      launchFrameskipOptions.length + delta) % launchFrameskipOptions.length].value;
  } else if (id === "auto_index") {
    config.autoIndex = config.autoIndex ? 0 : 1;
  }
  writeSettings();
  dirty = true;
}

function activateSetting(now) {
  var id = settingRows[selected].id;
  if (id === "index") {
    indexGames(now);
  } else if (id === "system_check") {
    runAction("developer:system_check", now);
  } else if (id === "input") {
    view = INPUT;
    showToast("Input monitor", now);
  } else if (id === "developer") {
    selected = 0;
    scroll = 0;
    view = DEVELOPER;
    showToast("Developer", now);
  } else if (id === "about") {
    view = ABOUT;
    showToast("About", now);
  } else {
    changeSetting(1, now);
  }
}

function handleInput(input, now) {
  var pressed = input & ~prevInput;
  var flushRet;
  inputMask = input;
  if (pressed & BTN_Y) {
    flushRet = JS2300.flushLog();
    showToast(flushRet === 0 ? "Log flushed" : "Log failed " + String(flushRet), now);
    return;
  }
  if (pressed & BTN_B) {
    back(now);
    return;
  }
  if (view === HOME) {
    if (repeated(input, BTN_UP, now, 360, 170)) moveHomeGrid(-2);
    else if (repeated(input, BTN_DOWN, now, 360, 170)) moveHomeGrid(2);
    else if (repeated(input, BTN_LEFT, now, 360, 170)) moveHomeGrid(-1);
    else if (repeated(input, BTN_RIGHT, now, 360, 170)) moveHomeGrid(1);
    if (pressed & BTN_A) openHomeItem(now);
  } else if (view === BROWSER) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, entries.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, entries.length);
    if (pressed & BTN_LEFT) move(-8, entries.length);
    if (pressed & BTN_RIGHT) move(8, entries.length);
    if (pressed & BTN_A) openEntry(now);
  } else if (view === SYSTEMS) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, systems.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, systems.length);
    if (pressed & BTN_A) openSystemList(now);
  } else if (view === INDEX_LIST) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, currentItems.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, currentItems.length);
    if (pressed & BTN_LEFT) move(-8, currentItems.length);
    if (pressed & BTN_RIGHT) move(8, currentItems.length);
    if (pressed & BTN_A) openIndexedItem(now);
  } else if (view === DEVELOPER) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, developerItems.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, developerItems.length);
    if (pressed & BTN_A) openDeveloperItem(now);
  } else if (view === SCRIPT_LIST) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, scriptItems.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, scriptItems.length);
    if (pressed & BTN_A) runSelectedScript(now);
  } else if (view === SYSTEM_CHECK) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, systemCheckRows.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, systemCheckRows.length);
    if (pressed & BTN_LEFT) move(-5, systemCheckRows.length);
    if (pressed & BTN_RIGHT) move(5, systemCheckRows.length);
  } else if (view === VIDEO_MODE) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, videoModes.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, videoModes.length);
    if (pressed & BTN_A)
      runAction("video:" + (videoModes[selected].noAudio ? "n:" : "") +
        String(videoModes[selected].id) + ":" + pendingVideoPath, now);
  } else if (view === SETTINGS) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, settingRows.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, settingRows.length);
    if (pressed & BTN_LEFT) changeSetting(-1, now);
    if (pressed & BTN_RIGHT) changeSetting(1, now);
    if (pressed & BTN_A) activateSetting(now);
  } else if (view === LAUNCH) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, launchRows.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, launchRows.length);
    if (pressed & BTN_LEFT) cycleLaunch(-1);
    if (pressed & BTN_RIGHT) cycleLaunch(1);
    if (pressed & BTN_A) {
      if (selected === 8) runLaunchGame(now);
      else cycleLaunch(1);
    }
    if (pressed & BTN_START) runLaunchGame(now);
  }
}

JS2300.log("unifrog frontend start");
loadSettings();
loadThemeFile();
loadIndex();
if (config.autoIndex)
  indexGames(JS2300.now());
loadSystemCheckReport(JS2300.now());

while (running) {
  var now = JS2300.now();
  var input = JS2300.input.poll();
  var rawInput = input;
  if (rawInput !== prevInput) dirty = true;
  updateBattery(now);
  if (startupInputGate) {
    inputMask = rawInput;
    repeatMask = 0;
    nextRepeatMs = 0;
    if (startupInputGateMask < 0)
      startupInputGateMask = rawInput;
    if (startupInputGateMask !== 0) {
      input = rawInput & ~startupInputGateMask;
    }
    if ((rawInput & startupInputGateMask) !== 0 && input === 0) {
      if (dirty) {
        draw(now);
        dirty = false;
      } else {
        JS2300.sleep(16);
      }
      prevInput = 0;
      continue;
    }
    if ((rawInput & startupInputGateMask) === 0) {
      startupInputGate = false;
      startupInputGateMask = 0;
    }
  }
  handleInput(input, now);
  if (toast && now >= toastUntilMs) {
    toast = "";
    dirty = true;
  }
  if (now < motionUntilMs)
    dirty = true;
  if (marqueeActive() && now >= nextMarqueeMs) {
    dirty = true;
    nextMarqueeMs = now + 140;
  }
  if (dirty) {
    draw(now);
    dirty = false;
    if (!frontendReadyLogged) {
      JS2300.log("frontend ready input_ms=" + String(now) + " view=" + String(view));
      frontendReadyLogged = true;
    }
  } else {
    JS2300.sleep(16);
  }
  prevInput = input;
}

JS2300.exit("frontend closed");
