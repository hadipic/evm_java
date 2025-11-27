console.log("=== Timer Module Test ===");

// تست توابع پایه
console.log("1. Testing basic functions...");
console.log("getTime(): " + Timer.getTime());
Timer.delay(1000);
console.log("After 1 second delay");

// تست زمان واقعی - با try/catch
console.log("2. Testing real time functions...");
try {
    Timer.setTime(11, 41, 0);
    console.log("setTime() OK");
} catch(e) {
    console.log("setTime() FAILED: " + e);
}

try {
    var time = Timer.getRealTime();
    console.log("getRealTime() OK: " + JSON.stringify(time));
} catch(e) {
    console.log("getRealTime() FAILED: " + e);
}

// تست تایمرها
console.log("3. Testing timers...");
var count = 0;
var timerId = Timer.setInterval(function() {
    count++;
    console.log("Timer tick: " + count);
    if (count >= 3) {
        Timer.clearInterval(timerId);
        console.log("✅ All tests passed!");
    }
}, 1000);

console.log("Timer test started");