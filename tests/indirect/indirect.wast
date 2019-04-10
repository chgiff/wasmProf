(module
.. types, imports, and exports snipped...
  (func $main (param $0 i32) (result i32)
    (local $1 i32)
    (set_local $1
      (i32.const 10)
    )
    (block $label$0
      (block $label$1
        (block $label$2
          (br_if $label$2
            (i32.gt_u
              (tee_local $0
                (i32.add
                  (get_local $0)
                  (i32.const -1)
                )
              )
              (i32.const 10)
            )
          )
          (block $label$3
            (block $label$4
              (block $label$5
                (br_table $label$1 $label$5 $label$4 $label$3 $label$2 $label$2 $label$2 $label$2 $label$2 $label$2 $label$0 $label$1
                  (get_local $0)
                )
              )
              (return
                (i32.const 13)
              )
            )
            (return
              (i32.const 11)
            )
          )
          (return
            (i32.const 102)
          )
        )
        (set_local $1
          (i32.const 0)
        )
      )
      (return
        (get_local $1)
      )
    )
    (i32.const 1040)
  )