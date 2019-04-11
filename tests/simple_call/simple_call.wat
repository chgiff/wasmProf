(module
 (table 0 anyfunc)
 (memory $0 1)
 (export "memory" (memory $0))
 (export "func1" (func $func1))
 (export "func2" (func $func2))
 (export "main" (func $main))
 (func $func1 (; 0 ;) (param $0 i32) (param $1 i32) (result i32)
  (i32.mul
   (get_local $1)
   (get_local $0)
  )
 )
 (func $func2 (; 1 ;) (param $0 i32) (param $1 i32) (param $2 i32) (result i32)
  (i32.add
   (i32.add
    (get_local $1)
    (get_local $0)
   )
   (get_local $2)
  )
 )
 (func $main (; 2 ;) (result i32)
  (local $0 i32)
  (call $func2
   (call $func1
    (i32.const 99)
    (i32.const 10)
   )
   (tee_local $0
    (call $func2
     (i32.const 1)
     (i32.const 5)
     (i32.const 7)
    )
   )
   (get_local $0)
  )
 )
)