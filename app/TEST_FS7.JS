function testFixedFS() {
    console.log("🧪 Testing Fixed Filesystem...");
    
    // تست وضعیت mount
    let mountStatus = fs.checkMount();
    console.log("Mount Status:", mountStatus);
    
    // تست نوشتن مستقیم
    let directTest = fs.testDirectWrite();
    console.log("Direct Write Test:", directTest);
    
    if (directTest.fopen_write) {
        console.log("✅ fopen write works!");
        
        // تست با writeFileSync جدید
        try {
            let result = fs.writeFileSync("/sdcard/fixed_test.txt", "Hello Fixed FS!");
            console.log("writeFileSync result:", result);
            
            if (result) {
                let content = fs.readFileSync("/sdcard/fixed_test.txt");
                console.log("Read back:", content);
                fs.unlink("/sdcard/fixed_test.txt");
            }
        } catch(e) {
            console.log("❌ writeFileSync failed:", e);
        }
    } else {
        console.log("❌ Even direct fopen doesn't work!");
    }
}

testFixedFS();
