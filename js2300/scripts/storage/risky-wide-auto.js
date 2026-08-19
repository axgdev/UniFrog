// unifrog: mode=extension

JS2300.video.clear(0x0000);
JS2300.video.text(14, 18, "Risky storage stress", 0xffff);
JS2300.video.text(14, 52, "wide", 0xbdf7);
JS2300.video.text(14, 206, "May require a power cycle", 0x7bef);
JS2300.video.present();
var ret = JS2300.system.action("developer:storage_stress:wide");
JS2300.log("storage risky profile=wide ret=" + ret);
if (ret <= 0)
  throw new Error("wide failed");
