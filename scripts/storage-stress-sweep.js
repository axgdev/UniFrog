// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/storage-stress-tests.js");

FD.main("storage-stress", function() {
  FD.append("INFO storage_stress_sweep profiles=stable_only");
  FD.append("INFO storage_stress_sweep note=Default sweep stops at wide25 because 0017/0018 show wide37+ can drop the card or crash-loop. Use storage-stress-aggressive.js for risky profiles.");
  FD.runSuite("storage-stress", FD.storageStressTests.stable);
});
