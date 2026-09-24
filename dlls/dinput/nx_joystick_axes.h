/* Switch stick values (-32768..32767) as DirectInput axes (0..65535).
 * LGPL-2.1-or-later. */
#ifndef __WINE_DINPUT_NX_JOYSTICK_AXES_H
#define __WINE_DINPUT_NX_JOYSTICK_AXES_H

struct nx_joystick_axes
{
    int x, y, z, rx, ry, rz;
};

static inline struct nx_joystick_axes nx_joystick_map_axes(int lx, int ly, int rx, int ry)
{
    struct nx_joystick_axes axes;
    axes.x = lx + 32768;
    axes.y = 32767 - ly;
    axes.rx = rx + 32768;
    axes.ry = 32767 - ry;
    axes.z = ry > 0 ? (ry * 65535 + 16383) / 32767 : 0;   /* right stick up: gas */
    axes.rz = ry < 0 ? (-ry * 65535 + 16384) / 32768 : 0; /* right stick down: brake */
    return axes;
}

#endif
