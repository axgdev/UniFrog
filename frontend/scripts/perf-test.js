var PERF_REPORT_PATH = "/media/mmcblk0/unifrog/perf-test-result.txt";
var perfLines = "";

function perfLog(text) {
  var line = "PERF " + text;
  perfLines += line + "\n";
  JS2300.log(line);
}

function perfHasFile(list, name) {
  var i;

  for (i = 0; i < list.length; i++) {
    if (!list[i].dir && list[i].name === name) return 1;
  }
  return 0;
}

function perfDrawFrame(frame) {
  var rects = [
    [0, 0, 320, 240, 0x0841],
    [0, 0, 320, 28, 0x1084],
    [0, 212, 320, 28, 0x1084],
    [18, 42, 284, 124, 0x1084],
    [26, 52, 118, 28, 0x18c6],
    [152, 52, 118, 28, 0x18c6],
    [26, 90, 244, 16, 0x39c7],
    [26, 114, 210, 16, 0x39c7],
    [26, 138, 176, 16, 0x39c7]
  ];

  rects[4][2] = 80 + frame * 3;
  rects[5][2] = 118 - frame * 2;
  JS2300.video.clear(0x0841);
  JS2300.video.rects(rects);
  JS2300.video.text(14, 10, "JS2300 performance", 0xffff);
  JS2300.video.text(30, 60, "Frame " + String(frame), 0xffff);
  JS2300.video.text(30, 94, "Draw rectangles", 0xffff);
  JS2300.video.text(30, 118, "Text labels", 0xffff);
  JS2300.video.text(30, 142, "Present", 0xffff);
  JS2300.video.text(14, 222, "Writing /unifrog/perf-test-result.txt", 0xbdf7);
  JS2300.video.present();
}

function perfDrawBench() {
  var start = JS2300.now();
  var i;

  for (i = 0; i < 12; i++) {
    perfDrawFrame(i);
  }
  perfLog("BENCH screen_draw_12_ms=" + String(JS2300.now() - start));
}

function perfListBench(path, label) {
  var start = JS2300.now();
  var list = JS2300.fs.list(path);

  perfLog("BENCH list_" + label + "_ms=" + String(JS2300.now() - start) +
    " count=" + String(list.length));
  return list;
}

function perfReadBench(path, label) {
  var start = JS2300.now();
  var text = JS2300.fs.readText(path);
  var size = text ? text.length : 0;

  perfLog("BENCH read_" + label + "_ms=" + String(JS2300.now() - start) +
    " bytes=" + String(size));
}

function perfLoadBench(path, label) {
  var start = JS2300.now();

  load(path);
  perfLog("BENCH load_" + label + "_ms=" + String(JS2300.now() - start));
}

function perfPackageChecks() {
  var root = perfListBench("/media/mmcblk0/unifrog", "unifrog");
  var scripts = perfListBench("/media/mmcblk0/unifrog/scripts", "scripts");

  perfLog("CHECK main_bytecode=" + String(perfHasFile(root, "main.js.mqbc")));
  perfLog("CHECK quick_menu=" + String(perfHasFile(root, "quick-menu.js")));
  perfLog("CHECK quick_menu_bytecode=" +
    String(perfHasFile(root, "quick-menu.js.mqbc")));
  perfLog("CHECK bytecode_manifest=" +
    String(perfHasFile(root, "bytecode-manifest.txt")));
  perfLog("CHECK perf_script=" + String(perfHasFile(scripts, "perf-test.js")));
  perfReadBench("/media/mmcblk0/unifrog/bytecode-manifest.txt", "bytecode_manifest");
}

var perfStart = JS2300.now();
perfLog("BEGIN");
perfLoadBench("app/theme.js", "theme");
perfLoadBench("app/constants.js", "constants");
perfLoadBench("app/catalog.js", "catalog");
perfPackageChecks();
perfDrawBench();
perfLog("SUMMARY total_ms=" + String(JS2300.now() - perfStart));
JS2300.fs.writeText(PERF_REPORT_PATH, perfLines);
JS2300.exit("perf test done");
