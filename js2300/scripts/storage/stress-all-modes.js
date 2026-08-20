// unifrog: mode=extension

var profiles = [
  "safe",
  "wide1",
  "wide2",
  "wide4",
  "wide8",
  "wide10",
  "wide12",
  "wide14",
  "wide16",
  "wide18",
  "wide20",
  "wide22",
  "wide24",
  "wide25",
  "wide50",
  "uhs12",
  "uhs25",
  "wide",
  "uhs",
  "wide37",
  "hs1"
];
var failures = 0;

function showStatus(title, line, note) {
  JS2300.video.clear(0x0000);
  JS2300.video.text(14, 18, title, 0xffff);
  JS2300.video.text(14, 52, line, 0xbdf7);
  JS2300.video.text(14, 206, note, 0x7bef);
  JS2300.video.present();
}

for (var i = 0; i < profiles.length; i++) {
  showStatus("All storage stress",
    (i + 1) + "/" + profiles.length + " " + profiles[i],
    "Risky modes may require a power cycle");
  var ret = JS2300.system.action("developer:storage_stress:" + profiles[i]);
  JS2300.log("storage all stress profile=" + profiles[i] + " ret=" + ret);
  if (ret <= 0)
    failures++;
  JS2300.sleep(250);
}

if (failures)
  throw new Error("storage failures=" + failures);
