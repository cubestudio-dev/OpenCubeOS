/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS website facade logic:
   - theme: light/dark, defaults to the system preference, manual choice
     persisted in localStorage ("oc-theme"), follows the system again
     only while the user has not overridden it
   - language: EN/ZH, defaults to the browser language, persisted in
     localStorage ("oc-lang"); re-applies every data-i18n /
     data-i18n-html node and re-fills SITE_DATA numbers afterwards
   - mobile nav (hamburger)
   - download page + stats filled from site-data.js (release script) */
(function () {
  "use strict";

  /* =================== theme =================== */
  var THEME_KEY = "oc-theme";
  var themeMedia = window.matchMedia
    ? window.matchMedia("(prefers-color-scheme: dark)") : null;

  function currentTheme() {
    return document.documentElement.getAttribute("data-theme") || "light";
  }
  function setTheme(t, persist) {
    document.documentElement.setAttribute("data-theme", t);
    if (persist) {
      try { localStorage.setItem(THEME_KEY, t); } catch (e) { /* private mode */ }
    }
    updateThemeButton();
  }
  function updateThemeButton() {
    var b = document.getElementById("theme-btn");
    if (!b) return;
    var dark = currentTheme() === "dark";
    b.textContent = dark ? "\u2600\uFE0F" : "\uD83C\uDF19";
    b.setAttribute("aria-label",
      window.OC_LANG === "zh"
        ? (dark ? "\u5207\u6362\u5230\u6D45\u8272\u4E3B\u9898" : "\u5207\u6362\u5230\u6DF1\u8272\u4E3B\u9898")
        : (dark ? "Switch to light theme" : "Switch to dark theme"));
  }
  /* the inline <head> snippet has already resolved the initial theme
     (system preference or the stored override) before CSS paints. */
  if (themeMedia && typeof themeMedia.addEventListener === "function") {
    themeMedia.addEventListener("change", function (ev) {
      /* follow the system ONLY while the user has not chosen manually */
      var stored = null;
      try { stored = localStorage.getItem(THEME_KEY); } catch (e) { /* noop */ }
      if (!stored) setTheme(ev.matches ? "dark" : "light", false);
    });
  }
  var themeBtn = document.getElementById("theme-btn");
  if (themeBtn) {
    themeBtn.addEventListener("click", function () {
      setTheme(currentTheme() === "dark" ? "light" : "dark", true);
    });
  }

  /* =================== language =================== */
  var LANG_KEY = "oc-lang";
  window.OC_LANG = "en";

  function detectLang() {
    /* explicit ?lang=zh|en wins (e.g. links from the zh docs center)
       and is persisted like a manual switch */
    try {
      var q = new URLSearchParams(window.location.search).get("lang");
      if (q === "en" || q === "zh") return q;
    } catch (e) { /* old browsers */ }
    try {
      var saved = localStorage.getItem(LANG_KEY);
      if (saved === "en" || saved === "zh") return saved;
    } catch (e) { /* private mode */ }
    var nav = (navigator.language || navigator.userLanguage || "en").toLowerCase();
    return nav.indexOf("zh") === 0 ? "zh" : "en";
  }

  function applyLang(lang) {
    window.OC_LANG = lang;
    var dict = window.I18N && window.I18N[lang];
    if (!dict) return;
    try { localStorage.setItem(LANG_KEY, lang); } catch (e) { /* noop */ }

    document.documentElement.setAttribute("lang", lang === "zh" ? "zh-CN" : "en");

    /* text nodes */
    var nodes = document.querySelectorAll("[data-i18n]");
    for (var i = 0; i < nodes.length; i++) {
      var k = nodes[i].getAttribute("data-i18n");
      if (dict[k] !== undefined) nodes[i].textContent = dict[k];
    }
    /* html nodes (values may embed <span id=..> placeholders) */
    var htmls = document.querySelectorAll("[data-i18n-html]");
    for (var j = 0; j < htmls.length; j++) {
      var hk = htmls[j].getAttribute("data-i18n-html");
      if (dict[hk] !== undefined) htmls[j].innerHTML = dict[hk];
    }
    /* placeholders / aria labels keyed with -ph / -al suffixes */
    var phs = document.querySelectorAll("[data-i18n-ph]");
    for (var p = 0; p < phs.length; p++) {
      var pk = phs[p].getAttribute("data-i18n-ph");
      if (dict[pk] !== undefined) phs[p].setAttribute("placeholder", dict[pk]);
    }

    /* page title */
    var tt = document.querySelector("title");
    if (tt) {
      var tk = tt.getAttribute("data-i18n");
      if (tk && dict[tk] !== undefined) tt.textContent = dict[tk];
    }

    /* language segment buttons */
    var segs = document.querySelectorAll(".seg button[data-lang]");
    for (var s = 0; s < segs.length; s++) {
      var on = segs[s].getAttribute("data-lang") === lang;
      segs[s].classList.toggle("active", on);
      segs[s].setAttribute("aria-pressed", on ? "true" : "false");
    }

    /* mobile nav toggle aria label */
    var nt = document.getElementById("nav-toggle");
    if (nt && dict["nav.menu"] !== undefined) {
      nt.setAttribute("aria-label", dict["nav.menu"]);
    }

    /* links into the docs center follow the language:
       EN -> docs/..., ZH -> docs/zh/... (idempotent, reversible).
       Runs AFTER the data-i18n-html replacement so dictionary-embedded
       hrefs are normalized too. */
    var docLinks = document.querySelectorAll('a[href^="docs/"]');
    for (var d = 0; d < docLinks.length; d++) {
      var href = docLinks[d].getAttribute("href");
      if (lang === "zh" && href.indexOf("docs/zh/") !== 0) {
        docLinks[d].setAttribute("href", "docs/zh/" + href.slice(5));
      } else if (lang === "en" && href.indexOf("docs/zh/") === 0) {
        docLinks[d].setAttribute("href", href.slice(8));
      }
    }

    updateThemeButton();
    /* dynamic parts of the download page are language-dependent */
    fillSiteData();
  }

  var segButtons = document.querySelectorAll(".seg button[data-lang]");
  for (var sb = 0; sb < segButtons.length; sb++) {
    segButtons[sb].addEventListener("click", function () {
      applyLang(this.getAttribute("data-lang"));
    });
  }

  /* =================== mobile nav =================== */
  var navToggle = document.getElementById("nav-toggle");
  if (navToggle) {
    navToggle.addEventListener("click", function () {
      var open = document.body.classList.toggle("nav-open");
      navToggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
    /* close when a link is picked */
    var links = document.querySelectorAll(".nav-links a");
    for (var l = 0; l < links.length; l++) {
      links[l].addEventListener("click", function () {
        document.body.classList.remove("nav-open");
        navToggle.setAttribute("aria-expanded", "false");
      });
    }
  }

  /* =================== release data =================== */
  function $(id) { return document.getElementById(id); }
  function text(id, v) { var e = $(id); if (e) e.textContent = v; }
  function href(id, v) { var e = $(id); if (e) e.href = v; }
  function fmt(n) {
    return n.toLocaleString ? n.toLocaleString("en-US") : String(n);
  }

  function fillSiteData() {
    var d = window.SITE_DATA;
    if (!d) return;

    /* facade stats (ids may live inside data-i18n-html values) */
    text("stat-cmds", d.stats.commands);
    text("stat-cmds2", d.stats.commands);
    text("stat-ifaces", d.stats.interfaces);
    text("stat-syscalls", d.stats.syscalls);
    text("stat-lines", d.stats.lines);
    text("stat-lines2", d.stats.lines);
    var fb = $("foot-build");
    if (fb) fb.textContent = (window.OC_LANG === "zh" ? "\u6784\u5EFA " : "build ") + d.build;
    var qv = document.querySelectorAll(".qsver, #qs-ver");
    for (var i = 0; i < qv.length; i++) qv[i].textContent = d.version;

    /* download page */
    if ($("dl-version")) {
      text("dl-version", d.version);
      text("dl-build", d.build);
      text("dl-date", d.date);

      var a = d.assets;
      var rows = [["iso", "iso"], ["src", "src"], ["ota", "ota"]];
      for (var r = 0; r < rows.length; r++) {
        var k = rows[r][0];
        var ast = a[k];
        if (!ast) continue;
        text(k + "-name", ast.name);
        var sizeEl = $(k + "-size");
        if (sizeEl) {
          sizeEl.textContent = ast.size_h + " (" + fmt(ast.size) +
            (window.OC_LANG === "zh" ? " \u5B57\u8282)" : " bytes)");
        }
        text(k + "-sha", "SHA256: " + ast.sha256);
        href(k + "-url", ast.url);
      }

      /* history table: current release row + the history array */
      var body = $("hist-body");
      if (body) {
        var html = "";
        if (a.iso) {
          html += "<tr><td class='v'>" + d.version + "</td><td>" + d.date +
                  "</td><td>" + (window.OC_LANG === "zh" ? "\u5F53\u524D" : "current") +
                  "</td><td>" + fmt(a.iso.size) +
                  "</td><td class='sha'>" + a.iso.sha256 + "</td></tr>";
        }
        for (var h = 0; h < (d.history || []).length; h++) {
          var e = d.history[h];
          html += "<tr><td class='v'>" + e.version + "</td><td>" + e.date +
                  "</td><td><a href='downloads/opencube-" + e.version +
                  ".iso'>iso</a></td><td>" + fmt(e.iso_size) +
                  "</td><td class='sha'>" + e.iso_sha + "</td></tr>";
        }
        body.innerHTML = html;
      }
    }

    /* sha click-to-copy (progressive enhancement) */
    var shas = document.querySelectorAll(".sha");
    for (var s2 = 0; s2 < shas.length; s2++) {
      (function (el) {
        if (el.dataset.copyBound) return;
        el.dataset.copyBound = "1";
        var t = function () {
          return window.I18N && window.I18N[window.OC_LANG] || {};
        };
        el.title = t()["sha.copy"] || "Click to copy";
        el.style.cursor = "pointer";
        el.addEventListener("click", function () {
          var txt = el.textContent.replace(/^SHA256:\s*/, "").trim();
          if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(txt).then(function () {
              var old = el.title;
              el.title = t()["sha.copied"] || "Copied!";
              setTimeout(function () { el.title = old; }, 1200);
            });
          }
        });
      })(shas[s2]);
    }
  }

  /* =================== boot =================== */
  applyLang(detectLang());
  /* fillSiteData() is called by applyLang(); keep an explicit call for
     the case where I18N failed to load (dictionary script blocked) */
  fillSiteData();
})();
