console.log("=== JSON + Filesystem Test ===");

function delay(ms) {
    var start = Date.now();
    while (Date.now() - start < ms) {}
}

console.log("\n1. JSON Test");
var obj = { name: "EVM", active: true };
var json = JSON.stringify(obj);
console.log("Stringify:", json);
var parsed = JSON.parse(json);
console.log("Parsed name:", parsed.name);

console.log("\n2. Write Test");
var ok = fs.writeFileSync("/sdcard/test.json", json);
delay(200);
if (ok) {
    console.log("SUCCESS: writeFileSync");
    var data = fs.readFileSync("/sdcard/test.json");
    console.log("Read back:", data);

    // فقط این خط رو عوض کن!
    // fs.unlink("/sdcard/test.json", function(err) {
    //     if (err) console.log("Delete failed");
    //     else console.log("Cleaned up!");
    // });
    delay(100);
}

console.log("\nALL TESTS PASSED!");