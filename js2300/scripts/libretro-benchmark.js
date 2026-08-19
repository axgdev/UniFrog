// unifrog: mode=extension
/*
 * Focused libretro benchmark runner.
 *
 * Optional config file:
 *   /media/mmcblk0/unifrog_data/config/libretro-benchmark.ini
 *
 * Supported keys:
 *   core=gpsp
 *   cores=gpsp,gpsp-gbac-prosty
 *   path=/media/mmcblk0/ROMS/GBA/game.gba
 *   frames=3600
 *   audio=1
 *   scpu=918
 *   frameskip=1
 *   frameskips=0,1,2,3
 */

var ROOT = "/media/mmcblk0";
var CONFIG = ROOT + "/unifrog_data/config/libretro-benchmark.ini";
var REPORT = ROOT + "/unifrog_data/logs/reports/libretro-benchmark.txt";
var started = JS2300.now();
var lines = [];

function log(line) {
  lines.push(line);
  JS2300.log("libretro_benchmark " + line);
}

function readText(path) {
  try {
    return JS2300.fs.readText(path) || "";
  } catch (e) {
    return "";
  }
}

function list(path) {
  try {
    return JS2300.fs.list(path) || [];
  } catch (e) {
    return [];
  }
}

function isFile(entry) {
  return !entry.type || entry.type === "file" || entry.type === 1;
}

function isDir(entry) {
  return entry.type === "dir" || entry.type === 2;
}

function parseConfig(text) {
  var out = {};
  var rows = text.split(/\r?\n/);

  for (var i = 0; i < rows.length; i++) {
    var line = rows[i].replace(/^\s+|\s+$/g, "");
    var eq;

    if (!line || line[0] === "#" || line[0] === ";")
      continue;
    eq = line.indexOf("=");
    if (eq <= 0)
      continue;
    out[line.slice(0, eq).replace(/\s+$/g, "")] =
      line.slice(eq + 1).replace(/^\s+/g, "");
  }
  return out;
}

function endsWithAny(name, suffixes) {
  var lower = name.toLowerCase();

  for (var i = 0; i < suffixes.length; i++)
    if (lower.slice(-suffixes[i].length) === suffixes[i])
      return true;
  return false;
}

function findRom(dir, suffixes, depth) {
  var entries = list(dir);

  for (var i = 0; i < entries.length; i++) {
    var name = entries[i].name || "";
    var path = dir + "/" + name;

    if (name === "." || name === "..")
      continue;
    if (isFile(entries[i]) && endsWithAny(name, suffixes))
      return path;
  }
  if (depth <= 0)
    return "";
  for (var j = 0; j < entries.length; j++) {
    var child = entries[j].name || "";
    var found;

    if (child === "." || child === "..")
      continue;
    if (!isDir(entries[j]))
      continue;
    found = findRom(dir + "/" + child, suffixes, depth - 1);
    if (found)
      return found;
  }
  return "";
}

function option(config, key, fallback) {
  return config[key] && config[key].length ? config[key] : fallback;
}

function splitList(value) {
  var out = [];
  var parts = (value || "").split(",");

  for (var i = 0; i < parts.length; i++) {
    var item = parts[i].replace(/^\s+|\s+$/g, "");

    if (item.length)
      out.push(item);
  }
  return out;
}

function frameskipName(value) {
  if (value === "0")
    return "off";
  if (value === "1")
    return "auto";
  if (value === "2")
    return "fixed1";
  if (value === "3")
    return "fixed2";
  return "unknown";
}

function main() {
  var config = parseConfig(readText(CONFIG));
  var cores = splitList(option(config, "cores", option(config, "core", "gpsp")));
  var path = option(config, "path", "");
  var frames = option(config, "frames", "3600");
  var audio = option(config, "audio", "1");
  var scpu = option(config, "scpu", "918");
  var frameskips = splitList(option(config, "frameskips",
    option(config, "frameskip", "0,1,2,3")));
  var failed = 0;
  var run = 0;
  var elapsed;
  var report;

  if (!cores.length)
    cores = ["gpsp"];
  if (!frameskips.length)
    frameskips = ["0", "1", "2", "3"];
  if (!path)
    path = findRom(ROOT + "/ROMS/GBA", [".gba", ".zip"], 3);
  if (!path) {
    log("FAIL no_rom folder=" + ROOT + "/ROMS/GBA config=" + CONFIG);
    throw new Error("No benchmark ROM found");
  }

  try {
    JS2300.system.action("toast:Libretro benchmark " + cores.length + " core(s)");
  } catch (toastErr) {}

  for (var coreIndex = 0; coreIndex < cores.length; coreIndex++) {
    var core = cores[coreIndex];

    for (var mode = 0; mode < frameskips.length; mode++) {
      var frameskip = frameskips[mode];
      var action = "run+core=" + core +
        ",audio=" + audio +
        ",frames=" + frames +
        ",scpu=" + scpu +
        ",frameskip=" + frameskip +
        ":" + path;
      var ret;
      var modeElapsed;
      var modeStart = JS2300.now();

      log("RUN run=" + run + " mode=" + mode + " core=" + core +
        " frames=" + frames + " audio=" + audio + " scpu=" + scpu +
        " frameskip=" + frameskip + " name=" + frameskipName(frameskip) +
        " path=" + path);
      ret = JS2300.system.action(action);
      modeElapsed = JS2300.now() - modeStart;
      log((ret > 0 ? "PASS" : "FAIL") + " run=" + run + " mode=" + mode +
        " core=" + core + " ret=" + ret + " elapsed_ms=" + modeElapsed +
        " frameskip=" + frameskip + " name=" + frameskipName(frameskip));
      if (ret <= 0)
        failed++;
      run++;
    }
  }

  elapsed = JS2300.now() - started;
  log((failed ? "FAIL" : "PASS") + " runs=" + run + " cores=" + cores.length +
    " modes=" + frameskips.length + " failed=" + failed +
    " elapsed_ms=" + elapsed);

  report = "show=1\n";
  report += "title=Libretro Benchmark\n";
  report += "detail=Runs one or more libretro core/game modes with perf_cpu, perf_audio, and perf_slow logs enabled in the retained log.\n";
  report += "detail=config=" + CONFIG + "\n";
  report += "detail=cores=" + cores.join(",") + " frames=" + frames +
    " audio=" + audio + " scpu=" + scpu +
    " frameskips=" + frameskips.join(",") + "\n";
  report += "detail=path=" + path + "\n";
  report += "detail=failed=" + failed + " elapsed_ms=" + elapsed + "\n";
  for (var i = 0; i < lines.length; i++)
    report += "item|OK|" + i + "|" + lines[i] + "\n";
  JS2300.fs.writeText(REPORT, report);
  JS2300.log("libretro_benchmark report=" + REPORT + " failed=" + failed);

  if (failed)
    throw new Error("libretro benchmark failed runs=" + failed);
}

main();
