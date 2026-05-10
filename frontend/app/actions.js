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
  buildSystemSummary();
  pushNav();
  selected = 0;
  scroll = 0;
  view = SYSTEMS;
  showToast("ROM folders", now);
}

function openSystemIndex(index, now) {
  if (index < 0 || index >= systems.length) return;
  pushNav();
  currentSystem = systems[index];
  currentListTitle = currentSystem;
  currentItems = loadSystemItems(currentSystem);
  selected = 0;
  scroll = 0;
  view = INDEX_LIST;
  showToast(String(currentItems.length) + " games", now);
}

function openSystemList(now) {
  openSystemIndex(selected, now);
}

function openSystemByName(name, now) {
  pushNav();
  currentSystem = name;
  currentListTitle = name;
  currentItems = loadSystemItems(name);
  selected = 0;
  scroll = 0;
  view = INDEX_LIST;
  showToast(String(currentItems.length) + " games", now);
}

function openMediaIndex(now) {
  if (indexLoaded && mediaItems.length > 0)
    currentItems = mediaItems;
  else
    currentItems = loadMediaItems();
  if (currentItems.length === 0) {
    openBrowser("Media", "/media/mmcblk0", "media");
    return;
  }
  pushNav();
  currentSystem = "Media";
  currentListTitle = "Media";
  selected = 0;
  scroll = 0;
  view = INDEX_LIST;
  showToast(String(currentItems.length) + " files", now);
}

function refreshScripts(now) {
  var list = JS2300.fs.list(SCRIPT_DIR);
  var i;
  pushNav();
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
  else if (id === "perf") runAction("script:" + SCRIPT_DIR + "/perf-test.js", now);
  else if (id === "storage") runAction("developer:storage_test", now);
  else if (id === "storage_full") runAction("developer:storage_full_test", now);
  else if (id === "storage_mode") {
    pushNav();
    selected = 0;
    scroll = 0;
    view = STORAGE_MODE;
    showToast("Storage mode", now);
  }
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

function runSelectedStorageMode(now) {
  if (storageModeItems.length === 0) return;
  runAction("developer:storage_mode_test:" + storageModeItems[selected].id, now);
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
  pushNav();
  pendingGamePath = full;
  applyLaunchDefaults(full, preferredCore);
  view = LAUNCH;
  showToast("Launch options", now);
}

function openIndexedItem(now) {
  var item;
  if (currentItems.length === 0) return;
  item = currentItems[selected];
  if (item.core === "video") {
    pushNav();
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
    pushNav();
    selected = 0;
    scroll = 0;
    view = SETTINGS;
    showToast("Settings", now);
  } else if (id === "developer") {
    pushNav();
    selected = 0;
    scroll = 0;
    view = DEVELOPER;
    showToast("Developer", now);
  } else if (id === "files") openBrowser("Files", "/media/mmcblk0", "all");
}

function runAction(id, now) {
  var ret = JS2300.system.action(id);
  if (ret < 0) {
    showToast("Action failed " + String(ret), now);
  } else if (ret === 0) {
    running = false;
  } else {
    startupInputGate = true;
    startupInputGateMask = -1;
    startupInputGateLogged = false;
    startupInputGateStartMs = 0;
    prevInput = 0;
    repeatMask = 0;
    nextRepeatMs = 0;
    if (startsWithText(id, "run")) goHome(now);
    showToast("Returned", now);
    dirty = true;
  }
  return ret;
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
  var experimentalStorage = JS2300.system.action("storage:experimental") > 0;
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
  if (!experimentalStorage)
    writeSettings();
  if (runAction("run+" + options + ":" + pendingGamePath, now) >= 0 && experimentalStorage)
    writeSettings();
}

function openEntry(now) {
  var entry;
  var full;
  if (entries.length === 0) return;
  entry = entries[selected];
  if (filter === "firmware" && entry.bootUnset) {
    writeBootDefault("", now);
    return;
  }
  full = joinPath(path, entry.name);
  if (entry.dir) {
    pushNav();
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
    pushNav();
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

function setSelectedFirmwareDefault(now) {
  var entry;

  if (view !== BROWSER || filter !== "firmware" || entries.length === 0)
    return;
  entry = entries[selected];
  if (entry.bootUnset) {
    writeBootDefault("", now);
  } else if (!entry.dir && isValidBootAsdName(entry.name)) {
    writeBootDefault(entry.name, now);
  }
}

function back(now) {
  var state;
  if (view === HOME) {
    showToast("Home", now);
  } else if (navStack.length > 0) {
    state = navStack[navStack.length - 1];
    navStack.length = navStack.length - 1;
    restoreNavState(state);
    showToast("Back", now);
  } else {
    goHome(now);
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
      id === "developer" || id === "reboot" || id === "about") {
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
    pushNav();
    view = INPUT;
    showToast("Input monitor", now);
  } else if (id === "developer") {
    pushNav();
    selected = 0;
    scroll = 0;
    view = DEVELOPER;
    showToast("Developer", now);
  } else if (id === "reboot") {
    runAction("reboot", now);
  } else if (id === "about") {
    pushNav();
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
    if (pressed & BTN_SELECT) setSelectedFirmwareDefault(now);
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
  } else if (view === STORAGE_MODE) {
    if (repeated(input, BTN_UP, now, 360, 150)) move(-1, storageModeItems.length);
    else if (repeated(input, BTN_DOWN, now, 360, 150)) move(1, storageModeItems.length);
    if (pressed & BTN_A) runSelectedStorageMode(now);
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
