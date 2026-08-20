// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/storage-stress-tests.js");

FD.main("storage-stress-aggressive", function() {
  FD.append("INFO storage_stress_aggressive profiles=uhs_wide37_hs1_last");
  if (JS2300.system.action("developer:storage_runtime_sweep_supported") > 0) {
    FD.append("INFO storage_stress_aggressive note=Risky profiles may drop the SD card, fail restore, or require a power cycle.");
    FD.runSuiteWithOptions("storage-stress-aggressive",
      FD.storageStressTests.aggressive, { stopOnFail: false });
  } else {
    FD.append("INFO storage_stress_aggressive note=Skipped: runtime profile switching is unavailable.");
  }
});
