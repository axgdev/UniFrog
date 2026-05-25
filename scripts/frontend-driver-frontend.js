// unifrog: mode=extension

load("frontend-driver/_fd-lib.js");
load("frontend-driver/frontend-tests.js");

FD.main("frontend", function() {
  FD.runSuite("frontend", FD.frontendTests);
});
