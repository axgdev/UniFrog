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
    clearNav();
    selected = 0;
    scroll = 0;
    view = SYSTEM_CHECK;
    showToast("System check", now);
    if (JS2300.fs.writeText)
      JS2300.fs.writeText(SYSTEM_CHECK_REPORT_PATH, clearSystemCheckShow(text));
  }
}

function pushSystem(system, item) {
  var i;
  for (i = 0; i < systems.length; i++) {
    if (systems[i] === system) {
      systemCounts[i]++;
      if (item) systemItemLists[i].push(item);
      return;
    }
  }
  systems.push(system);
  systemCounts.push(1);
  systemItemLists.push(item ? [item] : []);
}

function clearIndexData() {
  indexItems = [];
  mediaItems = [];
  systems = [];
  systemCounts = [];
  systemItemLists = [];
}

function rangeEquals(text, start, end, value) {
  var i;
  if (end - start !== value.length) return false;
  for (i = 0; i < value.length; i++) {
    if (text.charCodeAt(start + i) !== value.charCodeAt(i)) return false;
  }
  return true;
}

function rangeText(text, start, end) {
  var out = "";
  var i;
  var c;
  for (i = start; i < end; i++) {
    c = text.charCodeAt(i);
    if (c !== 13) out += String.fromCharCode(c);
  }
  return out;
}

function findSep(text, start, end) {
  var pos = text.indexOf("|", start);
  if (pos < 0 || pos >= end) return -1;
  return pos;
}

function addIndexFields(isMedia, system, core, pathText, label) {
  var item;
  if (!pathText) return;
  if (shouldHideFile(basename(pathText), parentPath(pathText))) return;
  if (!label) label = basename(pathText);
  if (isMedia) {
    mediaItems.push({
      system: "Media",
      core: "video",
      path: pathText,
      label: label
    });
  } else {
    if (!system) system = "Other";
    item = {
      system: system,
      core: core,
      path: pathText,
      label: label
    };
    indexItems.push(item);
    pushSystem(system, item);
  }
}

function addIndexLineRange(text, start, end) {
  var p1;
  var p2;
  var p3;
  var p4;
  var isGame;
  var isMediaLine;

  while (end > start && text.charCodeAt(end - 1) === 13) end--;
  if (end <= start) return;
  p1 = findSep(text, start, end);
  if (p1 < 0) return;
  isGame = rangeEquals(text, start, p1, "game");
  isMediaLine = !isGame && rangeEquals(text, start, p1, "media");
  if (!isGame && !isMediaLine) return;
  p2 = findSep(text, p1 + 1, end);
  if (p2 < 0) return;
  p3 = findSep(text, p2 + 1, end);
  if (p3 < 0) return;
  p4 = findSep(text, p3 + 1, end);
  if (p4 < 0) return;
  addIndexFields(isMediaLine,
    rangeText(text, p1 + 1, p2),
    rangeText(text, p2 + 1, p3),
    rangeText(text, p3 + 1, p4),
    rangeText(text, p4 + 1, end));
}

function parseIndex(text, append) {
  var start = 0;
  var end;
  if (!append) clearIndexData();
  if (!text) return;
  while (start < text.length) {
    end = text.indexOf("\n", start);
    if (end < 0) {
      addIndexLineRange(text, start, text.length);
      break;
    } else {
      addIndexLineRange(text, start, end);
      start = end + 1;
    }
  }
}

function loadIndex() {
  var start = JS2300.now();
  var readStart = start;
  var gameText = JS2300.fs.readText ? JS2300.fs.readText(INDEX_PATH) : null;
  var mediaText = JS2300.fs.readText ? JS2300.fs.readText(MEDIA_INDEX_PATH) : null;
  var readDone = JS2300.now();
  clearIndexData();
  parseIndex(gameText ? gameText : "", 1);
  parseIndex(mediaText ? mediaText : "", 1);
  indexLoaded = true;
  indexLoadPending = false;
  JS2300.log("frontend index loaded games=" + String(indexItems.length) +
    " media=" + String(mediaItems.length) + " read_ms=" +
    String(readDone - readStart) + " parse_ms=" +
    String(JS2300.now() - readDone) + " ms=" + String(JS2300.now() - start));
}

