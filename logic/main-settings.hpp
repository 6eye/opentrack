/* Copyright (c) 2015, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#pragma once

#include <QString>
#include "options/options.hpp"
#include "api/plugin-api.hpp"
#include "spline/axis-opts.hpp"

#include "export.hpp"

enum reltrans_state
{
    reltrans_disabled   = 0,
    reltrans_enabled    = 1,
    reltrans_non_center = 2,
};

namespace main_settings_impl {

using namespace options;

struct OTR_LOGIC_EXPORT key_opts
{
    value<QString> keycode, guid;
    value<int> button;

    key_opts(bundle b, const QString& name);
};

struct OTR_LOGIC_EXPORT module_settings
{
    bundle b;
    value<QString> tracker_dll, filter_dll, protocol_dll;
    module_settings();
};

struct OTR_LOGIC_EXPORT main_settings final
{
    bundle b, b_map;
    axis_opts a_x, a_y, a_z;
    axis_opts a_yaw, a_pitch, a_roll;
    axis_opts* all_axis_opts[6];
    value<reltrans_state> reltrans_mode { b, "relative-translation-mode", reltrans_disabled };
    value<bool> reltrans_disable_tx, reltrans_disable_ty, reltrans_disable_tz;
    value<bool> reltrans_disable_src_yaw, reltrans_disable_src_pitch, reltrans_disable_src_roll;
    value<bool> tray_enabled, tray_start;
    value<bool> center_at_startup;
    //value<int> center_method;
    value<int> neck_z;
    value<bool> neck_enable;
    key_opts key_start_tracking1, key_start_tracking2;
    key_opts key_stop_tracking1, key_stop_tracking2;
    key_opts key_toggle_tracking1, key_toggle_tracking2;
    key_opts key_restart_tracking1, key_restart_tracking2;
    key_opts key_center1, key_center2;
    key_opts key_toggle1, key_toggle2;
    key_opts key_zero1, key_zero2;
    key_opts key_toggle_press1, key_toggle_press2;
    key_opts key_zero_press1, key_zero_press2;
    value<bool> tracklogging_enabled;
    value<QString> tracklogging_filename;

    main_settings();
};

} // ns main_settings_impl

using key_opts = main_settings_impl::key_opts;
using module_settings = main_settings_impl::module_settings;
using main_settings = main_settings_impl::main_settings;
