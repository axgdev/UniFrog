var SMOKE_REPORT_PATH = "/media/mmcblk0/unifrog/smoke-test-result.txt";
var smokeLines = "";
var smokePass = 0;
var smokeFail = 0;

function smokeLog(text) {
  var line = "SMOKE " + text;
  smokeLines += line + "\n";
  JS2300.log(line);
}

function smokeCheck(name, ok, detail) {
  if (ok) smokePass++;
  else smokeFail++;
  smokeLog((ok ? "PASS " : "FAIL ") + name + (detail ? " " + detail : ""));
}

function smokeList(path) {
  var start = JS2300.now();
  var list = JS2300.fs.list(path);
  smokeLog("LIST path=" + path + " count=" + String(list.length) +
    " ms=" + String(JS2300.now() - start));
  return list;
}

function smokeHasFile(list, name) {
  var i;
  for (i = 0; i < list.length; i++) {
    if (!list[i].dir && list[i].name === name)
      return true;
  }
  return false;
}

function smokeCountLines(text) {
  var count = 0;
  var i;
  if (!text) return 0;
  for (i = 0; i < text.length; i++) {
    if (text.charCodeAt(i) === 10)
      count++;
  }
  if (text.length > 0 && text.charCodeAt(text.length - 1) !== 10)
    count++;
  return count;
}

function smokeDraw() {
  var start = JS2300.now();
  var size = JS2300.video.size();
  var imageRet = -2;
  JS2300.video.clear(0x0841);
  JS2300.video.rects([
    [8, 28, 304, 22, 0xfda0],
    [8, 58, 148, 28, 0x18c6],
    [164, 58, 148, 28, 0x56b5],
    [8, 94, 304, 72, 0x1084],
    [8, 206, 304, 1, 0x39c7]
  ]);
  if (JS2300.video.image)
    imageRet = JS2300.video.image(
      "/media/mmcblk0/unifrog/themes/system-icons/icons/gba.png",
      264, 102, 36, 36);
  JS2300.video.text(16, 35, "UniFrog smoke test", 0x0841);
  JS2300.video.text(18, 68, String(size.width) + "x" + String(size.height), 0xffff);
  JS2300.video.text(18, 106, "Video, text, rects, present", 0xffff);
  JS2300.video.text(18, 224, "See log.txt for results", 0xbdf7);
  JS2300.video.present();
  smokeLog("BENCH video_draw_ms=" + String(JS2300.now() - start));
  smokeCheck("video.size", size.width === 320 && size.height === 240,
    String(size.width) + "x" + String(size.height));
  smokeCheck("video.image", imageRet === 0, "ret=" + String(imageRet));
}

function smokeInputWindow() {
  var start = JS2300.now();
  var polls = 0;
  var nonzero = 0;
  var last = 0;
  while (JS2300.now() - start < 500) {
    last = JS2300.input.poll();
    if (last !== 0)
      nonzero++;
    polls++;
    JS2300.sleep(16);
  }
  smokeLog("BENCH input_window_ms=" + String(JS2300.now() - start) +
    " polls=" + String(polls) + " nonzero=" + String(nonzero) +
    " last=0x" + String(last));
  smokeCheck("input.poll", polls > 10, "polls=" + String(polls));
}

function smokeSleep() {
  var start = JS2300.now();
  var elapsed;
  JS2300.sleep(50);
  elapsed = JS2300.now() - start;
  smokeLog("BENCH sleep_50_ms=" + String(elapsed));
  smokeCheck("clock.sleep", elapsed >= 40 && elapsed <= 120,
    "elapsed=" + String(elapsed));
}

function smokeCoreFiles() {
  var cores = smokeList("/media/mmcblk0/unifrog/cores");
  var expected = [
    "js2300.bin", "gambatte.bin", "gpsp.bin", "picodrive.bin",
    "snes9x2005.bin", "snes9x2002.bin", "quicknes.bin", "fceumm.bin",
    "gearboy.bin", "pce-fast.bin", "qpsx.bin", "pmp-video.bin"
  ];
  var i;
  for (i = 0; i < expected.length; i++)
    smokeCheck("core_file." + expected[i], smokeHasFile(cores, expected[i]), "");
}

function smokeFiles() {
  var root = smokeList("/media/mmcblk0");
  var unifrog = smokeList("/media/mmcblk0/unifrog");
  var scripts = smokeList("/media/mmcblk0/unifrog/scripts");
  var themes = smokeList("/media/mmcblk0/unifrog/themes");
  var icons = smokeList("/media/mmcblk0/unifrog/themes/system-icons/icons");
  var settings = JS2300.fs.readText("/media/mmcblk0/unifrog/settings.ini");
  var theme = JS2300.fs.readText("/media/mmcblk0/unifrog/themes/default.ini");
  var gameIndex = JS2300.fs.readText("/media/mmcblk0/unifrog/game-index.txt");
  var mediaIndex = JS2300.fs.readText("/media/mmcblk0/unifrog/media-index.txt");
  smokeCheck("sd_root", root.length > 0, "count=" + String(root.length));
  smokeCheck("unifrog_main", smokeHasFile(unifrog, "main.js"), "");
  smokeCheck("script_packaged", smokeHasFile(scripts, "smoke-test.js"), "");
  smokeCheck("theme_default", smokeHasFile(themes, "default.ini"), "");
  smokeCheck("theme_icon_gba", smokeHasFile(icons, "gba.png"), "");
  smokeCheck("settings_read", settings !== null, "");
  smokeCheck("theme_read", theme !== null, "");
  smokeLog("INDEX games_lines=" + String(smokeCountLines(gameIndex)) +
    " media_lines=" + String(smokeCountLines(mediaIndex)));
}

function smokeBacklightBattery() {
  var battery = JS2300.system.battery();
  var brightness = JS2300.system.backlight();
  var restore = brightness;
  smokeLog("BATTERY percent=" + String(battery.percent) +
    " low=" + String(battery.low));
  smokeLog("BACKLIGHT current=" + String(brightness));
  smokeCheck("backlight_get", brightness >= 0 && brightness <= 100, "");
  if (brightness >= 0) {
    smokeCheck("backlight_set_same",
      JS2300.system.backlight(restore) === restore,
      "level=" + String(restore));
  }
}

function smokeWriteReport() {
  var text = smokeLines + "SMOKE SUMMARY pass=" + String(smokePass) +
    " fail=" + String(smokeFail) + "\n";
  var ret = JS2300.fs.writeText(SMOKE_REPORT_PATH, text);
  smokeCheck("report_write", ret === 0, "ret=" + String(ret));
}

var smokeStart = JS2300.now();
smokeLog("BEGIN");
smokeDraw();
smokeSleep();
smokeFiles();
smokeCoreFiles();
smokeBacklightBattery();
smokeInputWindow();
smokeLog("SUMMARY pass=" + String(smokePass) + " fail=" + String(smokeFail) +
  " total_ms=" + String(JS2300.now() - smokeStart));
smokeWriteReport();
JS2300.flushLog();
JS2300.exit("smoke test done");
