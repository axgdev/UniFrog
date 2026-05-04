var BUTTONS = {
  up: "UP",
  down: "DOWN",
  left: "LEFT",
  right: "RIGHT",
  a: "A",
  b: "B",
  x: "X",
  y: "Y",
  start: "START",
  select: "SELECT"
};

var DEFAULT_ITEMS = [
  { id: "firmware", label: "Firmware", detail: "Boot another ASD image" },
  { id: "last", label: "Last Game", detail: "Resume the last game" },
  { id: "games", label: "Games", detail: "Browse games by system" },
  { id: "index", label: "Index games", detail: "Scan the SD card" },
  { id: "media", label: "Media", detail: "Indexed video files" },
  { id: "settings", label: "Settings", detail: "Display input audio power" },
  { id: "files", label: "File Browser", detail: "Open content from the SD card" }
];

function fallbackRuntime() {
  function log() {}
  return {
    log: log,
    flushLog: function() {},
    now: function() { return 0; },
    exit: function() {},
    video: {
      size: function() { return { width: 320, height: 240 }; },
      clear: function() {},
      rects: function() {},
      text: function() {},
      present: function() {}
    },
    input: {
      poll: function() { return 0; },
      pressed: function() { return false; },
      down: function() { return false; },
      repeated: function() { return false; }
    },
    system: {
      battery: function() { return { percent: null, charging: false }; }
    }
  };
}

function drawTopBar(rt, theme, title) {
  var battery = rt.system && rt.system.battery ? rt.system.battery() : {};
  var percent = typeof battery.percent === "number" ? String(battery.percent) + "%" : "--";

  rt.video.rects([
    [0, 0, 320, 24, theme.colors.panel],
    [0, 24, 320, 1, theme.colors.edge]
  ]);
  rt.video.text(10, 7, title, theme.text.title);
  rt.video.text(274, 7, percent, theme.text.muted);
}

function drawMenu(rt, theme, state) {
   var rows = [];
   var i;
   var item;
   var y;
   var selected;

  rt.video.clear(theme.colors.background);
  drawTopBar(rt, theme, "UniFrog");

   for (i = 0; i < state.items.length && i < 6; i += 1) {
      y = 34 + i * 31;
      selected = i === state.selected;
      rows.push([8, y, 304, 27, selected ? theme.colors.accent : theme.colors.row]);
      rows.push([8, y + 27, 304, 1, theme.colors.edge]);
  }

  rt.video.rects(rows);

   for (i = 0; i < state.items.length && i < 6; i += 1) {
      item = state.items[i];
    y = 34 + i * 31;
    selected = i === state.selected;
    rt.video.text(16, y + 5, item.label, selected ? theme.text.selected : theme.text.primary);
    rt.video.text(156, y + 5, item.detail, selected ? theme.text.selectedMuted : theme.text.muted);
  }

  rt.video.text(10, 226, "A open   B back   START menu", theme.text.footer);
  rt.video.present();
}

function activate(rt, state) {
  var item = state.items[state.selected];
  rt.log("unifrog frontend activate", item.id);
  rt.flushLog();
}

function createShell(options) {
  var rt;
  var theme;
  var state;

  options = options || {};
  rt = options.runtime || fallbackRuntime();
  theme = options.theme;
  state = {
    selected: 0,
    items: DEFAULT_ITEMS,
    running: true,
    lastNavAt: 0,
    exitChordAt: 0
  };

  function moveSelection(delta) {
    var now = rt.now();
    if (now - state.lastNavAt < 180) {
      return;
    }
    state.selected = (state.selected + state.items.length + delta) % state.items.length;
    state.lastNavAt = now;
  }

  function update() {
    var now = rt.now();
    var exitChord = rt.input.down(BUTTONS.select) && rt.input.down(BUTTONS.start);

    if (exitChord) {
      if (!state.exitChordAt) {
        state.exitChordAt = now;
      } else if (now - state.exitChordAt >= 300) {
        state.running = false;
      }
      return;
    }
    state.exitChordAt = 0;

    if (rt.input.repeated(BUTTONS.up, 320, 140)) {
      moveSelection(-1);
    }
    if (rt.input.repeated(BUTTONS.down, 320, 140)) {
      moveSelection(1);
    }
    if (rt.input.pressed(BUTTONS.a)) {
      activate(rt, state);
    }
  }

  function draw() {
    drawMenu(rt, theme, state);
  }

  function run() {
    rt.log("unifrog frontend start");
    while (state.running) {
      rt.input.poll();
      update();
      drawMenu(rt, theme, state);
    }
    rt.exit("frontend closed");
  }

  return { run: run, update: update, draw: draw, state: state };
}
