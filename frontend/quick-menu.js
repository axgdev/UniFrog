var BTN_SELECT = 1 << 10;
var BTN_START = 1 << 11;
var quickSelected = 0;
var quickStatus = JS2300.system.action("quick:status");
var quickSlot = JS2300.system.action("quick:state-slot");
var quickCpu = JS2300.system.action("quick:cpu");
var quickFastForward = JS2300.system.action("quick:fast-forward-status");
var quickFastForwardSpeed = JS2300.system.action("quick:fast-forward-speed");
var quickFrameskip = JS2300.system.action("quick:frameskip");
var quickToastText = "";
var quickToastUntil = 0;
var quickItems = [
  "Resume",
  "Save state",
  "Load state",
  "Save slot",
  "CPU",
  "Fast forward",
  "FF speed",
  "Frameskip",
  "Audio",
  "Display",
  "Backlight",
  "Return to menu"
];

function quickAudio() {
  return quickStatus % 10;
}

function quickDisplay() {
  return Math.floor(quickStatus / 10) % 10;
}

function quickBacklight() {
  return Math.floor(quickStatus / 100);
}

function quickDisplayLabel() {
  var mode = quickDisplay();
  if (mode === 1) return "Stretch";
  if (mode === 2) return "Original";
  return "Fit";
}

function quickCpuLabel() {
  if (quickCpu > 0) return String(quickCpu) + " MHz";
  return "Default";
}

function quickFrameskipLabel() {
  if (quickFrameskip === 1) return "Auto";
  if (quickFrameskip === 2) return "1";
  if (quickFrameskip === 3) return "2";
  return "Off";
}

function quickDetail(index) {
  if (index === 0) return "B";
  if (index === 1) return "R slot " + String(quickSlot);
  if (index === 2) return "L slot " + String(quickSlot);
  if (index === 3) return String(quickSlot);
  if (index === 4) return quickCpuLabel();
  if (index === 5) return quickFastForward ? "On" : "Off";
  if (index === 6) return String(quickFastForwardSpeed) + "x";
  if (index === 7) return quickFrameskipLabel();
  if (index === 8) return quickAudio() ? "On" : "Off";
  if (index === 9) return quickDisplayLabel();
  if (index === 10) return String(quickBacklight()) + "%";
  if (index === 11) return "X";
  return "";
}

function quickToast(text) {
  quickToastText = text;
  quickToastUntil = JS2300.now() + 900;
}

function quickDraw() {
  var i;
  var y;
  var rowColor;
  var textColor;
  var detailColor;
  var rects = [
    [0, 0, 320, 240, 0x0841],
    [0, 0, 320, 28, 0x1084],
    [0, 216, 320, 24, 0x1084],
    [22, 35, 276, 176, 0x1084],
    [22, 35, 276, 1, 0x39c7],
    [22, 210, 276, 1, 0x39c7]
  ];

  JS2300.video.clear(0x0841);
  JS2300.video.rects(rects);
  JS2300.video.text(14, 10, "Quick menu", 0xffff);
  JS2300.video.text(184, 10, "SELECT+START", 0xbdf7);

  for (i = 0; i < quickItems.length; i++) {
    y = 42 + i * 14;
    if (i === quickSelected) {
      rowColor = 0xfda0;
      textColor = 0x0841;
      detailColor = 0x2945;
      JS2300.video.rects([[32, y - 3, 256, 13, rowColor]]);
    } else {
      textColor = 0xffff;
      detailColor = 0xad55;
    }
    JS2300.video.text(42, y, quickItems[i], textColor);
    JS2300.video.text(202, y, quickDetail(i), detailColor);
  }

  if (quickToastText && JS2300.now() < quickToastUntil)
    JS2300.video.text(22, 205, quickToastText, 0xfda0);
  JS2300.video.text(12, 224, "L load  R save  X menu  B resume", 0xbdf7);
  JS2300.video.present();
}

function quickResume() {
  JS2300.system.action("quick:resume");
  JS2300.exit("quick resume");
}

function quickReturnToMenu() {
  JS2300.system.action("quick:return");
  JS2300.exit("quick return");
}

function quickCycleSlot(delta) {
  var ret;
  if (delta < 0) ret = JS2300.system.action("quick:state-slot-prev");
  else ret = JS2300.system.action("quick:state-slot-next");
  if (ret >= 0) {
    quickSlot = ret;
    quickToast("Slot " + String(quickSlot));
  } else {
    quickToast("Slot failed");
  }
}

function quickSaveState() {
  var ret = JS2300.system.action("quick:save-state");
  if (ret >= 0) {
    quickSlot = ret;
    quickToast("Saved slot " + String(quickSlot));
  } else {
    quickToast("Save failed");
  }
}

