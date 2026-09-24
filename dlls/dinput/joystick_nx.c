/* Switch pad as a DirectInput joystick. Copyright 2026 Wine-NX contributors.
 * LGPL-2.1-or-later. The pad state comes from the existing XInput backend;
 * this device exists only when that backend is running on the Switch. */

#include <stdarg.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "dinput.h"
#include "xinput.h"

#include "dinput_private.h"
#include "device_private.h"
#include "nx_joystick_axes.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dinput);

static const GUID nx_joystick_guid = {0xb5703d6b,0x49a7,0x4bc8,{0xa4,0x9b,0x2b,0x6f,0xc0,0x84,0x38,0x7a}};
static const struct dinput_device_vtbl nx_joystick_vtbl;

typedef DWORD (WINAPI *nx_get_pad_state_fn)(DWORD, XINPUT_STATE *);
typedef BOOL (WINAPI *nx_has_pad_fn)(void);
static nx_get_pad_state_fn nx_get_pad_state;
static nx_has_pad_fn nx_has_pad;
static INIT_ONCE nx_pad_once = INIT_ONCE_STATIC_INIT;

struct nx_joystick
{
    struct dinput_device base;
};

static BOOL CALLBACK nx_pad_init( INIT_ONCE *once, void *param, void **context )
{
    HMODULE xinput = LoadLibraryW( L"C:\\windows\\system32\\xinput1_3.dll" );
    if (xinput)
    {
        nx_get_pad_state = (nx_get_pad_state_fn)GetProcAddress( xinput, "WineNxGetPadState" );
        nx_has_pad = (nx_has_pad_fn)GetProcAddress( xinput, "WineNxHasPad" );
        if (!nx_get_pad_state || !nx_has_pad)
        {
            nx_get_pad_state = NULL;
            nx_has_pad = NULL;
            FreeLibrary( xinput );
        }
    }
    return TRUE;
}

static BOOL nx_pad_state( XINPUT_STATE *state )
{
    InitOnceExecuteOnce( &nx_pad_once, nx_pad_init, NULL, NULL );
    return nx_get_pad_state && nx_get_pad_state( 0, state ) == ERROR_SUCCESS;
}

static BOOL nx_pad_available(void)
{
    InitOnceExecuteOnce( &nx_pad_once, nx_pad_init, NULL, NULL );
    return nx_has_pad && nx_has_pad();
}

HRESULT nx_joystick_enum_device( DWORD flags, DIDEVICEINSTANCEW *instance, DWORD version )
{
    DWORD size = instance->dwSize;

    if ((flags & DIEDFL_FORCEFEEDBACK) || !nx_pad_available()) return DIERR_DEVICENOTREG;
    memset( instance, 0, size );
    instance->dwSize = size;
    instance->guidInstance = nx_joystick_guid;
    instance->guidProduct = nx_joystick_guid;
    if (version >= 0x0800)
        instance->dwDevType = DI8DEVTYPE_JOYSTICK | (DI8DEVTYPEJOYSTICK_STANDARD << 8);
    else
        instance->dwDevType = DIDEVTYPE_JOYSTICK | (DIDEVTYPEJOYSTICK_GAMEPAD << 8);
    lstrcpyW( instance->tszInstanceName, L"Switch Controller" );
    lstrcpyW( instance->tszProductName, L"Wine-NX Switch Controller" );
    return DI_OK;
}

static BOOL nx_enum_object( struct dinput_device *device, const DIPROPHEADER *filter, DWORD flags,
                            enum_object_callback callback, UINT index, DIDEVICEOBJECTINSTANCEW *object, void *context )
{
    if (flags != DIDFT_ALL && !(flags & DIDFT_GETTYPE( object->dwType ))) return DIENUM_CONTINUE;
    switch (filter->dwHow)
    {
    case DIPH_DEVICE: break;
    case DIPH_BYOFFSET: if (filter->dwObj != object->dwOfs) return DIENUM_CONTINUE; break;
    case DIPH_BYID:
        if ((filter->dwObj & 0x00ffffff) != (object->dwType & 0x00ffffff)) return DIENUM_CONTINUE;
        break;
    default: return DIENUM_CONTINUE;
    }
    return callback( device, index, NULL, object, context );
}

