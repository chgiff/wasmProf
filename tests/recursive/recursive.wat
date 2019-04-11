(module
 (table 0 anyfunc)
 (memory $0 1)
 (export "memory" (memory $0))
 (export "func2" (func $func2))
 (export "main" (func $main))
 (func $func2 (; 0 ;) (param $0 i32) (result i32)
  (block $label$0
   (br_if $label$0
    (i32.lt_s
     (get_local $0)
     (i32.const 1)
    )
   )
   (return
    (i32.add
     (call $func2
      (i32.add
       (get_local $0)
       (i32.const -1)
      )
     )
     (i32.const 1)
    )
   )
  )
  (get_local $0)
 )
 (func $main (; 1 ;) (result i32)
  (call $func2
   (i32.const 4)
  )
 )
)