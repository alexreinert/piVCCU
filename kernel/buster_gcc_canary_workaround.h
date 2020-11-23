#ifdef BUSTER_GCC_CANARY_WORKAROUND

unsigned long __stack_chk_guard;

void __stack_chk_guard_setup(void)
{
  __stack_chk_guard = current->stack_canary;
}

void __stack_chk_fail(void)
{
  panic ("stack-protector: Kernel stack is corrupted in: %pB",
    __builtin_return_address (0));
}

#endif