function quickLoadState() {
  var ret = JS2300.system.action("quick:load-state");
  if (ret >= 0) {
    quickSlot = ret;
    quickToast("Loaded slot " + String(quickSlot));
  } else {
    quickToast("Load failed");
  }
}

function quickCycleCpu(delta) {
  var ret;
  if (delta < 0) ret = JS2300.system.action("quick:cpu-prev");
  else ret = JS2300.system.action("quick:cpu-next");
  if (ret > 0) {
    quickCpu = ret;
    quickToast("CPU " + quickCpuLabel());
  } else {
    quickToast("CPU failed");
  }
}

function quickActivate() {
  if (quickSelected === 0) {
    quickResume();
    return 1;
  }
  if (quickSelected === 1) {
    quickSaveState();
    return 0;
  }
  if (quickSelected === 2) {
    quickLoadState();
    return 0;
  }
  if (quickSelected === 3) {
    quickCycleSlot(1);
    return 0;
  }
  if (quickSelected === 4) {
    quickCycleCpu(1);
    return 0;
  }
  if (quickSelected === 5) {
    quickFastForward = JS2300.system.action("quick:fast-forward");
    quickToast(quickFastForward ? "Fast forward on" : "Fast forward off");
    return 0;
  }
  if (quickSelected === 6) {
    quickFastForwardSpeed = JS2300.system.action("quick:fast-forward-speed-next");
    quickToast("FF speed " + String(quickFastForwardSpeed) + "x");
    return 0;
  }
  if (quickSelected === 7) {
    quickFrameskip = JS2300.system.action("quick:frameskip-next");
    quickToast("Frameskip " + quickFrameskipLabel());
    return 0;
  }
  if (quickSelected === 8) {
    quickStatus = JS2300.system.action("quick:audio");
    quickToast(quickAudio() ? "Audio on" : "Audio off");
    return 0;
  }
  if (quickSelected === 9) {
    quickStatus = JS2300.system.action("quick:display");
    quickToast("Display " + quickDisplayLabel());
    return 0;
  }
  if (quickSelected === 10) {
    quickStatus = JS2300.system.action("quick:backlight");
    quickToast("Backlight " + String(quickBacklight()) + "%");
    return 0;
  }
  if (quickSelected === 11) {
    quickReturnToMenu();
    return 1;
  }
  return 0;
}

function quickAdjust(delta) {
  if (quickSelected === 3) quickCycleSlot(delta);
  else if (quickSelected === 4) quickCycleCpu(delta);
  else if (quickSelected === 6) {
    if (delta < 0)
      quickFastForwardSpeed = JS2300.system.action("quick:fast-forward-speed-prev");
    else
      quickFastForwardSpeed = JS2300.system.action("quick:fast-forward-speed-next");
    quickToast("FF speed " + String(quickFastForwardSpeed) + "x");
  } else if (quickSelected === 7) {
    if (delta < 0)
      quickFrameskip = JS2300.system.action("quick:frameskip-prev");
    else
      quickFrameskip = JS2300.system.action("quick:frameskip-next");
    quickToast("Frameskip " + quickFrameskipLabel());
  }
}

function quickWaitRelease() {
  var start = JS2300.now();
  var mask = JS2300.input.poll();

  while ((JS2300.now() - start) < 160 ||
      ((mask & BTN_SELECT) && (mask & BTN_START))) {
    quickDraw();
    JS2300.sleep(16);
    mask = JS2300.input.poll();
  }
}

quickDraw();
quickWaitRelease();
for (;;) {
  JS2300.input.poll();
  if (JS2300.input.repeated("UP", 240, 85)) {
    quickSelected--;
    if (quickSelected < 0) quickSelected = quickItems.length - 1;
  }
  if (JS2300.input.repeated("DOWN", 240, 85)) {
    quickSelected++;
    if (quickSelected >= quickItems.length) quickSelected = 0;
  }
  if (JS2300.input.pressed("LEFT")) quickAdjust(-1);
  if (JS2300.input.pressed("RIGHT")) quickAdjust(1);
  if (JS2300.input.pressed("A") || JS2300.input.pressed("START")) {
    if (quickActivate()) break;
  }
  if (JS2300.input.pressed("L")) quickLoadState();
  if (JS2300.input.pressed("R")) quickSaveState();
  if (JS2300.input.pressed("B")) {
    quickResume();
    break;
  }
  if (JS2300.input.pressed("X")) {
    quickReturnToMenu();
    break;
  }
  quickDraw();
  JS2300.sleep(16);
}
