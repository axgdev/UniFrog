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
var systemItemLists = [];
var indexLoaded = false;
var indexLoadPending = true;
var currentItems = [];
var currentSystem = "";
var currentListTitle = "";
var navStack = [];
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
var scriptItems = [];
var systemCheckRows = [];
var systemCheckTitle = "System Check";
var systemCheckDetail = "";
var pendingDeveloperCore = "";
var pendingDeveloperCorePath = "";
var frontendReadyLogged = false;
var deferImageLoads = true;
var imageLoadsThisFrame = 0;
var imageLoadPending = false;
var imageAttemptPaths = [];
var imageBadPaths = [];
var motionStartMs = 0;
var motionUntilMs = 0;
var nextMarqueeMs = 0;
var IMAGE_LOADS_PER_FRAME = 2;
var NAV_STACK_MAX = 16;

JS2300.log("unifrog frontend start");
loadSettings();
loadThemeFile();
if (config.autoIndex) {
  indexLoadPending = false;
  indexGames(JS2300.now());
}
loadSystemCheckReport(JS2300.now());
nextBatteryMs = JS2300.now() + 750;

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
        noteFrontendReady(now);
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
    if (deferImageLoads) {
      deferImageLoads = false;
      dirty = true;
    } else if (imageLoadPending) {
      dirty = true;
    }
    noteFrontendReady(now);
  } else {
    JS2300.sleep(16);
  }
  if (indexLoadPending && frontendReadyLogged)
    ensureIndexLoaded(JS2300.now(), 1);
  prevInput = input;
}

JS2300.exit("frontend closed");
