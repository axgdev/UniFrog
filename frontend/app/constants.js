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
var STORAGE_MODE = 12;

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
var DEFAULT_OPTIONS_PATH = "/media/mmcblk0/unifrog/defaults/frontend.opt";
var USER_OPTIONS_PATH = "/media/mmcblk0/unifrog/user/frontend.opt";
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
var languageOptions = [
  { label: "English", value: "en" },
  { label: "Español", value: "es" },
  { label: "Français", value: "fr" },
  { label: "Deutsch", value: "de" },
  { label: "Português", value: "pt" },
  { label: "中文", value: "zh-Hans" },
  { label: "日本語", value: "ja" },
  { label: "한국어", value: "ko" },
  { label: "العربية", value: "ar" }
];
var fontOptions = [
  { label: "Noto Sans", value: "/media/mmcblk0/unifrog/fonts/NotoSans-Regular.ttf" },
  { label: "Arabic", value: "/media/mmcblk0/unifrog/fonts/NotoSansArabic-Regular.ttf" },
  { label: "Devanagari", value: "/media/mmcblk0/unifrog/fonts/NotoSansDevanagari-Regular.ttf" },
  { label: "Bengali", value: "/media/mmcblk0/unifrog/fonts/NotoSansBengali-Regular.ttf" },
  { label: "Tamil", value: "/media/mmcblk0/unifrog/fonts/NotoSansTamil-Regular.ttf" },
  { label: "Telugu", value: "/media/mmcblk0/unifrog/fonts/NotoSansTelugu-Regular.ttf" },
  { label: "Japanese", value: "/media/mmcblk0/unifrog/fonts/NotoSansCJKjp-Regular.otf" },
  { label: "CJK", value: "/media/mmcblk0/unifrog/fonts/NotoSansCJKsc-Regular.otf" }
];
var fontSizeOptions = [
  { label: "10", value: 10 }, { label: "11", value: 11 },
  { label: "12", value: 12 }, { label: "13", value: 13 },
  { label: "14", value: 14 }, { label: "15", value: 15 }
];
var fastSdOptions = [
  { label: "Boot", value: "boot" },
  { label: "hs1", value: "hs1" },
  { label: "wide50", value: "wide50" },
  { label: "wide", value: "wide" },
  { label: "uhs12", value: "uhs12" },
  { label: "uhs25", value: "uhs25" },
  { label: "uhs", value: "uhs" }
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
var settingRootRows = [
  { id: "settings_display", label: "Display" },
  { id: "settings_launch", label: "Launch" },
  { id: "settings_library", label: "Library" },
  { id: "settings_system", label: "System" },
  { id: "settings_tools", label: "Tools" }
];
var settingDisplayRows = [
  { id: "brightness", label: "Backlight" },
  { id: "language", label: "Language" },
  { id: "font", label: "Font" },
  { id: "font_size", label: "Font size" },
  { id: "av", label: "AV output" }
];
var settingLaunchRows = [
  { id: "audio", label: "Audio" },
  { id: "gain", label: "Gain" },
  { id: "cpu", label: "CPU" },
  { id: "ge", label: "GE" },
  { id: "frameskip", label: "Frameskip" },
  { id: "fast_sd", label: "Fast SD" }
];
var settingLibraryRows = [
  { id: "auto_index", label: "Auto index boot" },
  { id: "rom_roots", label: "ROM roots" },
  { id: "index", label: "Index library" }
];
var settingSystemRows = [
  { id: "system_check", label: "System check" },
  { id: "input", label: "Input monitor" },
  { id: "about", label: "About" },
  { id: "reboot", label: "Reboot" }
];
var settingToolRows = [
  { id: "developer", label: "Developer tools" },
  { id: "storage_mode", label: "Storage mode test" },
  { id: "reboot", label: "Reboot" },
  { id: "about", label: "About" }
];
var settingRows = settingRootRows;
