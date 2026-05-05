function shortText(text, limit) {
  var out = "";
  var i;
  if (!text) return "";
  if (text.length <= limit) return text;
  for (i = 0; i < limit - 1 && i < text.length; i++)
    out += String.fromCharCode(text.charCodeAt(i));
  return out + "~";
}

function textWindow(text, start, count) {
  var out = "";
  var i;
  if (!text) return "";
  for (i = 0; i < count && start + i < text.length; i++)
    out += String.fromCharCode(text.charCodeAt(start + i));
  return out;
}

function marqueeText(text, limit, active, now) {
  var padded;
  var span;
  var phase;
  var offset;
  if (!text) return "";
  if (text.length <= limit) return text;
  if (!active) return shortText(text, limit);
  padded = text + "   ";
  span = padded.length;
  phase = Math.floor((now - motionStartMs) / 170) % (span + 6);
  if (phase < 3) offset = 0;
  else if (phase >= span + 3) offset = 0;
  else offset = phase - 3;
  if (offset + limit <= padded.length)
    return textWindow(padded, offset, limit);
  return textWindow(padded, offset, padded.length - offset) +
    textWindow(padded, 0, limit - (padded.length - offset));
}

function lowerAscii(text) {
  var out = "";
  var i;
  var c;
  for (i = 0; i < text.length; i++) {
    c = text.charCodeAt(i);
    if (c >= 65 && c <= 90) c += 32;
    out += String.fromCharCode(c);
  }
  return out;
}

function startsWithText(text, prefix) {
  var i;
  if (text.length < prefix.length) return false;
  for (i = 0; i < prefix.length; i++) {
    if (text.charCodeAt(i) !== prefix.charCodeAt(i)) return false;
  }
  return true;
}

function endsWithCI(text, suffix) {
  var offset;
  var i;
  var a;
  var b;
  if (text.length < suffix.length) return false;
  offset = text.length - suffix.length;
  for (i = 0; i < suffix.length; i++) {
    a = text.charCodeAt(offset + i);
    b = suffix.charCodeAt(i);
    if (a >= 65 && a <= 90) a += 32;
    if (b >= 65 && b <= 90) b += 32;
    if (a !== b) return false;
  }
  return true;
}

function containsCI(text, part) {
  return lowerAscii(text).indexOf(lowerAscii(part)) >= 0;
}

function readKey(text, key) {
  var i = 0;
  var line = "";
  var prefix = key + "=";
  var c;
  if (!text) return "";
  while (i <= text.length) {
    if (i === text.length) c = 10;
    else c = text.charCodeAt(i);
    if (c === 10 || c === 13) {
      if (startsWithText(line, prefix))
        return lineValue(line, prefix.length);
      line = "";
    } else {
      line += String.fromCharCode(c);
    }
    i++;
  }
  return "";
}

function lineValue(line, offset) {
  var out = "";
  var i;
  for (i = offset; i < line.length; i++)
    out += String.fromCharCode(line.charCodeAt(i));
  return out;
}

function toInt(text, fallback) {
  var i;
  var value = 0;
  var sign = 1;
  var seen = 0;
  if (!text) return fallback;
  if (text.charCodeAt(0) === 45) {
    sign = -1;
    i = 1;
  } else {
    i = 0;
  }
  for (; i < text.length; i++) {
    var c = text.charCodeAt(i);
    if (c < 48 || c > 57) break;
    value = value * 10 + c - 48;
    seen = 1;
  }
  return seen ? value * sign : fallback;
}

function hexValue(c) {
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 65 && c <= 70) return c - 55;
  if (c >= 97 && c <= 102) return c - 87;
  return -1;
}

function parseColor(text, fallback) {
  var i = 0;
  var value = 0;
  var digits = 0;
  var h;
  if (!text) return fallback;
  if (text.charCodeAt(0) === 35) i = 1;
  else if (text.length > 2 && text.charCodeAt(0) === 48 &&
      (text.charCodeAt(1) === 120 || text.charCodeAt(1) === 88)) i = 2;
  for (; i < text.length; i++) {
    h = hexValue(text.charCodeAt(i));
    if (h < 0) break;
    value = value * 16 + h;
    digits++;
  }
  return digits ? value : fallback;
}

function clampIndex(value, length) {
  if (value < 0) return 0;
  if (value >= length) return length - 1;
  return value;
}

function optionIndex(options, value, fallback) {
  var i;
  for (i = 0; i < options.length; i++) {
    if (options[i].value === value) return i;
  }
  return fallback;
}

function field(text, index) {
  var out = "";
  var current = 0;
  var i;
  var c;
  for (i = 0; i < text.length; i++) {
    c = text.charCodeAt(i);
    if (c === 124) {
      if (current === index) return out;
      current++;
      out = "";
    } else if (current === index) {
      out += String.fromCharCode(c);
    }
  }
  return current === index ? out : "";
}

function hex8(value) {
  var out = "";
  var digits = "0123456789ABCDEF";
  var i;
  for (i = 7; i >= 0; i--) out += digits[(value >> (i * 4)) & 15];
  return out;
}
