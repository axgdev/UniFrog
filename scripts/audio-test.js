// GB300 audio diagnostics runner.

var reportPath = "/media/mmcblk0/unifrog_data/logs/reports/audio-test-last.txt";
var start = JS2300.now();
var ret;
var report;

try {
  JS2300.system.action("toast:Audio test starting");
} catch (toastStartErr) {}

ret = JS2300.system.action("developer:audio_test");

report = "show=1\n" +
  "title=AUDIO TEST\n" +
  "detail=Listen for GB300 route tones\n" +
  "runner_ms=" + (JS2300.now() - start) + "\n" +
  "runner_ret=" + ret + "\n";

try {
  JS2300.log("audio-test ret=" + ret + " ms=" +
    (JS2300.now() - start));
} catch (logErr) {}

try {
  JS2300.fs.writeText(reportPath, report);
} catch (writeErr) {}

try {
  JS2300.system.action("toast:Audio test " +
    (ret > 0 ? "done" : "failed"));
} catch (toastDoneErr) {}

ret > 0 ? 0 : -1;
