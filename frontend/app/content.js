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

function hasAnyFolderHint(pathText, folders) {
  var p = pathText;
  var i;
  if (!endsWithCI(p, "/")) p += "/";
  for (i = 0; i < folders.length; i++) {
    if (containsCI(p, folders[i])) return true;
  }
  return false;
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

function isValidBootAsdName(name) {
  var i;
  if (!name || !isAsd(name) || name.charAt(0) === ".") return false;
  if (name.length >= 64) return false;
  for (i = 0; i < name.length; i++) {
    if (name.charAt(i) === "/" || name.charAt(i) === "\\" ||
        name.charAt(i) === ":" || name.charAt(i) === " " ||
        name.charAt(i) === "\t" || name.charAt(i) === "\r" ||
        name.charAt(i) === "\n")
      return false;
  }
  return true;
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
  if (filter === "firmware") {
    loadBootDefault();
    entries.unshift({
      name: "Use UniFrog by default",
      dir: false,
      bootUnset: true,
      key: ""
    });
  }
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
