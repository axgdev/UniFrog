// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/storage-stress-tests.js");

FD.main("storage-stress-aggressive", function() {
  FD.append("INFO storage_stress_aggressive profiles=wide37_hs_uhs");
  FD.append("INFO storage_stress_aggressive note=Risky profiles may drop the SD card, fail restore, or require a power cycle.");
  FD.runSuite("storage-stress-aggressive", FD.storageStressTests.aggressive);
});
