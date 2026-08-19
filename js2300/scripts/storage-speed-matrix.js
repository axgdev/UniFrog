// unifrog: mode=extension

function showStatus(line, note) {
  JS2300.video.clear(0x0000);
  JS2300.video.text(14, 18, "Storage speed matrix", 0xffff);
  JS2300.video.text(14, 52, line, 0xbdf7);
  JS2300.video.text(14, 206, note, 0x7bef);
  JS2300.video.present();
}

showStatus("Running quick all-mode sweep", "Report: /unifrog_data/reports/storage-speed-matrix.txt");
var ret = JS2300.system.action("developer:storage_speed_matrix");
JS2300.log("storage speed matrix ret=" + ret);
showStatus(ret > 0 ? "Complete" : "Failed or stopped",
  "Power-cycle only if the screen stops changing");
if (ret <= 0)
  throw new Error("storage speed matrix failed");
