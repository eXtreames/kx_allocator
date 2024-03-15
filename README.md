***ONE HEADER, WITHOUT DEPENDENCIES***. Custom allocator with optimized memory management and collecting information about current allocator instance ( allocated size, free space, used space, count of buckets ).
<br/>
<br/> ***Support only x64 and maybe only MSVC ( maybe diffrent compilers wont support __stosb and __stosq ( need to change definition KX_ALLOCATOR_MEMSET_B and KX_ALLOCATOR_MEMSET_Q ) )***
<br/>
<br/>
<br/> > Support of different memory alloc and free functions ( see functions - set_allocation_routine, set_free_routine ). ****Note - without assigning this routines - allocator won't work.****
<br/> > Support for changing block size in runtime. ( via function change_block_size. Count of blocks - compile time ( BLOCK_COUNT variable ) ).
<br/> > Support zeroing memory on allocation or free ( see KX_ALLOCATOR_ZERO_FREE_MEMORY and KX_ALLOCATOR_ZERO_ALLOCATED_MEMORY flags ).
<br/> > Support manual garbage collecting ( erasing buckets, which are not in use. Example - user allocated memory (20480 bytes), freed memory. After calling gc() this bucket ( which contains this memory) will be cleared. If gc() will not been called, this bucket will remain for later use, but it will take up space  ).
<br/> > Automatic gc also supported ( need set KX_ALLOCATOR_USE_AUTOMATIC_GC to 1 in #define and call function set_is_needed_gc_routine with your own logic of gc ). You need to create logic, to determine when garbage collection is needed by calling collect_information. You need to return bool, which indicates the need of garbage collection.