static HRESULT nx_joystick_enum_objects( IDirectInputDevice8W *iface, const DIPROPHEADER *filter, DWORD flags,
                                         enum_object_callback callback, void *context )
{
    static const struct
    {
        const GUID *guid;
        DWORD offset;
        const WCHAR *name;
        USHORT usage;
    } axes[] =
    {
        {&GUID_XAxis, DIJOFS_X, L"Left X", 0x30},
        {&GUID_YAxis, DIJOFS_Y, L"Left Y", 0x31},
        {&GUID_ZAxis, DIJOFS_Z, L"Gas (right stick up)", 0x32},
        {&GUID_RxAxis, DIJOFS_RX, L"Right X", 0x33},
        {&GUID_RyAxis, DIJOFS_RY, L"Right Y", 0x34},
        {&GUID_RzAxis, DIJOFS_RZ, L"Brake (right stick down)", 0x35},
    };
    static const WCHAR *const button_names[] =
    {
        L"B", L"A", L"Y", L"X", L"L", L"R", L"Minus", L"Plus",
        L"Left stick", L"Right stick", L"ZL", L"ZR",
    };
    struct dinput_device *device = CONTAINING_RECORD( iface, struct dinput_device, IDirectInputDevice8W_iface );
    DIDEVICEOBJECTINSTANCEW object = {.dwSize = sizeof(object)};
    UINT i;

    for (i = 0; i < ARRAY_SIZE(axes); ++i)
    {
        object.guidType = *axes[i].guid;
        object.dwOfs = axes[i].offset;
        object.dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE( i );
        object.dwFlags = DIDOI_ASPECTPOSITION;
        object.wUsagePage = 1; /* HID generic desktop */
        object.wUsage = axes[i].usage;
        lstrcpyW( object.tszName, axes[i].name );
        if (nx_enum_object( device, filter, flags, callback, i, &object, context ) == DIENUM_STOP)
            return DIENUM_STOP;
    }

    object.guidType = GUID_POV;
    object.dwOfs = DIJOFS_POV(0);
    object.dwType = DIDFT_POV | DIDFT_MAKEINSTANCE(0);
    object.dwFlags = 0;
    object.wUsagePage = 1;
    object.wUsage = 0x39;
    lstrcpyW( object.tszName, L"D-pad" );
    if (nx_enum_object( device, filter, flags, callback, i, &object, context ) == DIENUM_STOP)
        return DIENUM_STOP;

    object.guidType = GUID_Button;
    object.dwFlags = 0;
    object.wUsagePage = 9; /* HID button */
    for (i = 0; i < ARRAY_SIZE(button_names); ++i)
    {
        object.dwOfs = DIJOFS_BUTTON(i);
        object.dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i);
        object.wUsage = i + 1;
        lstrcpyW( object.tszName, button_names[i] );
        if (nx_enum_object( device, filter, flags, callback, ARRAY_SIZE(axes) + 1 + i,
                            &object, context ) == DIENUM_STOP)
            return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

static BOOL nx_init_axis( struct dinput_device *device, UINT index, struct hid_value_caps *caps,
                          const DIDEVICEOBJECTINSTANCEW *object, void *context )
{
    struct object_properties *properties = device->object_properties + index;
    properties->bit_size = 16;
    properties->logical_min = 0;
    properties->logical_max = 65535;
    properties->physical_min = 0;
    properties->physical_max = 65535;
    properties->range_min = 0;
    properties->range_max = 65535;
    properties->saturation = 10000;
    properties->granularity = 1;
    return DIENUM_CONTINUE;
}

HRESULT nx_joystick_create_device( struct dinput *dinput, const GUID *guid, IDirectInputDevice8W **out )
{
    static const DIPROPHEADER filter = {.dwSize = sizeof(filter), .dwHeaderSize = sizeof(filter), .dwHow = DIPH_DEVICE};
    struct nx_joystick *joystick;
    DIJOYSTATE2 *device_state;
    HRESULT hr;

    *out = NULL;
    if (!IsEqualGUID( guid, &nx_joystick_guid ) && !IsEqualGUID( guid, &GUID_Joystick ))
        return DIERR_DEVICENOTREG;
    if (!nx_pad_available()) return DIERR_DEVICENOTREG;
    if (!(joystick = calloc( 1, sizeof(*joystick) ))) return E_OUTOFMEMORY;
    dinput_device_init( &joystick->base, &nx_joystick_vtbl, &nx_joystick_guid, dinput );
    joystick->base.dwCoopLevel = DISCL_NONEXCLUSIVE | DISCL_BACKGROUND;
    hr = nx_joystick_enum_device( 0, &joystick->base.instance, dinput->dwVersion );
    if (FAILED(hr)) goto failed;
    joystick->base.caps.dwDevType = joystick->base.instance.dwDevType;
    hr = dinput_device_init_device_format( &joystick->base.IDirectInputDevice8W_iface );
    if (FAILED(hr)) goto failed;
    nx_joystick_enum_objects( &joystick->base.IDirectInputDevice8W_iface, &filter, DIDFT_AXIS,
                              nx_init_axis, NULL );
    device_state = (DIJOYSTATE2 *)joystick->base.device_state;
    device_state->lX = device_state->lRx = 32768;
    device_state->lY = device_state->lRy = 32767;
    device_state->rgdwPOV[0] = -1;
    *out = &joystick->base.IDirectInputDevice8W_iface;
    return DI_OK;

failed:
    IDirectInputDevice8_Release( &joystick->base.IDirectInputDevice8W_iface );
    return hr;
}

static LONG nx_axis_value( struct dinput_device *device, UINT index, LONG value )
{
    struct object_properties *properties = device->object_properties + index;
    LONGLONG size = (LONGLONG)properties->range_max - properties->range_min;
    return properties->range_min + (size * value) / 65535;
}

static void nx_set_axis( IDirectInputDevice8W *iface, UINT index, DWORD offset, LONG value,
                         DWORD time, DWORD sequence, BOOL *changed )
{
    struct dinput_device *device = CONTAINING_RECORD( iface, struct dinput_device, IDirectInputDevice8W_iface );
    LONG *target = (LONG *)(device->device_state + offset);
    if (*target == value) return;
    *target = value;
    queue_event( iface, index, value, time, sequence );
    *changed = TRUE;
}

static HRESULT nx_joystick_poll( IDirectInputDevice8W *iface )
{
    static const WORD buttons[] =
    {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
    };
    struct dinput_device *device = CONTAINING_RECORD( iface, struct dinput_device, IDirectInputDevice8W_iface );
    XINPUT_STATE state = {0};
    struct nx_joystick_axes axes;
    DIJOYSTATE2 *current = (DIJOYSTATE2 *)device->device_state;
    DWORD time = GetCurrentTime(), sequence;
    LONG pov = -1;
    BOOL changed = FALSE;
    UINT i;

    /* A disconnected pad releases its controls instead of leaving a pedal held. */
    nx_pad_state( &state );
    axes = nx_joystick_map_axes( state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                                 state.Gamepad.sThumbRX, state.Gamepad.sThumbRY );
    EnterCriticalSection( &device->crit );
    sequence = device->dinput->evsequence++;
    nx_set_axis( iface, 0, DIJOFS_X, nx_axis_value( device, 0, axes.x ), time, sequence, &changed );
    nx_set_axis( iface, 1, DIJOFS_Y, nx_axis_value( device, 1, axes.y ), time, sequence, &changed );
    nx_set_axis( iface, 2, DIJOFS_Z, nx_axis_value( device, 2, axes.z ), time, sequence, &changed );
    nx_set_axis( iface, 5, DIJOFS_RZ, nx_axis_value( device, 5, axes.rz ), time, sequence, &changed );
    nx_set_axis( iface, 3, DIJOFS_RX, nx_axis_value( device, 3, axes.rx ), time, sequence, &changed );
    nx_set_axis( iface, 4, DIJOFS_RY, nx_axis_value( device, 4, axes.ry ), time, sequence, &changed );

    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) pov = 0;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) pov = pov < 0 ? 9000 : 4500;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) pov = pov < 0 ? 18000 : 13500;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) pov = pov < 0 ? 27000 :
        (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? 31500 : 22500;
    nx_set_axis( iface, 6, DIJOFS_POV(0), pov, time, sequence, &changed );

    for (i = 0; i < ARRAY_SIZE(buttons) + 2; ++i)
    {
        BYTE value = i < ARRAY_SIZE(buttons) ? !!(state.Gamepad.wButtons & buttons[i]) :
            i == ARRAY_SIZE(buttons) ? !!state.Gamepad.bLeftTrigger : !!state.Gamepad.bRightTrigger;
        value = value ? 0x80 : 0;
        if (current->rgbButtons[i] == value) continue;
        current->rgbButtons[i] = value;
        queue_event( iface, 7 + i, value, time, sequence );
        changed = TRUE;
    }
    if (changed && device->hEvent) SetEvent( device->hEvent );
    LeaveCriticalSection( &device->crit );
    return DI_OK;
}

