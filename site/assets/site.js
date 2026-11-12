/* Toile — project site. Tiny vanilla JS: mobile nav + code copy. No network. */
(function () {
  "use strict";
  var nav = document.getElementById("nav");
  var toggle = document.getElementById("nav-toggle");
  if (nav && toggle) {
    toggle.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }
  document.querySelectorAll("pre").forEach(function (block) {
    var btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = "copy";
    btn.className = "copy";
    btn.addEventListener("click", function () {
      var text = block.innerText;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(function () {
          btn.textContent = "copied";
          setTimeout(function () { btn.textContent = "copy"; }, 1500);
        });
      } else {
        btn.textContent = "no clipboard";
      }
    });
    block.style.position = "relative";
    block.appendChild(btn);
  });
})();