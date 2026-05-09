var homeItems = [
  { id: "firmware", label: "Firmware", detail: "Boot firmware" },
  { id: "system:Game Boy Advance", label: "Game Boy Advance", detail: "GBA", icon: "gba" },
  { id: "system:Game Boy / Color", label: "Game Boy", detail: "GB GBC", icon: "gb" },
  { id: "system:Nintendo (NES)", label: "Nintendo", detail: "NES FDS", icon: "nes" },
  { id: "system:Super Nintendo", label: "Super Nintendo", detail: "SNES", icon: "snes" },
  { id: "system:Sega Genesis", label: "Sega Genesis", detail: "MD SMS GG", icon: "genesis" },
  { id: "system:PC Engine", label: "PC Engine", detail: "PCE SGX", icon: "pcengine" },
  { id: "system:PlayStation", label: "PlayStation", detail: "PS1", icon: "psx" },
  { id: "media", label: "Media", detail: "Videos music images", icon: "media" },
  { id: "last", label: "Continue", detail: "Last game" },
  { id: "index", label: "Index Library", detail: "Scan SD card" },
  { id: "settings", label: "Settings", detail: "Display audio input" },
  { id: "developer", label: "Developer", detail: "Checks scripts tools" },
  { id: "files", label: "File Browser", detail: "Open any file" }
];
var developerItems = [
  { id: "system_check", label: "System check", detail: "Detect stale SD files" },
  { id: "smoke", label: "Smoke test", detail: "Run packaged diagnostics" },
  { id: "perf", label: "Performance test", detail: "Measure JS screens" },
  { id: "storage", label: "Storage test", detail: "Measure SD reads" },
  { id: "storage_full", label: "Storage full test", detail: "Probe SD modes" },
  { id: "storage_mode", label: "Storage mode test", detail: "Choose SD mode" },
  { id: "exception", label: "Test exception", detail: "Open crash screen" },
  { id: "cpu_exception", label: "CPU exception", detail: "Execute BREAK" },
  { id: "run_core", label: "Run core file", detail: "/unifrog/cores" },
  { id: "scripts", label: "Scripts", detail: "/unifrog/scripts" }
];
var storageModeItems = [
  { id: "safe", label: "safe", detail: "1-bit 25 MHz baseline" },
  { id: "hs1", label: "hs1", detail: "1-bit high speed" },
  { id: "uhs12", label: "uhs12", detail: "UHS SDR12" },
  { id: "uhs25", label: "uhs25", detail: "UHS SDR25" },
  { id: "wide", label: "wide", detail: "4-bit high speed" },
  { id: "uhs", label: "uhs", detail: "UHS SDR50 path" },
  { id: "wide50", label: "wide50", detail: "4-bit 50 MHz cap" }
];
var psxFolders = ["/psx/", "/ps1/", "/playstation/"];
var coreCatalog = [
  {
    label: "gpSP",
    value: "gpsp",
    system: "Game Boy Advance",
    suffixes: [".gba"],
    folders: ["/gba/", "/game boy advance/"]
  },
  {
    label: "gpSP multicore",
    value: "gpsp_multicore",
    system: "Game Boy Advance",
    suffixes: [".gba"],
    folders: ["/gba/", "/game boy advance/"]
  },
  {
    label: "Gambatte",
    value: "gambatte",
    system: "Game Boy / Color",
    suffixes: [".gb", ".gbc"],
    folders: ["/gb/", "/gbc/", "/gbb/", "/game boy/"]
  },
  {
    label: "Gearboy",
    value: "gearboy",
    system: "Game Boy / Color",
    suffixes: [".gb", ".gbc"],
    folders: ["/gb/", "/gbc/", "/gbb/", "/game boy/"]
  },
  {
    label: "QuickNES",
    value: "quicknes",
    system: "Nintendo (NES)",
    suffixes: [".nes"],
    folders: ["/nes/", "/famicom/"]
  },
  {
    label: "FCEUmm",
    value: "fceumm",
    system: "Nintendo (NES)",
    suffixes: [".nes", ".fds"],
    folders: ["/nes/", "/famicom/"]
  },
  {
    label: "Snes9x 2005",
    value: "snes9x2005",
    system: "Super Nintendo",
    suffixes: [".sfc", ".smc"],
    folders: ["/snes/", "/super nintendo/"]
  },
  {
    label: "Snes9x 2002",
    value: "snes9x2002",
    system: "Super Nintendo",
    suffixes: [".sfc", ".smc"],
    folders: ["/snes/", "/super nintendo/"]
  },
  {
    label: "PicoDrive",
    value: "picodrive",
    system: "Sega Genesis",
    suffixes: [".md", ".gen", ".smd", ".32x", ".sms", ".gg", ".sg"],
    folders: ["/genesis/", "/megadrive/", "/mega drive/", "/sms/", "/gg/"]
  },
  {
    label: "PCE Fast",
    value: "pce-fast",
    system: "PC Engine",
    suffixes: [".pce", ".sgx"],
    folders: ["/pce/", "/pc engine/", "/turbografx/"]
  },
  {
    label: "QPSX",
    value: "qpsx",
    system: "PlayStation",
    suffixes: [".cue", ".iso", ".img", ".pbp"],
    folders: psxFolders
  },
  {
    label: "PMP Video",
    value: "pmp-video",
    system: "Media",
    suffixes: [".avi"],
    folders: ["/video/", "/videos/", "/media/"]
  }
];
