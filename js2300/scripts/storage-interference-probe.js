// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");

FD.main("storage-interference", function() {
  var tests = [
    {
      name: "baseline_quick_storage",
      run: function() {
        var start = JS2300.now();
        var ret = JS2300.system.action("developer:storage_quick_benchmark");
        FD.append((ret > 0 ? "PASS " : "FAIL ") +
          "baseline_quick_storage ret=" + ret +
          " ms=" + (JS2300.now() - start));
        return ret > 0 ? 0 : -1;
      }
    },
    {
      name: "display_then_storage",
      run: function() {
        var start = JS2300.now();
        var displayRet = JS2300.system.action("developer:display_benchmark");
        var storageRet = JS2300.system.action("developer:storage_quick_benchmark");
        FD.append((displayRet > 0 && storageRet > 0 ? "PASS " : "FAIL ") +
          "display_then_storage display_ret=" + displayRet +
          " storage_ret=" + storageRet + " ms=" + (JS2300.now() - start));
        return displayRet > 0 && storageRet > 0 ? 0 : -1;
      }
    },
    {
      name: "safe_read_write_stress",
      run: function() {
        var start = JS2300.now();
        var ret = JS2300.system.action("developer:storage_stress:safe");
        FD.append((ret > 0 ? "PASS " : "FAIL ") +
          "safe_read_write_stress ret=" + ret +
          " ms=" + (JS2300.now() - start));
        return ret > 0 ? 0 : -1;
      }
    },
    {
      name: "post_stress_quick_storage",
      run: function() {
        var start = JS2300.now();
        var ret = JS2300.system.action("developer:storage_quick_benchmark");
        FD.append((ret > 0 ? "PASS " : "FAIL ") +
          "post_stress_quick_storage ret=" + ret +
          " ms=" + (JS2300.now() - start));
        return ret > 0 ? 0 : -1;
      }
    }
  ];

  FD.append("INFO storage_interference_probe purpose=baseline_vs_display_vs_read_write");
  FD.append("INFO storage_interference_probe note=Use the wireless controller while this runs to correlate RF polling with SD/display logs.");
  FD.notify("Storage interference probe");
  FD.runSuite("storage-interference", tests);
});
