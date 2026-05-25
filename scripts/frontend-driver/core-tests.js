// unifrog: mode=extension

FD.coreTests = [
  { name: "picodrive_md", run: function() {
    return FD.runCore("picodrive_md", "MD", "picodrive",
      [".zip", ".md", ".gen", ".bin", ".smd"]);
  }},
  { name: "picodrive_gg", run: function() {
    return FD.runCore("picodrive_gg", "GG", "picodrive", [".zip", ".gg"]);
  }},
  { name: "quicknes_fc", run: function() {
    return FD.runCore("quicknes_fc", "FC", "quicknes", [".zip", ".nes"]);
  }},
  { name: "quicknes_nes", run: function() {
    return FD.runCore("quicknes_nes", "NES", "quicknes", [".zip", ".nes"]);
  }},
  { name: "gambatte_gb", run: function() {
    return FD.runCore("gambatte_gb", "GB", "gambatte",
      [".zip", ".gb", ".gbc"]);
  }},
  { name: "gambatte_gbc", run: function() {
    return FD.runCore("gambatte_gbc", "GBC", "gambatte",
      [".zip", ".gbc", ".gb"]);
  }},
  { name: "gearboy", run: function() {
    return FD.runCore("gearboy", "GB", "gearboy", [".zip", ".gb", ".gbc"]);
  }},
  { name: "fceumm", run: function() {
    return FD.runCore("fceumm", "FC", "fceumm", [".zip", ".nes"]);
  }},
  { name: "snes9x2005", run: function() {
    return FD.runCore("snes9x2005", "SFC", "snes9x2005",
      [".zip", ".sfc", ".smc"]);
  }},
  { name: "snes9x2002", run: function() {
    return FD.runCore("snes9x2002", "SFC", "snes9x2002",
      [".zip", ".sfc", ".smc"]);
  }},
  { name: "gpsp", run: function() {
    return FD.runCore("gpsp", "GBA", "gpsp", [".zip", ".gba"]);
  }},
  { name: "gpsp_gbac_prosty", run: function() {
    return FD.runCore("gpsp_gbac_prosty", "GBA", "gpsp-gbac-prosty",
      [".zip", ".gba"]);
  }},
  { name: "pce_fast", run: function() {
    return FD.runCore("pce_fast", "PCE", "pce-fast", [".zip", ".pce"]);
  }},
  { name: "qpsx", run: function() {
    return FD.runCore("qpsx", "PS", "qpsx",
      [".chd", ".cue", ".bin", ".zip"]);
  }}
];
