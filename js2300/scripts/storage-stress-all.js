// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/storage-stress-tests.js");

FD.main("storage-stress-all", function() {
  FD.append("INFO storage_stress_all profiles=stable_and_aggressive");
  if (JS2300.system.action("developer:storage_runtime_sweep_supported") > 0) {
    FD.append("INFO storage_stress_all note=Runs every profile and continues after failures when storage can be restored.");
    FD.runSuiteWithOptions("storage-stress-all",
      FD.storageStressTests.all, { stopOnFail: false });
  } else {
    FD.append("INFO storage_stress_all note=Runtime profile switching is unavailable; testing the active boot profile only.");
    FD.runSuite("storage-stress-all", [FD.storageStressTests.boot()]);
  }
});
