// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/storage-stress-tests.js");

FD.main("storage-stress", function() {
  FD.append("INFO storage_stress_sweep profiles=stable_only");
  if (JS2300.system.action("developer:storage_runtime_sweep_supported") > 0) {
    FD.append("INFO storage_stress_sweep note=Default sweep stops at wide25 because 0017/0018 show wide37+ can drop the card or crash-loop. Use storage-stress-aggressive.js for risky profiles.");
    FD.runSuite("storage-stress", FD.storageStressTests.stable);
  } else {
    FD.append("INFO storage_stress_sweep note=Runtime profile switching is unavailable; testing the active boot profile only.");
    FD.runSuite("storage-stress", [FD.storageStressTests.boot()]);
  }
});
