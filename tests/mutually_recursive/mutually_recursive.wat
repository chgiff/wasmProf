(module
 (table 0 anyfunc)
 (memory $0 1)
 (export "memory" (memory $0))
 (export "f1" (func $f1))
 (export "f2" (func $f2))
 (export "main" (func $main))
 (func $f1 (; 0 ;) (param $0 i32) (param $1 i32) (result i32)
  (call $f2
   (i32.add
    (get_local $1)
    (get_local $0)
   )
  )
 )
 (func $f2 (; 1 ;) (param $0 i32) (result i32)
  (block $label$0
   (br_if $label$0
    (i32.gt_s
     (get_local $0)
     (i32.const 10)
    )
   )
   (set_local $0
    (call $f1
     (get_local $0)
     (i32.add
      (get_local $0)
      (i32.const 3)
     )
    )
   )
  )
  (get_local $0)
 )
 (func $main (; 2 ;) (result i32)
  (call $f1
   (i32.const 1)
   (i32.const 0)
  )
 )
)
