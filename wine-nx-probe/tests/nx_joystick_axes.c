#include <assert.h>
#include <stdio.h>
#include "../../dlls/dinput/nx_joystick_axes.h"

int main(void)
{
    struct nx_joystick_axes axes;

    axes = nx_joystick_map_axes(0, 0, 0, 0);
    assert(axes.x == 32768 && axes.y == 32767 && axes.rx == 32768 && axes.ry == 32767);
    assert(!axes.z && !axes.rz);

    axes = nx_joystick_map_axes(-32768, 32767, 32767, 16384);
    assert(axes.x == 0 && axes.y == 0 && axes.rx == 65535 && axes.ry == 16383);
    assert(axes.z == 32769 && !axes.rz);

    axes = nx_joystick_map_axes(32767, -32768, -32768, -16384);
    assert(axes.x == 65535 && axes.y == 65535 && axes.rx == 0 && axes.ry == 49151);
    assert(!axes.z && axes.rz == 32768);

    axes = nx_joystick_map_axes(0, 0, 0, -32768);
    assert(!axes.z && axes.rz == 65535);
    axes = nx_joystick_map_axes(0, 0, 0, 32767);
    assert(axes.z == 65535 && !axes.rz);

    puts("DirectInput Switch axes: steering and split pedals passed");
    return 0;
}
