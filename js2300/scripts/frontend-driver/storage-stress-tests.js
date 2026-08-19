// unifrog: mode=extension

FD.storageStressTests = (function() {
  var stableProfiles = [
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
    "wide25"
  ];
  var aggressiveProfiles = [
    "wide50",
    "uhs12",
    "uhs25",
    "wide",
    "uhs",
    "wide37",
    "hs1"
  ];
  var info = {
    safe: "1-bit non-high-speed safe mode; used as the first stability baseline",
    wide1: "4-bit 1 MHz, 3.3V, no high-speed timing",
    wide2: "4-bit 2 MHz, 3.3V, no high-speed timing",
    wide4: "4-bit 4 MHz, 3.3V, no high-speed timing",
    wide8: "4-bit 8 MHz, 3.3V, no high-speed timing",
    wide10: "4-bit 10 MHz, 3.3V, no high-speed timing",
    wide12: "4-bit 12 MHz, 3.3V, no high-speed timing",
    wide14: "4-bit 14 MHz, 3.3V, no high-speed timing",
    wide16: "4-bit 16 MHz, 3.3V, no high-speed timing",
    wide18: "4-bit 18 MHz, 3.3V, no high-speed timing",
    wide20: "4-bit 20 MHz, 3.3V, no high-speed timing",
    wide22: "4-bit 22 MHz, 3.3V, no high-speed timing",
    wide24: "4-bit 24 MHz, 3.3V, no high-speed timing",
    wide25: "4-bit 25 MHz, 3.3V, no high-speed timing",
    wide37: "4-bit 37 MHz, 3.3V, SD high-speed timing",
    hs1: "1-bit high-speed request; controller selected about 49.5 MHz in tests",
    wide50: "4-bit 50 MHz, 3.3V, SD high-speed timing",
    uhs12: "4-bit UHS SDR12 request; enables 1.8V-capable path if the stack accepts it",
    uhs25: "4-bit UHS SDR25 request; controller selected about 49.5 MHz in tests",
    wide: "4-bit high-speed auto/max request; controller selected about 49.5 MHz in tests",
    uhs: "4-bit UHS SDR50 auto/max request; highest-risk experimental profile"
  };

  function buildTests(profiles, order) {
    var tests = [];
    for (var i = 0; i < profiles.length; i++) {
    (function(profile) {
      tests.push({
        name: "storage_stress_" + profile,
        run: function() {
          var start = JS2300.now();
          var ret;
          FD.append("RUN storage_stress profile=" + profile +
            " order=" + order + " detail=" + (info[profile] || ""));
          FD.notify("Storage stress " + profile);
          ret = JS2300.system.action("developer:storage_stress:" + profile);
          FD.append((ret > 0 ? "PASS " : "FAIL ") +
            "storage_stress profile=" + profile + " ret=" + ret +
            " ms=" + (JS2300.now() - start));
          return ret > 0 ? 0 : -1;
        }
      });
    })(profiles[i]);
    }
    return tests;
  }

  return {
    boot: function() {
      return buildTests(["safe"], "active_boot_profile")[0];
    },
    single: function(profile) {
      return buildTests([profile], "single_profile");
    },
    stable: buildTests(stableProfiles, "stable_only"),
    aggressive: buildTests(aggressiveProfiles, "aggressive_only"),
    all: buildTests(stableProfiles.concat(aggressiveProfiles),
      "conservative_to_aggressive"),
    info: info
  };
})();
