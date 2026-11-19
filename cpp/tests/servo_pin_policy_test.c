#define _GNU_SOURCE
#include "../../tools/servo_v11_pins.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);exit(1);} } while(0)

int main(void){
    /* Actual board regression: unbind removes the scl owner on A24 while
     * preserving the output direction. Direct PWM mux may use that free pin
     * without a GPIO request or a direction change. */
    struct gpioline_info before={0},now={0};
    before.line_offset=now.line_offset=24;
    before.flags=GPIOLINE_FLAG_KERNEL|GPIOLINE_FLAG_IS_OUT;
    strcpy(before.consumer,"scl");now.flags=GPIOLINE_FLAG_IS_OUT;
    CHECK(servo_gpio_line_idle(&before,&now,1));
    CHECK(!servo_gpio_line_idle(&before,&now,0));
    /* A continuing/new owner, an unrelated owner, or an output that was
     * already unowned must never inherit the recovery-GPIO exception. */
    now.flags|=GPIOLINE_FLAG_KERNEL;
    CHECK(!servo_gpio_line_idle(&before,&now,1));
    now.flags=GPIOLINE_FLAG_IS_OUT;strcpy(now.consumer,"other");
    CHECK(!servo_gpio_line_idle(&before,&now,1));now.consumer[0]=0;
    strcpy(before.consumer,"other");CHECK(!servo_gpio_line_idle(&before,&now,1));
    strcpy(before.consumer,"scl");before.flags=GPIOLINE_FLAG_IS_OUT;
    CHECK(!servo_gpio_line_idle(&before,&now,1));
    before.flags=GPIOLINE_FLAG_KERNEL;
    before.line_offset=now.line_offset=31;
    CHECK(!servo_gpio_line_idle(&before,&now,1));
    before.line_offset=now.line_offset=25;strcpy(before.consumer,"scl");
    CHECK(!servo_gpio_line_idle(&before,&now,1));
    strcpy(before.consumer,"sda");CHECK(servo_gpio_line_idle(&before,&now,1));
    before.line_offset=24;CHECK(!servo_gpio_line_idle(&before,&now,1));
    /* Free inputs stay usable; requested inputs are still refused. */
    now.flags=0;CHECK(servo_gpio_line_idle(&before,&now,0));
    now.flags=GPIOLINE_FLAG_KERNEL;CHECK(!servo_gpio_line_idle(&before,&now,1));
    puts("PASS: released I2C recovery direction and active/unrelated GPIO rejection");
    return 0;
}
