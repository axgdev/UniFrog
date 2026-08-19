// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/core-tests.js");

FD.main("core", function() {
  FD.runSuite("core", FD.coreTests);
});
