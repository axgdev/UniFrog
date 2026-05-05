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
  return folderNameMatches(basename(dir), psxFolders);
}

function folderNameMatches(name, folders) {
  var i;
  for (i = 0; i < folders.length; i++) {
    if (lowerAscii(name) === lowerAscii(folders[i])) return true;
  }
  return false;
}

function catalogForFolderName(name) {
  var i;
  for (i = 0; i < coreCatalog.length; i++) {
    if (folderNameMatches(name, coreCatalog[i].folders))
      return coreCatalog[i];
  }
  return null;
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
  if (isLegacyStubName(name)) return false;
  return true;
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
  var dir = parentPath(pathText);
  var guard = 0;
  var cat;
  while (dir && guard < 16) {
    cat = catalogForFolderName(basename(dir));
    if (cat) return cat;
    if (dir === "/media/mmcblk0" || dir === "/") break;
    dir = parentPath(dir);
    guard++;
  }
  return null;
}

function gameCoreOptions(pathText) {
  var out = [];
  var i;
  var cat = catalogForPath(pathText);
  if (cat) pushCoreOption(out, cat);
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
  if (filter === "games") return isGame(entry.name) &&
    catalogForPath(joinPath(path, entry.name));
  if (filter === "media") return isVideo(entry.name);
  if (filter === "firmware") return isAsd(entry.name);
  if (filter === "core") return entry.dir || isCoreFile(entry.name);
  return true;
}

function sortEntries(list) {
  var out = [];
  var i;
  var j;
  var gap;
  var tmp;
  for (i = 0; i < list.length; i++) {
    if (accepted(list[i])) {
      list[i].key = lowerAscii(list[i].name);
      out.push(list[i]);
    }
  }
  for (gap = Math.floor(out.length / 2); gap > 0; gap = Math.floor(gap / 2)) {
    for (i = gap; i < out.length; i++) {
      tmp = out[i];
      j = i;
      while (j >= gap && compareEntries(out[j - gap], tmp) > 0) {
        out[j] = out[j - gap];
        j -= gap;
      }
      out[j] = tmp;
    }
  }
  return out;
}

function compareEntries(a, b) {
  if (a.dir && !b.dir) return -1;
  if (!a.dir && b.dir) return 1;
  if (a.key < b.key) return -1;
  if (a.key > b.key) return 1;
  return 0;
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
  pushNav();
  title = newTitle;
  path = newPath;
  filter = newFilter;
  view = BROWSER;
  refreshBrowser();
}
