(function () {
  const root = document.documentElement;
  const button = document.getElementById("themeToggle");

  function setTheme(theme) {
    root.setAttribute("data-theme", theme);
    try {
      localStorage.setItem("pcap-theme", theme);
    } catch (_) {}
    button.setAttribute("aria-label", theme === "dark" ? "Switch to light theme" : "Switch to dark theme");
    button.querySelector(".theme-icon").textContent = theme === "dark" ? "☀" : "◐";
  }

  let saved = null;
  try {
    saved = localStorage.getItem("pcap-theme");
  } catch (_) {}

  if (saved === "dark" || saved === "light") {
    setTheme(saved);
  } else if (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches) {
    setTheme("dark");
  } else {
    setTheme("light");
  }

  button.addEventListener("click", function () {
    setTheme(root.getAttribute("data-theme") === "dark" ? "light" : "dark");
  });
})();
