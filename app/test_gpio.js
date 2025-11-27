// Test GPIO Module
console.log("=== Testing GPIO Module ===");

console.log("typeof gpio:", typeof gpio);
console.log("typeof gpio.readButton:", typeof gpio.readButton);

if (typeof gpio !== 'undefined' && typeof gpio.readButton === 'function') {
    console.log("✅ gpio module is available");
    
    // تست خواندن دکمه‌ها
    console.log("Testing buttons...");
    for (var i = 0; i < 4; i++) {
        var state = gpio.readButton(i);
        console.log("Button", i + ":", state);
    }
} else {
    console.log("❌ gpio module not available");
}

console.log("=== GPIO Test Complete ===");