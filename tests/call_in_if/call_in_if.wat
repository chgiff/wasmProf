(module
 (table 0 anyfunc)
 (memory $0 1)
 (export "memory" (memory $0))
 (export "add" (func $add))
 (export "doub" (func $doub))
 (export "main" (func $main))
 (func $add (; 0 ;) (param $0 i32) (param $1 i32) (result i32)
  (i32.add
   (get_local $1)
   (get_local $0)
  )
 )
 (func $doub (; 1 ;) (param $0 i32) (result i32)
  (i32.shl
   (get_local $0)
   (i32.const 1)
  )
 )
 (func $main (; 2 ;) (result i32)
  (local $0 i32)
  (set_local $0
   (i32.const 0)
  )
  (block $label$0
   (br_if $label$0
    (i32.lt_s
     (call $doub
      (i32.const 2)
     )
     (i32.const 4)
    )
   )
   (set_local $0
    (call $add
     (i32.const 1)
     (i32.const 4)
    )
   )
  )
  (get_local $0)
 )
)
