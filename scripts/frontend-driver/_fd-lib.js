var FD = (function() {
  var ROOT = "/media/mmcblk0";
  var LOG_DIR = ROOT + "/unifrog_data/logs/frontend-driver";
  var STATE = LOG_DIR + "/state.ini";
  var RESUME_STATE = ROOT + "/unifrog_data/frontend-driver.state";
  var REPORT = LOG_DIR + "/report.txt";
  var CRASH = ROOT + "/unifrog_data/last-crash.ini";
  var ROMS = ROOT + "/ROMS";
  var MAX_CRASHES = 2;

  function now() { return JS2300.now(); }
  function readText(path) {
    try { return JS2300.fs.readText(path) || ""; } catch (e) { return ""; }
  }
  function writeText(path, text) {
    JS2300.fs.writeText(path, text);
  }
  function flushLog() {
    try { JS2300.flushLog(); } catch (e) {}
  }
  function append(line) {
    var old = readText(REPORT);
    writeText(REPORT, old + line + "\n");
    JS2300.log("frontend-driver " + line);
  }
  function notify(text) {
    try { JS2300.system.action("toast:" + text); } catch (e) {}
  }
  function value(text, key, fallback) {
    var lines = text.split("\n");
    var prefix = key + "=";
    for (var i = 0; i < lines.length; i++)
      if (lines[i].indexOf(prefix) === 0)
        return lines[i].slice(prefix.length);
    return fallback;
  }
  function writeState(suite, step, phase, name, crashes) {
    var text = "active=1\nsuite=" + suite + "\nstep=" + step + "\nphase=" + phase +
      "\nname=" + name + "\ncrashes=" + (crashes || 0) +
      "\nupdated_ms=" + now() + "\n";
    writeText(STATE, text);
    writeText(RESUME_STATE, text);
    flushLog();
  }
  function finishState(suite, step) {
    var text = "active=0\nsuite=" + suite + "\nstep=" + step +
      "\nphase=done\nupdated_ms=" + now() + "\n";
    writeText(STATE, text);
    writeText(RESUME_STATE, text);
    flushLog();
  }
  function disableState(reason, suite, step, name, crashes) {
    var text = "active=0\nsuite=" + suite + "\nstep=" + step + "\nphase=disabled" +
      "\nname=" + name + "\ncrashes=" + crashes + "\nreason=" + reason +
      "\nupdated_ms=" + now() + "\n";
    writeText(STATE, text);
    writeText(RESUME_STATE, text);
    flushLog();
  }
  function list(path) {
    try { return JS2300.fs.list(path) || []; } catch (e) { return []; }
  }
  function entryName(entry) { return entry.name || entry.path || ""; }
  function isFile(entry) {
    return entry.type === "file" || entry.kind === "file" ||
      entry.isFile === true || entry.dir === false;
  }
  function findRom(folder, suffixes) {
    var root = ROMS + "/" + folder;
    var entries = list(root);
    for (var i = 0; i < entries.length; i++) {
      var name = entryName(entries[i]);
      var lower = name.toLowerCase();
      if (!isFile(entries[i]) && entries[i].type)
        continue;
      for (var s = 0; s < suffixes.length; s++)
        if (lower.slice(-suffixes[s].length) === suffixes[s])
          return root + "/" + name;
    }
    return "";
  }
  function runCore(name, folder, core, suffixes) {
    var rom = findRom(folder, suffixes);
    var action;
    var ret;
    if (!rom) {
      append("SKIP " + name + " no_rom folder=" + folder);
      return 0;
    }
    action = "run+core=" + core + ",audio=0,frames=180:" + rom;
    append("RUN " + name + " core=" + core + " rom=" + rom);
    notify("Testing " + name);
    var start = now();
    ret = JS2300.system.action(action);
    append((ret > 0 ? "PASS " : "FAIL ") + name + " ret=" + ret +
      " ms=" + (now() - start));
    return ret > 0 ? 0 : -1;
  }
  function runSuite(suite, tests) {
    var state = readText(STATE);
    var active = value(state, "active", "0") === "1";
    var stateSuite = value(state, "suite", "");
    var step = active && stateSuite === suite ?
      parseInt(value(state, "step", "0"), 10) : 0;
    var phase = value(state, "phase", "");
    var previous = value(state, "name", "");
    var crashes = parseInt(value(state, "crashes", "0"), 10) || 0;
    var crash = readText(CRASH);

    if (active && stateSuite === suite && phase === "running") {
      crashes++;
      append("CRASH_OR_REBOOT suite=" + suite + " step=" + step +
        " name=" + previous + " crashes=" + crashes);
      if (crash) {
        append("CRASH_MARKER " + crash.replace(/\n/g, " "));
        writeText(CRASH, "");
      }
      if (crashes >= MAX_CRASHES) {
        append("ABORT suite=" + suite + " step=" + step +
          " name=" + previous + " reason=crash_loop_guard");
        disableState("crash_loop_guard", suite, step, previous, crashes);
        return;
      }
      step++;
    } else {
      crashes = 0;
    }

    for (; step < tests.length; step++) {
      writeState(suite, step, "running", tests[step].name, crashes);
      notify("Running " + suite + " " + (step + 1) + "/" + tests.length);
      try {
        tests[step].run();
        crashes = 0;
      } catch (e) {
        append("FAIL " + suite + "." + tests[step].name + " exception=" + e);
      }
      writeState(suite, step + 1, "done", tests[step].name, crashes);
      JS2300.sleep(250);
    }
    finishState(suite, step);
  }
  function main(suite, fn) {
    var state = readText(STATE);
    if (state.indexOf("active=1") >= 0)
      append("frontend-driver resume suite=" + suite + " ms=" + now());
    else
      writeText(REPORT, "frontend-driver start suite=" + suite +
        " ms=" + now() + "\n");
    fn();
    append("DONE suite=" + suite + " ms=" + now());
  }
  return {
    LOG_DIR: LOG_DIR,
    ROMS: ROMS,
    append: append,
    notify: notify,
    main: main,
    runSuite: runSuite,
    runCore: runCore,
    readText: readText
  };
})();
