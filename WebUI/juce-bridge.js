/* =====================================================================
   juce-bridge.js —— 极简 JUCE 8 Web 前端桥（滑块 relay 协议）

   WebView2 里 JUCE 会在「文档创建」阶段注入 window.__JUCE__
   （backend + initialisationData），官方前端框架是一个 ES module；
   WebView2 里 ES module 不可靠，这里按同一套事件协议实现一个等价物，
   这样页面只用传统 <script> 标签。

   协议（与 JUCE 8.0.7 juce_WebControlRelayEvents.h / index.js 完全一致）：

     C++ → JS : { eventType:"propertiesChanged", start, end, skew, interval, ... }
                { eventType:"valueChanged", value:<scaled 值，本插件是 0..100> }
     JS → C++ : { eventType:"valueChanged", value:<scaled 值> }
                { eventType:"sliderDragStarted" } / { eventType:"sliderDragEnded" }
                { eventType:"requestInitialUpdate" }

   另外：直接用浏览器打开 index.html 时（没有 __JUCE__），
   这里装一个 mock，方便不启动插件也能调 UI。
   ===================================================================== */
(function () {
  "use strict";

  /* ---------------- 监听列表 ---------------- */
  function makeListeners() {
    var fns = [];
    return {
      addListener: function (fn) { fns.push(fn); return fns.length - 1; },
      removeListener: function (i) { if (i >= 0 && i < fns.length) fns[i] = null; },
      call: function () {
        for (var i = 0; i < fns.length; i++) {
          if (!fns[i]) continue;
          try { fns[i](); } catch (e) {
        /* 单个监听器出错不拖垮整条链，但必须上报（DAW 里没有 DevTools） */
        if (window.__toireReportError) window.__toireReportError("listener", e);
      }
        }
      }
    };
  }

  /* ---------------- 滑块状态 ---------------- */
  function SliderState(name) {
    this.name = name;
    this.identifier = "__juce__slider" + name;
    this.scaledValue = 0;
    this.properties = {
      start: 0, end: 100, skew: 1, interval: 0,
      name: name, label: "", numSteps: 1000, parameterIndex: -1
    };
    this.valueChangedEvent = makeListeners();
    this.propertiesChangedEvent = makeListeners();

    var self = this;
    window.__JUCE__.backend.addEventListener(this.identifier, function (ev) {
      self.handleEvent(ev);
    });

    /* 页面加载完成后再请求初值：C++ 侧构造 attachment 时页面还没加载，
       那次 initialUpdate 会被丢掉，所以这里补一次。 */
    window.__JUCE__.backend.emitEvent(this.identifier, { eventType: "requestInitialUpdate" });
  }

  SliderState.prototype.handleEvent = function (ev) {
    if (!ev) return;

    if (ev.eventType === "valueChanged") {
      if (typeof ev.value === "number") {
        this.scaledValue = ev.value;
        this.valueChangedEvent.call();
      }
    } else if (ev.eventType === "propertiesChanged") {
      if (typeof ev.start === "number")    this.properties.start = ev.start;
      if (typeof ev.end === "number")      this.properties.end = ev.end;
      if (typeof ev.skew === "number" && ev.skew > 0) this.properties.skew = ev.skew;
      if (typeof ev.interval === "number") this.properties.interval = ev.interval;
      if (ev.name)    this.properties.name = String(ev.name);
      if (ev.label)   this.properties.label = String(ev.label);
      this.propertiesChangedEvent.call();
    }
  };

  SliderState.prototype.getScaledValue = function () {
    return this.scaledValue;
  };

  SliderState.prototype.getNormalisedValue = function () {
    var p = this.properties;
    var span = p.end - p.start;
    if (span === 0) return 0;
    var n = (this.scaledValue - p.start) / span;
    if (n < 0) n = 0;
    if (n > 1) n = 1;
    return Math.pow(n, p.skew);
  };

  SliderState.prototype.normalisedToScaledValue = function (n) {
    var p = this.properties;
    if (typeof n !== "number" || !isFinite(n)) return this.scaledValue;
    if (n < 0) n = 0;
    if (n > 1) n = 1;
    return Math.pow(n, 1 / p.skew) * (p.end - p.start) + p.start;
  };

  SliderState.prototype.snapToLegalValue = function (v) {
    var p = this.properties;
    if (!p.interval) return v;
    var snapped = p.start + p.interval * Math.floor((v - p.start) / p.interval + 0.5);
    if (snapped < p.start) snapped = p.start;
    if (snapped > p.end) snapped = p.end;
    return snapped;
  };

  SliderState.prototype.setScaledValue = function (v) {
    this.scaledValue = this.snapToLegalValue(v);
    window.__JUCE__.backend.emitEvent(this.identifier, {
      eventType: "valueChanged", value: this.scaledValue
    });
  };

  SliderState.prototype.setNormalisedValue = function (n) {
    this.setScaledValue(this.normalisedToScaledValue(n));
  };

  SliderState.prototype.sliderDragStarted = function () {
    window.__JUCE__.backend.emitEvent(this.identifier, { eventType: "sliderDragStarted" });
  };

  SliderState.prototype.sliderDragEnded = function () {
    window.__JUCE__.backend.emitEvent(this.identifier, { eventType: "sliderDragEnded" });
  };

  /* ---------------- 浏览器预览用的 mock ----------------
     直接双击 index.html（没有 JUCE 后端）时也要能看 UI。 */
  if (typeof window.__JUCE__ === "undefined" ||
      !window.__JUCE__ ||
      !window.__JUCE__.backend) {
    /* 注意：插件本身也是用 file:// 加载界面的，和"浏览器预览"无法区分，
       所以不能靠协议判断。这里照常装 mock（保证预览可用），
       但**必须让用户看见"这是演示模式"** —— 否则界面看着完全正常、
       其实一个参数都没连上 C++，是最难排查的故障。 */
    installMock();

    if (window.__toireReportError) {
      window.__toireReportError ("bridge",
        new Error ("MOCK MODE —— 没有连上 C++（window.__JUCE__ 未注入）。" +
                   "界面可以点但不生效，请检查 WebView2 运行时是否正常。"));
    }
  }

  /* ---------------- 注册到 window.__JUCE__ ---------------- */
  var states = {};

  window.__JUCE__.getSliderState = function (name) {
    if (!states[name]) states[name] = new SliderState(name);
    return states[name];
  };

  function installMock() {
    var listeners = {};
    var values = {
      peakReduction: 30, makeupGain: 50,
      meterAttack: 62, meterRelease: 110, meterMode: 0, bypass: 0, preset: 0
    };
    var defs = {
      peakReduction: { start: 0, end: 100, skew: 1, interval: 0.1 },
      makeupGain:    { start: 0, end: 100, skew: 1, interval: 0.1 },
      meterAttack:   { start: 5, end: 300, skew: 1, interval: 1 },
      meterRelease:  { start: 30, end: 2000, skew: 1, interval: 1 },
      meterMode:     { start: 0, end: 2, skew: 1, interval: 1 },
      bypass:        { start: 0, end: 1, skew: 1, interval: 1 },
      preset:        { start: 0, end: 11, skew: 1, interval: 1 }
    };

    function fire(id, payload) {
      var arr = listeners[id] || [];
      for (var i = 0; i < arr.length; i++) {
        try { arr[i](payload); } catch (e) { /* ignore */ }
      }
    }

    window.__JUCE__ = {
      initialisationData: {
        __juce__platform: ["windows"],
        __juce__functions: [],
        __juce__toggles: [],
        __juce__comboBoxes: [],
        __juce__registeredGlobalEventIds: [],
        __juce__sliders: ["peakReduction", "makeupGain", "meterAttack", "meterRelease", "meterMode", "bypass", "preset"]
      },
      backend: {
        addEventListener: function (id, fn) {
          if (!listeners[id]) listeners[id] = [];
          listeners[id].push(fn);
          return [id, listeners[id].length - 1];
        },
        removeEventListener: function () {},
        emitEvent: function (id, payload) {
          if (!payload) return;
          var name = String(id).replace("__juce__slider", "");

          if (payload.eventType === "requestInitialUpdate") {
            var d = defs[name] || { start: 0, end: 100, skew: 1, interval: 0.1 };
            setTimeout(function () {
              fire(id, {
                eventType: "propertiesChanged",
                start: d.start, end: d.end, skew: d.skew, interval: d.interval,
                name: name, label: "", numSteps: 1001, parameterIndex: 0
              });
              fire(id, { eventType: "valueChanged", value: values[name] });
            }, 0);
          } else if (payload.eventType === "valueChanged") {
            values[name] = payload.value;
            fire(id, { eventType: "valueChanged", value: payload.value });
          }
        },
        emitByBackend: function (id, json) {
          fire(id, typeof json === "string" ? JSON.parse(json) : json);
        }
      }
    };

    /* 假遥测：慢速正弦，方便看表头与 T4 亮度 */
    var t0 = Date.now();
    setInterval(function () {
      var t = (Date.now() - t0) / 1000;
      var g = -9 * (0.55 + 0.45 * Math.sin(t * 1.1)) * (0.65 + 0.35 * Math.sin(t * 2.7 + 1.2));
      fire("telemetry", [g, -12, -6, g, Math.floor(t * 30)]);
    }, 33);
  }
})();
