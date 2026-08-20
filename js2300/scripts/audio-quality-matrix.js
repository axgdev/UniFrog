// unifrog: mode=extension
/*
 * Cross-device audio quality matrix.
 *
 * Populate:
 *   /media/mmcblk0/unifrog_data/config/audio-quality-matrix.ini
 *
 * First run creates that template. Edit game_path and video_path to point at
 * real files on the SD card, then run the script again on each board.
 *
 * Retained logs contain the quantitative route, write-failure, underrun,
 * decoder-progress, and timing evidence. Listen during each run for gaps,
 * clipped attacks, idle noise, and synchronization drift. The native video
 * stage is the production video path; the FFmpeg stage is audio-only by
 * default because software H.264 video is diagnostic-only on this target.
 */

var ROOT = "/media/mmcblk0";
var CONFIG = ROOT + "/unifrog_data/config/audio-quality-matrix.ini";
var REPORT = ROOT + "/unifrog_data/logs/reports/audio-quality-matrix.txt";
var TEMPLATE =
  "# Replace both placeholder paths before running again.\n" +
  "game_path=/media/mmcblk0/ROMS/GBA/REPLACE_ME.gba\n" +
  "game_core=gpsp\n" +
  "video_path=/media/mmcblk0/VIDEOS/REPLACE_ME.mp4\n" +
  "duration_s=15\n" +
  "game_fps=60\n" +
  "scpu=918\n" +
  "frameskip=1\n" +
  "video_modes=native,ffmpeg_audio\n" +
  "allow_software_video=0\n" +
  "run_route_diagnostics=1\n" +
  "require_route_diagnostics=0\n";
var started = JS2300.now();
var lines = [];
var failed = 0;

function log(line) {
  lines.push(line);
  JS2300.log("audio_quality_matrix " + line);
}

