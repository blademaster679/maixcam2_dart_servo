"""Host compiler syntax check with explicit SDK stubs; NOT an E907 BSP build."""
import subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parent
headers={
'rtthread.h':'''#include <stdint.h>
typedef void *rt_thread_t;
#define RT_NULL ((void*)0)
#define RT_EOK 0
#define RT_TICK_PER_SECOND 100
#define RT_CONSOLE_DEVICE_NAME "uart1"
unsigned rt_tick_get(void);
void rt_kprintf(const char*,...);
void rt_thread_mdelay(int);
rt_thread_t rt_thread_create(const char*,void(*)(void*),void*,unsigned,unsigned,unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
''',
'finsh.h':'#define MSH_CMD_EXPORT(fn, desc) void *export_##fn = (void*)&fn;\n',
'ax_common.h':'#include <stdint.h>\nuint32_t ax_readl(uint32_t);\nvoid ax_writel(uint32_t,uint32_t);\n'}
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp)
    for name,text in headers.items():(d/name).write_text(text)
    (d/'drv_pwm.h').write_bytes((root/'research/riscv__drivers__pwm__drv_pwm.h').read_bytes())
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-fsyntax-only','-I',str(d),str(root/'firmware/servo_bench.c')],check=True)
    preamble='''#include "drv_pwm.h"
#include "ax_common.h"
#define PWM_CLK_SEL_FREQ_HZ 24000000U
#define PWM_LOADCOUNT_OFF(n) ((n)*0x14U)
#define PWM_LOADCOUNT2_OFF(n) (0xB0U+(n)*4U)
#define PWM_CONTROLREG_OFF(n) (8U+(n)*0x14U)
#define PWM_MODE 0x1EU
static struct {unsigned channel_status;} pwm_status[3];
static uint32_t pwm_base_addr[3];
'''
    extension=d/'extension.c'
    extension.write_text(preamble+(root/'firmware/pwm_us_extension.c.inc').read_text())
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-fsyntax-only','-I',str(d),str(extension)],check=True)
print('PASS: both C sources, host syntax only; target headers/link/hardware not validated')
