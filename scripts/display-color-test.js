// Display color-format comparison runner.

var reportPath = "/media/mmcblk0/unifrog_data/logs/reports/display-color-test.txt";
var start = JS2300.now();
var ret;
var report = "";

try {
  JS2300.system.action("toast:Display color test starting");
} catch (e) {}

ret = JS2300.system.action("developer:display_color_test");

try {
  report = JS2300.fs.readText(reportPath);
} catch (readErr) {
  report = "show=1\n" +
    "title=DISPLAY COLOR TEST\n" +
    "detail=Report read failed\n" +
    "error=" + readErr + "\n";
}

try {
  JS2300.system.action("toast:Display color test " +
    (ret > 0 ? "done" : "failed"));
} catch (e2) {}

try {
  JS2300.fs.writeText("/media/mmcblk0/unifrog_data/logs/reports/display-color-test-last.txt",
    report + "runner_ms=" + (JS2300.now() - start) + "\n" +
    "runner_ret=" + ret + "\n");
} catch (writeErr) {}
