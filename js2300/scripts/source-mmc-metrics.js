// unifrog: mode=extension
/*
 * Quantitative source-vs-vendor MMC host comparison.
 *
 * The source host now boots and passes the short checks, so this script keeps
 * the runtime swap controls unchanged and turns the existing diagnostic actions
 * into a repeatable report.  It deliberately records the raw child reports and
 * parsed summary numbers because future driver changes need objective evidence
 * for throughput, CRC correctness, request errors, and vendor recovery.
 */

var started = JS2300.now();
/* Keep reports with the other collected diagnostics so host log imports include it. */
var REPORT = "/media/mmcblk0/unifrog_data/logs/reports/source-mmc-metrics.txt";
var QUICK = "/media/mmcblk0/unifrog_data/logs/reports/storage-quick-benchmark.txt";
var QUICK_ALT = "/media/mmcblk0/unifrog_data/reports/storage-quick-benchmark.txt";
var MODES = "/media/mmcblk0/unifrog_data/logs/reports/storage-quick-modes.txt";
var MATRIX = "/media/mmcblk0/unifrog_data/logs/reports/storage-speed-matrix.txt";
var lines = [];
var failures = [];

function elapsed() {
  return JS2300.now() - started;
}

function emit(kind, name, summary, detail) {
  var line = kind + "|" + name + "|" + summary;
  if (detail)
    line += "|" + detail;
  lines.push(line);
  JS2300.log("source_mmc_metrics " + name + " " + summary +
    (detail ? " " + detail : ""));
}

function fail(name, detail) {
  failures.push(name + (detail ? " " + detail : ""));
  emit("item", name, "FAIL", detail || "");
}

function statusName(code) {
  if (code === 0) return "vendor";
  if (code === 1) return "source";
  if (code === 2) return "diag_vendor";
  if (code === 3) return "diag_source";
  return "unknown(" + code + ")";
}

function action(name, id) {
  var before = JS2300.now();
  var ret = JS2300.system.action(id);
  emit("item", name, ret > 0 || ret === 0 ? "ret=" + ret : "FAIL ret=" + ret,
    "action=" + id + " ms=" + (JS2300.now() - before));
  if (ret < 0)
    failures.push(name + " ret=" + ret);
  return ret;
}

function readFirst(paths) {
  for (var i = 0; i < paths.length; i++) {
    var text = JS2300.fs.readText(paths[i]);
    if (text)
      return { path: paths[i], text: text };
  }
  return { path: "", text: "" };
}

function matchNumber(text, pattern) {
  var m = text.match(pattern);
  if (!m)
    return "";
  return m[1];
}

function contains(text, needle) {
  return text.indexOf(needle) >= 0;
}

function parseQuick(name) {
  var report = readFirst([QUICK, QUICK_ALT]);
  if (!report.text) {
    fail(name + "_report", "missing quick benchmark report");
    return;
  }

  var read = matchNumber(report.text, /Source read\|([0-9]+) KiB\/s/);
  var write = matchNumber(report.text, /Synced temp write\|([0-9]+) KiB\/s/);
  var readback = matchNumber(report.text, /Temp readback\|([0-9]+) KiB\/s/);
  var crcMismatch = contains(report.text, "expected_crc=") &&
    !report.text.match(/item\|OK\|Temp readback\|/);

  emit("metric", name + "_quick",
    "read_kib_s=" + read + " write_kib_s=" + write +
    " readback_kib_s=" + readback,
    "path=" + report.path + " crc_mismatch=" + (crcMismatch ? 1 : 0));
  if (crcMismatch)
    failures.push(name + "_quick crc_mismatch");
}

function parseModes(name) {
  var report = readFirst([MODES]);
  if (!report.text) {
    fail(name + "_modes_report", "missing quick modes report");
    return;
  }

  var failuresText = matchNumber(report.text, /quick_modes failures=([0-9]+)/);
  var first = matchNumber(report.text, /first_failed=([^ ]+)/);
  var crcMismatch = contains(report.text, "storage_quick_modes_crc_mismatch") ||
    contains(report.text, "crc_mismatch");
  emit("metric", name + "_modes",
    "failures=" + failuresText + " first_failed=" + first,
    "crc_mismatch=" + (crcMismatch ? 1 : 0) + " path=" + report.path);
  if (failuresText !== "0" || crcMismatch)
    failures.push(name + "_modes failures=" + failuresText +
      " crc_mismatch=" + (crcMismatch ? 1 : 0));
}

