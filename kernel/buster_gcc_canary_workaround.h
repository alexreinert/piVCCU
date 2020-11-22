#ifdef BUSTER_GCC_CANARY_WORKAROUND

unsigned long __stack_chk_guard;

void __stack_chk_guard_setup(void)
{
  __stack_chk_guard = 0xB233A44D;
}

void __stack_chk_fail(void)
{
  dump_stack ();
  panic ("Stack-protector:kernel stack is corrupted in:%pa\n",
    __builtin_return_address (0));
}

#endif
