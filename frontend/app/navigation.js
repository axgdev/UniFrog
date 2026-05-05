function makeNavState() {
  return {
    view: view,
    selected: selected,
    scroll: scroll,
    path: path,
    filter: filter,
    title: title,
    entries: entries,
    currentItems: currentItems,
    currentSystem: currentSystem,
    currentListTitle: currentListTitle,
    pendingVideoPath: pendingVideoPath,
    pendingGamePath: pendingGamePath,
    pendingDeveloperCore: pendingDeveloperCore,
    pendingDeveloperCorePath: pendingDeveloperCorePath,
    scriptItems: scriptItems,
    systemCheckRows: systemCheckRows,
    systemCheckTitle: systemCheckTitle,
    systemCheckDetail: systemCheckDetail
  };
}

function restoreNavState(state) {
  view = state.view;
  selected = state.selected;
  scroll = state.scroll;
  path = state.path;
  filter = state.filter;
  title = state.title;
  entries = state.entries ? state.entries : [];
  currentItems = state.currentItems ? state.currentItems : [];
  currentSystem = state.currentSystem ? state.currentSystem : "";
  currentListTitle = state.currentListTitle ? state.currentListTitle : "";
  pendingVideoPath = state.pendingVideoPath ? state.pendingVideoPath : "";
  pendingGamePath = state.pendingGamePath ? state.pendingGamePath : "";
  pendingDeveloperCore = state.pendingDeveloperCore ? state.pendingDeveloperCore : "";
  pendingDeveloperCorePath = state.pendingDeveloperCorePath ?
    state.pendingDeveloperCorePath : "";
  scriptItems = state.scriptItems ? state.scriptItems : [];
  systemCheckRows = state.systemCheckRows ? state.systemCheckRows : [];
  systemCheckTitle = state.systemCheckTitle ? state.systemCheckTitle : "System Check";
  systemCheckDetail = state.systemCheckDetail ? state.systemCheckDetail : "";
  startMotion(JS2300.now());
}

function clearNav() {
  navStack.length = 0;
}

function pushNav() {
  var i;
  if (navStack.length >= NAV_STACK_MAX) {
    for (i = 1; i < navStack.length; i++)
      navStack[i - 1] = navStack[i];
    navStack.length = NAV_STACK_MAX - 1;
  }
  navStack[navStack.length] = makeNavState();
}

function goHome(now) {
  clearNav();
  view = HOME;
  selected = 0;
  scroll = 0;
  showToast("Home", now);
}

function startMotion(now) {
  motionStartMs = now;
  motionUntilMs = now + 190;
  nextMarqueeMs = now;
  dirty = true;
}

function motionStep(now) {
  var span = 190;
  var elapsed = now - motionStartMs;
  if (elapsed <= 0) return 0;
  if (elapsed >= span) return 8;
  return Math.floor((elapsed * 8) / span);
}