function ensureIndexLoaded(now, quiet) {
  if (!indexLoaded)
    loadIndex();
  if (!quiet)
    showToast(String(indexItems.length) + " games", now);
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

function scanSystemDirectory(dir, depth, scan, cat) {
  var list;
  var i;
  var full;
  var label;
  if (depth > 10 || scan.files > 4096 || scan.dirs > 512) return;
  scan.dirs++;
  drawScan(dir, scan.games, scan.media);
  list = JS2300.fs.list(dir);
  for (i = 0; i < list.length; i++) {
    full = joinPath(dir, list[i].name);
    if (list[i].dir) {
      if (!skipDir(list[i].name, full))
        scanSystemDirectory(full, depth + 1, scan, cat);
    } else {
      scan.files++;
      if (shouldHideFile(list[i].name, dir))
        continue;
      label = list[i].name;
      if (cat.system === "Media") {
        if (isVideo(list[i].name)) {
          scan.mediaText += "media|Media|video|" + full + "|" + label + "\n";
          scan.media++;
        }
      } else {
        scan.gameText += "game|" + cat.system + "|" + cat.value + "|" +
          full + "|" + label + "\n";
        scan.games++;
      }
      if ((scan.files % 24) === 0)
        drawScan(dir, scan.games, scan.media);
    }
  }
}

function expandRomRoot(root) {
  if (!root || root === "/") return "/media/mmcblk0";
  if (startsWithText(root, "/media/mmcblk0")) return root;
  if (root.charCodeAt(0) === 47) return "/media/mmcblk0" + root;
  return "/media/mmcblk0/" + root;
}

function scanRoot(root, scan) {
  var dir = expandRomRoot(root);
  var list = JS2300.fs.list(dir);
  var i;
  var full;
  var cat;
  drawScan(dir, scan.games, scan.media);
  scan.dirs++;
  for (i = 0; i < list.length; i++) {
    if (!list[i].dir) continue;
    cat = catalogForFolderName(list[i].name);
    if (!cat) continue;
    full = joinPath(dir, list[i].name);
    scanSystemDirectory(full, 1, scan, cat);
  }
}

function forEachRomRoot(callback, scan) {
  var text = config.romRoots || "/ROMS|/";
  var start = 0;
  var i;
  var part;
  for (i = 0; i <= text.length; i++) {
    if (i === text.length || text.charCodeAt(i) === 124 ||
        text.charCodeAt(i) === 44) {
      part = trimAscii(textWindow(text, start, i - start));
      if (part) callback(part, scan);
      start = i + 1;
    }
  }
}

function nativeIndexGames(now) {
  var result;
  var text;
  if (!JS2300.fs.index) return 0;
  drawScan(config.romRoots, 0, 0);
  result = JS2300.fs.index(config.romRoots, INDEX_PATH, MEDIA_INDEX_PATH);
  if (!result || !result.ok) return 0;
  loadIndex();
  text = String(result.games) + " games indexed";
  if (result.truncated) text = text + " limited";
  showToast(text, now);
  JS2300.log("frontend native index games=" + String(result.games) +
    " media=" + String(result.media) + " files=" + String(result.files) +
    " dirs=" + String(result.dirs) + " ms=" + String(result.ms));
  clearNav();
  view = HOME;
  selected = 0;
  scroll = 0;
  return 1;
}

function indexGames(now) {
  var scan = { gameText: "", mediaText: "", games: 0, media: 0, files: 0, dirs: 0 };
  if (nativeIndexGames(now)) return;
  forEachRomRoot(scanRoot, scan);
  if (JS2300.fs.writeText) {
    JS2300.fs.writeText(INDEX_PATH, scan.gameText);
    JS2300.fs.writeText(MEDIA_INDEX_PATH, scan.mediaText);
  }
  parseIndex(scan.gameText, 0);
  parseIndex(scan.mediaText, 1);
  indexLoaded = true;
  indexLoadPending = false;
  showToast(String(scan.games) + " games indexed", now);
  clearNav();
  view = HOME;
  selected = 0;
  scroll = 0;
}
