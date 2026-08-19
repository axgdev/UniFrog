var REPORT = "/media/mmcblk0/unifrog_data/logs/reports/storage-quick-modes.txt";

function main() {
  var start = JS2300.now();
  var ret = JS2300.system.action("developer:storage_quick_modes");
  var report = "";

  try {
    report = JS2300.fs.readText(REPORT);
  } catch (readErr) {
    report = "show=1\n" +
      "title=STORAGE QUICK MODES\n" +
      "item|FAIL|Report|Unable to read " + REPORT + "|" + readErr + "\n";
  }
  if (report === null || report === undefined)
    report = "";
  JS2300.log("storage-quick-modes ret=" + ret +
    " ms=" + (JS2300.now() - start) + " report_bytes=" + report.length);
  return ret > 0 ? 0 : -1;
}

main();
