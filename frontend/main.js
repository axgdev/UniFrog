var __frontendLoadStart = JS2300.now();

function frontendModuleStart(path) {
  var start = JS2300.now();
  JS2300.log("frontend module load_start path=" + path + " total_ms=" +
    String(start - __frontendLoadStart));
  return start;
}

function frontendModuleEnd(path, start) {
  JS2300.log("frontend module loaded path=" + path + " ms=" +
    String(JS2300.now() - start) + " total_ms=" +
    String(JS2300.now() - __frontendLoadStart));
}

function frontendStartupMark(stage, start) {
  JS2300.log("frontend phase=" + stage + " ms=" +
    String(JS2300.now() - start) + " total_ms=" +
    String(JS2300.now() - __frontendLoadStart));
}

var __frontendModuleStart;

__frontendModuleStart = frontendModuleStart("app/theme.js");
load("app/theme.js");
frontendModuleEnd("app/theme.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/constants.js");
load("app/constants.js");
frontendModuleEnd("app/constants.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/catalog.js");
load("app/catalog.js");
frontendModuleEnd("app/catalog.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/text.js");
load("app/text.js");
frontendModuleEnd("app/text.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/navigation.js");
load("app/navigation.js");
frontendModuleEnd("app/navigation.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/content.js");
load("app/content.js");
frontendModuleEnd("app/content.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/settings.js");
load("app/settings.js");
frontendModuleEnd("app/settings.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/index.js");
load("app/index.js");
frontendModuleEnd("app/index.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/views.js");
load("app/views.js");
frontendModuleEnd("app/views.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/actions.js");
load("app/actions.js");
frontendModuleEnd("app/actions.js", __frontendModuleStart);
__frontendModuleStart = frontendModuleStart("app/app.js");
load("app/app.js");
frontendModuleEnd("app/app.js", __frontendModuleStart);
