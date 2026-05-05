var __frontendLoadStart = JS2300.now();

function __frontendLoad(path) {
  var start = JS2300.now();
  JS2300.log("frontend module load_start path=" + path + " total_ms=" +
    String(start - __frontendLoadStart));
  load(path);
  JS2300.log("frontend module loaded path=" + path + " ms=" +
    String(JS2300.now() - start) + " total_ms=" +
    String(JS2300.now() - __frontendLoadStart));
}

function frontendStartupMark(stage, start) {
  JS2300.log("frontend phase=" + stage + " ms=" +
    String(JS2300.now() - start) + " total_ms=" +
    String(JS2300.now() - __frontendLoadStart));
}

__frontendLoad("app/theme.js");
__frontendLoad("app/constants.js");
__frontendLoad("app/catalog.js");
__frontendLoad("app/text.js");
__frontendLoad("app/navigation.js");
__frontendLoad("app/content.js");
__frontendLoad("app/settings.js");
__frontendLoad("app/index.js");
__frontendLoad("app/views.js");
__frontendLoad("app/actions.js");
__frontendLoad("app/app.js");