function parseMatrix(name) {
  var report = readFirst([MATRIX]);
  if (!report.text) {
    emit("item", name + "_matrix_report", "missing", "optional=1");
    return;
  }

  var failuresText = matchNumber(report.text, /failures=([0-9]+)/);
  var safe = matchNumber(report.text, /safe=([0-9-]+)/);
  emit("metric", name + "_matrix",
    "failures=" + failuresText + " safe=" + safe,
    "path=" + report.path);
  if (failuresText !== "" && failuresText !== "0")
    failures.push(name + "_matrix failures=" + failuresText);
}

function runQuickSet(name) {
  var ret = action(name + "_quick", "developer:storage_quick_benchmark");
  if (ret <= 0)
    failures.push(name + "_quick ret=" + ret);
  parseQuick(name);
}

function runMeasuredDriver(name, target, expectVendor) {
  var ret = action(name + "_switch", "storage:swap_driver:" + target);
  if (ret < 0 && expectVendor) {
    emit("item", name + "_unavailable", "skip", "target=" + target);
    return false;
  }
  if (ret < 0) {
    fail(name + "_switch_required", "target=" + target + " ret=" + ret);
    return false;
  }

  JS2300.sleep(300);
  ret = action(name + "_diag", "storage:swap_driver:diagnose");
  if (ret < 0) {
    fail(name + "_diag", "ret=" + ret);
    return false;
  }
  JS2300.sleep(200);
  runQuickSet(name + "_diag");
  action(name + "_dump", "storage:swap_driver:dump");

  ret = action(name + "_diag_exit", "storage:swap_driver:" + target);
  if (ret < 0)
    fail(name + "_diag_exit", "ret=" + ret);
  JS2300.sleep(300);
  runQuickSet(name);
  return true;
}

emit("item", "start", "ok", "elapsed_ms=0");
var initial = action("status_initial", "storage:swap_driver:status");
emit("metric", "initial_driver", statusName(initial), "code=" + initial);

var vendorRan = false;
if (initial !== 1)
  vendorRan = runMeasuredDriver("vendor", "vendor", true);
else
  emit("item", "vendor_baseline", "skip", "source_default_build=1");

var sourceRan = runMeasuredDriver("source", "source", false);
if (sourceRan) {
  var modesRet = action("source_modes", "developer:storage_quick_modes");
  if (modesRet <= 0)
    failures.push("source_modes ret=" + modesRet);
  parseModes("source");

  var matrixRet = action("source_matrix", "developer:storage_speed_matrix");
  if (matrixRet <= 0)
    failures.push("source_matrix ret=" + matrixRet);
  parseMatrix("source");
}

if (vendorRan) {
  action("vendor_recovery_switch", "storage:swap_driver:vendor");
  JS2300.sleep(500);
  runQuickSet("vendor_recovery");
} else if (initial === 1) {
  action("final_source", "storage:swap_driver:source");
}

var finalStatus = action("status_final", "storage:swap_driver:status");
emit("metric", "final_driver", statusName(finalStatus), "code=" + finalStatus);

var text = "show=1\n";
text += "title=Source MMC Metrics\n";
text += "detail=Vendor/source storage comparison with parsed throughput, CRC, mode-sweep, and diagnostic pass/fail signals.\n";
text += "detail=elapsed_ms=" + elapsed() + " failures=" + failures.length + "\n";
for (var i = 0; i < lines.length; i++)
  text += lines[i] + "\n";
if (failures.length)
  text += "detail=failed=" + failures.join("; ") + "\n";
JS2300.fs.writeText(REPORT, text);
JS2300.log("source_mmc_metrics report=" + REPORT + " failures=" + failures.length);

if (failures.length)
  throw new Error("source MMC metrics failed: " + failures.join("; "));
