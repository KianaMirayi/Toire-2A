/* =====================================================================
   TOiRE-2A —— 前端逻辑（ES5，无外部依赖）
   · 两个可拖拽旋钮（GAIN / PEAK REDUCTION），WebSliderRelay 双向绑定
   · VU 表：C++ 30Hz 推 gainReductionDb（语义 = VU 刻度上的 dB 值），这里插值画针
   · 3 位表头开关：meterMode（0 / 0.5 / 1 归一化）↔ APVTS 的 AudioParameterChoice
   · 表头弹道：meterAttack / meterRelease 两个 range
   · frame 计数判断音频是否在跑，停住时优雅归零
   ===================================================================== */
(function () {
  "use strict";

  var DEG = Math.PI / 180;
  var KNOB_MIN = 0;
  var KNOB_MAX = 100;
  var SWEEP = 270;          /* 旋钮机械行程 270° */
  var DIAL_SIZE = 185;      /* 旋钮刻度盘尺寸（CSS px） */

  function $(id) { return document.getElementById(id); }

  function nowMs() {
    return (window.performance && window.performance.now)
      ? window.performance.now()
      : Date.now();
  }

  function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }

  /* 统一的错误上报：DAW 里没有 DevTools，任何异常都必须显示到页面顶部红条，
     否则用户只看到"没反应"。 */
  function reportError(prefix, e) {
    try {
      var el = document.getElementById("errors");
      if (!el) return;
      el.style.display = "block";
      el.textContent = prefix + ": " + (e && e.message ? e.message : e) +
                       "\n" + (e && e.stack ? e.stack : "");
    } catch (ignored) { /* 连错误条都写不了就没办法了 */ }
  }

  /* 让 juce-bridge.js / 其它脚本也能把错误送到红条上 */
  window.__toireReportError = reportError;

  /* ================= 旋钮 ================= */
  function Knob(cfg) {
    this.cfg = cfg;
    this.unit = $(cfg.unit);
    this.rot = $(cfg.rot);
    this.num = $(cfg.num);
    this.dial = $(cfg.dial);
    this.value = cfg.def;
    this.state = null;
    try { this.state = window.__JUCE__.getSliderState(cfg.param); }
    catch (e) { reportError("knob(" + cfg.param + ")", e); }

    var self = this;
    if (this.state) {
      this.state.valueChangedEvent.addListener(function () { self.syncFromState(); });
      this.state.propertiesChangedEvent.addListener(function () { self.syncFromState(); });
    }

    this.bind();
    this.drawDial();
    this.syncFromState();
  }

  Knob.prototype.syncFromState = function () {
    if (!this.state) return;
    var v = this.state.getScaledValue();
    if (typeof v === "number" && isFinite(v)) this.setVisual(v, false);
  };

  Knob.prototype.setVisual = function (v, push) {
    v = clamp(v, KNOB_MIN, KNOB_MAX);
    this.value = v;

    var deg = -SWEEP / 2 + SWEEP * (v / KNOB_MAX);
    this.rot.style.transform = "rotate(" + deg.toFixed(2) + "deg)";
    this.num.textContent = String(Math.round(v));

    if (push && this.state) this.state.setNormalisedValue(v / KNOB_MAX);
  };

  Knob.prototype.bind = function () {
    var self = this;
    var drag = null;

    function onMove(e) {
      if (!drag) return;
      /* 在窗口外松手 / Alt-Tab / DAW 抢走鼠标捕获时收不到 mouseup：
         用按键状态兜底，否则 drag 永远非空、之后不按键划过旋钮也会改值 */
      if (e.buttons === 0) { onUp(); return; }
      var scale = e.shiftKey ? 0.12 : 0.42;
      self.setVisual(drag.v + (drag.y - e.clientY) * scale, true);
    }

    function onUp() {
      if (drag && self.state) self.state.sliderDragEnded();
      drag = null;
      self.unit.classList.remove("drag");
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      window.removeEventListener("blur", onUp);
      document.body.style.cursor = "";
    }

    this.unit.addEventListener("mousedown", function (e) {
      if (e.button !== 0) return;
      e.preventDefault();
      drag = { y: e.clientY, v: self.value };
      if (self.state) self.state.sliderDragStarted();
      self.unit.classList.add("drag");
      document.body.style.cursor = "ns-resize";
      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
      window.addEventListener("blur", onUp);
    });

    this.unit.addEventListener("wheel", function (e) {
      e.preventDefault();
      var step = e.shiftKey ? 0.1 : 1;
      self.setVisual(self.value + (e.deltaY > 0 ? -step : step), true);
    }, { passive: false });

    this.unit.addEventListener("dblclick", function () {
      self.setVisual(self.cfg.def, true);
    });

    /* 键盘（宿主里也能用）：方向键 ±1，Shift ±0.1 */
    this.unit.setAttribute("tabindex", "0");
    this.unit.addEventListener("keydown", function (e) {
      var step = e.shiftKey ? 0.1 : 1;
      if (e.key === "ArrowUp" || e.key === "ArrowRight") {
        self.setVisual(self.value + step, true); e.preventDefault();
      } else if (e.key === "ArrowDown" || e.key === "ArrowLeft") {
        self.setVisual(self.value - step, true); e.preventDefault();
      }
    });
  };

  /* 刻度盘：0..100，每 2 一格，每 10 一个白色数字（黑面板 + 亮字，照 ref-REV） */
  Knob.prototype.drawDial = function () {
    var canvas = this.dial;
    var size = DIAL_SIZE;
    var dpr = window.devicePixelRatio || 1;

    canvas.width = Math.round(size * dpr);
    canvas.height = Math.round(size * dpr);

    var ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size, size);

    var cx = size / 2, cy = size / 2;
    var a0 = -SWEEP / 2 * DEG;
    var a1 = SWEEP / 2 * DEG;
    var i, j;

    /* 细刻度：外缘统一 r=63，主刻度更长 */
    for (i = 0; i <= 50; i++) {
      var v = i * 2;
      var a = a0 + (a1 - a0) * (v / KNOB_MAX);
      var major = (i % 5 === 0);
      var r1 = major ? 67.5 : 73.8;
      var r2 = 77.5;

      ctx.beginPath();
      ctx.moveTo(cx + Math.sin(a) * r1, cy - Math.cos(a) * r1);
      ctx.lineTo(cx + Math.sin(a) * r2, cy - Math.cos(a) * r2);
      ctx.lineWidth = major ? 1.4 : 0.9;
      ctx.strokeStyle = major
        ? "rgba(178,180,196,0.52)"
        : "rgba(140,142,158,0.26)";
      ctx.stroke();
    }

    /* 数字：白色 + 黑色投影（压在黑色面板上） */
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "italic 600 13.8px 'Palatino Linotype', 'Book Antiqua', Georgia, serif";

    for (j = 0; j <= 10; j++) {
      var val = j * 10;
      var aa = a0 + (a1 - a0) * (val / KNOB_MAX);
      var rr = 82.5;
      var x = cx + Math.sin(aa) * rr;
      var y = cy - Math.cos(aa) * rr;

      ctx.fillStyle = "rgba(0,0,0,0.88)";
      ctx.fillText(String(val), x, y + 1);
      ctx.fillStyle = "rgba(242,238,224,0.97)";
      ctx.fillText(String(val), x, y);
    }
  };

  /* ================= VU 表 ================= */
  /* 刻度（不等距，右侧密集）：值 → 角度（度；0 = 正上，右正左负）
     参考 UAD LA-2A 真表：-20 在左下、+3 在右上，弧几乎占满表盘宽度。
     之前用 -52..+18 再乘 1.35 得到 -70..+24（左偏且弧度小），
     现在把角度表本身做对称（-52..+30），并且不再乘增益。 */
  var ARC_GAIN = 1.0;

  var VU_POINTS = [
    [-20, -49.6], [-10, -39.3], [-7, -29.8], [-5, -24.1], [-3, -11.1],
    [-1, 0.8], [0, 13.3], [1, 22.7], [2, 34.2], [3, 46.9]
  ];

  /* 指针映射：
       "scale"  —— 按刻度表插值（0 dB 落在刻度「0」，>0 进红区）← 与表盘印的数字对齐
       "linear" —— 需求原文的线性映射（0 dB 顶到 +3、-20 dB 顶到 -20）
     两种都不越界：超出范围一律顶到端点。 */
  var NEEDLE_MODE = "scale";

  var NEEDLE_TOP = 18;
  var NEEDLE_BOTTOM = -52;
  var NEEDLE_DB_SPAN = 20;

  function vuAngle(v) {   /* 刻度线位置（度），已含 ARC_GAIN */
    if (v <= VU_POINTS[0][0]) return VU_POINTS[0][1] * ARC_GAIN;
    for (var i = 1; i < VU_POINTS.length; i++) {
      if (v <= VU_POINTS[i][0]) {
        var a = VU_POINTS[i - 1], b = VU_POINTS[i];
        var f = (v - a[0]) / (b[0] - a[0]);
        return (a[1] + f * (b[1] - a[1])) * ARC_GAIN;
      }
    }
    return VU_POINTS[VU_POINTS.length - 1][1] * ARC_GAIN;
  }

  function needleAngle(vDb) {
    if (NEEDLE_MODE === "linear") {
      var t = clamp((vDb + NEEDLE_DB_SPAN) / NEEDLE_DB_SPAN, 0, 1);
      return (NEEDLE_BOTTOM + t * (NEEDLE_TOP - NEEDLE_BOTTOM)) * ARC_GAIN * DEG;
    }
    return vuAngle(clamp(vDb, -20, 3)) * DEG;
  }

  var meter = {
    canvas: null,
    ctx: null,
    dpr: 1,
    w: 0,
    h: 0,
    face: null,      /* 静态层（刻度、文字） */
    px: 0, py: 0, R: 0,
    caption: "GAIN REDUCTION"
  };

  /* 主刻度（值, 是否长刻度）：右侧 0..+3 密集 */
  var TICKS = [
    [-20, 1], [-17, 0], [-14, 0], [-12, 0], [-10, 1], [-8.5, 0],
    [-7, 1], [-6, 0], [-5, 1], [-4, 0], [-3, 1], [-2.5, 0],
    [-1.5, 0], [-1, 1], [-0.5, 0], [0, 1], [0.5, 0],
    [1, 1], [1.5, 0], [2, 1], [2.5, 0], [3, 1]
  ];

  function setupMeter() {
    var c = $("meterCanvas");
    if (!c) return false;

    var rect = c.getBoundingClientRect();
    meter.dpr = window.devicePixelRatio || 1;
    meter.w = Math.round(rect.width) || 236;
    meter.h = Math.round(rect.height) || 104;

    c.width = Math.round(meter.w * meter.dpr);
    c.height = Math.round(meter.h * meter.dpr);

    meter.canvas = c;
    meter.ctx = c.getContext("2d");
    meter.ctx.setTransform(meter.dpr, 0, 0, meter.dpr, 0, 0);

    /* 针轴贴面底；弧半径放大到接近面高（真表的弧顶几乎顶到表盘上沿），
       因为刻度弧 -52°..+30° 的角平分线偏左 11°，弧心要右移一点才在面里居中 */
    /* 实测 UAD（ref-UAD.png 拟合）：
         圆心在表盘高度的 138%（针轴藏在表框后面，不在可见面上）
         半径 = 表盘高度 × 1.00
         弧角 -54° .. +50°（几乎对称，0 刻度在正上方）
       之前 py 放在画布底部、R 用 0.96×h，弧被压扁且右端收太早。 */
    meter.py = meter.h * 1.30;
    meter.R = Math.min(meter.h * 1.00, meter.w * 0.54);
    meter.px = meter.w * 0.5;

    rebuildFace();
    return true;
  }

  function rebuildFace() {
    if (!meter.w || !meter.h) return;

    meter.face = document.createElement("canvas");
    meter.face.width = Math.round(meter.w * meter.dpr);
    meter.face.height = Math.round(meter.h * meter.dpr);

    var ctx = meter.face.getContext("2d");
    ctx.setTransform(meter.dpr, 0, 0, meter.dpr, 0, 0);
    drawMeterFace(ctx);
  }

  function drawMeterFace(ctx) {
    var px = meter.px, py = meter.py, R = meter.R;
    /* 字号按画布宽度等比缩放（基准宽度 186px）——表放大时字跟着放大 */
    var F = meter.w / 186;
    var i;
    var a0 = (vuAngle(-20) - 4 - 90) * DEG;
    var a1 = (vuAngle(3) + 4 - 90) * DEG;

    /* 主弧线：-20 一直画到 +3（红区盖在上面，弧是连续的） */
    ctx.beginPath();
    ctx.arc(px, py, R, a0, a1, false);
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = "rgba(32,24,12,0.90)";
    ctx.stroke();

    /* 红区：+1 .. +3，和主弧【同一半径】只是加粗换色，接在弧的末端 */
    ctx.beginPath();
    ctx.arc(px, py, R, (vuAngle(1) - 90) * DEG, a1, false);
    ctx.lineWidth = 4.2;
    ctx.strokeStyle = "rgba(172,26,16,0.95)";
    ctx.stroke();

    /* 刻度线：由弧线【向内】长（真表的短刻度都在弧内侧） */
    for (i = 0; i < TICKS.length; i++) {
      var tv = TICKS[i][0];
      var major = TICKS[i][1] === 1;
      var a = vuAngle(tv) * DEG;
      var r2 = R - (major ? 6 : 3.5);
      var red = (tv >= 1);

      ctx.beginPath();
      ctx.moveTo(px + Math.sin(a) * R, py - Math.cos(a) * R);
      ctx.lineTo(px + Math.sin(a) * r2, py - Math.cos(a) * r2);
      ctx.lineWidth = major ? 1.3 : 0.8;
      ctx.strokeStyle = red
        ? "rgba(150,22,12,0.92)"
        : "rgba(36,28,16,0.86)";
      ctx.stroke();
    }

    /* 数字：印在弧【内侧】（真表排版），沿弧线旋转 */
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "700 " + (6 * F) + "px Georgia, 'Times New Roman', serif";

    for (i = 0; i < VU_POINTS.length; i++) {
      var val = VU_POINTS[i][0];
      var ang = vuAngle(val) * DEG;
      var txt = (val > 0) ? ("+" + val) : String(val);

      /* 越靠右（+1..+3）数字往下挪一点，避开红带 */
      var lr = R - (val >= 1 ? 15 : 13);

      var x = px + Math.sin(ang) * lr;
      var y = py - Math.cos(ang) * lr;

      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(ang);

      ctx.fillStyle = "rgba(255,250,236,0.45)";
      ctx.fillText(txt, 0, 1);
      ctx.fillStyle = (val >= 1) ? "rgba(152,20,12,0.98)" : "rgba(38,30,17,0.95)";
      ctx.fillText(txt, 0, 0);

      ctx.restore();
    }

    /* 顶部两角的 VU 字样（照真表左右对称） */
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "italic 700 " + (9 * F) + "px Georgia, 'Times New Roman', serif";
    ctx.fillStyle = "rgba(64,50,26,0.88)";
    ctx.fillText("VU", 16 * F, 14 * F);
    ctx.fillText("VU", meter.w - 16 * F, 14 * F);

    /* 中央厂牌 */
    ctx.textAlign = "center";
    ctx.font = "italic 700 " + (8 * F) + "px Georgia, 'Times New Roman', serif";
    ctx.fillStyle = "rgba(52,38,18,0.90)";
    ctx.fillText("TOiRE", px, py - 36 * F);

    ctx.font = "700 " + (4.2 * F) + "px 'Segoe UI', Arial, sans-serif";
    ctx.fillStyle = "rgba(84,64,34,0.88)";
    ctx.fillText("E N G I N E E R I N G   C O.", px, py - 27 * F);

    /* 底部小字（随表头模式变化） */
    var cap = (meter.caption === "OUTPUT LEVEL")
      ? "O U T P U T   L E V E L"
      : "G A I N   R E D U C T I O N";
    ctx.font = "700 " + (4.6 * F) + "px 'Segoe UI', Arial, sans-serif";
    ctx.fillStyle = "rgba(74,56,28,0.88)";
    ctx.fillText(cap, px, py - 10 * F);
  }

  function drawNeedle(vDb) {
    var ctx = meter.ctx;
    if (!ctx) return;

    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, meter.canvas.width, meter.canvas.height);
    if (meter.face) ctx.drawImage(meter.face, 0, 0);
    ctx.setTransform(meter.dpr, 0, 0, meter.dpr, 0, 0);

    var px = meter.px, py = meter.py, R = meter.R;
    var a = needleAngle(vDb);

    /* 指针投影 */
    ctx.save();
    ctx.translate(px, py);
    ctx.rotate(a);
    /* 针体 + 柔和投影一次画完：用 canvas 原生 shadowBlur，
       光源在左上 → 影子落到右下。比叠多层线更干净（不会变成"雾"）。 */
    ctx.lineCap = "round";
    /* 投影：三层，偏移量逐层加大、宽度逐层收窄 —— 形成"从针身向外逐渐变淡"的衰减，
       而不是一条和针分家的灰带。偏移在屏幕空间，所以要先反旋 a。 */
    var i2, layers = [
      [1.6, 1.9, 3.8, 0.13],
      [2.3, 2.7, 2.6, 0.19],
      [3.0, 3.5, 1.6, 0.23]
    ];
    for (i2 = 0; i2 < layers.length; i2++) {
      var lx = layers[i2][0], ly = layers[i2][1];
      var rx = lx * Math.cos(-a) - ly * Math.sin(-a);
      var ry = lx * Math.sin(-a) + ly * Math.cos(-a);
      ctx.save();
      ctx.translate(rx, ry);
      ctx.beginPath();
      ctx.moveTo(0, 10);
      ctx.lineTo(0, -(R - 2));
      ctx.lineWidth = layers[i2][2];
      ctx.strokeStyle = "rgba(46,30,10," + layers[i2][3] + ")";
      ctx.stroke();
      ctx.restore();
    }
    ctx.beginPath();
    ctx.moveTo(0, 10);
    ctx.lineTo(0, -(R - 2));
    ctx.lineWidth = 2.0;
    ctx.strokeStyle = "rgba(26,20,12,0.97)";
    ctx.stroke();
    /* 关掉投影（高光由下面的针体那一趟画，避免画两遍） */
    ctx.shadowColor   = "transparent";
    ctx.shadowBlur    = 0;
    ctx.shadowOffsetX = 0;
    ctx.shadowOffsetY = 0;
    ctx.restore();

    /* 指针本体（细黑，1.5px，从底部中心摆出） */
    ctx.save();
    ctx.translate(px, py);
    ctx.rotate(a);
    ctx.beginPath();
    ctx.moveTo(0, 10);
    ctx.lineTo(0, -(R - 2));
    ctx.lineCap = "round";
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = "rgba(22,17,10,0.96)";
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(0, -8);
    ctx.lineTo(0, -(R - 7));
    ctx.lineWidth = 0.45;
    ctx.strokeStyle = "rgba(255,255,255,0.34)";
    ctx.stroke();
    ctx.restore();

    /* 轴心 */
    var g = ctx.createRadialGradient(px - 1.8, py - 1.8, 0.4, px, py, 6);
    g.addColorStop(0, "#bcbcb1");
    g.addColorStop(0.5, "#5d5d55");
    g.addColorStop(1, "#141310");

    ctx.beginPath();
    ctx.arc(px, py, 5.4, 0, Math.PI * 2);
    ctx.fillStyle = g;
    ctx.fill();
    ctx.lineWidth = 0.7;
    ctx.strokeStyle = "rgba(20,16,8,0.90)";
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(px - 3.4, py);
    ctx.lineTo(px + 3.4, py);
    ctx.lineWidth = 0.9;
    ctx.strokeStyle = "rgba(18,14,8,0.80)";
    ctx.stroke();
  }

  /* ================= 3 位表头开关（meterMode）================= */
  var mmState = null;
  var meterMode = -1;

  function modeFromState() {
    if (!mmState) return 0;
    var n = 0;
    try { n = mmState.getNormalisedValue(); } catch (e) { n = 0; }
    if (typeof n !== "number" || !isFinite(n)) n = 0;
    return clamp(Math.round(n * 2), 0, 2);
  }

  function applyMode(m) {
    m = clamp(m, 0, 2);
    if (m === meterMode) return;
    meterMode = m;

    var opts = document.querySelectorAll(".mm-opt");
    for (var i = 0; i < opts.length; i++) {
      var on = (parseInt(opts[i].getAttribute("data-mode"), 10) === m);
      opts[i].className = on ? "mm-opt is-on" : "mm-opt";
    }

    var thumb = $("mmThumb");
    if (thumb) thumb.style.left = (3 + m * 13) + "px";

    var cap = $("grCap");
    if (cap) {
      cap.textContent = (m === 0) ? "GAIN REDUCTION"
                      : (m === 1) ? "OUTPUT +10" : "OUTPUT +4";
    }

    meter.caption = (m === 0) ? "GAIN REDUCTION" : "OUTPUT LEVEL";
    rebuildFace();
  }

  function setMode(m, push) {
    applyMode(m);

    if (push && mmState) {
      /* 归一化写法（0 / 0.5 / 1）；properties 还没到就别乱写，等回读 */
      var p = mmState.properties || {};
      var end = (typeof p.end === "number") ? p.end : 0;
      if (end >= 2) mmState.setNormalisedValue(m / end);
    }
  }

  function initMeterMode() {
    var i, opts = document.querySelectorAll(".mm-opt");

    for (i = 0; i < opts.length; i++) {
      (function (el) {
        el.addEventListener("click", function (ev) {
          ev.preventDefault();
          setMode(parseInt(el.getAttribute("data-mode"), 10), true);
        });
      })(opts[i]);
    }

    var rail = $("mmRail");
    if (rail) {
      rail.addEventListener("click", function (ev) {
        var r = rail.getBoundingClientRect();
        var y = ev.clientY - r.top;
        var m = (y < r.height / 3) ? 0 : ((y < r.height * 2 / 3) ? 1 : 2);
        setMode(m, true);
      });
    }

    try { mmState = window.__JUCE__.getSliderState("meterMode"); } catch (e) { mmState = null; }

    if (mmState) {
      mmState.valueChangedEvent.addListener(function () { applyMode(modeFromState()); });
      mmState.propertiesChangedEvent.addListener(function () { applyMode(modeFromState()); });
    }

    applyMode(modeFromState());
  }

  /* ================= 主逻辑 ================= */
  var targetV = 0;
  var showedV = 0;
  var lastFrame = -1;
  var lastFrameAt = -1e9;
  var grNum = null, t4GlowEl = null, t4CoreEl = null;
  var started = false;

  function fmtDb(v) {
    if (v > 0.05) return "+" + v.toFixed(1);
    if (v < -0.05) return v.toFixed(1);
    return "0.0";
  }

  function onTelemetry(d) {
    if (!d || typeof d.length !== "number" || d.length < 5) return;

    var f = d[4];
    if (f !== lastFrame) {
      lastFrame = f;
      lastFrameAt = nowMs();
    }

    var g = d[0];
    if (typeof g === "number" && isFinite(g)) {
      targetV = clamp(g, -60, 20);
    }
  }

  /* 帧内缓存：值没变就不重绘 / 不写 DOM —— 空闲时把整屏 canvas 重绘省掉 */
  var lastNeedleV = 1e9, lastGrText = "", lastT4At = 0;

  function frame() {
    /* 先排下一帧：即使本帧抛异常，动画链也不会断（否则表针永久冻结） */
    window.requestAnimationFrame(frame);
    try {
      frameBody();
    } catch (e) {
      reportError("frame", e);
    }
  }

  function frameBody() {
    var alive = (nowMs() - lastFrameAt) < 300;
    var want = alive ? targetV : 0;

    /* 弹道在 C++，这里只做 30 Hz → 60 fps 的帧间插值 */
    var k = (want < showedV) ? 0.75 : 0.55;
    showedV += (want - showedV) * k;
    if (Math.abs(want - showedV) < 0.02) showedV = want;

    if (showedV !== lastNeedleV) {
      drawNeedle(showedV);
      lastNeedleV = showedV;

      if (grNum) {
        var txt = fmtDb(showedV);
        if (txt !== lastGrText) { grNum.textContent = txt; lastGrText = txt; }
      }
    }

    /* T4 呼吸：周期 4.6 s，20 Hz 更新足够，不必每帧写 style */
    var t4now = nowMs();
    if (t4now - lastT4At >= 50) {
      lastT4At = t4now;
      var t4g = clamp(Math.abs(showedV) / 9, 0, 1);
      var t4breath = 0.5 + 0.5 * Math.sin (t4now * 0.001 * (2 * Math.PI / 4.6));
      if (t4GlowEl)
        t4GlowEl.style.opacity = ((0.30 + 0.50 * t4g) * (0.72 + 0.28 * t4breath)).toFixed(3);
      if (t4CoreEl)
        t4CoreEl.style.opacity = ((0.35 + 0.45 * t4g) * (0.80 + 0.20 * t4breath)).toFixed(3);
    }
  }

  function initMeterTune() {
    bindMeterParam("meterAttack", document.getElementById("mAtk"), document.getElementById("mAtkNum"));
    bindMeterParam("meterRelease", document.getElementById("mRel"), document.getElementById("mRelNum"));
  }

  /* 表头弹道：两个 range 控件，和 C++ 的 meterAttack / meterRelease 双向绑定 */
  function bindMeterParam(name, inputEl, numEl) {
    if (!inputEl || !numEl) return;

    var st = null;
    try { st = window.__JUCE__.getSliderState(name); } catch (e) { st = null; }
    if (!st) return;

    var rangeOf = function () {
      var p = st.properties || {};
      var lo = (typeof p.start === "number") ? p.start : 0;
      var hi = (typeof p.end === "number") ? p.end : 1;
      if (!(hi > lo)) hi = lo + 1;
      return { lo: lo, hi: hi };
    };

    var pull = function () {
      var r = rangeOf();
      var v = Math.round(st.getScaledValue());
      inputEl.min = r.lo;
      inputEl.max = r.hi;
      inputEl.value = v;
      numEl.textContent = v;
    };

    inputEl.addEventListener("input", function () {
      var r = rangeOf();
      var v = parseFloat(inputEl.value);
      numEl.textContent = Math.round(v);
      st.setNormalisedValue((v - r.lo) / (r.hi - r.lo));
    });

    st.valueChangedEvent.addListener(pull);
    st.propertiesChangedEvent.addListener(pull);
    pull();
  }

  function init() {
    if (started) return;
    started = true;

    if (!window.__JUCE__ || !window.__JUCE__.backend) {
      throw new Error("window.__JUCE__ bridge missing");
    }

    var gainKnob = new Knob({
      param: "makeupGain", unit: "gainUnit", rot: "gainRot",
      num: "gainNum", dial: "gainDial", def: 50
    });

    var peakKnob = new Knob({
      param: "peakReduction", unit: "peakUnit", rot: "peakRot",
      num: "peakNum", dial: "peakDial", def: 30
    });

    window.__JUCE__.backend.addEventListener("telemetry", onTelemetry);

    grNum = $("grNum");
    t4GlowEl = $("t4Glow");
    t4CoreEl = $("t4Core");

    if (!setupMeter()) throw new Error("meter canvas missing");

    /* 3 位表头开关（必须在 setupMeter 之后，切档要重画静态层） */
    initMeterMode();

    var resizePending = false;
    window.addEventListener("resize", function () {
      /* 拖窗口会连发几十次 resize，每次都要重画两个刻度盘 + 重建表盘位图。
         合并到下一帧，一帧只做一次。 */
      if (resizePending) return;
      resizePending = true;
      window.requestAnimationFrame(function () {
        resizePending = false;
        try {
          gainKnob.drawDial();
          peakKnob.drawDial();
          setupMeter();
          lastNeedleV = 1e9;   /* 画布被重建了，强制下一帧重绘指针 */
        } catch (e) { reportError("resize", e); }
      });
    });

    /* ============ 出厂预设下拉 ============ */
    /* 名称必须与 C++ 的 src/Presets/FactoryPresets.h 一一对应 */
    var PRESET_NAMES = [
      "Init", "Light Comp",
      "Vocal 1", "Vocal 2", "Vocal 3", "Vocal 4",
      "Bass", "Bass Limit",
      "Snare Color", "Snare Limiter",
      "Roomy", "Mix Bus"
    ];

    function initPresets () {
      var sel = document.getElementById("presetSelect");
      var prev = document.getElementById("presetPrev");
      var next = document.getElementById("presetNext");
      if (!sel) return;

      var st = null;
      try { st = window.__JUCE__.getSliderState("preset"); } catch (e) { st = null; }

      /* 填充下拉 */
      var i;
      for (i = 0; i < PRESET_NAMES.length; i++) {
        var opt = document.createElement("option");
        opt.value = String(i);
        opt.textContent = PRESET_NAMES[i];
        sel.appendChild(opt);
      }

      /* ── 用户预设：存 / 读文件（走 C++ 的原生文件对话框）── */
      var emitPresetAction = function (action) {
        try {
          if (window.__JUCE__ && window.__JUCE__.backend && window.__JUCE__.backend.emitEvent) {
            window.__JUCE__.backend.emitEvent("presetAction", { action: action });
          }
        } catch (e) { /* ignore */ }
      };
      var btnSave = document.getElementById("presetSave");
      var btnLoad = document.getElementById("presetLoad");
      if (btnSave) btnSave.addEventListener("click", function () { emitPresetAction("save"); });
      if (btnLoad) btnLoad.addEventListener("click", function () { emitPresetAction("load"); });

      /* C++ 保存/载入完成后回传名字 → 在下拉里加一个"用户预设"项并选中 */
      try {
        window.__JUCE__.backend.addEventListener("presetLoaded", function (payload) {
          var name = (payload && payload.length) ? String(payload[0]) : "";
          if (!name) return;
          var opt = null, i2;
          for (i2 = 0; i2 < sel.options.length; i2++) {
            if (sel.options[i2].getAttribute("data-user") && sel.options[i2].textContent === name) {
              opt = sel.options[i2];
              break;
            }
          }
          if (!opt) {
            opt = document.createElement("option");
            opt.textContent = name;
            opt.setAttribute("data-user", "1");
            opt.style.color = "#9fd6a8";     /* 用户预设用绿色区分 */
            sel.appendChild(opt);
          }
          /* 用户预设不改写 preset 参数，只切换显示 */
          sel.value = opt.value;
        });
      } catch (e) { /* ignore */ }

      /* 最后一次选中的「出厂预设」号。用户预设项没有数字 value，
         点 ‹ / › 时必须靠它而不是 parseInt(sel.value)（否则 = NaN，下拉永久空白）*/
      var lastIndex = 0;

      var applyIndex = function (idx) {
        if (!isFinite(idx)) idx = 0;
        idx = clamp(idx, 0, PRESET_NAMES.length - 1);
        lastIndex = idx;
        sel.value = String(idx);
        if (st) st.setNormalisedValue(idx / (PRESET_NAMES.length - 1));
      };

      sel.addEventListener("change", function () {
        var opt = sel.options[sel.selectedIndex];
        if (opt && opt.getAttribute("data-user")) {
          /* 用户预设：让 C++ 按名字去读同目录下的 <名字>.toire2a */
          emitPresetAction("load:" + opt.textContent);
          return;
        }
        applyIndex(parseInt(sel.value, 10));
      });
      if (prev) prev.addEventListener("click", function () { applyIndex(lastIndex - 1); });
      if (next) next.addEventListener("click", function () { applyIndex(lastIndex + 1); });

      /* C++ 侧（宿主 Program 菜单）改了 → 同步下拉 */
      if (st) {
        var pull = function () {
          var n = st.getNormalisedValue();
          var idx = Math.round(n * (PRESET_NAMES.length - 1));
          if (String(idx) !== sel.value) sel.value = String(idx);
        };
        st.valueChangedEvent.addListener(pull);
        st.propertiesChangedEvent.addListener(pull);
        pull();
      }
    }

    /* 两个装饰开关：点击切换高光（COMP/LIMIT 与 POWER） */
    try {
      var swLeft = document.querySelector(".sw-left");
      if (swLeft) swLeft.addEventListener("click", function () {
        swLeft.className = swLeft.className.indexOf("alt") >= 0 ? "switch sw-left" : "switch sw-left alt";
      });
      var swRight = document.querySelector(".sw-right");
      var bypassState = null;
      try { bypassState = window.__JUCE__.getSliderState("bypass"); } catch (e2) { bypassState = null; }
      var bypassed = false;
      /* 注意：块内函数声明在 ES5 严格模式下是 SyntaxError，用函数表达式 */
      var syncBypass = function (on) {
        bypassed = on;
        if (swRight) swRight.className = bypassed ? "switch sw-right alt" : "switch sw-right";
        if (bypassState) bypassState.setNormalisedValue(bypassed ? 1 : 0);
      };
      if (bypassState) {
        bypassState.valueChangedEvent.addListener(function () {
          var on = bypassState.getNormalisedValue() > 0.5;
          if (on !== bypassed) {
            bypassed = on;
            if (swRight) swRight.className = bypassed ? "switch sw-right alt" : "switch sw-right";
          }
        });
      }
      if (swRight) swRight.addEventListener("click", function () { syncBypass(!bypassed); });
    } catch (e) { /* ignore */ }

    /* 表头弹道控件（meterAttack / meterRelease） */
    try { initMeterTune(); } catch (e) { /* 参数不存在时忽略 */ }

    /* 出厂预设下拉 */
    try { initPresets(); } catch (e2) { /* 参数不存在时忽略 */ }

    /* 首帧：即使还没有遥测也要把表画出来 */
    window.requestAnimationFrame(frame);

    /* 告诉 C++：页面这边准备好了（诊断用，可选） */
    try {
      if (window.__JUCE__.backend.emitEvent) {
        window.__JUCE__.backend.emitEvent("ui-ready", { t: nowMs() });
      }
    } catch (e) { /* ignore */ }
  }

  function boot() {
    /* 全局兜底：任何未捕获异常 / Promise 拒绝都进红条 */
    window.onerror = function (msg, src, line, col, err) {
      reportError("window.onerror", err || new Error(msg + " @" + src + ":" + line));
      return false;
    };
    window.addEventListener("unhandledrejection", function (ev) {
      reportError("unhandledrejection", ev && ev.reason ? ev.reason : new Error("rejected"));
    });

    try {
      init();
    } catch (e) {
      reportError("FATAL", e);
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", boot);
  } else {
    boot();
  }
})();
