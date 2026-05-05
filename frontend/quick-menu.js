var BTN_SELECT = 1 << 10;
var BTN_START = 1 << 11;
var quickSelected = 0;
var quickStatus = JS2300.system.action("quick:status");
var quickToastText = "";
var quickToastUntil = 0;
var quickItems = [
  "Resume",
  "Return to menu",
  "Audio",
  "Display",
  "Backlight"
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

function quickDetail(index) {
  if (index === 0) return "Continue playing";
  if (index === 1) return "Close core";
  if (index === 2) return quickAudio() ? "On" : "Off";
  if (index === 3) return quickDisplayLabel();
  if (index === 4) return String(quickBacklight()) + "%";
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
    [0, 212, 320, 28, 0x1084],
    [26, 44, 268, 132, 0x1084],
    [26, 44, 268, 1, 0x39c7],
    [26, 175, 268, 1, 0x39c7]
  ];

  JS2300.video.clear(0x0841);
  JS2300.video.rects(rects);
  JS2300.video.text(14, 10, "Quick menu", 0xffff);
  JS2300.video.text(184, 10, "SELECT+START", 0xbdf7);

  for (i = 0; i < quickItems.length; i++) {
    y = 54 + i * 23;
    if (i === quickSelected) {
      rowColor = 0xfda0;
      textColor = 0x0841;
      detailColor = 0x2945;
      JS2300.video.rects([[34, y - 5, 252, 19, rowColor]]);
    } else {
      textColor = 0xffff;
      detailColor = 0xad55;
    }
    JS2300.video.text(44, y, quickItems[i], textColor);
    JS2300.video.text(198, y, quickDetail(i), detailColor);
  }

  if (quickToastText && JS2300.now() < quickToastUntil) {
    JS2300.video.text(22, 194, quickToastText, 0xfda0);
  }
  JS2300.video.text(14, 222, "A select  B resume  X menu", 0xbdf7);
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

function quickActivate() {
  if (quickSelected === 0) {
    quickResume();
    return 1;
  }
  if (quickSelected === 1) {
    quickReturnToMenu();
    return 1;
  }
  if (quickSelected === 2) {
    quickStatus = JS2300.system.action("quick:audio");
    quickToast(quickAudio() ? "Audio on" : "Audio off");
    return 0;
  }
  if (quickSelected === 3) {
    quickStatus = JS2300.system.action("quick:display");
    quickToast("Display " + quickDisplayLabel());
    return 0;
  }
  if (quickSelected === 4) {
    quickStatus = JS2300.system.action("quick:backlight");
    quickToast("Backlight " + String(quickBacklight()) + "%");
    return 0;
  }
  return 0;
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
  if (JS2300.input.pressed("A") || JS2300.input.pressed("START")) {
    if (quickActivate()) break;
  }
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
