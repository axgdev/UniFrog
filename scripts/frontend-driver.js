// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/frontend-tests.js");
load("frontend-driver/core-tests.js");

FD.main("all", function() {
  FD.runSuite("frontend", FD.frontendTests);
  FD.runSuite("core", FD.coreTests);
});
