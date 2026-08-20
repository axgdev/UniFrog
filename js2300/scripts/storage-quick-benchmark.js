var REPORT = "/media/mmcblk0/unifrog_data/logs/reports/storage-quick-benchmark.txt";

function main() {
  var start = JS2300.now();
  var ret = JS2300.system.action("developer:storage_quick_benchmark");
  var report = "";

  try {
    report = JS2300.fs.readText(REPORT);
  } catch (e) {
    report = "show=1\n" +
      "title=STORAGE QUICK BENCH\n" +
      "item|FAIL|Report|Unable to read " + REPORT + "|" + e + "\n";
  }
  JS2300.log("storage-quick-benchmark ret=" + ret +
    " ms=" + (JS2300.now() - start) + " report_bytes=" + report.length);
  return ret > 0 ? 0 : -1;
}

main();
