#define MSH_CMD_EXPORT(fn, desc) void *export_##fn = (void*)&fn;
