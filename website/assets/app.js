/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS website facade logic - fills the download page and the
   stats from site-data.js (regenerated at release time). No frameworks. */
(function () {
  "use strict";
  var d = window.SITE_DATA;
  if (!d) return;

  function $(id) { return document.getElementById(id); }
  function text(id, v) { var e = $(id); if (e) e.textContent = v; }
  function href(id, v) { var e = $(id); if (e) e.href = v; }
  function fmt(n) {
    return n.toLocaleString ? n.toLocaleString("en-US") : String(n);
  }

  /* --- facade stats --- */
  text("stat-cmds", d.stats.commands);
  text("stat-cmds2", d.stats.commands);
  text("stat-ifaces", d.stats.interfaces);
  text("stat-syscalls", d.stats.syscalls);
  text("stat-lines", d.stats.lines);
  text("stat-lines2", d.stats.lines);
  text("foot-build", "build " + d.build);
  var qv = document.querySelectorAll(".qsver, #qs-ver");
  for (var i = 0; i < qv.length; i++) qv[i].textContent = d.version;

  /* --- download page --- */
  if ($("dl-version")) {
    text("dl-version", d.version);
    text("dl-build", d.build);
    text("dl-date", d.date);

    var a = d.assets;
    var rows = [
      ["iso", "iso"], ["src", "src"], ["ota", "ota"]
    ];
    for (var r = 0; r < rows.length; r++) {
      var k = rows[r][0];
      var ast = a[k];
      if (!ast) continue;
      text(k + "-name", ast.name);
      text(k + "-size", ast.size_h + " (" + fmt(ast.size) + " bytes)");
      text(k + "-sha", "SHA256: " + ast.sha256);
      href(k + "-url", ast.url);
    }

    /* history table: current release row + the history array */
    var body = $("hist-body");
    if (body) {
      var html = "";
      if (a.iso) {
        html += "<tr><td class='v'>" + d.version + "</td><td>" + d.date +
                "</td><td>current</td><td>" + fmt(a.iso.size) +
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

  /* --- smooth copy buttons on sha fields (progressive enhancement) --- */
  var shas = document.querySelectorAll(".sha, .dl-card .sha");
  for (var s = 0; s < shas.length; s++) {
    (function (el) {
      el.title = "Click to copy";
      el.style.cursor = "pointer";
      el.addEventListener("click", function () {
        var t = el.textContent.replace(/^SHA256:\s*/, "").trim();
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(t).then(function () {
            var old = el.title;
            el.title = "Copied!";
            setTimeout(function () { el.title = old; }, 1200);
          });
        }
      });
    })(shas[s]);
  }
})();
