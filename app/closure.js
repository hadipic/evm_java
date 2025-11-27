print("------test closure-------");
function test_closure() {
  var a = 100;
  function f() {
    print(a);
  }
  return f;
}

var f = test_closure();
f();
print("------end of test closure-------");