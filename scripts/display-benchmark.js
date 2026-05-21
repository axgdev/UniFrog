// Display benchmark runner.

var reportPath = "/media/mmcblk0/unifrog_data/display-benchmark.txt";
var start = JS2300.now();
var ret;
var report = "";

try {
  JS2300.system.action("toast:Display benchmark starting");
} catch (e) {}

ret = JS2300.system.action("developer:display_benchmark");

try {
  report = JS2300.fs.readText(reportPath);
} catch (readErr) {
  report = "show=1\n" +
    "title=DISPLAY BENCHMARK\n" +
    "detail=Report read failed\n" +
    "error=" + readErr + "\n";
}

try {
  JS2300.system.action("toast:Display benchmark " +
    (ret > 0 ? "done" : "failed"));
} catch (e2) {}

try {
  JS2300.fs.writeText("/media/mmcblk0/unifrog_data/display-benchmark-last.txt",
    report + "runner_ms=" + (JS2300.now() - start) + "\n" +
    "runner_ret=" + ret + "\n");
} catch (writeErr) {}
