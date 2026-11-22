/* Forced include for T-Kernel C on Cortex-M.
   thumbv6m defines __arm__, and the broadcom CPU headers then emit
   CPSR and CP15 instructions this core does not have. Undefine it so
   those headers take the portable side. libstr.c is not on this link. */
#undef __arm__
