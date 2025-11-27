console.log("=== Web Server Starting ===");

if (!EVM || !EVM.ready) {
  console.log("Waiting for EVM...");
  setTimeout(arguments.callee, 100);
  return;
}

console.log("EVM ready. Starting HTTP server...");

// Start background web server
Net.httpListen("80", (req, res) => {
  console.log("HTTP:", req.method, req.url);

  // Main page - HTML with JS
  if (req.url === "/" || req.url === "/index.html") {
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.send(`
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>ESP32 Console</title>
  <style>
    body {background:#000;color:#0f0;font-family:monospace;padding:20px;text-align:center;}
    h1 {font-size:28px;margin:10px;}
    button {padding:15px 30px;margin:10px;font-size:18px;background:#0f0;color:#000;
            border:none;border-radius:10px;cursor:pointer;box-shadow:0 0 10px #0f0;}
    button:hover {background:#0c0;}
    pre {background:#111;color:#fff;padding:15px;margin:20px auto;
         border-radius:8px;text-align:left;max-width:90%;white-space:pre-wrap;
         font-size:14px;border:1px solid #0f0;}
  </style>
</head>
<body>
  <h1>ESP32 Web Console</h1>
  <button onclick="run('status')">Status</button>
  <button onclick="run('heap')">Heap</button>
  <button onclick="run('restart')">Restart</button>
  <button onclick="run('lcd')">LCD Test</button>
  <pre id="output">Connected! Ready.\n</pre>

  <script>
    const out = document.getElementById('output');
    function run(cmd) {
      out.textContent += "> " + cmd + "\\n";
      fetch("/cmd?" + cmd)
        .then(r => r.text())
        .then(text => {
          out.textContent += text + "\\n";
          out.scrollTop = out.scrollHeight;
        })
        .catch(err => out.textContent += "Error: " + err + "\\n");
    }
  </script>
</body>
</html>
    `.trim());
    return;
  }

  // Command handler
  if (req.url.startsWith("/cmd?")) {
    let cmd = req.url.split("?")[1];
    console.log("CMD:", cmd);
    let result = "";

    if (cmd === "status") {
      let s = WiFi.status();
      result = "AP:  " + (s.ap ? "ON  (HadiESP)" : "OFF") + "\\n" +
               "STA: " + (s.sta ? "ON  (" + WiFi.staIP() + ")" : "OFF");
    }
    else if (cmd === "heap") {
      result = "Free Heap: " + heap_caps_get_free_size(0) + " bytes\\n" +
               "Total: " + heap_caps_get_total_size(0) + " bytes";
    }
    else if (cmd === "restart") {
      result = "Restarting ESP32 in 3s...";
      setTimeout(() => esp_restart(), 3000);
    }
    else if (cmd === "lcd") {
      LCD.fill(0xF800); // Red background
      LCD.print(10, 30, "HADI ESP32", 0xFFFF);
      LCD.print(10, 60, "Web Control", 0x07E0);
      result = "LCD Updated! Red screen + text";
    }
    else {
      result = "Unknown command: " + cmd;
    }

    res.setHeader("Content-Type", "text/plain");
    res.send(result);
    return;
  }

  // 404
  res.status(404).send("Not Found");
});

console.log("Web Server RUNNING!");
console.log("Open browser:");
console.log("http://192.168.4.1");
console.log("Or scan QR from phone");
console.log("Server runs in background - JS console is FREE!");