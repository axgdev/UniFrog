// unifrog: type=libretro-core
// unifrog: system=chip8

var CHIP8 = (function () {
   var LOW_WIDTH = 64;
   var LOW_HEIGHT = 32;
   var HIGH_WIDTH = 128;
   var HIGH_HEIGHT = 64;
   var MAX_WIDTH = 128;
   var MAX_HEIGHT = 64;
   var ROM_START = 0x200;
   var FONT_START = 0x050;
   var CYCLES_PER_FRAME = 14;

   var width = LOW_WIDTH;
   var height = LOW_HEIGHT;
   var scale = 5;
   var originX = 0;
   var originY = 40;

   var memory = new Uint8Array(65536);
   var v = new Uint8Array(16);
   var stack = new Uint16Array(64);
   var keys = new Uint8Array(16);
   var rpl = new Uint8Array(16);
   var pixels = new Uint8Array((MAX_WIDTH * MAX_HEIGHT + 1) >> 1);
   var palette = new Uint16Array(16);
   var romBuffer = new Uint8Array(65536 - ROM_START);
   var audio = new Int16Array(512);
   var pc = ROM_START;
   var indexReg = 0;
   var sp = 0;
   var delayTimer = 0;
   var soundTimer = 0;
   var waitKeyReg = -1;
   var planeMask = 1;
   var dirty = 1;
   var running = 1;
   var rng = 0x12345678;
   var audioPhase = 0;
   var silenceTail = 0;
   var stackWarned = 0;
   var opcodeWarned = 0;
   var frameCounter = 0;
   var keyMap = [
      0x2, 0x8, 0x4, 0x6,
      0x5, 0x0, 0xa, 0xb,
      0x7, 0x9, 0x1, 0x3
   ];

   var FONT = [
      0xf0, 0x90, 0x90, 0x90, 0xf0,
      0x20, 0x60, 0x20, 0x20, 0x70,
      0xf0, 0x10, 0xf0, 0x80, 0xf0,
      0xf0, 0x10, 0xf0, 0x10, 0xf0,
      0x90, 0x90, 0xf0, 0x10, 0x10,
      0xf0, 0x80, 0xf0, 0x10, 0xf0,
      0xf0, 0x80, 0xf0, 0x90, 0xf0,
      0xf0, 0x10, 0x20, 0x40, 0x40,
      0xf0, 0x90, 0xf0, 0x90, 0xf0,
      0xf0, 0x90, 0xf0, 0x10, 0xf0,
      0xf0, 0x90, 0xf0, 0x90, 0x90,
      0xe0, 0x90, 0xe0, 0x90, 0xe0,
      0xf0, 0x80, 0x80, 0x80, 0xf0,
      0xe0, 0x90, 0x90, 0x90, 0xe0,
      0xf0, 0x80, 0xf0, 0x80, 0xf0,
      0xf0, 0x80, 0xf0, 0x80, 0x80
   ];

   var DEMO_ROM = [
      0x00, 0xe0, 0x60, 0x1c, 0x61, 0x0d, 0xa2, 0x0c,
      0xd0, 0x15, 0x12, 0x0a, 0x9e, 0x92, 0x9c, 0x92,
      0x6e
   ];

   palette[0] = 0x0000;
   palette[1] = 0xffff;
   palette[2] = 0x07ff;
   palette[3] = 0xf81f;
   for (var i = 4; i < 16; i++)
      palette[i] = 0xffff;

   function lowerCode(c)
   {
      return c >= 65 && c <= 90 ? c + 32 : c;
   }

   function endsWithCI(text, suffix)
   {
      if (!text || text.length < suffix.length)
         return false;
      var off = text.length - suffix.length;
      for (var i = 0; i < suffix.length; i++) {
         if (lowerCode(text.charCodeAt(off + i)) !==
             lowerCode(suffix.charCodeAt(i)))
            return false;
      }
      return true;
   }

   function clearPacked()
   {
      for (var i = 0; i < pixels.length; i++)
         pixels[i] = 0;
      dirty = 1;
   }

   function clearPlanes(mask)
   {
      var keep = (~mask) & 15;

      for (var i = 0; i < pixels.length; i++) {
         var packed = pixels[i];
         pixels[i] = (((packed >> 4) & keep) << 4) | (packed & keep);
      }
      dirty = 1;
   }

   function setDisplayMode(hires)
   {
      if (hires) {
         width = HIGH_WIDTH;
         height = HIGH_HEIGHT;
         scale = 2;
         originX = 32;
         originY = 56;
      } else {
         width = LOW_WIDTH;
         height = LOW_HEIGHT;
         scale = 5;
         originX = 0;
         originY = 40;
      }
      clearPacked();
   }

   function posOf(x, y)
   {
      return y * width + x;
   }

   function getPackedPixel(pos)
   {
      var packed = pixels[pos >> 1];
      return (pos & 1) ? (packed & 15) : (packed >> 4);
   }

   function setPackedPixel(pos, value)
   {
      var off = pos >> 1;
      var packed = pixels[off];

      value &= 15;
      pixels[off] = (pos & 1) ?
         ((packed & 0xf0) | value) :
         ((packed & 0x0f) | (value << 4));
   }

   function pixelAt(x, y)
   {
      return getPackedPixel(posOf(x, y));
   }

   function putPixel(x, y, value)
   {
      setPackedPixel(posOf(x, y), value);
      dirty = 1;
   }

   function putPlanePixel(x, y, value)
   {
      var pos = posOf(x, y);
      var old = getPackedPixel(pos);
      var keep = (~planeMask) & 15;

      setPackedPixel(pos, (old & keep) | (value & planeMask));
      dirty = 1;
   }

   function xorPixel(x, y)
   {
      if (width === HIGH_WIDTH && (x < 0 || x >= width ||
          y < 0 || y >= height))
         return 0;
      x &= width - 1;
      y &= height - 1;
      var pos = posOf(x, y);
      var old = getPackedPixel(pos);
      setPackedPixel(pos, old ^ planeMask);
      dirty = 1;
      return old & planeMask;
   }

   function scrollDown(lines)
   {
      if (lines <= 0)
         return;
      for (var y = height - 1; y >= 0; y--) {
         for (var x = 0; x < width; x++)
            putPlanePixel(x, y, y >= lines ? pixelAt(x, y - lines) : 0);
      }
   }

   function scrollUp(lines)
   {
      if (lines <= 0)
         return;
      for (var y = 0; y < height; y++) {
         for (var x = 0; x < width; x++)
            putPlanePixel(x, y, y + lines < height ? pixelAt(x, y + lines) : 0);
      }
   }

   function scrollRight()
   {
      for (var y = 0; y < height; y++) {
         for (var x = width - 1; x >= 0; x--)
            putPlanePixel(x, y, x >= 4 ? pixelAt(x - 4, y) : 0);
      }
   }

   function scrollLeft()
   {
      for (var y = 0; y < height; y++) {
         for (var x = 0; x < width; x++)
            putPlanePixel(x, y, x + 4 < width ? pixelAt(x + 4, y) : 0);
      }
   }

   function reset()
   {
      for (var i = 0; i < memory.length; i++)
         memory[i] = 0;
      for (var j = 0; j < v.length; j++) {
         v[j] = 0;
         keys[j] = 0;
      }
      for (var k = 0; k < stack.length; k++)
         stack[k] = 0;
      for (var f = 0; f < FONT.length; f++)
         memory[FONT_START + f] = FONT[f];
      pc = ROM_START;
      indexReg = 0;
      sp = 0;
      delayTimer = 0;
      soundTimer = 0;
      waitKeyReg = -1;
      planeMask = 1;
      stackWarned = 0;
      opcodeWarned = 0;
      frameCounter = 0;
      running = 1;
      rng = 0x12345678;
      silenceTail = 0;
      setDisplayMode(0);
   }

   function loadBytes(bytes, count)
   {
      var max = memory.length - ROM_START;

      if (count > max)
         count = max;
      for (var i = 0; i < count; i++)
         memory[ROM_START + i] = bytes[i] & 255;
      return count;
   }

   function noteKeyUse(mask, key)
   {
      if (key >= 0 && key < 16)
         return mask | (1 << key);
      return mask;
   }

   function detectControlKeys(count)
   {
      var used = 0;
      var lastReg = new Uint8Array(16);
      var lastValid = new Uint8Array(16);
      var end = ROM_START + count;

      if (end > memory.length)
         end = memory.length;
      for (var addr = ROM_START; addr + 1 < end; addr += 2) {
         var op = ((memory[addr] << 8) | memory[addr + 1]) & 0xffff;
         var x = (op >> 8) & 15;
         var kk = op & 255;

         if ((op & 0xf000) === 0x6000) {
            lastReg[x] = kk & 15;
            lastValid[x] = kk < 16 ? 1 : 0;
         } else if ((op & 0xf0ff) === 0xe09e ||
                    (op & 0xf0ff) === 0xe0a1) {
            if (lastValid[x])
               used = noteKeyUse(used, lastReg[x]);
         }
      }
      return used;
   }

   function applyControlProfile(used)
   {
      keyMap[0] = 0x5;
      keyMap[1] = 0x8;
      keyMap[2] = 0x7;
      keyMap[3] = 0x9;
      keyMap[4] = 0x6;
      keyMap[5] = 0x4;
      keyMap[6] = 0x2;
      keyMap[7] = 0x0;
      keyMap[8] = 0xa;
      keyMap[9] = 0xb;
      keyMap[10] = 0x1;
      keyMap[11] = 0x3;

      if ((used & ((1 << 2) | (1 << 4) | (1 << 6) | (1 << 8))) ===
          ((1 << 2) | (1 << 4) | (1 << 6) | (1 << 8)) &&
          !(used & ((1 << 5) | (1 << 7) | (1 << 9)))) {
         keyMap[0] = 0x2;
         keyMap[1] = 0x8;
         keyMap[2] = 0x4;
         keyMap[3] = 0x6;
         keyMap[4] = 0x5;
         keyMap[6] = 0xa;
         keyMap[7] = 0xb;
         keyMap[8] = 0x7;
         keyMap[9] = 0x9;
         JS2300.log("chip8 controls profile keypad-2846");
      } else {
         JS2300.log("chip8 controls profile keypad-5789");
      }
   }

   function loadContent(path)
   {
      var count = -1;

      reset();
      if (path && !endsWithCI(path, ".js") && !endsWithCI(path, ".mjs"))
         count = JS2300.fs.readBytesInto(path, romBuffer, romBuffer.length);
      if (count > 0) {
         loadBytes(romBuffer, count);
         applyControlProfile(detectControlKeys(count));
         JS2300.log("chip8 rom bytes", count);
      } else {
         loadBytes(DEMO_ROM, DEMO_ROM.length);
         applyControlProfile(detectControlKeys(DEMO_ROM.length));
         JS2300.log("chip8 using built-in demo");
      }
   }

   function pollKeys()
   {
      var mask = JS2300.input.poll();

      for (var i = 0; i < 16; i++)
         keys[i] = 0;
      for (var b = 0; b < keyMap.length; b++) {
         if (mask & (1 << b))
            keys[keyMap[b] & 15] = 1;
      }
   }

   function firstPressedKey()
   {
      for (var i = 0; i < 16; i++) {
         if (keys[i])
            return i;
      }
      return -1;
   }

   function nextRandom()
   {
      rng ^= (rng << 13);
      rng ^= (rng >>> 17);
      rng ^= (rng << 5);
      rng >>>= 0;
      return rng & 255;
   }

   function skipNext()
   {
      pc = (pc + 2) & 0xffff;
   }

   function warnOpcode(op)
   {
      if (opcodeWarned)
         return;
      JS2300.log("chip8 unhandled opcode", (pc - 2) & 0xffff, op);
      opcodeWarned = 1;
   }

   function drawSprite(x, y, n)
   {
      var collision = 0;

      if (n === 0) {
         for (var row16 = 0; row16 < 16; row16++) {
            var sprite16 = ((memory[(indexReg + row16 * 2) & 0xffff] << 8) |
                            memory[(indexReg + row16 * 2 + 1) & 0xffff]) &
                           0xffff;
            for (var bit16 = 0; bit16 < 16; bit16++) {
               if (sprite16 & (0x8000 >> bit16))
                  collision |= xorPixel(x + bit16, y + row16);
            }
         }
      } else {
         for (var row = 0; row < n; row++) {
            var sprite = memory[(indexReg + row) & 0xffff];
            for (var bit = 0; bit < 8; bit++) {
               if (sprite & (0x80 >> bit))
                  collision |= xorPixel(x + bit, y + row);
            }
         }
      }
      v[0xf] = collision & 15;
   }

   function step()
   {
      if (!running)
         return;
      if (waitKeyReg >= 0) {
         var key = firstPressedKey();
         if (key < 0)
            return;
         v[waitKeyReg] = key;
         waitKeyReg = -1;
      }
      if (pc + 1 >= memory.length) {
         running = 0;
         JS2300.log("chip8 stopped: pc outside memory", pc);
         return;
      }

      var op = ((memory[pc] << 8) | memory[pc + 1]) & 0xffff;
      var nnn = op & 0x0fff;
      var n = op & 15;
      var x = (op >> 8) & 15;
      var y = (op >> 4) & 15;
      var kk = op & 255;
      pc = (pc + 2) & 0xffff;

      switch (op & 0xf000) {
      case 0x0000:
         if (op === 0x00e0)
            clearPlanes(planeMask);
         else if (op === 0x00ee && sp > 0)
            pc = stack[--sp] & 0xffff;
         else if (op === 0x00ee && !stackWarned) {
            JS2300.log("chip8 stack underflow", pc);
            stackWarned = 1;
         }
         else if (op === 0x00fb)
            scrollRight();
         else if (op === 0x00fc)
            scrollLeft();
         else if (op === 0x00fd)
            running = 0;
         else if (op === 0x00fe)
            setDisplayMode(0);
         else if (op === 0x00ff)
            setDisplayMode(1);
         else if ((op & 0x00f0) === 0x00c0)
            scrollDown(n);
         else if ((op & 0x00f0) === 0x00d0)
            scrollUp(n);
         else
            warnOpcode(op);
         break;
      case 0x1000:
         pc = nnn;
         break;
      case 0x2000:
         if (sp < stack.length)
            stack[sp++] = pc;
         else if (!stackWarned) {
            JS2300.log("chip8 stack overflow", pc, nnn);
            stackWarned = 1;
         }
         pc = nnn;
         break;
      case 0x3000:
         if (v[x] === kk)
            skipNext();
         break;
      case 0x4000:
         if (v[x] !== kk)
            skipNext();
         break;
      case 0x5000:
         if (n === 0 && v[x] === v[y])
            skipNext();
         else if (n === 2) {
            var sr = x;
            var step = x <= y ? 1 : -1;
            var off = 0;

            while (1) {
               memory[(indexReg + off) & 0xffff] = v[sr];
               if (sr === y)
                  break;
               sr += step;
               off++;
            }
         } else if (n === 3) {
            var lr = x;
            var lstep = x <= y ? 1 : -1;
            var loff = 0;

            while (1) {
               v[lr] = memory[(indexReg + loff) & 0xffff];
               if (lr === y)
                  break;
               lr += lstep;
               loff++;
            }
         } else {
            warnOpcode(op);
         }
         break;
      case 0x6000:
         v[x] = kk;
         break;
      case 0x7000:
         v[x] = (v[x] + kk) & 255;
         break;
      case 0x8000:
         switch (n) {
         case 0x0: v[x] = v[y]; break;
         case 0x1: v[x] = v[x] | v[y]; break;
         case 0x2: v[x] = v[x] & v[y]; break;
         case 0x3: v[x] = v[x] ^ v[y]; break;
         case 0x4:
            var sum = v[x] + v[y];
            v[x] = sum & 255;
            v[0xf] = sum > 255 ? 1 : 0;
            break;
         case 0x5:
            var ge = v[x] >= v[y] ? 1 : 0;
            v[x] = (v[x] - v[y]) & 255;
            v[0xf] = ge;
            break;
         case 0x6:
            var lsb = v[x] & 1;
            v[x] = v[x] >> 1;
            v[0xf] = lsb;
            break;
         case 0x7:
            var rge = v[y] >= v[x] ? 1 : 0;
            v[x] = (v[y] - v[x]) & 255;
            v[0xf] = rge;
            break;
         case 0xe:
            var msb = (v[x] >> 7) & 1;
            v[x] = (v[x] << 1) & 255;
            v[0xf] = msb;
            break;
         }
         break;
      case 0x9000:
         if (n === 0 && v[x] !== v[y])
            skipNext();
         break;
      case 0xa000:
         indexReg = nnn;
         break;
      case 0xb000:
         pc = (nnn + v[x]) & 0xffff;
         break;
      case 0xc000:
         v[x] = nextRandom() & kk;
         break;
      case 0xd000:
         drawSprite(v[x], v[y], n);
         break;
      case 0xe000:
         if (kk === 0x9e && keys[v[x] & 15])
            skipNext();
         else if (kk === 0xa1 && !keys[v[x] & 15])
            skipNext();
         break;
      case 0xf000:
         if (kk === 0x01) {
            planeMask = x & 15;
            break;
         }
         if (x === 0 && kk === 0x00 && pc + 1 < memory.length) {
            indexReg = ((memory[pc] << 8) | memory[pc + 1]) & 0xffff;
            pc = (pc + 2) & 0xffff;
            break;
         }
         switch (kk) {
         case 0x07:
            v[x] = delayTimer;
            break;
         case 0x0a:
            waitKeyReg = x;
            break;
         case 0x15:
            delayTimer = v[x];
            break;
         case 0x18:
            soundTimer = v[x];
            break;
         case 0x1e:
            indexReg = (indexReg + v[x]) & 0xffff;
            break;
         case 0x02:
         case 0x3a:
            break;
         case 0x29:
         case 0x30:
            indexReg = FONT_START + (v[x] & 15) * 5;
            break;
         case 0x33:
            memory[indexReg & 0xffff] = (v[x] / 100) | 0;
            memory[(indexReg + 1) & 0xffff] = ((v[x] / 10) | 0) % 10;
            memory[(indexReg + 2) & 0xffff] = v[x] % 10;
            break;
         case 0x55:
            for (var sx = 0; sx <= x; sx++)
               memory[(indexReg + sx) & 0xffff] = v[sx];
            break;
         case 0x65:
            for (var lx = 0; lx <= x; lx++)
               v[lx] = memory[(indexReg + lx) & 0xffff];
            break;
         case 0x75:
            for (var rs = 0; rs <= x; rs++)
               rpl[rs] = v[rs];
            break;
         case 0x85:
            for (var rl = 0; rl <= x; rl++)
               v[rl] = rpl[rl];
            break;
         default:
            warnOpcode(op);
            break;
         }
         break;
      default:
         warnOpcode(op);
         break;
      }
   }

   function render()
   {
      JS2300.core.clear(0);
      JS2300.core.blitIndexed4Scaled(
         originX, originY, width, height, scale, pixels, palette);
      JS2300.core.present();
      dirty = 0;
   }

   function playBeep()
   {
      for (var i = 0; i < audio.length; i++) {
         audio[i] = audioPhase < 16 ? 9000 : -9000;
         audioPhase = (audioPhase + 1) & 31;
      }
      JS2300.core.audioS16(audio, 1);
      silenceTail = 48;
   }

   function playSilenceTail()
   {
      for (var i = 0; i < audio.length; i++)
         audio[i] = 0;
      JS2300.core.audioS16(audio, 1);
      silenceTail--;
   }

   return {
      load: function (path) {
         var content = JS2300.core.contentPath ?
            JS2300.core.contentPath() : path;

         if (JS2300.mode && JS2300.mode() !== "libretro-core") {
            JS2300.log("chip8 core script must run as libretro-core");
            return false;
         }
         loadContent(content || path);
         render();
         return true;
      },
      run: function () {
         pollKeys();
         frameCounter++;
         if (running) {
            for (var i = 0; i < CYCLES_PER_FRAME; i++)
               step();
            if (delayTimer > 0)
               delayTimer--;
            if (soundTimer > 0) {
               soundTimer--;
               playBeep();
            } else if (silenceTail > 0) {
               playSilenceTail();
            }
         }
         if ((frameCounter % 600) === 0)
            JS2300.log("chip8 progress", pc, indexReg, sp, planeMask,
               width, height, v[0], v[1], v[2], v[0xf]);
         if (dirty)
            render();
         return running ? 1 : 0;
      },
      unload: function () {
         return true;
      }
   };
})();

function retroLoad(path)
{
   return CHIP8.load(path);
}

function retroRun()
{
   return CHIP8.run();
}

function retroUnload()
{
   return CHIP8.unload();
}
