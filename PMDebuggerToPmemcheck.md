In general, I try to add some new features on top of Pmemcheck instead of giving a new debugging tool like PMDebugger.

In this file, I briefly show what has changed and why for each commit. For more details, you can read our paper: Fast, Flexible, and Comprehensive Bug Detection for Persistent Memory Programs.  

### just add new structure -- d0df66235227777c7b1c0d9b035f2c2ac2f35773

- Add memory location information array  which includes **arr_md** structure and **pmem_stores_array** structure.

- Information of most stores is deleted in a short term, so we use the array to keep track of hot and short-lived information and avoid the overhead of tree reorganizations (reorganizations in Oset structure).
- Most stores can be collectively flushed, so we use arr_md structure to collectively maintain and update persistency status for high performance.



### modify the algorithm of trace_pmem_store() -- f0f262471645a79ea50a8074ab36927b12185904

- Add memory location information into the memory location information array.
- A new function for the array structure to split stores (array_split_stores ()).
- A new function for the array structure to handle multiple stores (array_handle_with_mult_stores()).

- A new function for the array structure to handle multiple overwrite warns (array_add_mult_overwrite_warn()).
- A new function for the array structure to update minimum and maximum address in arr_md structure (update_array_metadata_minandmax()).

- A new function for the array structure to compare a region with minimum and maximum address in arr_md structure (cmp_with_arr_minandmax()).



### modify the algorithm of do_flush -- 1ec603086958d841b35e46ffef02d9c66116a698

- Leverage arr_md structure to collectively maintain and update persistency status for high performance when processing writeback instructions (array_process_flush()).



### modify the algorithm of do_fence -- 263d2af8510304ccc7613c8660e903522ce8ef6e

- Leverage arr_md structure to process fence instruction and the way is similar to processing writeback instructions (array_process_fence()).
- Directly invalidates arr_md structure to achieve quickly deletion after processing fence instruction.



### fix bugs and pass the simple verification (perl tests/vg_regtest pmemcheck -- 58e2ccbad20bdfe9d88d80b4adc704cc43517928

- Add codes in print_store_stats() to print not persistent stores in the array
- Add codes in case VG_USERREQ__PMC_SET_CLEAN to remove regions from memory location information array

