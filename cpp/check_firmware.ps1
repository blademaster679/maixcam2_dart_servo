param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$package = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $PSScriptRoot 'build/c_stubs'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
@'
#include <stdint.h>
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
'@ | Set-Content -LiteralPath (Join-Path $dir 'rtthread.h') -Encoding utf8
'#define MSH_CMD_EXPORT(fn, desc) void *export_##fn = (void*)&fn;' | Set-Content -LiteralPath (Join-Path $dir 'finsh.h') -Encoding utf8
@'
#include <stdint.h>
uint32_t ax_readl(uint32_t);
void ax_writel(uint32_t,uint32_t);
'@ | Set-Content -LiteralPath (Join-Path $dir 'ax_common.h') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $package 'research/riscv__drivers__pwm__drv_pwm.h') -Destination (Join-Path $dir 'drv_pwm.h')
& $Compiler '-std=c99' '-Wall' '-Wextra' '-Werror' '-fsyntax-only' '-I' $dir (Join-Path $package 'firmware/servo_bench.c')
if ($LASTEXITCODE -ne 0) { throw 'Firmware C syntax failed' }
$prefix = @'
#include "drv_pwm.h"
#include "ax_common.h"
#define PWM_CLK_SEL_FREQ_HZ 24000000U
#define PWM_LOADCOUNT_OFF(n) ((n)*0x14U)
#define PWM_LOADCOUNT2_OFF(n) (0xB0U+(n)*4U)
#define PWM_CONTROLREG_OFF(n) (8U+(n)*0x14U)
#define PWM_MODE 0x1EU
static struct {unsigned channel_status;} pwm_status[3];
static uint32_t pwm_base_addr[3];

'@
$extension = Get-Content -Raw -LiteralPath (Join-Path $package 'firmware/pwm_us_extension.c.inc')
($prefix + "`n" + $extension) | Set-Content -LiteralPath (Join-Path $dir 'extension.c') -Encoding utf8
& $Compiler '-std=c99' '-Wall' '-Wextra' '-Werror' '-fsyntax-only' '-I' $dir (Join-Path $dir 'extension.c')
if ($LASTEXITCODE -ne 0) { throw 'PWM extension C syntax failed' }
Write-Output 'PASS: C99 host syntax, NOT an E907 SDK build or hardware validation'
