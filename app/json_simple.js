print("=== JSON Performance Test ===");

// تست performance برای parse
print("\n📊 Parse Performance Test");

// ایجاد داده تست ساده‌تر
function createTestData() {
    var data = {
        users: [],
        metadata: {
            version: "1.0",
            timestamp: Date.now()
        }
    };
    
    // ایجاد 50 کاربر ساده
    for (var i = 0; i < 50; i++) {
        data.users.push({
            id: i,
            name: "User " + i,
            email: "user" + i + "@example.com",
            active: i % 2 === 0,
            score: Math.random() * 100
        });
    }
    
    var jsonStr = JSON.stringify(data);
    print("Created test data: " + jsonStr.length + " characters");
    return jsonStr;
}

var testData = createTestData();
var iterations = 0;
var startTime = Date.now();
var endTime = startTime + 5000; // 5 ثانیه تست

// تست performance
while (Date.now() < endTime) {
    var parsed = JSON.parse(testData);
    var stringified = JSON.stringify(parsed);
    iterations++;
}

var totalTime = Date.now() - startTime;
var opsPerSec = (iterations * 1000) / totalTime;

print("Performance results:");
print("   Time: " + totalTime + "ms");
print("   Operations: " + iterations);
print("   Ops/sec: " + opsPerSec.toFixed(2));

// تست memory
print("\n📊 Memory Test");

function memoryTest() {
    var smallObj = { a: 1, b: "test", c: true };
    var mediumObj = {
        items: [1, 2, 3, 4, 5],
        config: { enabled: true, mode: "auto" },
        data: { x: 10, y: 20 }
    };
    
    var largeObj = {
        array: [],
        nested: {
            level1: {
                level2: {
                    level3: {
                        value: "deep"
                    }
                }
            }
        }
    };
    
    // پر کردن آرایه بزرگ
    for (var i = 0; i < 100; i++) {
        largeObj.array.push({
            index: i,
            value: Math.random(),
            active: i % 3 === 0
        });
    }
    
    // تست سریالایز
    var start = Date.now();
    var smallJson = JSON.stringify(smallObj);
    var mediumJson = JSON.stringify(mediumObj);
    var largeJson = JSON.stringify(largeObj);
    var serializeTime = Date.now() - start;
    
    // تست پارس
    start = Date.now();
    var parsedSmall = JSON.parse(smallJson);
    var parsedMedium = JSON.parse(mediumJson);
    var parsedLarge = JSON.parse(largeJson);
    var parseTime = Date.now() - start;
    
    print("Memory Test Results:");
    print("   Small object: " + smallJson.length + " chars");
    print("   Medium object: " + mediumJson.length + " chars");
    print("   Large object: " + largeJson.length + " chars");
    print("   Serialize time: " + serializeTime + "ms");
    print("   Parse time: " + parseTime + "ms");
    
    // اعتبارسنجی
    var valid = (
        parsedSmall.a === smallObj.a &&
        parsedMedium.items.length === mediumObj.items.length &&
        parsedLarge.array.length === largeObj.array.length
    );
    
    print("   Validation: " + (valid ? "PASS" : "FAIL"));
}

memoryTest();

// تست استرس
print("\n📊 Stress Test");

function stressTest() {
    var complexData = {
        nestedArrays: [],
        deepObjects: {},
        mixedTypes: []
    };
    
    // ایجاد آرایه‌های تودرتو
    for (var i = 0; i < 10; i++) {
        var nestedArray = [];
        for (var j = 0; j < 10; j++) {
            nestedArray.push({
                id: i * 10 + j,
                values: [j, j+1, j+2],
                flags: [true, false, true]
            });
        }
        complexData.nestedArrays.push(nestedArray);
    }
    
    // ایجاد آبجکت‌های عمیق
    var current = complexData.deepObjects;
    for (var depth = 0; depth < 5; depth++) {
        current["level" + depth] = {
            value: depth,
            next: {}
        };
        current = current["level" + depth].next;
    }
    
    // انواع داده مختلف
    complexData.mixedTypes = [
        null,
        true,
        false,
        42,
        3.14159,
        "string",
        [1, 2, 3],
        { object: true },
        undefined
    ];
    
    try {
        var json = JSON.stringify(complexData);
        var parsed = JSON.parse(json);
        
        print("Stress Test Results:");
        print("   Complex data size: " + json.length + " chars");
        print("   Nested arrays: " + parsed.nestedArrays.length);
        print("   Mixed types: " + parsed.mixedTypes.length);
        print("   Status: PASS");
    } catch (e) {
        print("Stress Test Failed: " + e);
    }
}

stressTest();

print("\n🎉 JSON Performance Test Completed!");