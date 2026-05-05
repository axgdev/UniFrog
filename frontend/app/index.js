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
      pushSystem(item.system, item);
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

function parseIndex(text, append) {
  var line = "";
  var i;
  var c;
  if (!append) clearIndexData();
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

function nativeIndexGames(now) {
  var result;
  var text;
  if (!JS2300.fs.index) return 0;
  drawScan("/media/mmcblk0", 0, 0);
  result = JS2300.fs.index("/media/mmcblk0", INDEX_PATH, MEDIA_INDEX_PATH);
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
  scanDirectory("/media/mmcblk0", 0, scan);
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
