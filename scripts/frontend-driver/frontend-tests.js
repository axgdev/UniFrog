// unifrog: mode=extension

FD.frontendTests = [
  { name: "storage_recover", run: function() {
    var start = JS2300.now();
    var ret = JS2300.system.action("storage:recover");
    FD.append((ret > 0 ? "PASS " : "FAIL ") + "storage_recover ret=" + ret +
      " ms=" + (JS2300.now() - start));
    return ret > 0 ? 0 : -1;
  }},
  { name: "fs_rw_report_dir", run: function() {
    var path = FD.LOG_DIR + "/fs-rw-test.txt";
    var text = "frontend-driver fs rw " + JS2300.now() + "\n";
    JS2300.fs.writeText(path, text);
    var read = JS2300.fs.readText(path);
    var ok = read === text;
    FD.append((ok ? "PASS " : "FAIL ") + "fs_rw_report_dir path=" + path +
      " bytes=" + read.length);
    return ok ? 0 : -1;
  }},
  { name: "fs_list_required_dirs", run: function() {
    var dirs = [
      "/media/mmcblk0/unifrog",
      "/media/mmcblk0/unifrog_data",
      "/media/mmcblk0/unifrog_data/scripts",
      "/media/mmcblk0/unifrog_data/logs",
      "/media/mmcblk0/unifrog_data/screenshots"
    ];
    var ok = true;
    for (var i = 0; i < dirs.length; i++) {
      var entries = JS2300.fs.list(dirs[i]);
      FD.append("INFO fs_list path=" + dirs[i] + " entries=" +
        entries.length);
      if (!entries || entries.length < 0)
        ok = false;
    }
    FD.append((ok ? "PASS " : "FAIL ") + "fs_list_required_dirs count=" +
      dirs.length);
    return ok ? 0 : -1;
  }},
  { name: "index_roms", run: function() {
    var start = JS2300.now();
    var result = JS2300.fs.index(FD.ROMS,
      FD.LOG_DIR + "/games.tsv", FD.LOG_DIR + "/media.tsv");
    var ok = result && result.ok === true && result.games > 0;
    FD.append((ok ? "PASS " : "FAIL ") + "index_roms result=" +
      JSON.stringify(result) + " wall_ms=" + (JS2300.now() - start));
    return ok ? 0 : -1;
  }},
  { name: "battery", run: function() {
    var status = JS2300.system.battery();
    var ok = status && status.percent >= -1 && status.percent <= 100;
    FD.append((ok ? "PASS " : "FAIL ") + "battery result=" +
      JSON.stringify(status));
    return ok ? 0 : -1;
  }},
  { name: "toast", run: function() {
    var ret = JS2300.system.action("toast:Frontend driver toast OK");
    FD.append((ret > 0 ? "PASS " : "FAIL ") + "toast ret=" + ret);
    return ret > 0 ? 0 : -1;
  }},
  { name: "backlight_sweep", run: function() {
    var original = 50;
    var readOk = true;
    var levels = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
    var details = [];
    try {
      original = JS2300.system.backlight();
    } catch (e) {
      readOk = false;
    }
    for (var i = 0; i < levels.length; i++) {
      var ret = JS2300.system.backlight(levels[i]);
      var current = JS2300.system.backlight();
      details.push(levels[i] + ":" + ret + "/" + current);
      if (ret !== levels[i] || current !== levels[i]) {
        try { JS2300.system.backlight(original); } catch (restoreErr) {}
        FD.append("FAIL backlight_sweep read_ok=" + readOk +
          " original=" + original + " details=" + details.join(","));
        return -1;
      }
      JS2300.sleep(80);
    }
    try { JS2300.system.backlight(original); } catch (restoreErr2) {}
    FD.append("PASS backlight_sweep read_ok=" + readOk +
      " original=" + original + " levels=" + levels.join(",") +
      " details=" + details.join(","));
    return 0;
  }},
  { name: "av_output_modes", run: function() {
    var original = 0;
    var modes = [0, 1];
    var details = [];
    try { original = JS2300.system.avOutput(); } catch (e) {}
    for (var i = 0; i < modes.length; i++) {
      var ret = JS2300.system.avOutput(modes[i]);
      var current = JS2300.system.avOutput();
      details.push(modes[i] + ":" + ret + "/" + current);
      if (ret !== modes[i] || current !== modes[i]) {
        try { JS2300.system.avOutput(original); } catch (restoreErr) {}
        FD.append("FAIL av_output_modes original=" + original +
          " details=" + details.join(","));
        return -1;
      }
      JS2300.sleep(80);
    }
    try { JS2300.system.avOutput(original); } catch (restoreErr2) {}
    FD.append("PASS av_output_modes original=" + original +
      " modes=" + modes.join(",") + " details=" + details.join(","));
    return 0;
  }},
  { name: "language_packs", run: function() {
    var languages = ["espanol", "francais", "deutsch", "italiano"];
    var ok = true;
    var details = [];
    for (var i = 0; i < languages.length; i++) {
      var path = "/media/mmcblk0/unifrog_data/languages/" +
        languages[i] + ".ini";
      var text = JS2300.fs.readText(path);
      var hasDefault = text.indexOf("Default=") >= 0;
      var hasConfig = text.indexOf("Config=") >= 0;
      details.push(languages[i] + ":" + text.length + "/" +
        (hasDefault ? "D" : "-") + (hasConfig ? "C" : "-"));
      if (!hasDefault || !hasConfig)
        ok = false;
    }
    FD.append((ok ? "PASS " : "FAIL ") + "language_packs details=" +
      details.join(","));
    return ok ? 0 : -1;
  }},
  { name: "system_check", run: function() {
    var start = JS2300.now();
    var ret = JS2300.system.action("developer:system_check");
    FD.append((ret >= 0 ? "PASS " : "FAIL ") + "system_check ret=" + ret +
      " ms=" + (JS2300.now() - start));
    return ret >= 0 ? 0 : -1;
  }},
  { name: "display_benchmark", run: function() {
    var start = JS2300.now();
    var ret = JS2300.system.action("developer:display_benchmark");
    var report = "";
    try {
      report = JS2300.fs.readText("/media/mmcblk0/unifrog_data/display-benchmark.txt");
    } catch (e) {}
    FD.append((ret > 0 ? "PASS " : "FAIL ") +
      "display_benchmark ret=" + ret + " ms=" + (JS2300.now() - start) +
      " report_bytes=" + report.length);
    return ret > 0 ? 0 : -1;
  }},
  { name: "display_color_test", run: function() {
    var start = JS2300.now();
    var ret = JS2300.system.action("developer:display_color_test");
    var report = "";
    try {
      report = JS2300.fs.readText("/media/mmcblk0/unifrog_data/logs/reports/display-color-test.txt");
    } catch (e) {}
    FD.append((ret > 0 ? "PASS " : "FAIL ") +
      "display_color_test ret=" + ret + " ms=" + (JS2300.now() - start) +
      " report_bytes=" + report.length);
    return ret > 0 ? 0 : -1;
  }},
  { name: "storage_quick_benchmark", run: function() {
    var start = JS2300.now();
    var ret = JS2300.system.action("developer:storage_quick_benchmark");
    var report = "";
    try {
      report = JS2300.fs.readText("/media/mmcblk0/unifrog_data/logs/reports/storage-quick-benchmark.txt");
    } catch (e) {}
    FD.append((ret > 0 ? "PASS " : "FAIL ") +
      "storage_quick_benchmark ret=" + ret + " ms=" + (JS2300.now() - start) +
      " report_bytes=" + report.length);
    return ret > 0 ? 0 : -1;
  }}
];