function readText(path) {
  try {
    return JS2300.fs.readText(path) || "";
  } catch (e) {
    return "";
  }
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

function option(config, key, fallback) {
  return config[key] && config[key].length ? config[key] : fallback;
}

function numberOption(config, key, fallback) {
  var value = parseInt(option(config, key, "" + fallback), 10);
  return value > 0 ? value : fallback;
}

function splitList(value) {
  var out = [];
  var parts = (value || "").split(",");

  for (var i = 0; i < parts.length; i++) {
    var item = parts[i].replace(/^\s+|\s+$/g, "");
    if (item)
      out.push(item);
  }
  return out;
}

function lower(value) {
  return (value || "").toLowerCase();
}

function runRouteDiagnostics(config) {
  var mode = lower(option(config, "run_route_diagnostics", "0"));
  var required = option(config, "require_route_diagnostics", "0") !== "0";

  if (mode === "0" || mode === "false" || mode === "off" || mode === "no")
    return;
  if (mode === "strict")
    required = true;
  if (mode === "1" || mode === "true" || mode === "on" ||
      mode === "yes" || mode === "tones" || mode === "audio" ||
      mode === "audible" || mode === "strict") {
    probe("route_diagnostics", "developer:audio_test", required);
    return;
  }
  log("WARN name=route_diagnostics_skipped reason=unknown_mode value=" + mode);
}

function booleanOption(config, key, fallback) {
  var value = lower(option(config, key, fallback ? "1" : "0"));

  return !(value === "0" || value === "false" || value === "off" ||
    value === "no");
}

function runVideoMode(config, mode, durationMs, videoPath) {
  var mediaMode = mode;
  var videoEnabled = "1";

  if (mode === "ffmpeg_audio") {
    mediaMode = "ffmpeg";
    videoEnabled = "0";
  } else if (mode === "ffmpeg_video") {
    if (!booleanOption(config, "allow_software_video", false)) {
      log("WARN name=video_ffmpeg_video_skipped reason=software_h264_diagnostic_only set_allow_software_video=1");
      return;
    }
    mediaMode = "ffmpeg";
    videoEnabled = "1";
  }
  run("video_" + mode, "media+mode=" + mediaMode +
    ",audio=1,video=" + videoEnabled +
    ",ms=" + durationMs + ":" + videoPath);
}

function run(name, action) {
  var before = JS2300.now();
  var ret;

  try {
    JS2300.system.action("toast:Audio test " + name);
  } catch (toastErr) {}
  ret = JS2300.system.action(action);
  log((ret > 0 ? "PASS" : "FAIL") + " name=" + name + " ret=" + ret +
    " elapsed_ms=" + (JS2300.now() - before) + " action=" + action);
  if (ret <= 0)
    failed++;
  JS2300.sleep(1000);
}

function probe(name, action, required) {
  var before = JS2300.now();
  var ret;

  try {
    JS2300.system.action("toast:Audio probe " + name);
  } catch (toastErr) {}
  ret = JS2300.system.action(action);
  log((ret > 0 ? "PASS" : (required ? "FAIL" : "WARN")) +
    " name=" + name + " ret=" + ret +
    " elapsed_ms=" + (JS2300.now() - before) + " action=" + action);
  if (ret <= 0 && required)
    failed++;
  JS2300.sleep(1000);
}

function writeReport(configState) {
  var report = "show=1\n";
  report += "title=Audio Quality Matrix\n";
  report += "detail=" + configState + "\n";
  report += "detail=config=" + CONFIG + "\n";
  report += "detail=Listen for gaps, clipped attacks, idle noise, and A/V drift. Inspect retained logs for write failures and underruns.\n";
  report += "detail=failed=" + failed + " elapsed_ms=" +
    (JS2300.now() - started) + "\n";
  if (configState.indexOf("template") >= 0 ||
      configState.indexOf("Populate") >= 0) {
    var templateLines = TEMPLATE.split(/\r?\n/);
    for (var t = 0; t < templateLines.length; t++)
      if (templateLines[t])
        report += "detail=ini: " + templateLines[t] + "\n";
  }
  for (var i = 0; i < lines.length; i++) {
    var status = "FAIL";

    if (lines[i].slice(0, 4) === "PASS")
      status = "OK";
    else if (lines[i].slice(0, 4) === "WARN")
      status = "OK";
    report += "item|" + status + "|" + i + "|" + lines[i] + "\n";
  }
  JS2300.fs.writeText(REPORT, report);
  JS2300.log("audio_quality_matrix report=" + REPORT + " failed=" + failed);
}

function main() {
  var text = readText(CONFIG);
  var config;
  var gamePath;
  var videoPath;
  var durationS;
  var gameFrames;
  var modes;

  if (!text) {
    JS2300.fs.writeText(CONFIG, TEMPLATE);
    text = readText(CONFIG);
    log("CONFIG_CREATED path=" + CONFIG);
    if (!text) {
      log("FAIL config_write_unreadable path=" + CONFIG);
      failed++;
      writeReport("Configuration template could not be read back; create /media/mmcblk0/unifrog_data/config and add audio-quality-matrix.ini.");
    } else {
      failed++;
      writeReport("Configuration template created; populate game_path and video_path, then rerun.");
    }
    throw new Error("Audio quality matrix setup required");
  }

  config = parseConfig(text);
  gamePath = option(config, "game_path", "");
  videoPath = option(config, "video_path", "");
  if (!gamePath || !videoPath || gamePath.indexOf("REPLACE_ME") >= 0 ||
      videoPath.indexOf("REPLACE_ME") >= 0) {
    log("FAIL placeholder_paths config=" + CONFIG);
    failed++;
    writeReport("Populate game_path and video_path, then rerun.");
    throw new Error("Audio quality matrix paths are not configured");
  }

  durationS = numberOption(config, "duration_s", 15);
  gameFrames = durationS * numberOption(config, "game_fps", 60);
  modes = splitList(option(config, "video_modes",
    "native,ffmpeg_audio,ffmpeg_video"));

  runRouteDiagnostics(config);

  run("game_" + option(config, "game_core", "gpsp"),
    "run+core=" + option(config, "game_core", "gpsp") +
    ",audio=1,frames=" + gameFrames +
    ",scpu=" + option(config, "scpu", "918") +
    ",frameskip=" + option(config, "frameskip", "1") + ":" + gamePath);

  for (var i = 0; i < modes.length; i++)
    runVideoMode(config, modes[i], durationS * 1000, videoPath);

  writeReport("Completed game and video audio matrix on this board.");
  if (failed)
    throw new Error("Audio quality matrix failed runs=" + failed);
}

main();