static HRESULT nx_joystick_acquire( IDirectInputDevice8W *iface ) { return DI_OK; }
static HRESULT nx_joystick_unacquire( IDirectInputDevice8W *iface ) { return DI_OK; }

static HRESULT nx_joystick_get_property( IDirectInputDevice8W *iface, DWORD property,
                                         DIPROPHEADER *header, const DIDEVICEOBJECTINSTANCEW *object )
{
    struct dinput_device *device = CONTAINING_RECORD( iface, struct dinput_device, IDirectInputDevice8W_iface );
    switch (property)
    {
    case (DWORD_PTR)DIPROP_PRODUCTNAME:
        lstrcpyW( ((DIPROPSTRING *)header)->wsz, device->instance.tszProductName );
        return DI_OK;
    case (DWORD_PTR)DIPROP_INSTANCENAME:
        lstrcpyW( ((DIPROPSTRING *)header)->wsz, device->instance.tszInstanceName );
        return DI_OK;
    case (DWORD_PTR)DIPROP_JOYSTICKID:
        ((DIPROPDWORD *)header)->dwData = 0;
        return DI_OK;
    default:
        return DIERR_UNSUPPORTED;
    }
}

static const struct dinput_device_vtbl nx_joystick_vtbl =
{
    NULL,
    nx_joystick_poll,
    NULL,
    nx_joystick_acquire,
    nx_joystick_unacquire,
    nx_joystick_enum_objects,
    nx_joystick_get_property,
    NULL, NULL, NULL, NULL, NULL,
};
