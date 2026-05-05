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

function imagePathSeen(paths, path) {
  var i;
  for (i = 0; i < paths.length; i++) {
    if (paths[i] === path) return true;
  }
  return false;
}

function rememberImagePath(paths, path) {
  if (!imagePathSeen(paths, path))
    paths[paths.length] = path;
}

function drawItemIcon(item, x, y, active) {
  var path = iconForItem(item);
  var ret = -1;
  var attempted;

  if (path && imagePathSeen(imageBadPaths, path)) {
    drawIconFallback(x, y, active);
    return;
  }
  attempted = path && imagePathSeen(imageAttemptPaths, path);
  if (path && JS2300.video.image && !deferImageLoads &&
      (attempted || imageLoadsThisFrame < IMAGE_LOADS_PER_FRAME)) {
    if (!attempted) {
      rememberImagePath(imageAttemptPaths, path);
      imageLoadsThisFrame++;
    }
    ret = JS2300.video.image(path, x, y, 36, 36);
    if (ret !== 0)
      rememberImagePath(imageBadPaths, path);
  } else if (path && JS2300.video.image && !deferImageLoads) {
    imageLoadPending = true;
  }
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
  imageLoadsThisFrame = 0;
  imageLoadPending = false;
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
